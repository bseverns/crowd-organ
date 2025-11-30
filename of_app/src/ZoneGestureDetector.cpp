#include "ZoneGestureDetector.h"

#include "ofLog.h"

#include <algorithm>
#include <string>

namespace {
float clamp01(float value) {
    return ofClamp(value, 0.0f, 1.0f);
}

// Friendly names for composing sweep strings. Keeping them here makes it easy
// to update copywriting without diving into logic below.
const std::array<std::string, 4> kRowNames = {"top", "upper_mid", "lower_mid", "bottom"};
const std::array<std::string, 4> kColumnNames = {"left", "mid_left", "mid_right", "right"};

std::string rowName(int row) {
    return row < static_cast<int>(kRowNames.size()) ? kRowNames[row] : "row" + std::to_string(row);
}

std::string columnName(int col) {
    return col < static_cast<int>(kColumnNames.size()) ? kColumnNames[col] : "col" + std::to_string(col);
}
} // namespace

ZoneGestureDetector::ZoneGestureDetector() = default;

void ZoneGestureDetector::setConfig(const Config& newConfig) {
    config = newConfig;
}

void ZoneGestureDetector::updateCamera(int camId,
                                       int rows,
                                       int cols,
                                       const std::vector<float>& zones,
                                       uint64_t timestampMs,
                                       std::vector<ZoneGestureEvent>& outEvents) {
    if (rows <= 0 || cols <= 0) {
        ofLogWarning("ZoneGestureDetector") << "cam " << camId << " reported invalid grid size " << rows << "x" << cols
                                             << " – ignoring heatmap";
        return;
    }

    int expectedSize = rows * cols;
    if (static_cast<int>(zones.size()) != expectedSize) {
        ofLogWarning("ZoneGestureDetector") << "cam " << camId << " heatmap size mismatch: got " << zones.size()
                                             << " values for " << rows << "x" << cols << " grid (" << expectedSize
                                             << " expected)";
        return;
    }

    auto& history = histories[camId];
    auto& cameraState = cameraStates[camId];

    if (cameraState.rows != rows || cameraState.cols != cols) {
        if (cameraState.rows != 0 && cameraState.cols != 0) {
            ofLogNotice("ZoneGestureDetector") << "cam " << camId << " grid changed from " << cameraState.rows << "x"
                                               << cameraState.cols << " to " << rows << "x" << cols
                                               << "; clearing history so detections track the new layout";
        }
        history.clear();
        cameraState.rows = rows;
        cameraState.cols = cols;
        cameraState.pulseTrackers.assign(expectedSize, PulseTracker{});
    } else if (static_cast<int>(cameraState.pulseTrackers.size()) != expectedSize) {
        cameraState.pulseTrackers.assign(expectedSize, PulseTracker{});
    }

    ZoneSample sample;
    sample.timestamp = timestampMs;
    sample.values = zones;
    sample.rows = rows;
    sample.cols = cols;
    history.push_back(sample);

    // Trim the backlog so we only carry the last few seconds of context per
    // camera. The detectors rely on this sliding window to avoid stale ghosts.
    uint64_t minTimestamp = (timestampMs > config.historyMs) ? timestampMs - config.historyMs : 0;
    while (!history.empty() && history.front().timestamp < minTimestamp) {
        history.pop_front();
    }

    detectSweeps(camId, history, outEvents);
    detectPulses(camId, rows, cols, sample, outEvents);
}

void ZoneGestureDetector::removeCamera(int camId) {
    histories.erase(camId);
    cameraStates.erase(camId);
    lastTriggerTimes.erase(camId);
}

bool ZoneGestureDetector::canTrigger(int camId, const std::string& type, uint64_t timestamp, uint64_t cooldownMs) {
    auto camIt = lastTriggerTimes.find(camId);
    if (camIt == lastTriggerTimes.end()) {
        return true;
    }
    auto typeIt = camIt->second.find(type);
    if (typeIt == camIt->second.end()) {
        return true;
    }
    return timestamp >= typeIt->second + cooldownMs;
}

void ZoneGestureDetector::rememberTrigger(int camId, const std::string& type, uint64_t timestamp) {
    lastTriggerTimes[camId][type] = timestamp;
}

