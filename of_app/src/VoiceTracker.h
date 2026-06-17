#pragma once

#include "ofMain.h"

#include <cstdint>
#include <vector>

class VoiceTracker {
public:
    struct Config {
        float maxMatchDistance = 0.35f;
        float previousPositionWeight = 0.25f;
        float maxPredictionSeconds = 0.75f;
        uint64_t staleTrackMs = 2500;
        int maxVoices = 8;
    };

    struct Blob {
        glm::vec3 position = glm::vec3(0.0f);
        float size = 0.0f;
    };

    struct Track {
        int voiceId = -1;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        uint64_t lastUpdate = 0;
        bool active = false;
    };

    struct Assignment {
        int blobIndex = -1;
        int voiceId = -1;
    };

    static std::vector<Assignment> assign(const std::vector<Blob>& blobs,
                                          const std::vector<Track>& tracks,
                                          const Config& config,
                                          uint64_t nowMs);
};
