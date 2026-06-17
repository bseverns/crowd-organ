#include "GestureHistory.h"
#include "GlobalGestureDetector.h"
#include "VoiceGestureDetector.h"
#include "ZoneGestureDetector.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct ReplayEvent {
    uint64_t timestamp = 0;
    std::string address;
    std::vector<std::string> args;
    int sourceLine = 0;
};

struct ExpectedEvent {
    std::string scope;
    int id = -1;
    std::string type;

    bool operator<(const ExpectedEvent& other) const {
        if (scope != other.scope) {
            return scope < other.scope;
        }
        if (id != other.id) {
            return id < other.id;
        }
        return type < other.type;
    }
};

std::string trim(const std::string& value) {
    const std::string whitespace = " \t\r\n";
    std::size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

void fail(const std::string& message, int line = 0) {
    if (line > 0) {
        std::cerr << "FAIL line " << line << ": " << message << std::endl;
    } else {
        std::cerr << "FAIL: " << message << std::endl;
    }
    std::exit(1);
}

int parseInt(const std::string& value, int line) {
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        fail("expected integer, got '" + value + "'", line);
    }
    return 0;
}

uint64_t parseTime(const std::string& value, int line) {
    try {
        return static_cast<uint64_t>(std::stoull(value));
    } catch (const std::exception&) {
        fail("expected timestamp, got '" + value + "'", line);
    }
    return 0;
}

float parseFloat(const std::string& value, int line) {
    try {
        return std::stof(value);
    } catch (const std::exception&) {
        fail("expected float, got '" + value + "'", line);
    }
    return 0.0f;
}

void requireArgs(const ReplayEvent& event, std::size_t count) {
    if (event.args.size() < count) {
        fail(event.address + " expected at least " + std::to_string(count) + " args, got " + std::to_string(event.args.size()),
             event.sourceLine);
    }
}

void parseFixture(const std::string& path, std::vector<ReplayEvent>& events, std::set<ExpectedEvent>& expected) {
    std::ifstream in(path);
    if (!in) {
        fail("could not open fixture " + path);
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream parts(line);
        std::string first;
        parts >> first;
        if (first == "expect") {
            ExpectedEvent expectation;
            parts >> expectation.scope;
            if (expectation.scope == "voice" || expectation.scope == "zone") {
                std::string id;
                parts >> id >> expectation.type;
                expectation.id = parseInt(id, lineNo);
            } else if (expectation.scope == "global") {
                parts >> expectation.type;
            } else {
                fail("unknown expectation scope '" + expectation.scope + "'", lineNo);
            }
            if (expectation.type.empty()) {
                fail("expectation missing gesture type", lineNo);
            }
            expected.insert(expectation);
            continue;
        }

        ReplayEvent event;
        event.timestamp = parseTime(first, lineNo);
        event.sourceLine = lineNo;
        parts >> event.address;
        if (event.address.empty()) {
            fail("event missing OSC address", lineNo);
        }

        std::string arg;
        while (parts >> arg) {
            event.args.push_back(arg);
        }
        events.push_back(event);
    }

    std::stable_sort(events.begin(), events.end(), [](const ReplayEvent& lhs, const ReplayEvent& rhs) {
        return lhs.timestamp < rhs.timestamp;
    });
}

void rememberVoiceEvents(const std::vector<VoiceGestureEvent>& events, std::set<ExpectedEvent>& observed) {
    for (const auto& event : events) {
        observed.insert({"voice", event.voiceId, event.type});
    }
}

void rememberZoneEvents(const std::vector<ZoneGestureEvent>& events, std::set<ExpectedEvent>& observed) {
    for (const auto& event : events) {
        observed.insert({"zone", event.camId, event.type});
    }
}

void rememberGlobalEvents(const std::vector<GlobalGestureEvent>& events, std::set<ExpectedEvent>& observed) {
    for (const auto& event : events) {
        observed.insert({"global", -1, event.type});
    }
}

void runFixture(const std::string& path) {
    std::vector<ReplayEvent> events;
    std::set<ExpectedEvent> expected;
    parseFixture(path, events, expected);

    GestureHistory history;
    history.setCapacity(60);
    VoiceGestureDetector voiceDetector;
    ZoneGestureDetector zoneDetector;
    GlobalGestureDetector globalDetector;

    std::set<ExpectedEvent> observed;
    std::set<int> activeVoices;
    float lastGlobalMotion = 0.0f;

    for (const auto& event : events) {
        if (event.address == "/room/voice/state") {
            requireArgs(event, 7);
            int voiceId = parseInt(event.args[0], event.sourceLine);
            glm::vec3 position(parseFloat(event.args[1], event.sourceLine),
                               parseFloat(event.args[2], event.sourceLine),
                               parseFloat(event.args[3], event.sourceLine));
            float motion = parseFloat(event.args[5], event.sourceLine);
            float energy = parseFloat(event.args[6], event.sourceLine);
            history.addSample(voiceId, position, motion, energy, event.timestamp);
            activeVoices.insert(voiceId);

            const auto* samples = history.getHistory(voiceId);
            if (samples != nullptr) {
                std::vector<VoiceGestureEvent> voiceEvents;
                voiceDetector.updateVoice(voiceId, *samples, voiceEvents);
                rememberVoiceEvents(voiceEvents, observed);
            }
        } else if (event.address == "/room/voice/disconnect") {
            requireArgs(event, 1);
            int voiceId = parseInt(event.args[0], event.sourceLine);
            history.removeVoice(voiceId);
            voiceDetector.removeVoice(voiceId);
            activeVoices.erase(voiceId);
        } else if (event.address == "/room/camera/zones") {
            requireArgs(event, 3);
            int camId = parseInt(event.args[0], event.sourceLine);
            int cols = parseInt(event.args[1], event.sourceLine);
            int rows = parseInt(event.args[2], event.sourceLine);
            int expectedZoneArgs = rows * cols;
            requireArgs(event, static_cast<std::size_t>(3 + expectedZoneArgs));

            std::vector<float> zones;
            zones.reserve(expectedZoneArgs);
            for (int i = 0; i < expectedZoneArgs; ++i) {
                zones.push_back(parseFloat(event.args[3 + i], event.sourceLine));
            }

            std::vector<ZoneGestureEvent> zoneEvents;
            zoneDetector.updateCamera(camId, rows, cols, zones, event.timestamp, zoneEvents);
            rememberZoneEvents(zoneEvents, observed);
        } else if (event.address == "/room/global/motion") {
            requireArgs(event, 1);
            lastGlobalMotion = parseFloat(event.args[0], event.sourceLine);
        } else {
            fail("unsupported replay address " + event.address, event.sourceLine);
        }

        std::vector<GlobalGestureEvent> globalEvents;
        globalDetector.update(lastGlobalMotion, static_cast<int>(activeVoices.size()), event.timestamp, globalEvents);
        rememberGlobalEvents(globalEvents, observed);
    }

    for (const auto& expectation : expected) {
        if (observed.find(expectation) == observed.end()) {
            std::ostringstream message;
            message << "missing expected " << expectation.scope;
            if (expectation.id >= 0) {
                message << " " << expectation.id;
            }
            message << " " << expectation.type;
            fail(message.str());
        }
    }

    std::cout << "replay fixture passed: " << path << std::endl;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fail("usage: crowdorgan_replay_fixture_tests <fixture> [fixture...]");
    }

    for (int i = 1; i < argc; ++i) {
        runFixture(argv[i]);
    }
    return 0;
}
