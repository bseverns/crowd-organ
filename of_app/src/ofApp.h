#pragma once

#include "ofMain.h"
#include "ofxOsc.h"

#include "GestureHistory.h"
#include "GlobalGestureDetector.h"
#include "VoiceGestureDetector.h"
#include "ZoneGestureDetector.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

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
    void keyPressed(int key) override;

private:
    struct OscRoute {
        std::string address = "/room/gesture/voice";
        std::string host = "127.0.0.1";
        int port = 9001;
    };

    struct VoiceState {
        glm::vec3 position = glm::vec3(0.0f);
        float size = 0.0f;
        float motion = 0.0f;
        float energy = 0.0f;
        uint64_t lastUpdate = 0;
    };

    struct OscSettings {
        int listenPort = 9000;
        std::string gestureHost = "127.0.0.1"; // legacy fallback for gesture routes
        int gesturePort = 9001;                // legacy fallback for gesture routes
        bool enableSending = true;

        OscRoute voiceStateRoute{.address = "/room/voice/state", .host = "127.0.0.1", .port = 9000};
        OscRoute voiceGestureRoute{.address = "/room/gesture/voice", .host = "127.0.0.1", .port = 9001};
        OscRoute zoneGestureRoute{.address = "/room/gesture/zone", .host = "127.0.0.1", .port = 9001};
        OscRoute globalGestureRoute{.address = "/room/gesture/global", .host = "127.0.0.1", .port = 9001};
    } settings;

    struct GestureDebug {
        std::string type;
        float strength = 0.0f;
        uint64_t timestamp = 0;
    };

    struct ZoneEventDebug {
        int camId = -1;
        std::string type;
        float strength = 0.0f;
        uint64_t timestamp = 0;
        int zoneIndex = -1;
        bool hasZoneIndex = false;
    };

    void loadSettings();
    void processOscMessages();
    void pruneVoices(uint64_t now);
    void updateVoiceGestures();
    void updateGlobalGestures(uint64_t now);
    void sendVoiceEvent(const VoiceGestureEvent& event);
    void sendZoneEvent(const ZoneGestureEvent& event);
    void sendGlobalEvent(const GlobalGestureEvent& event);
    ofxOscSender& getSenderForRoute(const OscRoute& route);
    void loadGestureConfig();

    ofxOscReceiver stateReceiver;
    std::map<std::pair<std::string, int>, std::unique_ptr<ofxOscSender>> gestureSenders;

    std::unordered_map<int, VoiceState> voices; // live state for each performer.

    std::unordered_map<int, GestureDebug> lastVoiceGestures; // most recent voice event per id.
    std::vector<ZoneEventDebug> recentZoneEvents;            // rolling zone event tape.
    std::vector<GestureDebug> recentGlobalEvents;            // crowd-wide eruptions/stillness pings.

    GestureHistory gestureHistory;             // per-voice motion breadcrumbs.
    VoiceGestureDetector voiceDetector;        // per-voice gesture logic.
    ZoneGestureDetector zoneDetector;          // camera grid sweep/pulse logic.
    GlobalGestureDetector globalDetector;      // crowd-wide eruption/stillness.

    float lastGlobalMotion = 0.0f;
    uint64_t lastGlobalMotionTimestamp = 0;
    uint64_t lastZoneUpdate = 0;

    std::size_t voiceHistoryCapacity = 60;
};

