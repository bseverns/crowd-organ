#pragma once

#include "ofMain.h"
#include <string>

/**
 * The detectors all report their findings through these light-weight structs.
 * They are intentionally plain so that both OSC emitters and unit tests can
 * serialize them without needing to pull in half the project. Treat them as
 * postcards from the analysis layer.
 */
struct VoiceGestureEvent {
    int voiceId = -1;          ///< Which performer triggered the gesture.
    std::string type;          ///< raise / lower / swipe_* / shake / burst / hold
    float strength = 0.0f;     ///< Normalized 0-1 intensity for musical mapping.
    float extra = 0.0f;        ///< Optional payload (e.g., hold duration fraction).
};

struct ZoneGestureEvent {
    int camId = -1;            ///< Camera that observed the crowd motion.
    std::string type;          ///< sweep_lr_top / sweep_tb_left / pulse / ...
    float strength = 0.0f;     ///< How confidently the detector felt about it.
    int zoneIndex = -1;        ///< Optional index into the 4x4 grid.
    bool hasZoneIndex = false; ///< Flag so receivers can branch without magic numbers.
};

struct GlobalGestureEvent {
    std::string type;          ///< eruption / stillness / custom future additions.
    float strength = 0.0f;     ///< Usually tied to crowd intensity or quietness.
};