void ZoneGestureDetector::detectSweeps(int camId, const std::deque<ZoneSample>& history, std::vector<ZoneGestureEvent>& outEvents) {
    if (history.size() < static_cast<std::size_t>(config.sweepMinSteps)) {
        return;
    }

    uint64_t now = history.back().timestamp;
    uint64_t minTimestamp = (now > config.sweepWindowMs) ? now - config.sweepWindowMs : 0;

    const auto& latest = history.back();
    int rows = latest.rows;
    int cols = latest.cols;
    if (rows <= 0 || cols <= 0 || static_cast<int>(latest.values.size()) != rows * cols) {
        ofLogWarning("ZoneGestureDetector") << "cam " << camId << " sweep detection skipped due to malformed heatmap";
        return;
    }

    // Each row/column keeps track of where the hottest cell lived for each
    // frame. Watching those indices drift lets us detect coherent sweeps.
    std::vector<std::vector<int>> rowMaxIndices(rows);
    std::vector<std::vector<int>> columnMaxIndices(cols);

    for (const auto& sample : history) {
        if (sample.timestamp < minTimestamp) {
            continue;
        }

        if (sample.rows != rows || sample.cols != cols || static_cast<int>(sample.values.size()) != rows * cols) {
            ofLogWarning("ZoneGestureDetector") << "cam " << camId << " skipped a malformed sample during sweep detection";
            continue;
        }

        for (int row = 0; row < rows; ++row) {
            int base = row * cols;
            int maxIndex = 0;
            float maxValue = sample.values[base];
            for (int col = 1; col < cols; ++col) {
                float value = sample.values[base + col];
                if (value > maxValue) {
                    maxValue = value;
                    maxIndex = col;
                }
            }
            rowMaxIndices[row].push_back(maxIndex);
        }

        for (int col = 0; col < cols; ++col) {
            int maxIndex = 0;
            float maxValue = sample.values[col];
            for (int row = 1; row < rows; ++row) {
                float value = sample.values[row * cols + col];
                if (value > maxValue) {
                    maxValue = value;
                    maxIndex = row;
                }
            }
            columnMaxIndices[col].push_back(maxIndex);
        }
    }

    // Rows: detect left/right motion.
    for (int row = 0; row < rows; ++row) {
        const auto& indices = rowMaxIndices[row];
        if (static_cast<int>(indices.size()) < config.sweepMinSteps) {
            continue;
        }

        bool increasing = true;
        bool decreasing = true;
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (indices[i] < indices[i - 1]) {
                increasing = false;
            }
            if (indices[i] > indices[i - 1]) {
                decreasing = false;
            }
        }

        int delta = indices.back() - indices.front();
        int base = row * cols;
        float rowMin = latest.values[base];
        float rowMax = latest.values[base];
        for (int col = 1; col < cols; ++col) {
            float value = latest.values[base + col];
            rowMin = std::min(rowMin, value);
            rowMax = std::max(rowMax, value);
        }
        float rowRange = rowMax - rowMin;
        if (rowRange < config.sweepMinStrength) {
            // If the energy band is too flat we skip so noise does not fire sweeps.
            continue;
        }

        ZoneGestureEvent event;
        event.camId = camId;
        event.strength = clamp01(rowRange);
        event.hasZoneIndex = false;

        if (increasing && delta >= 2) {
            event.type = "sweep_lr_" + rowName(row);
            if (canTrigger(camId, event.type, now, config.sweepCooldownMs)) {
                outEvents.push_back(event);
                rememberTrigger(camId, event.type, now);
                ofLogNotice("ZoneGestureDetector") << "cam " << camId << " " << event.type << " strength " << event.strength;
            }
        } else if (decreasing && delta <= -2) {
            event.type = "sweep_rl_" + rowName(row);
            if (canTrigger(camId, event.type, now, config.sweepCooldownMs)) {
                outEvents.push_back(event);
                rememberTrigger(camId, event.type, now);
                ofLogNotice("ZoneGestureDetector") << "cam " << camId << " " << event.type << " strength " << event.strength;
            }
        }
    }

    // Columns: mirror the logic for top/bottom waves.
    for (int col = 0; col < cols; ++col) {
        const auto& indices = columnMaxIndices[col];
        if (static_cast<int>(indices.size()) < config.sweepMinSteps) {
            continue;
        }

        bool increasing = true;
        bool decreasing = true;
        for (std::size_t i = 1; i < indices.size(); ++i) {
            if (indices[i] < indices[i - 1]) {
                increasing = false;
            }
            if (indices[i] > indices[i - 1]) {
                decreasing = false;
            }
        }

        int delta = indices.back() - indices.front();
        float colMin = latest.values[col];
        float colMax = latest.values[col];
        for (int row = 1; row < rows; ++row) {
            float value = latest.values[row * cols + col];
            colMin = std::min(colMin, value);
            colMax = std::max(colMax, value);
        }
        float colRange = colMax - colMin;
        if (colRange < config.sweepMinStrength) {
            continue;
        }

        ZoneGestureEvent event;
        event.camId = camId;
        event.strength = clamp01(colRange);
        event.hasZoneIndex = false;

        if (increasing && delta >= 2) {
            event.type = "sweep_tb_" + columnName(col);
            if (canTrigger(camId, event.type, now, config.sweepCooldownMs)) {
                outEvents.push_back(event);
                rememberTrigger(camId, event.type, now);
                ofLogNotice("ZoneGestureDetector") << "cam " << camId << " " << event.type << " strength " << event.strength;
            }
        } else if (decreasing && delta <= -2) {
            event.type = "sweep_bt_" + columnName(col);
            if (canTrigger(camId, event.type, now, config.sweepCooldownMs)) {
                outEvents.push_back(event);
                rememberTrigger(camId, event.type, now);
                ofLogNotice("ZoneGestureDetector") << "cam " << camId << " " << event.type << " strength " << event.strength;
            }
        }
    }
}

