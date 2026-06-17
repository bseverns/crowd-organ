#include "ofApp.h"

#include "ofJson.h"
#include "ofLog.h"
#include "ofxJSON.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace {
constexpr int kMinOscPort = 1;
constexpr int kMaxOscPort = 65535;
constexpr int kMaxVoiceId = 63;
constexpr int kMaxCameraId = 15;
constexpr int kMaxGridRows = 16;
constexpr int kMaxGridCols = 16;
constexpr int kMaxZoneValues = kMaxGridRows * kMaxGridCols;
constexpr int kMaxOscMessagesPerFrame = 512;
constexpr int kMinCameraDimension = 160;
constexpr int kMaxCameraDimension = 1920;

uint64_t nowMillis() {
    return static_cast<uint64_t>(ofGetElapsedTimeMillis());
}

bool isValidOscAddress(const std::string& address) {
    return !address.empty() && address.front() == '/';
}

bool isValidHost(const std::string& host) {
    return !host.empty();
}

bool isSafeDataRelativeJsonPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos) {
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return path.size() > 5 && path.substr(path.size() - 5) == ".json";
}

int clampInt(int value, int minValue, int maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

float clampFloat(float value, float minValue, float maxValue) {
    if (!std::isfinite(value)) {
        return minValue;
    }
    return std::max(minValue, std::min(value, maxValue));
}

uint64_t clampUInt64(uint64_t value, uint64_t minValue, uint64_t maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

int readJsonInt(const ofJson& json, const std::string& key, int current, int minValue, int maxValue) {
    if (!json.contains(key)) {
        return current;
    }
    const auto& node = json[key];
    if (!node.is_number_integer()) {
        ofLogWarning("settings") << key << " must be an integer; keeping " << current;
        return current;
    }
    int value = node.get<int>();
    int clamped = clampInt(value, minValue, maxValue);
    if (value != clamped) {
        ofLogWarning("settings") << key << " out of range (" << value << "); clamped to " << clamped;
    }
    return clamped;
}

bool readJsonBool(const ofJson& json, const std::string& key, bool current) {
    if (!json.contains(key)) {
        return current;
    }
    const auto& node = json[key];
    if (!node.is_boolean()) {
        ofLogWarning("settings") << key << " must be a boolean; keeping " << (current ? "true" : "false");
        return current;
    }
    return node.get<bool>();
}

std::string readJsonString(const ofJson& json, const std::string& key, const std::string& current) {
    if (!json.contains(key)) {
        return current;
    }
    const auto& node = json[key];
    if (!node.is_string()) {
        ofLogWarning("settings") << key << " must be a string; keeping " << current;
        return current;
    }
    return node.get<std::string>();
}

std::string formatAddress(std::string pattern, std::optional<int> voiceId = std::nullopt,
                          std::optional<int> camId = std::nullopt, std::optional<int> zoneIndex = std::nullopt,
                          std::optional<std::string> type = std::nullopt) {
    auto replaceAll = [](std::string& target, const std::string& from, const std::string& to) {
        std::size_t startPos = 0;
        while ((startPos = target.find(from, startPos)) != std::string::npos) {
            target.replace(startPos, from.length(), to);
            startPos += to.length();
        }
    };

    if (voiceId.has_value()) {
        replaceAll(pattern, "{id}", ofToString(*voiceId));
        replaceAll(pattern, "{voiceId}", ofToString(*voiceId));
    }
    if (zoneIndex.has_value()) {
        replaceAll(pattern, "{id}", ofToString(*zoneIndex));
        replaceAll(pattern, "{zoneIndex}", ofToString(*zoneIndex));
    }
    if (camId.has_value()) {
        replaceAll(pattern, "{camId}", ofToString(*camId));
    }
    if (type.has_value()) {
        replaceAll(pattern, "{type}", *type);
    }
    return pattern;
}
} // namespace

void ofApp::setup() {
    // Keep the render loop predictable so gesture windows measured in frames
    // roughly align with milliseconds in configs.
    ofSetFrameRate(60);
    ofSetVerticalSync(true);

    loadSettings();
    loadRoomCalibration();
    loadGestureConfig();
    setupSensors();

    // One receiver for the raw crowd telemetry, one sender for our gestures.
    if (settings.enableOscInput) {
        stateReceiver.setup(settings.listenPort);
    }
    if (settings.enableSending) {
        // Warm up senders for each configured route so failures are loud during boot.
        getSenderForRoute(settings.voiceStateRoute);
        getSenderForRoute(settings.voiceActiveRoute);
        getSenderForRoute(settings.voiceNoteRoute);
        getSenderForRoute(settings.globalMotionRoute);
        getSenderForRoute(settings.cameraZonesRoute);
        getSenderForRoute(settings.voiceGestureRoute);
        getSenderForRoute(settings.zoneGestureRoute);
        getSenderForRoute(settings.globalGestureRoute);
    }

    // Let configs tune how far back we remember per-voice history.
    gestureHistory.setCapacity(voiceHistoryCapacity);

    ofLogNotice() << "CrowdOrganHost "
                  << (settings.enableOscInput ? "listening for replay/bridge motion on port " + ofToString(settings.listenPort) : "OSC input disabled")
                  << ", sensors " << (settings.enableSensors ? "enabled" : "disabled")
                  << ", emitting to configured routes (see gesture_settings.json).";
}

void ofApp::update() {
    uint64_t now = nowMillis();
    updateSensors(now);        // production Kinect/webcam feature source
    processOscMessages();      // optional replay/bridge samples
    pruneVoices(now);          // toss stale performers so cooldowns reset
    updateVoiceGestures();     // per-voice raise/swipe/etc.
    updateGlobalGestures(now); // crowd-wide eruption/stillness
}

void ofApp::draw() {
    ofBackground(12);

    uint64_t now = nowMillis();
    float margin = 18.0f;
    float stageWidth = ofGetWidth() * 0.55f;
    float stageHeight = ofGetHeight() * 0.6f;
    ofRectangle stageRect(margin, margin, stageWidth, stageHeight);

    auto toScreen = [&](const glm::vec3& position) {
        float x = ofMap(position.x, -1.0f, 1.0f, stageRect.getLeft(), stageRect.getRight(), true);
        float y = ofMap(position.y, -1.0f, 1.0f, stageRect.getBottom(), stageRect.getTop(), true);
        return glm::vec2(x, y);
    };
    auto ageLabel = [&](uint64_t timestamp) {
        if (timestamp == 0) {
            return std::string("never");
        }
        return ofToString(now > timestamp ? now - timestamp : 0) + "ms ago";
    };

    // Header / summary panel.
    std::stringstream ss;
    ss << "Crowd Organ Host – full host pilot" << std::endl;
    ss << "room: " << roomCalibration.roomName << std::endl;
    ss << "voices tracked: " << voices.size() << std::endl;
    ss << "global motion: " << ofToString(lastGlobalMotion, 2) << std::endl;
    ss << "sources: sensors " << (settings.enableSensors ? (kinectReady ? "kinect" : "waiting") : "off")
       << " / osc " << (settings.enableOscInput ? "on" : "off") << std::endl;
    ss << "health: sensor " << ageLabel(lastSensorInputTimestamp)
       << " · osc " << ageLabel(lastOscInputTimestamp)
       << " · telemetry " << ageLabel(lastTelemetrySendTimestamp)
       << " · backlog warnings " << oscBacklogWarnings << std::endl;
    ss << "calibration: ignored zones " << countIgnoredZones() << std::endl;
    ss << "gesture out: " << settings.voiceGestureRoute.host << ":" << settings.voiceGestureRoute.port
       << " (voice path " << settings.voiceGestureRoute.address << ")" << std::endl;
    ss << "            " << settings.zoneGestureRoute.host << ":" << settings.zoneGestureRoute.port << " (zone path "
       << settings.zoneGestureRoute.address << ")" << std::endl;
    ss << "            " << settings.globalGestureRoute.host << ":" << settings.globalGestureRoute.port << " (global path "
       << settings.globalGestureRoute.address << ")";
    if (!settings.enableSending) {
        ss << " (muted)";
    }
    ss << std::endl;
    ss << "history window: " << gestureHistory.getCapacity() << " frames" << std::endl;
    ss << "zone feeds: " << (zoneDetector.getHistories().empty() ? "waiting" : "live") << std::endl;

    ofDrawBitmapStringHighlight(ss.str(), stageRect.getLeft(), stageRect.getTop() - 8, ofColor(0, 128, 128, 180), ofColor::white);

    // Stage sandbox: blob proxies + motion breadcrumbs.
    ofPushStyle();
    ofSetColor(24);
    ofDrawRectangle(stageRect);
    ofPopStyle();

    ofPushStyle();
    ofNoFill();
    ofSetColor(80, 180, 240, 180);
    ofDrawRectangle(stageRect);
    ofPopStyle();

    for (const auto& kv : voices) {
        int voiceId = kv.first;
        const VoiceState& state = kv.second;
        glm::vec2 screenPos = toScreen(state.position);

        const auto* history = gestureHistory.getHistory(voiceId);
        if (history && !history->empty()) {
            ofPolyline trail;
            glm::vec2 minPos = toScreen(history->front().position);
            glm::vec2 maxPos = minPos;
            for (const auto& sample : *history) {
                glm::vec2 pt = toScreen(sample.position);
                minPos = glm::min(minPos, pt);
                maxPos = glm::max(maxPos, pt);
                trail.addVertex(pt);
            }
            ofPushStyle();
            ofSetColor(80, 220, 160, 160);
            trail.simplify(0.4f);
            trail.draw();
            ofNoFill();
            ofSetColor(255, 180);
            ofDrawRectangle(ofRectangle(minPos, maxPos - minPos));
            ofPopStyle();
        }

        float radius = ofMap(state.size, 0.0f, 1.0f, 10.0f, 40.0f, true);
        ofColor blobColor = ofColor::fromHsb(static_cast<uint8_t>(ofMap(voiceId % 8, 0, 7, 40, 180)), 200, 240);
        blobColor.a = 210;
        ofPushStyle();
        ofSetColor(blobColor);
        ofDrawCircle(screenPos, radius);
        ofPopStyle();

        ofPushStyle();
        ofSetColor(255);
        std::stringstream label;
        label << "voice " << voiceId << "\nsize " << ofToString(state.size, 2) << "\nmotion " << ofToString(state.motion, 2);
        ofDrawBitmapString(label.str(), screenPos.x + radius + 6.0f, screenPos.y - radius);
        ofPopStyle();
    }

    // Gesture confidence bars + cooldown callouts.
    float barY = stageRect.getBottom() + margin * 1.2f;
    float barHeight = 14.0f;
    int voiceRow = 0;
    const auto& voiceCooldowns = voiceDetector.getLastTriggerTimes();
    const auto voiceConfig = voiceDetector.getConfig();
    for (const auto& kv : voices) {
        int voiceId = kv.first;
        auto barTop = barY + voiceRow * (barHeight + 10.0f);
        ofDrawBitmapStringHighlight("voice " + std::to_string(voiceId), stageRect.getLeft(), barTop + barHeight, ofColor(0, 90, 90, 150), ofColor::white);

        auto debugIt = lastVoiceGestures.find(voiceId);
        if (debugIt != lastVoiceGestures.end()) {
            const GestureDebug& dbg = debugIt->second;
            float ageMs = static_cast<float>(now > dbg.timestamp ? now - dbg.timestamp : 0);
            float alpha = ofClamp(1.0f - ageMs / 3000.0f, 0.15f, 1.0f);
            float barWidth = stageRect.getWidth() * 0.55f;
            float filled = barWidth * ofClamp(dbg.strength, 0.0f, 1.0f);

            uint64_t remainingMs = 0;
            auto vcIt = voiceCooldowns.find(voiceId);
            if (vcIt != voiceCooldowns.end()) {
                auto typeIt = vcIt->second.find(dbg.type);
                if (typeIt != vcIt->second.end()) {
                    uint64_t cooldownMs = voiceConfig.gestureCooldownMs;
                    if (dbg.type.find("burst") != std::string::npos) {
                        cooldownMs = voiceConfig.burstCooldownMs;
                    } else if (dbg.type.find("hold") != std::string::npos) {
                        cooldownMs = voiceConfig.holdCooldownMs;
                    }
                    uint64_t readyAt = typeIt->second + cooldownMs;
                    remainingMs = (now < readyAt) ? readyAt - now : 0;
                }
            }

            ofPushStyle();
            ofSetColor(140, 240, 180, static_cast<int>(200 * alpha));
            ofDrawRectangle(stageRect.getLeft() + 120.0f, barTop, filled, barHeight);
            ofNoFill();
            ofSetColor(80, 200, 240, static_cast<int>(190 * alpha));
            ofDrawRectangle(stageRect.getLeft() + 120.0f, barTop, barWidth, barHeight);
            ofPopStyle();

            std::stringstream gestureLabel;
            gestureLabel << dbg.type << " " << ofToString(dbg.strength, 2);
            if (remainingMs > 0) {
                gestureLabel << " · cooldown " << remainingMs << "ms";
            }
            ofDrawBitmapString(gestureLabel.str(), stageRect.getLeft() + 124.0f, barTop - 4.0f + barHeight);
        }

        voiceRow++;
    }

    // Zone heatmaps + pulse/sweep cooldowns.
    float panelX = stageRect.getRight() + margin;
    float panelWidth = ofGetWidth() - panelX - margin;
    float heatmapHeight = stageHeight * 0.48f;
    int camIdx = 0;

    const auto& histories = zoneDetector.getHistories();
    const auto& zoneCooldowns = zoneDetector.getLastTriggerTimes();
    for (const auto& kv : histories) {
        int camId = kv.first;
        const auto& history = kv.second;
        if (history.empty()) {
            continue;
        }

        float yOffset = stageRect.getTop() + camIdx * (heatmapHeight + margin);
        ofRectangle mapRect(panelX, yOffset, panelWidth, heatmapHeight);
        ofPushStyle();
        ofSetColor(28);
        ofDrawRectangle(mapRect);
        ofPopStyle();

        const auto& sample = history.back();
        int rows = std::max(1, sample.rows);
        int cols = std::max(1, sample.cols);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                int idx = row * cols + col;
                if (idx >= static_cast<int>(sample.values.size())) {
                    continue;
                }
                float value = ofClamp(sample.values[idx], 0.0f, 1.0f);
                ofColor cellColor;
                cellColor.setHsb(static_cast<uint8_t>(ofMap(value, 0.0f, 1.0f, 160, 12)), 200, ofMap(value, 0.0f, 1.0f, 60, 255));
                ofRectangle cellRect(mapRect.getLeft() + col * (mapRect.getWidth() / static_cast<float>(cols)),
                                     mapRect.getTop() + row * (mapRect.getHeight() / static_cast<float>(rows)),
                                     mapRect.getWidth() / static_cast<float>(cols),
                                     mapRect.getHeight() / static_cast<float>(rows));
                ofPushStyle();
                ofSetColor(cellColor);
                ofDrawRectangle(cellRect);
                if (isZoneIgnored(camId, idx)) {
                    ofSetColor(0, 180);
                    ofDrawRectangle(cellRect);
                    ofSetColor(255, 80, 80, 220);
                    ofDrawLine(cellRect.getTopLeft(), cellRect.getBottomRight());
                    ofDrawLine(cellRect.getTopRight(), cellRect.getBottomLeft());
                }
                ofPopStyle();

                std::string zoneLabel = getZoneLabel(camId, idx);
                if (!zoneLabel.empty()) {
                    ofDrawBitmapStringHighlight(zoneLabel, cellRect.getLeft() + 4.0f, cellRect.getTop() + 14.0f,
                                                ofColor(0, 0, 0, 120), ofColor::white);
                }
            }
        }

        ofPushStyle();
        ofNoFill();
        ofSetColor(200, 180);
        ofDrawRectangle(mapRect);
        ofPopStyle();

        std::stringstream mapLabel;
        mapLabel << "cam " << camId;
        std::string cameraLabel = getCameraLabel(camId);
        if (!cameraLabel.empty()) {
            mapLabel << " · " << cameraLabel;
        }
        mapLabel << " · last zone frame " << (now > sample.timestamp ? now - sample.timestamp : 0) << "ms ago";
        ofDrawBitmapStringHighlight(mapLabel.str(), mapRect.getLeft() + 6.0f, mapRect.getTop() + 16.0f, ofColor(0, 0, 0, 150), ofColor::white);

        auto cooldownIt = zoneCooldowns.find(camId);
        if (cooldownIt != zoneCooldowns.end()) {
            int line = 0;
            for (const auto& cd : cooldownIt->second) {
                const auto& type = cd.first;
                uint64_t lastFire = cd.second;
                uint64_t cooldownMs = (type.find("pulse") != std::string::npos) ? zoneDetector.getConfig().pulseCooldownMs : zoneDetector.getConfig().sweepCooldownMs;
                uint64_t readyAt = lastFire + cooldownMs;
                if (now < readyAt) {
                    std::stringstream cooldownMsg;
                    cooldownMsg << type << " ready in " << (readyAt - now) << "ms";
                    ofDrawBitmapStringHighlight(cooldownMsg.str(), mapRect.getLeft() + 6.0f, mapRect.getBottom() - 6.0f - line * 16.0f,
                                                ofColor(0, 0, 0, 140), ofColor::white);
                    line++;
                }
            }
        }

        camIdx++;
    }

    // Global gesture tape + recency hints.
    float globalY = stageRect.getBottom() + margin * 1.2f;
    float globalX = panelX;
    ofDrawBitmapStringHighlight("global gestures", globalX, globalY - 6.0f, ofColor(0, 60, 120, 160), ofColor::white);
    int gLine = 0;
    for (auto it = recentGlobalEvents.rbegin(); it != recentGlobalEvents.rend() && gLine < 4; ++it, ++gLine) {
        uint64_t age = now > it->timestamp ? now - it->timestamp : 0;
        std::stringstream g;
        g << it->type << " · " << ofToString(it->strength, 2) << " (" << age << "ms ago)";
        ofDrawBitmapString(g.str(), globalX, globalY + 16.0f * gLine + 6.0f);
    }

    uint64_t eruptionReady = 0;
    if (globalDetector.getLastEruptionTimestamp() > 0) {
        uint64_t readyAt = globalDetector.getLastEruptionTimestamp() + globalDetector.getConfig().eruptionCooldownMs;
        eruptionReady = (now < readyAt) ? readyAt - now : 0;
    }
    uint64_t stillReady = 0;
    if (globalDetector.getLastStillnessTimestamp() > 0) {
        uint64_t readyAt = globalDetector.getLastStillnessTimestamp() + globalDetector.getConfig().stillnessCooldownMs;
        stillReady = (now < readyAt) ? readyAt - now : 0;
    }
    std::stringstream gFooter;
    gFooter << "eruption cooldown: " << eruptionReady << "ms\nstillness cooldown: " << stillReady << "ms";
    ofDrawBitmapStringHighlight(gFooter.str(), globalX, globalY + 76.0f, ofColor(0, 0, 0, 160), ofColor::white);

    // Event ticker so performers can see what just fired.
    float tickerY = ofGetHeight() - margin * 0.75f;
    std::stringstream ticker;
    ticker << "zone events: ";
    int eventsShown = 0;
    for (auto it = recentZoneEvents.rbegin(); it != recentZoneEvents.rend() && eventsShown < 5; ++it, ++eventsShown) {
        ticker << it->type;
        if (it->hasZoneIndex) {
            ticker << "#" << it->zoneIndex;
        }
        ticker << "@" << it->camId << " (" << it->strength << ")  ";
    }
    ofDrawBitmapStringHighlight(ticker.str(), stageRect.getLeft(), tickerY, ofColor(60, 0, 60, 160), ofColor::white);
}

