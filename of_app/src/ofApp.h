#pragma once

#include "ofMain.h"
#include "ofxKinect.h"
#include "ofxOpenCv.h"
#include "ofxOsc.h"

#include "GestureHistory.h"
#include "GlobalGestureDetector.h"
#include "VoiceGestureDetector.h"
#include "VoiceTracker.h"
#include "ZoneGestureDetector.h"

#include <array>
#include <map>
#include <memory>
#include <set>
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
    static constexpr int kCameraCount = 2;

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
        glm::vec3 velocity = glm::vec3(0.0f);
        uint64_t lastUpdate = 0;
        bool active = false;
    };

    struct OscSettings {
        int listenPort = 9000;
        std::string gestureHost = "127.0.0.1"; // legacy fallback for gesture routes
        int gesturePort = 9001;                // legacy fallback for gesture routes
        bool enableSending = true;
        bool enableOscInput = true;
        bool enableSensors = true;
        std::string roomCalibrationFile = "room_calibration.json";

        OscRoute voiceStateRoute{.address = "/room/voice/state", .host = "127.0.0.1", .port = 9000};
        OscRoute voiceActiveRoute{.address = "/room/voice/active", .host = "127.0.0.1", .port = 9000};
        OscRoute voiceNoteRoute{.address = "/room/voice/note", .host = "127.0.0.1", .port = 9000};
        OscRoute globalMotionRoute{.address = "/room/global/motion", .host = "127.0.0.1", .port = 9000};
        OscRoute cameraZonesRoute{.address = "/room/camera/zones", .host = "127.0.0.1", .port = 9000};
        OscRoute voiceGestureRoute{.address = "/room/gesture/voice", .host = "127.0.0.1", .port = 9001};
        OscRoute zoneGestureRoute{.address = "/room/gesture/zone", .host = "127.0.0.1", .port = 9001};
        OscRoute globalGestureRoute{.address = "/room/gesture/global", .host = "127.0.0.1", .port = 9001};
    } settings;

    struct SensorSettings {
        int kinectMinDepthMm = 700;
        int kinectMaxDepthMm = 4000;
        int maxKinectVoices = 8;
        int minBlobArea = 1200;
        int maxBlobArea = 90000;
        float voiceMatchDistance = 0.35f;
        int cameraWidth = 640;
        int cameraHeight = 480;
        int camGridCols = 4;
        int camGridRows = 4;
        float cameraMotionFloor = 0.02f;
        float cameraSmoothing = 0.65f;
    } sensorSettings;

    struct CameraCalibration {
        std::string label;
        int gridCols = 0;
        int gridRows = 0;
        std::set<int> ignoredZones;
        std::unordered_map<int, std::string> zoneLabels;
    };

    struct RoomCalibration {
        std::string roomName = "default";
        std::string notes;
        std::unordered_map<int, CameraCalibration> cameras;
    } roomCalibration;

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

    struct CameraSource {
        ofVideoGrabber grabber;
        ofPixels previousGray;
        bool hasPrevious = false;
        bool ready = false;
        std::vector<float> smoothedZones;
    };

    struct KinectBlob {
        glm::vec3 position = glm::vec3(0.0f);
        float size = 0.0f;
    };

    void loadSettings();
    void loadRoomCalibration();
    void saveRoomCalibration() const;
    void setupSensors();
    void updateSensors(uint64_t now);
    void updateKinectVoices(uint64_t now);
    void updateWebcamMotion(uint64_t now);
    void processOscMessages();
    void pruneVoices(uint64_t now);
    void resetTrackingState(bool emitVoiceInactive);
    void ingestVoiceState(int voiceId, const glm::vec3& position, float size, float motion, float energy, uint64_t now, bool emitTelemetry);
    void ingestCameraZones(int camId, int rows, int cols, const std::vector<float>& zones, uint64_t now, bool emitTelemetry);
    void ingestGlobalMotion(float globalMotion, uint64_t now, bool emitTelemetry);
    bool isZoneIgnored(int camId, int zoneIndex) const;
    int countIgnoredZones() const;
    int getCameraGridCols(int camId) const;
    int getCameraGridRows(int camId) const;
    std::string getCameraLabel(int camId) const;
    std::string getZoneLabel(int camId, int zoneIndex) const;
    void updateVoiceGestures();
    void updateGlobalGestures(uint64_t now);
    void sendVoiceState(int voiceId, const VoiceState& state);
    void sendVoiceActive(int voiceId, bool active);
    void sendVoiceNote(int voiceId, float note, float velocity);
    void sendCameraZones(int camId, int rows, int cols, const std::vector<float>& zones);
    void sendGlobalMotion(float globalMotion);
    void sendVoiceEvent(const VoiceGestureEvent& event);
    void sendZoneEvent(const ZoneGestureEvent& event);
    void sendGlobalEvent(const GlobalGestureEvent& event);
    ofxOscSender& getSenderForRoute(const OscRoute& route);
    void loadGestureConfig();

    ofxOscReceiver stateReceiver;
    std::map<std::pair<std::string, int>, std::unique_ptr<ofxOscSender>> gestureSenders;

    ofxKinect kinect;
    bool kinectReady = false;
    ofxCvGrayscaleImage kinectThresholdImage;
    ofxCvContourFinder contourFinder;
    std::vector<unsigned char> kinectThresholdPixels;
    std::array<CameraSource, kCameraCount> cameraSources;

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
    uint64_t lastOscInputTimestamp = 0;
    uint64_t lastSensorInputTimestamp = 0;
    uint64_t lastTelemetrySendTimestamp = 0;
    int oscBacklogWarnings = 0;

    std::size_t voiceHistoryCapacity = 60;
};
