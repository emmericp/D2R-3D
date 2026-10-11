#pragma once
#include <algorithm>
#include <cmath>

namespace FollowCamera {
constexpr float DegreesPerDirection = 360.0f / 64.0f;
constexpr float RadiansToDegrees = 180.0f / 3.14159265358979323846f;

inline float Wrap(float degrees) {
    return degrees - 360.0f * std::floor((degrees + 180.0f) / 360.0f);
}

// D2R's continuous 0..64 facing angle maps 56 to +world X and 8 to +world Z.
// The camera's Z basis must oppose the facing vector for a negative-Z view.
inline float TargetYaw(float direction, float baseZx, float baseZz, float lookSign) {
    const float facing = 45.0f + direction * DegreesPerDirection;
    const float desiredZ = facing + (lookSign < 0.0f ? 180.0f : 0.0f);
    return Wrap(desiredZ - std::atan2(baseZz, baseZx) * RadiansToDegrees);
}

inline float Step(float current, float target, float seconds, float speed) {
    const float dt = std::clamp(seconds, 0.0f, 0.1f);
    return Wrap(current + Wrap(target - current) * (1.0f - std::exp(-speed * dt)));
}
}