void ofApp::exit() {
    if (kinectReady) {
        kinect.close();
        kinectReady = false;
    }
    for (auto& source : cameraSources) {
        if (source.ready) {
            source.grabber.close();
            source.ready = false;
        }
    }
    ofLogNotice() << "CrowdOrganHost shutting down.";
}

void ofApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        loadRoomCalibration();
        loadGestureConfig();
    } else if (key == 'c' || key == 'C') {
        saveRoomCalibration();
    }
}

void ofApp::loadSettings() {
    // We keep configuration lightweight: a single JSON file in bin/data/ so
    // touring rigs can tweak ports without recompiling.
    const std::string settingsPath = ofToDataPath("gesture_settings.json");
    if (!ofFile::doesFileExist(settingsPath)) {
        ofLogWarning() << "gesture_settings.json not found at " << settingsPath
                       << " – using defaults (" << settings.listenPort << ", " << settings.gestureHost << ":" << settings.gesturePort
                       << ")";
        return;
    }

    ofJson json;
    try {
        json = ofLoadJson(settingsPath);
    } catch (const std::exception& e) {
        ofLogError("settings") << "failed to parse gesture_settings.json: " << e.what() << "; using defaults";
        return;
    }

    settings.listenPort = readJsonInt(json, "listen_port", settings.listenPort, kMinOscPort, kMaxOscPort);
    settings.gestureHost = readJsonString(json, "gesture_host", settings.gestureHost);
    if (!isValidHost(settings.gestureHost)) {
        ofLogWarning("settings") << "gesture_host is empty; keeping 127.0.0.1";
        settings.gestureHost = "127.0.0.1";
    }
    settings.gesturePort = readJsonInt(json, "gesture_port", settings.gesturePort, kMinOscPort, kMaxOscPort);
    settings.enableSending = readJsonBool(json, "enable_sending", settings.enableSending);
    settings.enableOscInput = readJsonBool(json, "enable_osc_input", settings.enableOscInput);
    settings.enableSensors = readJsonBool(json, "enable_sensors", settings.enableSensors);
    std::string calibrationFile = readJsonString(json, "room_calibration_file", settings.roomCalibrationFile);
    if (isSafeDataRelativeJsonPath(calibrationFile)) {
        settings.roomCalibrationFile = calibrationFile;
    } else {
        ofLogWarning("settings") << "room_calibration_file must be a relative .json path under bin/data; keeping "
                                 << settings.roomCalibrationFile;
    }

    if (json.contains("sensors")) {
        const auto& sensors = json["sensors"];
        if (sensors.is_object()) {
            sensorSettings.kinectMinDepthMm = readJsonInt(sensors, "kinect_min_depth_mm", sensorSettings.kinectMinDepthMm, 1, 10000);
            sensorSettings.kinectMaxDepthMm = readJsonInt(sensors, "kinect_max_depth_mm", sensorSettings.kinectMaxDepthMm, sensorSettings.kinectMinDepthMm + 1, 10000);
            sensorSettings.maxKinectVoices = readJsonInt(sensors, "max_kinect_voices", sensorSettings.maxKinectVoices, 1, kMaxVoiceId + 1);
            sensorSettings.minBlobArea = readJsonInt(sensors, "min_blob_area", sensorSettings.minBlobArea, 1, 500000);
            sensorSettings.maxBlobArea = readJsonInt(sensors, "max_blob_area", sensorSettings.maxBlobArea, sensorSettings.minBlobArea + 1, 2000000);
            sensorSettings.cameraWidth = readJsonInt(sensors, "camera_width", sensorSettings.cameraWidth, kMinCameraDimension, kMaxCameraDimension);
            sensorSettings.cameraHeight = readJsonInt(sensors, "camera_height", sensorSettings.cameraHeight, kMinCameraDimension, kMaxCameraDimension);
            sensorSettings.camGridCols = readJsonInt(sensors, "cam_grid_cols", sensorSettings.camGridCols, 1, kMaxGridCols);
            sensorSettings.camGridRows = readJsonInt(sensors, "cam_grid_rows", sensorSettings.camGridRows, 1, kMaxGridRows);

            if (sensors.contains("voice_match_distance") && sensors["voice_match_distance"].is_number()) {
                sensorSettings.voiceMatchDistance = clampFloat(sensors["voice_match_distance"].get<float>(), 0.01f, 2.0f);
            }
            if (sensors.contains("camera_motion_floor") && sensors["camera_motion_floor"].is_number()) {
                sensorSettings.cameraMotionFloor = clampFloat(sensors["camera_motion_floor"].get<float>(), 0.0f, 1.0f);
            }
            if (sensors.contains("camera_smoothing") && sensors["camera_smoothing"].is_number()) {
                sensorSettings.cameraSmoothing = clampFloat(sensors["camera_smoothing"].get<float>(), 0.0f, 0.99f);
            }
        } else {
            ofLogWarning("settings") << "sensors must be an object; keeping sensor defaults";
        }
    }

    // Sync legacy host/port values into the gesture routes so folks can quickly
    // steer everything without touching the per-route section. Voice state keeps
    // its dashboard-friendly defaults unless explicitly overridden.
    settings.voiceGestureRoute.host = settings.gestureHost;
    settings.zoneGestureRoute.host = settings.gestureHost;
    settings.globalGestureRoute.host = settings.gestureHost;
    settings.voiceGestureRoute.port = settings.gesturePort;
    settings.zoneGestureRoute.port = settings.gesturePort;
    settings.globalGestureRoute.port = settings.gesturePort;

    // Optional route overrides let you route each logical event to a bespoke address/host/port.
    auto loadRoute = [](const ofJson& routesJson, const std::string& key, OscRoute& route) {
        if (!routesJson.contains(key)) {
            return;
        }
        const auto& node = routesJson[key];
        if (!node.is_object()) {
            ofLogWarning("settings") << "routes." << key << " must be an object; keeping defaults";
            return;
        }
        if (node.contains("address")) {
            std::string address = readJsonString(node, "address", route.address);
            if (isValidOscAddress(address)) {
                route.address = address;
            } else {
                ofLogWarning("settings") << "routes." << key << ".address must start with /; keeping " << route.address;
            }
        }
        if (node.contains("host")) {
            std::string host = readJsonString(node, "host", route.host);
            if (isValidHost(host)) {
                route.host = host;
            } else {
                ofLogWarning("settings") << "routes." << key << ".host is empty; keeping " << route.host;
            }
        }
        if (node.contains("port")) {
            route.port = readJsonInt(node, "port", route.port, kMinOscPort, kMaxOscPort);
        }
    };

    if (json.contains("routes")) {
        const auto& routes = json["routes"];
        if (routes.is_object()) {
            loadRoute(routes, "voice_state", settings.voiceStateRoute);
            loadRoute(routes, "voice_active", settings.voiceActiveRoute);
            loadRoute(routes, "voice_note", settings.voiceNoteRoute);
            loadRoute(routes, "global_motion", settings.globalMotionRoute);
            loadRoute(routes, "camera_zones", settings.cameraZonesRoute);
            loadRoute(routes, "voice_gesture", settings.voiceGestureRoute);
            loadRoute(routes, "zone_gesture", settings.zoneGestureRoute);
            loadRoute(routes, "global_gesture", settings.globalGestureRoute);
        } else {
            ofLogWarning("settings") << "routes must be an object; keeping route defaults";
        }
    }

    ofLogNotice() << "OSC routes resolved:"
                  << " voice state " << settings.voiceStateRoute.host << ":" << settings.voiceStateRoute.port << " "
                  << settings.voiceStateRoute.address << "; voice gesture " << settings.voiceGestureRoute.host << ":"
                  << settings.voiceGestureRoute.port << " " << settings.voiceGestureRoute.address << "; zone gesture "
                  << settings.zoneGestureRoute.host << ":" << settings.zoneGestureRoute.port << " "
                  << settings.zoneGestureRoute.address << "; global gesture " << settings.globalGestureRoute.host << ":"
                  << settings.globalGestureRoute.port << " " << settings.globalGestureRoute.address;
}

