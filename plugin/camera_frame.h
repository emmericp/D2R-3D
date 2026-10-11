#pragma once
#include <cstdint>

// Read-only interface for cosmetic plugins. Contains only copied render data,
// never pointers into the game's camera or the camera plugin's private state.
struct D2R3DCameraFrame {
    uint32_t size = sizeof(D2R3DCameraFrame);
    uint32_t version = 2;
    uint32_t active = 0;
    uint32_t reserved = 0;
    uint64_t timestampMs = 0;
    float right[3]{};
    float up[3]{};
    float back[3]{};
    float projection[4]{}; // perspective scale X/Y and screen offsets X/Y
    float eye[3]{};        // rendered camera position in the game's world
    float anchor[3]{};     // native ground-level camera target
};
using D2R3DGetCameraFrameFn = bool (*)(D2R3DCameraFrame*, uint32_t);
