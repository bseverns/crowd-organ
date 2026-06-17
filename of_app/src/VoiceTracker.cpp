#include "VoiceTracker.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace {
float clampFloat(float value, float minValue, float maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

glm::vec3 predictPosition(const VoiceTracker::Track& track, const VoiceTracker::Config& config, uint64_t nowMs) {
    if (track.lastUpdate == 0 || nowMs <= track.lastUpdate) {
        return track.position;
    }

    float dt = static_cast<float>(nowMs - track.lastUpdate) / 1000.0f;
    dt = clampFloat(dt, 0.0f, std::max(0.0f, config.maxPredictionSeconds));
    return track.position + (track.velocity * dt);
}

struct Candidate {
    int blobIndex = -1;
    int voiceId = -1;
    float score = 0.0f;
};
} // namespace

std::vector<VoiceTracker::Assignment> VoiceTracker::assign(const std::vector<Blob>& blobs,
                                                           const std::vector<Track>& tracks,
                                                           const Config& config,
                                                           uint64_t nowMs) {
    std::vector<Assignment> assignments;
    assignments.reserve(blobs.size());

    if (config.maxVoices <= 0 || blobs.empty()) {
        return assignments;
    }

    std::vector<Candidate> candidates;
    candidates.reserve(blobs.size() * tracks.size());
    std::set<int> knownVoiceIds;

    for (const auto& track : tracks) {
        if (track.voiceId < 0 || track.voiceId >= config.maxVoices) {
            continue;
        }
        knownVoiceIds.insert(track.voiceId);
        if (!track.active) {
            continue;
        }
        if (track.lastUpdate > 0 && nowMs > track.lastUpdate && nowMs - track.lastUpdate > config.staleTrackMs) {
            continue;
        }

        glm::vec3 predicted = predictPosition(track, config, nowMs);
        for (int blobIndex = 0; blobIndex < static_cast<int>(blobs.size()); ++blobIndex) {
            float predictedDistance = glm::length(blobs[blobIndex].position - predicted);
            if (predictedDistance > config.maxMatchDistance) {
                continue;
            }

            float previousDistance = glm::length(blobs[blobIndex].position - track.position);
            candidates.push_back({blobIndex,
                                  track.voiceId,
                                  predictedDistance + previousDistance * std::max(0.0f, config.previousPositionWeight)});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        if (std::fabs(lhs.score - rhs.score) > 0.0001f) {
            return lhs.score < rhs.score;
        }
        if (lhs.blobIndex != rhs.blobIndex) {
            return lhs.blobIndex < rhs.blobIndex;
        }
        return lhs.voiceId < rhs.voiceId;
    });

    std::set<int> assignedBlobs;
    std::set<int> assignedVoices;
    for (const auto& candidate : candidates) {
        if (assignedBlobs.find(candidate.blobIndex) != assignedBlobs.end() ||
            assignedVoices.find(candidate.voiceId) != assignedVoices.end()) {
            continue;
        }
        assignments.push_back({candidate.blobIndex, candidate.voiceId});
        assignedBlobs.insert(candidate.blobIndex);
        assignedVoices.insert(candidate.voiceId);
    }

    int nextVoiceId = 0;
    for (int blobIndex = 0; blobIndex < static_cast<int>(blobs.size()); ++blobIndex) {
        if (assignedBlobs.find(blobIndex) != assignedBlobs.end()) {
            continue;
        }

        while (nextVoiceId < config.maxVoices &&
               (knownVoiceIds.find(nextVoiceId) != knownVoiceIds.end() ||
                assignedVoices.find(nextVoiceId) != assignedVoices.end())) {
            ++nextVoiceId;
        }
        if (nextVoiceId >= config.maxVoices) {
            break;
        }

        assignments.push_back({blobIndex, nextVoiceId});
        assignedBlobs.insert(blobIndex);
        assignedVoices.insert(nextVoiceId);
        ++nextVoiceId;
    }

    std::sort(assignments.begin(), assignments.end(), [](const Assignment& lhs, const Assignment& rhs) {
        return lhs.blobIndex < rhs.blobIndex;
    });
    return assignments;
}