void ofApp::loadRoomCalibration() {
    const std::string calibrationPath = ofToDataPath(settings.roomCalibrationFile);
    if (!ofFile::doesFileExist(calibrationPath)) {
        ofLogWarning("calibration") << settings.roomCalibrationFile << " not found at " << calibrationPath
                                    << " – using sensor defaults";
        return;
    }

    ofJson json;
    try {
        json = ofLoadJson(calibrationPath);
    } catch (const std::exception& e) {
        ofLogError("calibration") << "failed to parse " << settings.roomCalibrationFile << ": " << e.what();
        return;
    }

    RoomCalibration loaded;
    loaded.roomName = readJsonString(json, "room_name", roomCalibration.roomName);
    loaded.notes = readJsonString(json, "notes", roomCalibration.notes);

    if (json.contains("kinect") && json["kinect"].is_object()) {
        const auto& kinectJson = json["kinect"];
        sensorSettings.kinectMinDepthMm = readJsonInt(kinectJson, "min_depth_mm", sensorSettings.kinectMinDepthMm, 1, 10000);
        sensorSettings.kinectMaxDepthMm = readJsonInt(kinectJson, "max_depth_mm", sensorSettings.kinectMaxDepthMm, sensorSettings.kinectMinDepthMm + 1, 10000);
        sensorSettings.minBlobArea = readJsonInt(kinectJson, "min_blob_area", sensorSettings.minBlobArea, 1, 500000);
        sensorSettings.maxBlobArea = readJsonInt(kinectJson, "max_blob_area", sensorSettings.maxBlobArea, sensorSettings.minBlobArea + 1, 2000000);
        sensorSettings.maxKinectVoices = readJsonInt(kinectJson, "max_voices", sensorSettings.maxKinectVoices, 1, kMaxVoiceId + 1);
        if (kinectJson.contains("voice_match_distance") && kinectJson["voice_match_distance"].is_number()) {
            sensorSettings.voiceMatchDistance = clampFloat(kinectJson["voice_match_distance"].get<float>(), 0.01f, 2.0f);
        }
    }

    if (json.contains("cameras") && json["cameras"].is_array()) {
        for (const auto& cameraJson : json["cameras"]) {
            if (!cameraJson.is_object()) {
                continue;
            }
            int camId = readJsonInt(cameraJson, "id", -1, 0, kMaxCameraId);
            if (camId < 0) {
                ofLogWarning("calibration") << "camera entry missing valid id; skipping";
                continue;
            }

            CameraCalibration camera;
            camera.label = readJsonString(cameraJson, "label", "");

            if (cameraJson.contains("grid") && cameraJson["grid"].is_object()) {
                const auto& gridJson = cameraJson["grid"];
                camera.gridCols = readJsonInt(gridJson, "cols", sensorSettings.camGridCols, 1, kMaxGridCols);
                camera.gridRows = readJsonInt(gridJson, "rows", sensorSettings.camGridRows, 1, kMaxGridRows);
            }

            if (cameraJson.contains("ignored_zones") && cameraJson["ignored_zones"].is_array()) {
                for (const auto& zoneJson : cameraJson["ignored_zones"]) {
                    if (!zoneJson.is_number_integer()) {
                        continue;
                    }
                    int zoneIndex = zoneJson.get<int>();
                    if (zoneIndex >= 0 && zoneIndex < kMaxZoneValues) {
                        camera.ignoredZones.insert(zoneIndex);
                    }
                }
            }

            if (cameraJson.contains("zone_labels") && cameraJson["zone_labels"].is_object()) {
                for (const auto& item : cameraJson["zone_labels"].items()) {
                    try {
                        int zoneIndex = std::stoi(item.key());
                        if (zoneIndex >= 0 && zoneIndex < kMaxZoneValues && item.value().is_string()) {
                            camera.zoneLabels[zoneIndex] = item.value().get<std::string>();
                        }
                    } catch (const std::exception&) {
                        ofLogWarning("calibration") << "zone label key '" << item.key() << "' is not numeric; skipping";
                    }
                }
            }

            loaded.cameras[camId] = camera;
        }
    }

    roomCalibration = loaded;
    for (int camId = 0; camId < kCameraCount; ++camId) {
        auto& source = cameraSources[camId];
        source.smoothedZones.assign(getCameraGridRows(camId) * getCameraGridCols(camId), 0.0f);
        source.hasPrevious = false;
    }

    ofLogNotice("calibration") << "loaded room '" << roomCalibration.roomName << "' with "
                               << roomCalibration.cameras.size() << " calibrated camera entries";
}

