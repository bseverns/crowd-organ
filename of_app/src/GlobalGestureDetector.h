#pragma once

#include "GestureEvents.h"
#include <deque>

/**
 * GlobalGestureDetector watches the room-wide motion metrics and decides when
 * the entire crowd erupts or collectively settles. These readings are meant to
 * steer master scenes, so the class keeps hysteresis and cooldowns front and
 * center. If you ever wanted to teach a workshop on crowd sensing, this file is
 * a great conversation starter.
 */
class GlobalGestureDetector {
public:
    struct Config {
        uint64_t historyMs = 5000;
        float eruptionLow = 0.25f;
        float eruptionHigh = 0.7f;
        uint64_t eruptionCooldownMs = 4500;
        uint64_t eruptionWindowMs = 1200;
        float stillnessMotionThreshold = 0.22f;
        uint64_t stillnessDurationMs = 3000;
        int stillnessMinVoices = 3;
        uint64_t stillnessCooldownMs = 6000;
    };

    GlobalGestureDetector();

    void setConfig(const Config& config);
    const Config& getConfig() const { return config; }

    void update(float globalMotion, int activeVoices, uint64_t timestampMs, std::vector<GlobalGestureEvent>& outEvents);
    void reset();

private:
    struct Sample {
        uint64_t timestamp = 0;
        float globalMotion = 0.0f;
        int activeVoices = 0;
    };

    Config config;
    std::deque<Sample> history;
    uint64_t lastEruption = 0;
    uint64_t lastStillness = 0;
    uint64_t stillnessStart = 0;
};

