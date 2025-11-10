#pragma once

#include "GestureEvents.h"
#include <array>
#include <deque>
#include <unordered_map>
#include <vector>

/**
 * ZoneGestureDetector stares at the 4x4 heatmaps coming from each camera and
 * tries to call out sweeps (directional waves of attention) or pulses (sudden
 * localized activity). It is less about individual performers and more about
 * how the crowd leans together. The comments here aim to demystify the state
 * machines so you can riff on them for your own stage layouts.
 */
class ZoneGestureDetector {
public:
    struct Config {
        uint64_t historyMs = 2000;
        uint64_t sweepWindowMs = 900;
        int sweepMinSteps = 3;
        float sweepMinStrength = 0.25f;
        uint64_t sweepCooldownMs = 1600;
        float pulseThreshold = 0.35f;
        float pulseSlopeThreshold = 0.05f;
        uint64_t pulseCooldownMs = 900;
    };

    ZoneGestureDetector();

    void setConfig(const Config& config);
    const Config& getConfig() const { return config; }

    void updateCamera(int camId, const std::array<float, 16>& zones, uint64_t timestampMs, std::vector<ZoneGestureEvent>& outEvents);
    void removeCamera(int camId);

private:
    struct ZoneSample {
        uint64_t timestamp = 0;
        std::array<float, 16> values{};
    };

    struct PulseTracker {
        bool initialized = false;
        float prevValue = 0.0f;
        float prevSlope = 0.0f;
        uint64_t lastTrigger = 0;
    };

    bool canTrigger(int camId, const std::string& type, uint64_t timestamp, uint64_t cooldownMs);
    void rememberTrigger(int camId, const std::string& type, uint64_t timestamp);
    void detectSweeps(int camId, const std::deque<ZoneSample>& history, std::vector<ZoneGestureEvent>& outEvents);
    void detectPulses(int camId, const ZoneSample& sample, std::vector<ZoneGestureEvent>& outEvents);

    Config config;
    std::unordered_map<int, std::deque<ZoneSample>> histories;
    std::unordered_map<int, std::unordered_map<std::string, uint64_t>> lastTriggerTimes;
    std::unordered_map<int, std::array<PulseTracker, 16>> pulseTrackers;
};