void ofApp::saveRoomCalibration() const {
    ofJson json;
    json["room_name"] = roomCalibration.roomName;
    json["notes"] = roomCalibration.notes;
    json["kinect"] = {
        {"min_depth_mm", sensorSettings.kinectMinDepthMm},
        {"max_depth_mm", sensorSettings.kinectMaxDepthMm},
        {"min_blob_area", sensorSettings.minBlobArea},
        {"max_blob_area", sensorSettings.maxBlobArea},
        {"max_voices", sensorSettings.maxKinectVoices},
        {"voice_match_distance", sensorSettings.voiceMatchDistance},
    };

    json["cameras"] = ofJson::array();
    for (int camId = 0; camId < kCameraCount; ++camId) {
        CameraCalibration camera;
        auto it = roomCalibration.cameras.find(camId);
        if (it != roomCalibration.cameras.end()) {
            camera = it->second;
        }

        ofJson cameraJson;
        cameraJson["id"] = camId;
        cameraJson["label"] = camera.label;
        int cols = camera.gridCols > 0 ? camera.gridCols : sensorSettings.camGridCols;
        int rows = camera.gridRows > 0 ? camera.gridRows : sensorSettings.camGridRows;
        cameraJson["grid"] = {
            {"cols", cols},
            {"rows", rows},
        };

        cameraJson["ignored_zones"] = ofJson::array();
        for (int zoneIndex : camera.ignoredZones) {
            cameraJson["ignored_zones"].push_back(zoneIndex);
        }

        cameraJson["zone_labels"] = ofJson::object();
        for (const auto& label : camera.zoneLabels) {
            cameraJson["zone_labels"][std::to_string(label.first)] = label.second;
        }

        json["cameras"].push_back(cameraJson);
    }

    const std::string calibrationPath = ofToDataPath(settings.roomCalibrationFile);
    std::ofstream out(calibrationPath);
    if (!out) {
        ofLogError("calibration") << "failed to open " << calibrationPath << " for writing";
        return;
    }
    out << json.dump(2) << std::endl;
    ofLogNotice("calibration") << "saved room calibration to " << calibrationPath;
}

