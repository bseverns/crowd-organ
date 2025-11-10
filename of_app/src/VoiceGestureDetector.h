#pragma once

#include "GestureEvents.h"
#include "GestureHistory.h"
#include <unordered_map>
#include <vector>

/**
 * VoiceGestureDetector takes the raw trail of positions for a single dancer
 * and tries to name what just happened. Each rule below corresponds directly
 * to the design doc, so if you are teaching or tweaking the vocabulary this
 * is the playground. We aim for clarity over cleverness so newcomers can fork
 * it and build their own signatures.
 */
class VoiceGestureDetector {
public:
    struct Config {
        float raiseDeltaY = 0.18f;
        float lowerDeltaY = 0.18f;
        float swipeDeltaX = 0.25f;
        float swipeOrthogonality = 1.6f;
        float raiseHorizontalLimit = 0.12f;
        float swipeVerticalLimit = 0.18f;
        float shakeRadius = 0.08f;
        int shakeMinSignFlips = 4;
        float shakeMinMotion = 0.08f;
        float burstSpeedThreshold = 1.5f;
        float burstMaxSpeed = 3.5f;
        float holdMotionThreshold = 0.05f;
        uint64_t holdDurationMs = 1200;
        uint64_t minWindowMs = 400;
        uint64_t maxWindowMs = 1200;
        uint64_t gestureCooldownMs = 900;
        uint64_t burstCooldownMs = 600;
        uint64_t holdCooldownMs = 1800;
    };

    VoiceGestureDetector();

    void setConfig(const Config& config);
    const Config& getConfig() const { return config; }

    /**
     * Feed the detector the most recent history for a voice. If a gesture fires
     * we append a VoiceGestureEvent to outEvents. Multiple rules can trigger
     * within one frame as long as their cooldowns allow it.
     */
    void updateVoice(int voiceId, const std::deque<GestureHistory::Sample>& samples, std::vector<VoiceGestureEvent>& outEvents);
    void removeVoice(int voiceId);

private:
    bool canTrigger(int voiceId, const std::string& type, uint64_t timestamp, uint64_t cooldownMs);
    void rememberTrigger(int voiceId, const std::string& type, uint64_t timestamp);

    Config config;
    std::unordered_map<int, std::unordered_map<std::string, uint64_t>> lastTriggerTimes;
};

