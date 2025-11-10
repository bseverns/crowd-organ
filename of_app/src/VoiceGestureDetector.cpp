#include "VoiceGestureDetector.h"

#include "ofLog.h"

#include <algorithm>
#include <cmath>

namespace {
float clamp01(float value) {
    return ofClamp(value, 0.0f, 1.0f);
}
} // namespace

VoiceGestureDetector::VoiceGestureDetector() = default;

void VoiceGestureDetector::setConfig(const Config& newConfig) {
    config = newConfig;
}

void VoiceGestureDetector::updateVoice(int voiceId, const std::deque<GestureHistory::Sample>& samples, std::vector<VoiceGestureEvent>& outEvents) {
    if (samples.size() < 2) {
        // Not enough breadcrumbs to deduce a pattern yet.
        return;
    }

    const auto& latest = samples.back();
    uint64_t now = latest.timestamp;
    uint64_t minTimestamp = (now > config.maxWindowMs) ? now - config.maxWindowMs : 0;

    // Find the first sample that still lives inside our max window. We scan
    // linearly because the buffers are tiny (< 1 second of frames).
    std::size_t startIdx = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (samples[i].timestamp >= minTimestamp) {
            startIdx = i;
            break;
        }
    }

    const auto& startSample = samples[startIdx];
    uint64_t windowDuration = now - startSample.timestamp;
    if (windowDuration < config.minWindowMs) {
        // We bail early to avoid reading tea leaves from too-short windows.
        return;
    }

    float minX = startSample.position.x;
    float maxX = startSample.position.x;
    float minY = startSample.position.y;
    float maxY = startSample.position.y;
    float cumulativeMotion = 0.0f;
    float maxSpeed = 0.0f;

    int signFlips = 0;
    bool hasPrevX = false;
    bool hasPrevY = false;
    float prevSignX = 0.0f;
    float prevSignY = 0.0f;

    // Walk the window, aggregating extrema, motion, and direction changes. Each
    // metric powers at least one of the gesture rules below.
    for (std::size_t i = startIdx; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        minX = std::min(minX, sample.position.x);
        maxX = std::max(maxX, sample.position.x);
        minY = std::min(minY, sample.position.y);
        maxY = std::max(maxY, sample.position.y);
        cumulativeMotion += sample.motion;
        maxSpeed = std::max(maxSpeed, glm::length(sample.velocity));

        if (i > startIdx) {
            // We do not count microscopic jitters towards shake sign flips, so
            // there is a soft threshold before we care about direction changes.
            if (std::abs(sample.velocity.x) > config.shakeMinMotion * 0.25f) {
                float sign = (sample.velocity.x >= 0.0f) ? 1.0f : -1.0f;
                if (hasPrevX && sign != prevSignX) {
                    ++signFlips;
                }
                prevSignX = sign;
                hasPrevX = true;
            }
            if (std::abs(sample.velocity.y) > config.shakeMinMotion * 0.25f) {
                float sign = (sample.velocity.y >= 0.0f) ? 1.0f : -1.0f;
                if (hasPrevY && sign != prevSignY) {
                    ++signFlips;
                }
                prevSignY = sign;
                hasPrevY = true;
            }
        }
    }

    float avgMotion = cumulativeMotion / static_cast<float>(samples.size() - startIdx);
    glm::vec3 displacement = latest.position - startSample.position;
    float deltaX = displacement.x;
    float deltaY = displacement.y;
    float horizontalSpan = maxX - minX;
    float verticalSpan = maxY - minY;
    float radius = std::max(horizontalSpan, verticalSpan);

    // Raise: significant upward travel with a narrow horizontal footprint.
    if (deltaY <= -config.raiseDeltaY && horizontalSpan <= config.raiseHorizontalLimit) {
        if (canTrigger(voiceId, "raise", now, config.gestureCooldownMs)) {
            VoiceGestureEvent event;
            event.voiceId = voiceId;
            event.type = "raise";
            event.strength = clamp01((-deltaY) / config.raiseDeltaY);
            event.extra = latest.position.y; // handy for mapping to register height.
            outEvents.push_back(event);
            rememberTrigger(voiceId, event.type, now);
            ofLogNotice("VoiceGestureDetector") << "voice " << voiceId << " raise strength " << event.strength;
        }
    }

    // Lower: mirror image of raise, rewarding committed downward travel.
    if (deltaY >= config.lowerDeltaY && horizontalSpan <= config.raiseHorizontalLimit) {
        if (canTrigger(voiceId, "lower", now, config.gestureCooldownMs)) {
            VoiceGestureEvent event;
            event.voiceId = voiceId;
            event.type = "lower";
            event.strength = clamp01(deltaY / config.lowerDeltaY);
            event.extra = latest.position.y;
            outEvents.push_back(event);
            rememberTrigger(voiceId, event.type, now);
            ofLogNotice("VoiceGestureDetector") << "voice " << voiceId << " lower strength " << event.strength;
        }
    }

    float absDeltaX = std::abs(deltaX);
    float absDeltaY = std::abs(deltaY);
    if (absDeltaX >= config.swipeDeltaX && absDeltaX > absDeltaY * config.swipeOrthogonality && absDeltaY <= config.swipeVerticalLimit) {
        std::string type = (deltaX < 0.0f) ? "swipe_left" : "swipe_right";
        if (canTrigger(voiceId, type, now, config.gestureCooldownMs)) {
            VoiceGestureEvent event;
            event.voiceId = voiceId;
            event.type = type;
            event.strength = clamp01(absDeltaX / config.swipeDeltaX);
            event.extra = 0.0f;
            outEvents.push_back(event);
            rememberTrigger(voiceId, event.type, now);
            ofLogNotice("VoiceGestureDetector") << "voice " << voiceId << " " << type << " strength " << event.strength;
        }
    }

    // Shake: small physical footprint but with lots of directional whiplash.
    if (radius <= config.shakeRadius && avgMotion >= config.shakeMinMotion && signFlips >= config.shakeMinSignFlips) {
        if (canTrigger(voiceId, "shake", now, config.gestureCooldownMs)) {
            VoiceGestureEvent event;
            event.voiceId = voiceId;
            event.type = "shake";
            event.strength = clamp01(avgMotion / (config.shakeMinMotion * 2.0f));
            event.extra = 0.0f;
            outEvents.push_back(event);
            rememberTrigger(voiceId, event.type, now);
            ofLogNotice("VoiceGestureDetector") << "voice " << voiceId << " shake strength " << event.strength;
        }
    }

    // Burst: reward sudden spikes in velocity regardless of direction.
    if (maxSpeed >= config.burstSpeedThreshold) {
        if (canTrigger(voiceId, "burst", now, config.burstCooldownMs)) {
            VoiceGestureEvent event;
            event.voiceId = voiceId;
            event.type = "burst";
            float denom = std::max(0.01f, config.burstMaxSpeed - config.burstSpeedThreshold);
            event.strength = clamp01((maxSpeed - config.burstSpeedThreshold) / denom);
            event.extra = 0.0f;
            outEvents.push_back(event);
            rememberTrigger(voiceId, event.type, now);
            ofLogNotice("VoiceGestureDetector") << "voice " << voiceId << " burst strength " << event.strength;
        }
    }

    // Hold: detect stillness sustained beyond the configured patience level.
    uint64_t holdStart = now;
    for (std::size_t i = samples.size(); i-- > startIdx;) {
        if (samples[i].motion > config.holdMotionThreshold) {
            holdStart = samples[i].timestamp;
            break;
        }
        if (i == startIdx) {
            holdStart = samples[startIdx].timestamp;
        }
    }
    uint64_t holdDuration = now - holdStart;
    if (avgMotion <= config.holdMotionThreshold && holdDuration >= config.holdDurationMs) {
        if (canTrigger(voiceId, "hold", now, config.holdCooldownMs)) {
            VoiceGestureEvent event;
            event.voiceId = voiceId;
            event.type = "hold";
            float denom = std::max(0.01f, config.holdMotionThreshold);
            event.strength = clamp01(1.0f - (avgMotion / denom));
            event.extra = clamp01(static_cast<float>(holdDuration) / static_cast<float>(config.holdDurationMs));
            outEvents.push_back(event);
            rememberTrigger(voiceId, event.type, now);
            ofLogNotice("VoiceGestureDetector") << "voice " << voiceId << " hold strength " << event.strength << " duration " << event.extra;
        }
    }
}

void VoiceGestureDetector::removeVoice(int voiceId) {
    lastTriggerTimes.erase(voiceId);
}

bool VoiceGestureDetector::canTrigger(int voiceId, const std::string& type, uint64_t timestamp, uint64_t cooldownMs) {
    auto voiceIt = lastTriggerTimes.find(voiceId);
    if (voiceIt == lastTriggerTimes.end()) {
        return true;
    }
    auto eventIt = voiceIt->second.find(type);
    if (eventIt == voiceIt->second.end()) {
        return true;
    }
    return timestamp >= eventIt->second + cooldownMs;
}

void VoiceGestureDetector::rememberTrigger(int voiceId, const std::string& type, uint64_t timestamp) {
    lastTriggerTimes[voiceId][type] = timestamp;
}