void ofApp::loadGestureConfig() {
    // Live-tunable gesture parameters live in bin/data/ so performers can poke
    // them mid-set. We stick with ofxJSON here because it is lightweight and
    // happy to reload on the fly.
    const std::string configFile = "gesture_tuning.json";
    ofxJSONElement json;
    if (!json.open(ofToDataPath(configFile))) {
        ofLogWarning() << "gesture tuning file missing or invalid; keeping current values (" << configFile << ")";
        return;
    }

    VoiceGestureDetector::Config voiceConfig = voiceDetector.getConfig();
    ZoneGestureDetector::Config zoneConfig = zoneDetector.getConfig();
    GlobalGestureDetector::Config globalConfig = globalDetector.getConfig();
    std::size_t historyCapacity = voiceHistoryCapacity;

    auto applyFloat = [](const ofxJSONElement& node, const std::string& key, float& target) {
        if (node.isMember(key) && node[key].isNumeric()) {
            target = node[key].asFloat();
        }
    };

    auto applyInt = [](const ofxJSONElement& node, const std::string& key, int& target) {
        if (node.isMember(key) && node[key].isNumeric()) {
            target = node[key].asInt();
        }
    };

    auto applyUInt64 = [](const ofxJSONElement& node, const std::string& key, uint64_t& target) {
        if (node.isMember(key) && node[key].isNumeric()) {
            double value = node[key].asDouble();
            if (value >= 0.0) {
                target = static_cast<uint64_t>(value);
            }
        }
    };

    if (json.isMember("voice_history_capacity") && json["voice_history_capacity"].isNumeric()) {
        historyCapacity = std::max<std::size_t>(1, static_cast<std::size_t>(json["voice_history_capacity"].asUInt()));
    }

    if (json.isMember("voice")) {
        const auto& voice = json["voice"];
        applyFloat(voice, "raise_delta_y", voiceConfig.raiseDeltaY);
        applyFloat(voice, "lower_delta_y", voiceConfig.lowerDeltaY);
        applyFloat(voice, "swipe_delta_x", voiceConfig.swipeDeltaX);
        applyFloat(voice, "swipe_orthogonality", voiceConfig.swipeOrthogonality);
        applyFloat(voice, "raise_horizontal_limit", voiceConfig.raiseHorizontalLimit);
        applyFloat(voice, "swipe_vertical_limit", voiceConfig.swipeVerticalLimit);
        applyFloat(voice, "shake_radius", voiceConfig.shakeRadius);
        applyInt(voice, "shake_min_sign_flips", voiceConfig.shakeMinSignFlips);
        applyFloat(voice, "shake_min_motion", voiceConfig.shakeMinMotion);
        applyFloat(voice, "burst_speed_threshold", voiceConfig.burstSpeedThreshold);
        applyFloat(voice, "burst_max_speed", voiceConfig.burstMaxSpeed);
        applyFloat(voice, "hold_motion_threshold", voiceConfig.holdMotionThreshold);
        applyUInt64(voice, "hold_duration_ms", voiceConfig.holdDurationMs);
        applyUInt64(voice, "min_window_ms", voiceConfig.minWindowMs);
        applyUInt64(voice, "max_window_ms", voiceConfig.maxWindowMs);
        applyUInt64(voice, "gesture_cooldown_ms", voiceConfig.gestureCooldownMs);
        applyUInt64(voice, "burst_cooldown_ms", voiceConfig.burstCooldownMs);
        applyUInt64(voice, "hold_cooldown_ms", voiceConfig.holdCooldownMs);
    }

    if (json.isMember("zone")) {
        const auto& zone = json["zone"];
        applyUInt64(zone, "history_ms", zoneConfig.historyMs);
        applyUInt64(zone, "sweep_window_ms", zoneConfig.sweepWindowMs);
        applyInt(zone, "sweep_min_steps", zoneConfig.sweepMinSteps);
        applyFloat(zone, "sweep_min_strength", zoneConfig.sweepMinStrength);
        applyUInt64(zone, "sweep_cooldown_ms", zoneConfig.sweepCooldownMs);
        applyFloat(zone, "pulse_threshold", zoneConfig.pulseThreshold);
        applyFloat(zone, "pulse_slope_threshold", zoneConfig.pulseSlopeThreshold);
        applyUInt64(zone, "pulse_cooldown_ms", zoneConfig.pulseCooldownMs);
    }

    if (json.isMember("global")) {
        const auto& global = json["global"];
        applyUInt64(global, "history_ms", globalConfig.historyMs);
        applyFloat(global, "eruption_low", globalConfig.eruptionLow);
        applyFloat(global, "eruption_high", globalConfig.eruptionHigh);
        applyUInt64(global, "eruption_cooldown_ms", globalConfig.eruptionCooldownMs);
        applyUInt64(global, "eruption_window_ms", globalConfig.eruptionWindowMs);
        applyFloat(global, "stillness_motion_threshold", globalConfig.stillnessMotionThreshold);
        applyUInt64(global, "stillness_duration_ms", globalConfig.stillnessDurationMs);
        applyInt(global, "stillness_min_voices", globalConfig.stillnessMinVoices);
        applyUInt64(global, "stillness_cooldown_ms", globalConfig.stillnessCooldownMs);
    }

    voiceConfig.raiseDeltaY = clampFloat(voiceConfig.raiseDeltaY, 0.01f, 2.0f);
    voiceConfig.lowerDeltaY = clampFloat(voiceConfig.lowerDeltaY, 0.01f, 2.0f);
    voiceConfig.swipeDeltaX = clampFloat(voiceConfig.swipeDeltaX, 0.01f, 2.0f);
    voiceConfig.swipeOrthogonality = clampFloat(voiceConfig.swipeOrthogonality, 0.1f, 10.0f);
    voiceConfig.raiseHorizontalLimit = clampFloat(voiceConfig.raiseHorizontalLimit, 0.0f, 2.0f);
    voiceConfig.swipeVerticalLimit = clampFloat(voiceConfig.swipeVerticalLimit, 0.0f, 2.0f);
    voiceConfig.shakeRadius = clampFloat(voiceConfig.shakeRadius, 0.01f, 2.0f);
    voiceConfig.shakeMinSignFlips = clampInt(voiceConfig.shakeMinSignFlips, 1, 64);
    voiceConfig.shakeMinMotion = clampFloat(voiceConfig.shakeMinMotion, 0.0f, 10.0f);
    voiceConfig.burstSpeedThreshold = clampFloat(voiceConfig.burstSpeedThreshold, 0.0f, 100.0f);
    voiceConfig.burstMaxSpeed = std::max(voiceConfig.burstSpeedThreshold + 0.01f, clampFloat(voiceConfig.burstMaxSpeed, 0.01f, 100.0f));
    voiceConfig.holdMotionThreshold = clampFloat(voiceConfig.holdMotionThreshold, 0.0f, 10.0f);
    voiceConfig.holdDurationMs = clampUInt64(voiceConfig.holdDurationMs, 1, 60000);
    voiceConfig.minWindowMs = clampUInt64(voiceConfig.minWindowMs, 1, 60000);
    voiceConfig.maxWindowMs = std::max(voiceConfig.minWindowMs, clampUInt64(voiceConfig.maxWindowMs, 1, 60000));
    voiceConfig.gestureCooldownMs = clampUInt64(voiceConfig.gestureCooldownMs, 0, 60000);
    voiceConfig.burstCooldownMs = clampUInt64(voiceConfig.burstCooldownMs, 0, 60000);
    voiceConfig.holdCooldownMs = clampUInt64(voiceConfig.holdCooldownMs, 0, 60000);

    zoneConfig.historyMs = clampUInt64(zoneConfig.historyMs, 1, 60000);
    zoneConfig.sweepWindowMs = clampUInt64(zoneConfig.sweepWindowMs, 1, zoneConfig.historyMs);
    zoneConfig.sweepMinSteps = clampInt(zoneConfig.sweepMinSteps, 2, 256);
    zoneConfig.sweepMinStrength = clampFloat(zoneConfig.sweepMinStrength, 0.0f, 1.0f);
    zoneConfig.sweepCooldownMs = clampUInt64(zoneConfig.sweepCooldownMs, 0, 60000);
    zoneConfig.pulseThreshold = clampFloat(zoneConfig.pulseThreshold, 0.0f, 1.0f);
    zoneConfig.pulseSlopeThreshold = clampFloat(zoneConfig.pulseSlopeThreshold, 0.0f, 1.0f);
    zoneConfig.pulseCooldownMs = clampUInt64(zoneConfig.pulseCooldownMs, 0, 60000);

    globalConfig.historyMs = clampUInt64(globalConfig.historyMs, 1, 60000);
    globalConfig.eruptionLow = clampFloat(globalConfig.eruptionLow, 0.0f, 1.0f);
    globalConfig.eruptionHigh = clampFloat(globalConfig.eruptionHigh, globalConfig.eruptionLow, 1.0f);
    globalConfig.eruptionCooldownMs = clampUInt64(globalConfig.eruptionCooldownMs, 0, 60000);
    globalConfig.eruptionWindowMs = clampUInt64(globalConfig.eruptionWindowMs, 1, globalConfig.historyMs);
    globalConfig.stillnessMotionThreshold = clampFloat(globalConfig.stillnessMotionThreshold, 0.0f, 1.0f);
    globalConfig.stillnessDurationMs = clampUInt64(globalConfig.stillnessDurationMs, 1, 60000);
    globalConfig.stillnessMinVoices = clampInt(globalConfig.stillnessMinVoices, 1, kMaxVoiceId + 1);
    globalConfig.stillnessCooldownMs = clampUInt64(globalConfig.stillnessCooldownMs, 0, 60000);

    voiceHistoryCapacity = std::min<std::size_t>(historyCapacity, 600);
    gestureHistory.setCapacity(voiceHistoryCapacity);
    voiceDetector.setConfig(voiceConfig);
    zoneDetector.setConfig(zoneConfig);
    globalDetector.setConfig(globalConfig);

    ofLogNotice() << "reloaded gesture tuning from " << configFile
                  << " (history " << voiceHistoryCapacity << " frames)";
}

