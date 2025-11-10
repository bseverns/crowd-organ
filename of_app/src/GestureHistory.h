#pragma once

#include "ofMain.h"
#include <deque>
#include <unordered_map>

/**
 * GestureHistory is the lowest-level diary we keep for each tracked voice.
 * Think of it as the sketchbook where we jot down raw body motion before
 * any of the detectors try to interpret it as a “raise”, “swipe”, or any
 * other higher-level gesture. By isolating this storage, we keep the rest of
 * the system focused on reading history instead of worrying about how to
 * archive it. The API is intentionally tiny so students can read it in one
 * sip and then trace how the detectors consume the data.
 */
class GestureHistory {
public:
    /**
     * A single sample that mirrors one frame coming off of the host. We log
     * the timestamp alongside position, derived velocity, and the raw
     * motion/energy feeds. Velocity is cached here so the detectors do not
     * have to recompute finite differences every frame.
     */
    struct Sample {
        uint64_t timestamp = 0; // milliseconds since boot
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float motion = 0.0f;
        float energy = 0.0f;
    };

    /**
     * Adjust the number of frames we keep per voice. Detectors only peek at a
     * sliding window, so capping the deque keeps memory predictable while still
     * letting us experiment with different gesture horizons.
     */
    void setCapacity(std::size_t capacityFrames);
    std::size_t getCapacity() const { return capacity; }

    /**
     * Push a fresh sample for a voice. The history doubles as a rolling
     * velocity calculator, so we derive the delta from the previous sample
     * before stashing the new one.
     */
    void addSample(int voiceId, const glm::vec3& position, float motion, float energy, uint64_t timestampMs);

    /// Drop a voice when it disappears from tracking so we do not leak memory.
    void removeVoice(int voiceId);

    /// Expose the stored deque for read-only gesture analysis.
    const std::deque<Sample>* getHistory(int voiceId) const;
    bool hasVoice(int voiceId) const;

private:
    /// One deque per voice id. We lean on STL to handle all the bookkeeping.
    std::unordered_map<int, std::deque<Sample>> histories;
    /// Default buffer length: 45 frames ≈ 0.75 seconds at 60 FPS.
    std::size_t capacity = 45;
};

