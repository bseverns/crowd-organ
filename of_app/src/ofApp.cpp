#include "ofApp.h"

#include "ofJson.h"
#include "ofLog.h"
#include "ofxJSON.h"

#include <array>
#include <sstream>
#include <vector>

namespace {
uint64_t nowMillis() {
    return static_cast<uint64_t>(ofGetElapsedTimeMillis());
}
} // namespace

void ofApp::setup() {
    // Keep the render loop predictable so gesture windows measured in frames
    // roughly align with milliseconds in configs.
    ofSetFrameRate(60);
    ofSetVerticalSync(true);

    loadSettings();
    loadGestureConfig();

    // One receiver for the raw crowd telemetry, one sender for our gestures.
    stateReceiver.setup(settings.listenPort);
    if (settings.enableSending) {
        gestureSender.setup(settings.gestureHost, settings.gesturePort);
    }

    // Let configs tune how far back we remember per-voice history.
    gestureHistory.setCapacity(voiceHistoryCapacity);

    ofLogNotice() << "CrowdOrganHost listening for motion on port " << settings.listenPort
                  << ", emitting gestures to " << settings.gestureHost << ":" << settings.gesturePort;
}

void ofApp::update() {
    uint64_t now = nowMillis();
    processOscMessages();      // grab fresh motion samples
    pruneVoices(now);          // toss stale performers so cooldowns reset
    updateVoiceGestures();     // per-voice raise/swipe/etc.
    updateGlobalGestures(now); // crowd-wide eruption/stillness
}

void ofApp::draw() {
    // Barebones HUD on purpose: it reminds visiting artists which ports matter.
    ofBackground(12);
    ofSetColor(245);

    std::stringstream ss;
    ss << "Crowd Organ Host – gesture pilot" << std::endl;
    ss << "voices tracked: " << voices.size() << std::endl;
    ss << "global motion: " << ofToString(lastGlobalMotion, 2) << std::endl;
    ss << "gesture out: " << settings.gestureHost << ":" << settings.gesturePort;
    if (!settings.enableSending) {
        ss << " (muted)";
    }
    ss << std::endl;
    ss << "history window: " << gestureHistory.getCapacity() << " frames" << std::endl;

    ofDrawBitmapStringHighlight(ss.str(), 20, 24, ofColor(0, 128, 128, 180), ofColor::white);

    std::string hint = "watch the console for gesture logs";
    if ((ofGetFrameNum() / 60) % 2 == 0) {
        ofDrawBitmapStringHighlight(hint, 20, ofGetHeight() - 24, ofColor(80, 0, 80, 160), ofColor::white);
    }
}

void ofApp::exit() {
    ofLogNotice() << "CrowdOrganHost shutting down.";
}

void ofApp::keyPressed(int key) {
    if (key == 'r' || key == 'R') {
        loadGestureConfig();
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

    auto json = ofLoadJson(settingsPath);
    if (json.contains("listen_port")) {
        settings.listenPort = json["listen_port"].get<int>();
    }
    if (json.contains("gesture_host")) {
        settings.gestureHost = json["gesture_host"].get<std::string>();
    }
    if (json.contains("gesture_port")) {
        settings.gesturePort = json["gesture_port"].get<int>();
    }
    if (json.contains("enable_sending")) {
        settings.enableSending = json["enable_sending"].get<bool>();
    }
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
            target = static_cast<uint64_t>(node[key].asDouble());
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

    voiceHistoryCapacity = historyCapacity;
    gestureHistory.setCapacity(voiceHistoryCapacity);
    voiceDetector.setConfig(voiceConfig);
    zoneDetector.setConfig(zoneConfig);
    globalDetector.setConfig(globalConfig);

    ofLogNotice() << "reloaded gesture tuning from " << configFile
                  << " (history " << voiceHistoryCapacity << " frames)";
}

void ofApp::processOscMessages() {
    std::vector<ZoneGestureEvent> zoneEvents;
    ofxOscMessage message;
    uint64_t now = nowMillis();

    while (stateReceiver.hasWaitingMessages()) {
        stateReceiver.getNextMessage(message);
        const std::string& address = message.getAddress();

        if (address == "/room/voice/state" && message.getNumArgs() >= 7) {
            // Voice payload mirrors the OSC schema: id, xyz, size, motion, energy.
            int voiceId = message.getArgAsInt(0);
            glm::vec3 position(message.getArgAsFloat(1), message.getArgAsFloat(2), message.getArgAsFloat(3));
            float size = message.getArgAsFloat(4);
            float motion = message.getArgAsFloat(5);
            float energy = message.getArgAsFloat(6);

            VoiceState& state = voices[voiceId];
            state.position = position;
            state.size = size;
            state.motion = motion;
            state.energy = energy;
            state.lastUpdate = now;

            gestureHistory.addSample(voiceId, position, motion, energy, now);
        } else if (address == "/room/voice/disconnect" && message.getNumArgs() >= 1) {
            int voiceId = message.getArgAsInt(0);
            voices.erase(voiceId);
            gestureHistory.removeVoice(voiceId);
            voiceDetector.removeVoice(voiceId);
            ofLogNotice() << "voice " << voiceId << " removed";
        } else if (address == "/room/camera/zones" && message.getNumArgs() >= 3 + 16) {
            // Zone messages include grid dimensions. We only listen for 4x4 maps.
            int camId = message.getArgAsInt(0);
            int rows = message.getArgAsInt(1);
            int cols = message.getArgAsInt(2);
            if (rows == 4 && cols == 4) {
                std::array<float, 16> zones{};
                for (int i = 0; i < 16; ++i) {
                    zones[i] = message.getArgAsFloat(3 + i);
                }
                zoneEvents.clear();
                zoneDetector.updateCamera(camId, zones, now, zoneEvents);
                for (const auto& event : zoneEvents) {
                    sendZoneEvent(event);
                }
                lastZoneUpdate = now;
            }
        } else if (address == "/room/global/motion" && message.getNumArgs() >= 1) {
            lastGlobalMotion = message.getArgAsFloat(0);
            lastGlobalMotionTimestamp = now;
        }
    }
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

void ofApp::sendVoiceEvent(const VoiceGestureEvent& event) {
    if (settings.enableSending) {
        ofxOscMessage message;
        message.setAddress("/room/gesture/voice");
        message.addIntArg(event.voiceId);
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        message.addFloatArg(event.extra);
        gestureSender.sendMessage(message, false);
    }
}

void ofApp::sendZoneEvent(const ZoneGestureEvent& event) {
    if (settings.enableSending) {
        ofxOscMessage message;
        message.setAddress("/room/gesture/zone");
        message.addIntArg(event.camId);
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        if (event.hasZoneIndex) {
            message.addIntArg(event.zoneIndex);
        }
        gestureSender.sendMessage(message, false);
    }
}

void ofApp::sendGlobalEvent(const GlobalGestureEvent& event) {
    if (settings.enableSending) {
        ofxOscMessage message;
        message.setAddress("/room/gesture/global");
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        gestureSender.sendMessage(message, false);
    }
}