void ofApp::setupSensors() {
    if (!settings.enableSensors) {
        return;
    }

    kinect.setRegistration(true);
    kinect.init();
    kinect.open();
    kinectReady = kinect.isConnected();
    if (kinectReady) {
        int width = static_cast<int>(kinect.getWidth());
        int height = static_cast<int>(kinect.getHeight());
        kinectThresholdImage.allocate(width, height);
        kinectThresholdPixels.assign(width * height, 0);
        ofLogNotice("sensors") << "Kinect ready at " << width << "x" << height;
    } else {
        ofLogWarning("sensors") << "Kinect not connected; OSC replay and webcams can still feed the host";
    }

    for (int camId = 0; camId < kCameraCount; ++camId) {
        auto& source = cameraSources[camId];
        source.grabber.setDeviceID(camId);
        source.ready = source.grabber.setup(sensorSettings.cameraWidth, sensorSettings.cameraHeight);
        source.smoothedZones.assign(getCameraGridRows(camId) * getCameraGridCols(camId), 0.0f);
        if (source.ready) {
            ofLogNotice("sensors") << "webcam " << camId << " ready at "
                                   << sensorSettings.cameraWidth << "x" << sensorSettings.cameraHeight;
        } else {
            ofLogWarning("sensors") << "webcam " << camId << " unavailable";
        }
    }
}

void ofApp::updateSensors(uint64_t now) {
    if (!settings.enableSensors) {
        return;
    }

    updateKinectVoices(now);
    updateWebcamMotion(now);
}

void ofApp::updateKinectVoices(uint64_t now) {
    if (!kinectReady) {
        return;
    }

    kinect.update();
    if (!kinect.isFrameNew()) {
        return;
    }

    const int width = static_cast<int>(kinect.getWidth());
    const int height = static_cast<int>(kinect.getHeight());
    if (width <= 0 || height <= 0) {
        return;
    }
    if (static_cast<int>(kinectThresholdPixels.size()) != width * height) {
        kinectThresholdImage.allocate(width, height);
        kinectThresholdPixels.assign(width * height, 0);
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float distanceMm = kinect.getDistanceAt(x, y);
            bool foreground = distanceMm >= sensorSettings.kinectMinDepthMm && distanceMm <= sensorSettings.kinectMaxDepthMm;
            kinectThresholdPixels[y * width + x] = foreground ? 255 : 0;
        }
    }

    kinectThresholdImage.setFromPixels(kinectThresholdPixels.data(), width, height);
    contourFinder.findContours(kinectThresholdImage,
                               sensorSettings.minBlobArea,
                               sensorSettings.maxBlobArea,
                               sensorSettings.maxKinectVoices,
                               false);

    std::vector<KinectBlob> blobs;
    blobs.reserve(contourFinder.blobs.size());
    for (const auto& blob : contourFinder.blobs) {
        float distanceMm = kinect.getDistanceAt(static_cast<int>(blob.centroid.x), static_cast<int>(blob.centroid.y));
        float normalizedDepth = ofMap(distanceMm,
                                      static_cast<float>(sensorSettings.kinectMinDepthMm),
                                      static_cast<float>(sensorSettings.kinectMaxDepthMm),
                                      0.0f,
                                      1.0f,
                                      true);

        KinectBlob normalized;
        normalized.position = glm::vec3(ofMap(blob.centroid.x, 0.0f, static_cast<float>(width), -1.0f, 1.0f, true),
                                        ofMap(blob.centroid.y, 0.0f, static_cast<float>(height), 0.0f, 1.0f, true),
                                        normalizedDepth);
        normalized.size = clampFloat(blob.area / static_cast<float>(sensorSettings.maxBlobArea), 0.0f, 1.0f);
        blobs.push_back(normalized);
    }

    std::vector<VoiceTracker::Blob> trackerBlobs;
    trackerBlobs.reserve(blobs.size());
    for (const auto& blob : blobs) {
        trackerBlobs.push_back({blob.position, blob.size});
    }

    std::vector<VoiceTracker::Track> tracks;
    tracks.reserve(voices.size());
    for (const auto& kv : voices) {
        tracks.push_back({kv.first,
                          kv.second.position,
                          kv.second.velocity,
                          kv.second.lastUpdate,
                          kv.second.active});
    }

    VoiceTracker::Config trackerConfig;
    trackerConfig.maxMatchDistance = sensorSettings.voiceMatchDistance;
    trackerConfig.maxVoices = std::min(sensorSettings.maxKinectVoices, kMaxVoiceId + 1);

    const auto assignments = VoiceTracker::assign(trackerBlobs, tracks, trackerConfig, now);
    for (const auto& assignment : assignments) {
        if (assignment.voiceId < 0 || assignment.blobIndex < 0 || assignment.blobIndex >= static_cast<int>(blobs.size())) {
            continue;
        }
        int voiceId = assignment.voiceId;
        const auto& blob = blobs[assignment.blobIndex];
        lastSensorInputTimestamp = now;

        auto previous = voices.find(voiceId);
        float motion = 0.0f;
        float energy = blob.size;
        if (previous != voices.end()) {
            motion = clampFloat(glm::length(blob.position - previous->second.position) * 3.0f, 0.0f, 1.0f);
            energy = clampFloat((previous->second.energy * 0.65f) + (std::max(blob.size, motion) * 0.35f), 0.0f, 1.0f);
        }

        ingestVoiceState(voiceId, blob.position, blob.size, motion, energy, now, true);
    }
}

void ofApp::updateWebcamMotion(uint64_t now) {
    float globalSum = 0.0f;
    int activeCameras = 0;

    for (int camId = 0; camId < kCameraCount; ++camId) {
        auto& source = cameraSources[camId];
        if (!source.ready) {
            continue;
        }

        source.grabber.update();
        if (!source.grabber.isFrameNew()) {
            continue;
        }

        const ofPixels& pixels = source.grabber.getPixels();
        const int width = static_cast<int>(pixels.getWidth());
        const int height = static_cast<int>(pixels.getHeight());
        const int channels = static_cast<int>(pixels.getNumChannels());
        if (width <= 0 || height <= 0 || channels <= 0) {
            continue;
        }
        if (source.hasPrevious && static_cast<int>(source.previousGray.size()) != width * height) {
            source.hasPrevious = false;
        }

        const int cols = getCameraGridCols(camId);
        const int rows = getCameraGridRows(camId);
        const int zoneCount = rows * cols;
        std::vector<float> zoneSums(zoneCount, 0.0f);
        std::vector<int> zoneCounts(zoneCount, 0);
        std::vector<unsigned char> currentGray(width * height, 0);

        const unsigned char* data = pixels.getData();
        for (int y = 0; y < height; ++y) {
            int row = std::min(rows - 1, (y * rows) / height);
            for (int x = 0; x < width; ++x) {
                int pixelIndex = y * width + x;
                int dataIndex = pixelIndex * channels;
                unsigned char gray = data[dataIndex];
                if (channels >= 3) {
                    gray = static_cast<unsigned char>((static_cast<int>(data[dataIndex]) +
                                                       static_cast<int>(data[dataIndex + 1]) +
                                                       static_cast<int>(data[dataIndex + 2])) /
                                                      3);
                }
                currentGray[pixelIndex] = gray;

                if (!source.hasPrevious) {
                    continue;
                }

                int col = std::min(cols - 1, (x * cols) / width);
                int zoneIndex = row * cols + col;
                float diff = std::abs(static_cast<int>(gray) - static_cast<int>(source.previousGray[pixelIndex])) / 255.0f;
                if (diff < sensorSettings.cameraMotionFloor) {
                    diff = 0.0f;
                }
                zoneSums[zoneIndex] += diff;
                zoneCounts[zoneIndex] += 1;
            }
        }

        if (!source.hasPrevious || static_cast<int>(source.previousGray.size()) != width * height) {
            source.previousGray.allocate(width, height, OF_PIXELS_GRAY);
            source.hasPrevious = true;
        }
        std::copy(currentGray.begin(), currentGray.end(), source.previousGray.getData());

        if (static_cast<int>(source.smoothedZones.size()) != zoneCount) {
            source.smoothedZones.assign(zoneCount, 0.0f);
        }

        float cameraMotion = 0.0f;
        for (int i = 0; i < zoneCount; ++i) {
            float raw = zoneCounts[i] > 0 ? zoneSums[i] / static_cast<float>(zoneCounts[i]) : 0.0f;
            raw = clampFloat(raw * 4.0f, 0.0f, 1.0f);
            source.smoothedZones[i] = clampFloat((source.smoothedZones[i] * sensorSettings.cameraSmoothing) +
                                                 (raw * (1.0f - sensorSettings.cameraSmoothing)),
                                                 0.0f,
                                                 1.0f);
            if (isZoneIgnored(camId, i)) {
                source.smoothedZones[i] = 0.0f;
            }
            cameraMotion += source.smoothedZones[i];
        }
        cameraMotion = zoneCount > 0 ? cameraMotion / static_cast<float>(zoneCount) : 0.0f;

        ingestCameraZones(camId, rows, cols, source.smoothedZones, now, true);
        lastSensorInputTimestamp = now;
        globalSum += cameraMotion;
        activeCameras += 1;
    }

    if (activeCameras > 0) {
        ingestGlobalMotion(globalSum / static_cast<float>(activeCameras), now, true);
    }
}

