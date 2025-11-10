#include "GestureHistory.h"

#include <algorithm>

void GestureHistory::setCapacity(std::size_t capacityFrames) {
    // Keep at least one frame so consumers never divide by zero, even if
    // somebody sets capacityFrames to 0 in a rogue config experiment.
    capacity = std::max<std::size_t>(1, capacityFrames);

    // Trim any existing buffers that are now oversized. This keeps the history
    // immediately in sync when we tweak settings from the UI or config file.
    for (auto& kv : histories) {
        auto& history = kv.second;
        while (history.size() > capacity) {
            history.pop_front();
        }
    }
}

void GestureHistory::addSample(int voiceId, const glm::vec3& position, float motion, float energy, uint64_t timestampMs) {
    auto& history = histories[voiceId];

    // Velocity is the most error-prone thing for students to recompute, so we
    // derive it once here. The timestamps come in milliseconds, so we convert to
    // seconds before dividing to avoid cartoonishly large speeds.
    glm::vec3 velocity(0.0f);
    if (!history.empty()) {
        const auto& prev = history.back();
        float dt = static_cast<float>(timestampMs > prev.timestamp ? timestampMs - prev.timestamp : 0) / 1000.0f;
        if (dt > 0.0f) {
            velocity = (position - prev.position) / dt;
        }
    }

    GestureHistory::Sample sample;
    sample.timestamp = timestampMs;
    sample.position = position;
    sample.velocity = velocity;
    sample.motion = motion;
    sample.energy = energy;

    history.push_back(sample);

    // Clamp the buffer so it never grows without bound during long sets.
    while (history.size() > capacity) {
        history.pop_front();
    }
}

void GestureHistory::removeVoice(int voiceId) {
    // Forget voices the moment tracking says they are gone. The detectors rely
    // on this to reset cooldowns the next time a dancer re-enters.
    histories.erase(voiceId);
}

const std::deque<GestureHistory::Sample>* GestureHistory::getHistory(int voiceId) const {
    auto it = histories.find(voiceId);
    if (it == histories.end()) {
        return nullptr;
    }
    return &it->second;
}

bool GestureHistory::hasVoice(int voiceId) const {
    return histories.find(voiceId) != histories.end();
}

