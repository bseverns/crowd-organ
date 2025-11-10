#include "GlobalGestureDetector.h"

#include "ofLog.h"

#include <algorithm>

namespace {
float clamp01(float value) {
    return ofClamp(value, 0.0f, 1.0f);
}
} // namespace

GlobalGestureDetector::GlobalGestureDetector() = default;

void GlobalGestureDetector::setConfig(const Config& newConfig) {
    config = newConfig;
}

void GlobalGestureDetector::update(float globalMotion, int activeVoices, uint64_t timestampMs, std::vector<GlobalGestureEvent>& outEvents) {
    Sample sample;
    sample.timestamp = timestampMs;
    sample.globalMotion = globalMotion;
    sample.activeVoices = activeVoices;
    history.push_back(sample);

    // Keep only the recent history. The windowed averages below depend on the
    // buffers not growing forever, otherwise older calm sections would drown
    // out new hype.
    uint64_t minTimestamp = (timestampMs > config.historyMs) ? timestampMs - config.historyMs : 0;
    while (!history.empty() && history.front().timestamp < minTimestamp) {
        history.pop_front();
    }

    // Split the history into "recent" and "previous" windows so we can detect
    // a crowd suddenly ramping up compared to its immediate past.
    uint64_t eruptionWindowStart = (timestampMs > config.eruptionWindowMs) ? timestampMs - config.eruptionWindowMs : 0;
    float previousAvg = 0.0f;
    int previousCount = 0;
    float recentAvg = 0.0f;
    int recentCount = 0;

    for (const auto& s : history) {
        if (s.timestamp < eruptionWindowStart) {
            previousAvg += s.globalMotion;
            ++previousCount;
        } else {
            recentAvg += s.globalMotion;
            ++recentCount;
        }
    }

    if (previousCount > 0) {
        previousAvg /= static_cast<float>(previousCount);
    }
    if (recentCount > 0) {
        recentAvg /= static_cast<float>(recentCount);
    }

    // Eruption is hysteretic: the crowd must have been chill, then cross the
    // high threshold. This avoids a single rowdy group spamming the scene.
    if (recentCount > 0 && previousCount > 0 && recentAvg >= config.eruptionHigh && previousAvg <= config.eruptionLow) {
        if (timestampMs >= lastEruption + config.eruptionCooldownMs) {
            GlobalGestureEvent event;
            event.type = "eruption";
            event.strength = clamp01((recentAvg - config.eruptionHigh) / std::max(0.01f, 1.0f - config.eruptionHigh));
            outEvents.push_back(event);
            lastEruption = timestampMs;
            ofLogNotice("GlobalGestureDetector") << "eruption strength " << event.strength << " (recent " << recentAvg << ", prev " << previousAvg << ")";
        }
    }

    // Stillness tracks how long a large portion of the room stays quiet. We
    // only consider it once enough voices are present so solos do not trigger it.
    if (globalMotion <= config.stillnessMotionThreshold && activeVoices >= config.stillnessMinVoices) {
        if (stillnessStart == 0) {
            stillnessStart = timestampMs;
        }
    } else {
        stillnessStart = 0;
    }

    if (stillnessStart > 0) {
        uint64_t stillnessDuration = timestampMs - stillnessStart;
        if (stillnessDuration >= config.stillnessDurationMs) {
            if (timestampMs >= lastStillness + config.stillnessCooldownMs) {
                GlobalGestureEvent event;
                event.type = "stillness";
                float motionStrength = clamp01(1.0f - (recentAvg / std::max(0.01f, config.stillnessMotionThreshold)));
                float voiceStrength = clamp01(static_cast<float>(activeVoices - config.stillnessMinVoices) / std::max(1.0f, static_cast<float>(config.stillnessMinVoices)));
                event.strength = clamp01(0.6f * motionStrength + 0.4f * voiceStrength);
                outEvents.push_back(event);
                lastStillness = timestampMs;
                ofLogNotice("GlobalGestureDetector") << "stillness strength " << event.strength << " duration " << stillnessDuration;
                stillnessStart = timestampMs; // maintain hysteresis
            }
        }
    }
}

void GlobalGestureDetector::reset() {
    history.clear();
    lastEruption = 0;
    lastStillness = 0;
    stillnessStart = 0;
}