void ofApp::ingestVoiceState(int voiceId, const glm::vec3& position, float size, float motion, float energy, uint64_t now, bool emitTelemetry) {
    if (voiceId < 0 || voiceId > kMaxVoiceId) {
        ofLogWarning() << "dropping voice state for out-of-range voice id " << voiceId;
        return;
    }

    VoiceState& state = voices[voiceId];
    bool wasActive = state.active;
    uint64_t previousUpdate = state.lastUpdate;
    glm::vec3 previousPosition = state.position;
    state.position = glm::vec3(clampFloat(position.x, -1.0f, 1.0f),
                               clampFloat(position.y, -1.0f, 1.0f),
                               clampFloat(position.z, 0.0f, 1.0f));
    state.size = clampFloat(size, 0.0f, 1.0f);
    state.motion = clampFloat(motion, 0.0f, 1.0f);
    state.energy = clampFloat(energy, 0.0f, 1.0f);
    if (previousUpdate > 0 && now > previousUpdate) {
        float dtSeconds = static_cast<float>(now - previousUpdate) / 1000.0f;
        state.velocity = dtSeconds > 0.0f ? (state.position - previousPosition) / dtSeconds : glm::vec3(0.0f);
    } else {
        state.velocity = glm::vec3(0.0f);
    }
    state.lastUpdate = now;
    state.active = true;

    gestureHistory.addSample(voiceId, state.position, state.motion, state.energy, now);

    if (emitTelemetry && settings.enableSending) {
        if (!wasActive) {
            sendVoiceActive(voiceId, true);
        }
        sendVoiceState(voiceId, state);
        sendVoiceNote(voiceId, ofMap(state.position.x, -1.0f, 1.0f, 48.0f, 72.0f, true), std::max(state.energy, 0.05f));
    }
}

void ofApp::ingestCameraZones(int camId, int rows, int cols, const std::vector<float>& zones, uint64_t now, bool emitTelemetry) {
    if (camId < 0 || camId > kMaxCameraId) {
        ofLogWarning() << "dropping zones for out-of-range camera id " << camId;
        return;
    }
    if (rows <= 0 || cols <= 0 || rows > kMaxGridRows || cols > kMaxGridCols || rows * cols > kMaxZoneValues) {
        ofLogWarning() << "camera " << camId << " reported invalid zone grid " << cols << "x" << rows << " – skipping";
        return;
    }
    if (static_cast<int>(zones.size()) < rows * cols) {
        ofLogWarning() << "camera " << camId << " zones message missing values: " << zones.size()
                       << " provided, expected " << rows * cols;
        return;
    }

    std::vector<float> clampedZones(rows * cols, 0.0f);
    for (int i = 0; i < rows * cols; ++i) {
        clampedZones[i] = isZoneIgnored(camId, i) ? 0.0f : clampFloat(zones[i], 0.0f, 1.0f);
    }

    std::vector<ZoneGestureEvent> zoneEvents;
    zoneDetector.updateCamera(camId, rows, cols, clampedZones, now, zoneEvents);
    for (const auto& event : zoneEvents) {
        sendZoneEvent(event);
    }
    lastZoneUpdate = now;

    if (emitTelemetry && settings.enableSending) {
        sendCameraZones(camId, rows, cols, clampedZones);
    }
}

void ofApp::ingestGlobalMotion(float globalMotion, uint64_t now, bool emitTelemetry) {
    lastGlobalMotion = clampFloat(globalMotion, 0.0f, 1.0f);
    lastGlobalMotionTimestamp = now;

    if (emitTelemetry && settings.enableSending) {
        sendGlobalMotion(lastGlobalMotion);
    }
}

bool ofApp::isZoneIgnored(int camId, int zoneIndex) const {
    auto camIt = roomCalibration.cameras.find(camId);
    if (camIt == roomCalibration.cameras.end()) {
        return false;
    }
    return camIt->second.ignoredZones.find(zoneIndex) != camIt->second.ignoredZones.end();
}

int ofApp::countIgnoredZones() const {
    int total = 0;
    for (const auto& camera : roomCalibration.cameras) {
        total += static_cast<int>(camera.second.ignoredZones.size());
    }
    return total;
}

int ofApp::getCameraGridCols(int camId) const {
    auto camIt = roomCalibration.cameras.find(camId);
    if (camIt != roomCalibration.cameras.end() && camIt->second.gridCols > 0) {
        return camIt->second.gridCols;
    }
    return sensorSettings.camGridCols;
}

int ofApp::getCameraGridRows(int camId) const {
    auto camIt = roomCalibration.cameras.find(camId);
    if (camIt != roomCalibration.cameras.end() && camIt->second.gridRows > 0) {
        return camIt->second.gridRows;
    }
    return sensorSettings.camGridRows;
}

std::string ofApp::getCameraLabel(int camId) const {
    auto camIt = roomCalibration.cameras.find(camId);
    if (camIt == roomCalibration.cameras.end()) {
        return "";
    }
    return camIt->second.label;
}

std::string ofApp::getZoneLabel(int camId, int zoneIndex) const {
    auto camIt = roomCalibration.cameras.find(camId);
    if (camIt == roomCalibration.cameras.end()) {
        return "";
    }
    auto labelIt = camIt->second.zoneLabels.find(zoneIndex);
    if (labelIt == camIt->second.zoneLabels.end()) {
        return "";
    }
    return labelIt->second;
}

void ofApp::processOscMessages() {
    ofxOscMessage message;
    uint64_t now = nowMillis();
    int messagesProcessed = 0;

    if (!settings.enableOscInput) {
        return;
    }

    while (stateReceiver.hasWaitingMessages() && messagesProcessed < kMaxOscMessagesPerFrame) {
        stateReceiver.getNextMessage(message);
        ++messagesProcessed;
        lastOscInputTimestamp = now;
        const std::string& address = message.getAddress();

        if (address == "/room/config/reload") {
            loadRoomCalibration();
            loadGestureConfig();
            ofLogNotice("config") << "reloaded calibration and gesture tuning via OSC";
        } else if (address == "/room/config/sending" && message.getNumArgs() >= 1) {
            settings.enableSending = message.getArgAsInt(0) != 0;
            ofLogNotice("config") << "OSC sending " << (settings.enableSending ? "enabled" : "disabled");
        } else if (address == "/room/config/sensors" && message.getNumArgs() >= 1) {
            settings.enableSensors = message.getArgAsInt(0) != 0;
            ofLogNotice("config") << "sensor capture " << (settings.enableSensors ? "enabled" : "disabled");
        } else if (address == "/room/global/reset") {
            resetTrackingState(true);
            ofLogNotice("config") << "tracking state reset via OSC";
        } else if (address == "/room/voice/state" && message.getNumArgs() >= 7) {
            // Voice payload mirrors the OSC schema: id, xyz, size, motion, energy.
            int voiceId = message.getArgAsInt(0);
            glm::vec3 position(message.getArgAsFloat(1), message.getArgAsFloat(2), message.getArgAsFloat(3));
            ingestVoiceState(voiceId, position, message.getArgAsFloat(4), message.getArgAsFloat(5), message.getArgAsFloat(6), now, true);
        } else if (address == "/room/voice/disconnect" && message.getNumArgs() >= 1) {
            int voiceId = message.getArgAsInt(0);
            if (voiceId < 0 || voiceId > kMaxVoiceId) {
                ofLogWarning() << "dropping disconnect for out-of-range voice id " << voiceId;
                continue;
            }
            voices.erase(voiceId);
            gestureHistory.removeVoice(voiceId);
            voiceDetector.removeVoice(voiceId);
            sendVoiceActive(voiceId, false);
            ofLogNotice() << "voice " << voiceId << " removed";
        } else if (address == "/room/camera/zones" && message.getNumArgs() >= 3) {
            int camId = message.getArgAsInt(0);
            int cols = message.getArgAsInt(1);
            int rows = message.getArgAsInt(2);
            if (rows <= 0 || cols <= 0 || rows > kMaxGridRows || cols > kMaxGridCols || rows * cols > kMaxZoneValues) {
                ofLogWarning() << "camera " << camId << " reported invalid zone grid " << cols << "x" << rows << " – skipping";
                continue;
            }
            int expectedArgs = 3 + rows * cols;
            if (message.getNumArgs() < expectedArgs) {
                ofLogWarning() << "camera " << camId << " zones message missing values: " << message.getNumArgs() - 3
                               << " provided, expected " << rows * cols;
                continue;
            }

            std::vector<float> zones(rows * cols, 0.0f);
            for (int i = 0; i < rows * cols; ++i) {
                zones[i] = message.getArgAsFloat(3 + i);
            }
            ingestCameraZones(camId, rows, cols, zones, now, true);
        } else if (address == "/room/global/motion" && message.getNumArgs() >= 1) {
            ingestGlobalMotion(message.getArgAsFloat(0), now, true);
        }
    }

    if (messagesProcessed >= kMaxOscMessagesPerFrame && stateReceiver.hasWaitingMessages()) {
        ++oscBacklogWarnings;
        ofLogWarning() << "OSC backlog exceeded " << kMaxOscMessagesPerFrame << " messages this frame; leaving remaining messages queued";
    }
}