void ZoneGestureDetector::detectPulses(int camId, int rows, int cols, const ZoneSample& sample, std::vector<ZoneGestureEvent>& outEvents) {
    auto stateIt = cameraStates.find(camId);
    if (stateIt == cameraStates.end()) {
        return;
    }

    auto& trackers = stateIt->second.pulseTrackers;
    int expectedSize = rows * cols;
    if (rows <= 0 || cols <= 0 || static_cast<int>(sample.values.size()) != expectedSize ||
        static_cast<int>(trackers.size()) != expectedSize) {
        ofLogWarning("ZoneGestureDetector") << "cam " << camId << " pulse detection skipped due to malformed heatmap";
        return;
    }

    uint64_t timestamp = sample.timestamp;

    for (int zoneIndex = 0; zoneIndex < expectedSize; ++zoneIndex) {
        auto& tracker = trackers[zoneIndex];
        float value = sample.values[zoneIndex];
        if (!tracker.initialized) {
            tracker.initialized = true;
            tracker.prevValue = value;
            tracker.prevSlope = 0.0f;
            tracker.lastTrigger = 0;
            continue;
        }

        float slope = value - tracker.prevValue;
        bool rising = tracker.prevSlope > config.pulseSlopeThreshold;
        bool falling = slope <= -config.pulseSlopeThreshold;

        // A pulse is a peaked mountain: we were rising, now we are falling, and
        // the energy got high enough to matter.
        if (rising && falling && value >= config.pulseThreshold) {
            if (timestamp >= tracker.lastTrigger + config.pulseCooldownMs) {
                ZoneGestureEvent event;
                event.camId = camId;
                event.type = "pulse_zone";
                event.hasZoneIndex = true;
                event.zoneIndex = zoneIndex;
                event.strength = clamp01((value - config.pulseThreshold) / std::max(0.01f, 1.0f - config.pulseThreshold));
                outEvents.push_back(event);
                tracker.lastTrigger = timestamp;
                rememberTrigger(camId, event.type + std::to_string(zoneIndex), timestamp);
                ofLogNotice("ZoneGestureDetector") << "cam " << camId << " pulse zone " << zoneIndex << " strength " << event.strength;
            }
        }

        tracker.prevSlope = slope;
        tracker.prevValue = value;
    }
}

