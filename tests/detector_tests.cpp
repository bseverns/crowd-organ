#include "GestureHistory.h"
#include "GlobalGestureDetector.h"
#include "VoiceGestureDetector.h"
#include "VoiceTracker.h"
#include "ZoneGestureDetector.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

bool hasEvent(const std::vector<VoiceGestureEvent>& events, const std::string& type) {
    for (const auto& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

bool hasEvent(const std::vector<ZoneGestureEvent>& events, const std::string& type) {
    for (const auto& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

bool hasEvent(const std::vector<GlobalGestureEvent>& events, const std::string& type) {
    for (const auto& event : events) {
        if (event.type == type) {
            return true;
        }
    }
    return false;
}

void testHistoryCapsAndVelocity() {
    GestureHistory history;
    history.setCapacity(2);
    history.addSample(3, glm::vec3(0.0f, 0.0f, 0.0f), 0.0f, 0.0f, 0);
    history.addSample(3, glm::vec3(0.5f, 0.0f, 0.0f), 0.0f, 0.0f, 500);
    history.addSample(3, glm::vec3(1.0f, 0.0f, 0.0f), 0.0f, 0.0f, 1000);

    const auto* samples = history.getHistory(3);
    expect(samples != nullptr, "history should exist for voice 3");
    expect(samples->size() == 2, "history should respect configured capacity");
    expect(samples->back().velocity.x > 0.9f && samples->back().velocity.x < 1.1f, "velocity should be derived in units per second");
}

void testVoiceRaise() {
    GestureHistory history;
    history.setCapacity(8);
    history.addSample(1, glm::vec3(0.0f, 0.5f, 0.2f), 0.05f, 0.2f, 0);
    history.addSample(1, glm::vec3(0.0f, 0.4f, 0.2f), 0.06f, 0.2f, 250);
    history.addSample(1, glm::vec3(0.0f, 0.2f, 0.2f), 0.07f, 0.2f, 600);

    VoiceGestureDetector detector;
    auto config = detector.getConfig();
    config.minWindowMs = 100;
    config.maxWindowMs = 1000;
    config.raiseDeltaY = 0.2f;
    config.raiseHorizontalLimit = 0.05f;
    detector.setConfig(config);

    std::vector<VoiceGestureEvent> events;
    detector.updateVoice(1, *history.getHistory(1), events);
    expect(hasEvent(events, "raise"), "voice detector should emit raise for upward travel");
}

void testNonSquareZoneSweep() {
    ZoneGestureDetector detector;
    auto config = detector.getConfig();
    config.sweepMinSteps = 3;
    config.sweepWindowMs = 1000;
    config.sweepMinStrength = 0.2f;
    config.sweepCooldownMs = 0;
    detector.setConfig(config);

    std::vector<ZoneGestureEvent> events;
    detector.updateCamera(0, 2, 4, {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    }, 0, events);
    detector.updateCamera(0, 2, 4, {
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    }, 300, events);
    detector.updateCamera(0, 2, 4, {
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
    }, 600, events);

    expect(hasEvent(events, "sweep_lr_top"), "zone detector should support non-square left-to-right sweeps");
}

void testGlobalStillness() {
    GlobalGestureDetector detector;
    auto config = detector.getConfig();
    config.stillnessMotionThreshold = 0.1f;
    config.stillnessDurationMs = 500;
    config.stillnessMinVoices = 2;
    config.stillnessCooldownMs = 0;
    detector.setConfig(config);

    std::vector<GlobalGestureEvent> events;
    detector.update(0.05f, 2, 100, events);
    detector.update(0.04f, 2, 700, events);

    expect(hasEvent(events, "stillness"), "global detector should emit stillness after quiet duration");
}

void testVoiceTrackerPredictionKeepsFastVoice() {
    VoiceTracker::Config config;
    config.maxMatchDistance = 0.2f;
    config.maxVoices = 4;

    std::vector<VoiceTracker::Track> tracks = {
        {0, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 1000, true},
    };
    std::vector<VoiceTracker::Blob> blobs = {
        {glm::vec3(0.45f, 0.0f, 0.0f), 0.2f},
    };

    auto assignments = VoiceTracker::assign(blobs, tracks, config, 1450);
    expect(assignments.size() == 1, "tracker should assign one fast-moving blob");
    expect(assignments[0].voiceId == 0, "tracker should use predicted position to keep the existing voice id");
}

void testVoiceTrackerAllocatesOnlyFreeSlots() {
    VoiceTracker::Config config;
    config.maxMatchDistance = 0.1f;
    config.maxVoices = 3;

    std::vector<VoiceTracker::Track> tracks = {
        {0, glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f), 1000, true},
        {2, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), 1000, true},
    };
    std::vector<VoiceTracker::Blob> blobs = {
        {glm::vec3(0.0f, 0.0f, 0.0f), 0.2f},
        {glm::vec3(0.2f, 0.0f, 0.0f), 0.2f},
    };

    auto assignments = VoiceTracker::assign(blobs, tracks, config, 1000);
    expect(assignments.size() == 1, "tracker should not exceed max voice slots");
    expect(assignments[0].voiceId == 1, "tracker should allocate the only free voice slot");
}
} // namespace

int main() {
    testHistoryCapsAndVelocity();
    testVoiceRaise();
    testNonSquareZoneSweep();
    testGlobalStillness();
    testVoiceTrackerPredictionKeepsFastVoice();
    testVoiceTrackerAllocatesOnlyFreeSlots();
    std::cout << "detector tests passed" << std::endl;
    return 0;
}
