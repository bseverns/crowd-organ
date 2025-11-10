#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

#include "GestureHistory.h"
#include "GlobalGestureDetector.h"
#include "VoiceGestureDetector.h"
#include "ZoneGestureDetector.h"

#include <unordered_map>

/**
 * ofApp is the conductor glue that ties together OSC I/O, gesture detection,
 * and quick diagnostics. The openFrameworks runtime calls the lifecycle hooks,
 * and we wire them up to mirror the architecture doc one-to-one so students can
 * correlate prose to code. Most helper methods below exist purely so each step
 * can be narrated with words and logs.
 */
class ofApp : public ofBaseApp {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void exit() override;

private:
    struct VoiceState {
        glm::vec3 position = glm::vec3(0.0f);
        float size = 0.0f;
        float motion = 0.0f;
        float energy = 0.0f;
        uint64_t lastUpdate = 0;
    };

    struct OscSettings {
        int listenPort = 9000;
        std::string gestureHost = "127.0.0.1";
        int gesturePort = 9001;
        bool enableSending = true;
    } settings;

    void loadSettings();
    void processOscMessages();
    void pruneVoices(uint64_t now);
    void updateVoiceGestures();
    void updateGlobalGestures(uint64_t now);
    void sendVoiceEvent(const VoiceGestureEvent& event);
    void sendZoneEvent(const ZoneGestureEvent& event);
    void sendGlobalEvent(const GlobalGestureEvent& event);

    ofxOscReceiver stateReceiver;
    ofxOscSender gestureSender;

    std::unordered_map<int, VoiceState> voices; // live state for each performer.

    GestureHistory gestureHistory;             // per-voice motion breadcrumbs.
    VoiceGestureDetector voiceDetector;        // per-voice gesture logic.
    ZoneGestureDetector zoneDetector;          // 4x4 grid sweep/pulse logic.
    GlobalGestureDetector globalDetector;      // crowd-wide eruption/stillness.

    float lastGlobalMotion = 0.0f;
    uint64_t lastGlobalMotionTimestamp = 0;
    uint64_t lastZoneUpdate = 0;

    std::size_t voiceHistoryCapacity = 60;
};

