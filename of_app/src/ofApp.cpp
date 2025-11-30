#include "ofApp.h"

#include "ofJson.h"
#include "ofLog.h"

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

    // Header / summary panel.
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
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                int idx = row * 4 + col;
                float value = ofClamp(sample.values[idx], 0.0f, 1.0f);
                ofColor cellColor;
                cellColor.setHsb(static_cast<uint8_t>(ofMap(value, 0.0f, 1.0f, 160, 12)), 200, ofMap(value, 0.0f, 1.0f, 60, 255));
                ofRectangle cellRect(mapRect.getLeft() + col * (mapRect.getWidth() / 4.0f),
                                     mapRect.getTop() + row * (mapRect.getHeight() / 4.0f),
                                     mapRect.getWidth() / 4.0f,
                                     mapRect.getHeight() / 4.0f);
                ofPushStyle();
                ofSetColor(cellColor);
                ofDrawRectangle(cellRect);
                ofPopStyle();
            }
        }

        ofPushStyle();
        ofNoFill();
        ofSetColor(200, 180);
        ofDrawRectangle(mapRect);
        ofPopStyle();

        std::stringstream mapLabel;
        mapLabel << "cam " << camId << " · last zone frame " << (now > sample.timestamp ? now - sample.timestamp : 0) << "ms ago";
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
    ofLogNotice() << "CrowdOrganHost shutting down.";
}

void ofApp::loadSettings() {
    // We keep configuration lightweight: a single JSON file in the app folder
    // so touring rigs can tweak ports without recompiling.
    if (!ofFile::doesFileExist("gesture_settings.json")) {
        ofLogWarning() << "gesture_settings.json not found – using defaults (" << settings.listenPort << ", " << settings.gestureHost
                       << ":" << settings.gesturePort << ")";
        return;
    }

    auto json = ofLoadJson("gesture_settings.json");
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
    uint64_t timestamp = nowMillis();
    lastVoiceGestures[event.voiceId] = {event.type, event.strength, timestamp};

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
    uint64_t timestamp = nowMillis();
    recentZoneEvents.push_back({event.camId, event.type, event.strength, timestamp, event.zoneIndex, event.hasZoneIndex});
    if (recentZoneEvents.size() > 12) {
        recentZoneEvents.erase(recentZoneEvents.begin());
    }

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
    uint64_t timestamp = nowMillis();
    recentGlobalEvents.push_back({event.type, event.strength, timestamp});
    if (recentGlobalEvents.size() > 8) {
        recentGlobalEvents.erase(recentGlobalEvents.begin());
    }

    if (settings.enableSending) {
        ofxOscMessage message;
        message.setAddress("/room/gesture/global");
        message.addStringArg(event.type);
        message.addFloatArg(event.strength);
        gestureSender.sendMessage(message, false);
    }
}