void ofApp::resetTrackingState(bool emitVoiceInactive) {
    if (emitVoiceInactive) {
        for (const auto& kv : voices) {
            if (kv.second.active) {
                sendVoiceActive(kv.first, false);
            }
        }
    }

    for (const auto& kv : voices) {
        gestureHistory.removeVoice(kv.first);
        voiceDetector.removeVoice(kv.first);
    }
    voices.clear();

    for (int camId = 0; camId <= kMaxCameraId; ++camId) {
        zoneDetector.removeCamera(camId);
    }
    globalDetector.reset();
    lastVoiceGestures.clear();
    recentZoneEvents.clear();
    recentGlobalEvents.clear();
    lastGlobalMotion = 0.0f;
    lastGlobalMotionTimestamp = 0;
    lastZoneUpdate = 0;
}

void ofApp::pruneVoices(uint64_t now) {
    // If a tracker goes silent for a couple seconds we assume the dancer left
    // view and we clear out their history so they come back fresh later.
    const uint64_t staleMs = 2500;
    for (auto it = voices.begin(); it != voices.end();) {
        if (now > it->second.lastUpdate && now - it->second.lastUpdate > staleMs) {
            int voiceId = it->first;
            gestureHistory.removeVoice(voiceId);
            voiceDetector.removeVoice(voiceId);
            if (it->second.active) {
                sendVoiceActive(voiceId, false);
            }
            it = voices.erase(it);
        } else {
            ++it;
        }
    }
}

void ofApp::updateVoiceGestures() {
    std::vector<VoiceGestureEvent> events;
    events.reserve(voices.size());

    for (const auto& kv : voices) {
        int voiceId = kv.first;
        const auto* history = gestureHistory.getHistory(voiceId);
        if (!history || history->size() < 2) {
            continue;
        }
        voiceDetector.updateVoice(voiceId, *history, events);
    }

    for (const auto& event : events) {
        sendVoiceEvent(event);
    }
}

void ofApp::updateGlobalGestures(uint64_t now) {
    std::vector<GlobalGestureEvent> events;
    int activeVoices = static_cast<int>(voices.size());
    globalDetector.update(lastGlobalMotion, activeVoices, now, events);
    for (const auto& event : events) {
        sendGlobalEvent(event);
    }
}

void ofApp::sendVoiceState(int voiceId, const VoiceState& state) {
    if (!settings.enableSending) {
        return;
    }

    ofxOscMessage message;
    message.setAddress(formatAddress(settings.voiceStateRoute.address, voiceId));
    message.addIntArg(voiceId);
    message.addFloatArg(state.position.x);
    message.addFloatArg(state.position.y);
    message.addFloatArg(state.position.z);
    message.addFloatArg(state.size);
    message.addFloatArg(state.motion);
    message.addFloatArg(state.energy);
    getSenderForRoute(settings.voiceStateRoute).sendMessage(message, false);
    lastTelemetrySendTimestamp = nowMillis();
}

void ofApp::sendVoiceActive(int voiceId, bool active) {
    if (!settings.enableSending) {
        return;
    }

    ofxOscMessage message;
    message.setAddress(formatAddress(settings.voiceActiveRoute.address, voiceId));
    message.addIntArg(voiceId);
    message.addIntArg(active ? 1 : 0);
    getSenderForRoute(settings.voiceActiveRoute).sendMessage(message, false);
    lastTelemetrySendTimestamp = nowMillis();
}

void ofApp::sendVoiceNote(int voiceId, float note, float velocity) {
    if (!settings.enableSending) {
        return;
    }

    ofxOscMessage message;
    message.setAddress(formatAddress(settings.voiceNoteRoute.address, voiceId));
    message.addIntArg(voiceId);
    message.addFloatArg(note);
    message.addFloatArg(clampFloat(velocity, 0.0f, 1.0f));
    getSenderForRoute(settings.voiceNoteRoute).sendMessage(message, false);
    lastTelemetrySendTimestamp = nowMillis();
}

void ofApp::sendCameraZones(int camId, int rows, int cols, const std::vector<float>& zones) {
    if (!settings.enableSending) {
        return;
    }

    ofxOscMessage message;
    message.setAddress(formatAddress(settings.cameraZonesRoute.address, std::nullopt, camId));
    message.addIntArg(camId);
    message.addIntArg(cols);
    message.addIntArg(rows);
    for (int i = 0; i < rows * cols && i < static_cast<int>(zones.size()); ++i) {
        message.addFloatArg(clampFloat(zones[i], 0.0f, 1.0f));
    }
    getSenderForRoute(settings.cameraZonesRoute).sendMessage(message, false);
    lastTelemetrySendTimestamp = nowMillis();
}

void ofApp::sendGlobalMotion(float globalMotion) {
    if (!settings.enableSending) {
        return;
    }

    ofxOscMessage message;
    message.setAddress(settings.globalMotionRoute.address);
    message.addFloatArg(clampFloat(globalMotion, 0.0f, 1.0f));
    getSenderForRoute(settings.globalMotionRoute).sendMessage(message, false);
    lastTelemetrySendTimestamp = nowMillis();
}

void ofApp::sendVoiceEvent(const VoiceGestureEvent& event) {
    uint64_t timestamp = nowMillis();
    lastVoiceGestures[event.voiceId] = {event.type, event.strength, timestamp};

    if (settings.enableSending) {
        ofxOscMessage message;
        message.setAddress(formatAddress(settings.voiceGestureRoute.address, event.voiceId));
        message.addIntArg(event.voiceId);
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        message.addFloatArg(event.extra);
        getSenderForRoute(settings.voiceGestureRoute).sendMessage(message, false);
    }
}

void ofApp::sendZoneEvent(const ZoneGestureEvent& event) {
    uint64_t timestamp = nowMillis();
    recentZoneEvents.push_back({event.camId, event.type, event.strength, timestamp, event.zoneIndex, event.hasZoneIndex});
    if (recentZoneEvents.size() > 12) {
        recentZoneEvents.erase(recentZoneEvents.begin());
    }

    if (settings.enableSending) {
        ofxOscMessage message;
        std::optional<int> zoneIndex = event.hasZoneIndex ? std::optional<int>(event.zoneIndex) : std::nullopt;
        message.setAddress(formatAddress(settings.zoneGestureRoute.address, std::nullopt, event.camId, zoneIndex));
        message.addIntArg(event.camId);
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        if (event.hasZoneIndex) {
            message.addIntArg(event.zoneIndex);
        }
        getSenderForRoute(settings.zoneGestureRoute).sendMessage(message, false);
    }
}

void ofApp::sendGlobalEvent(const GlobalGestureEvent& event) {
    uint64_t timestamp = nowMillis();
    recentGlobalEvents.push_back({event.type, event.strength, timestamp});
    if (recentGlobalEvents.size() > 8) {
        recentGlobalEvents.erase(recentGlobalEvents.begin());
    }

    if (settings.enableSending) {
        ofxOscMessage message;
        message.setAddress(formatAddress(settings.globalGestureRoute.address, std::nullopt, std::nullopt, std::nullopt, event.type));
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        getSenderForRoute(settings.globalGestureRoute).sendMessage(message, false);
    }
}

ofxOscSender& ofApp::getSenderForRoute(const OscRoute& route) {
    auto key = std::make_pair(route.host, route.port);
    auto it = gestureSenders.find(key);
    if (it == gestureSenders.end()) {
        auto sender = std::make_unique<ofxOscSender>();
        sender->setup(route.host, route.port);
        it = gestureSenders.emplace(key, std::move(sender)).first;
    }
    return *(it->second);
}
