#pragma once
#include "follow_camera.h"

namespace CameraOrbit {
constexpr float DefaultDistance = 28.0f;
// Offset from the game's isometric inclination; negative lowers the view.
constexpr float DefaultPitch = -12.0f;
constexpr float ControllerTurnScale = 0.5f;
constexpr float ReleaseDelay = 0.25f;

struct Stick { float right = 0, up = 0; };
inline Stick Filter(Stick input, float deadZone = 0.12f) {
    if (!std::isfinite(input.right) || !std::isfinite(input.up)) return {};
    const float length = std::hypot(input.right, input.up);
    if (length <= deadZone) return {};
    const float strength = (std::min(length, 1.0f) - deadZone) / (1.0f - deadZone);
    return {input.right * strength / length, input.up * strength / length};
}

struct State {
    float yaw = 0, pitch = DefaultPitch;
    double manualUntil = 0;
    bool returning = false;
    bool waitingForMovement = false;
    float returnSeconds = 0;
};
struct Position { float x=0, z=0; };
struct Motion {
    Position previous{};
    double lastMoved=0;
    bool ready=false;
    bool Observe(Position position, double now) {
        if (!std::isfinite(position.x) || !std::isfinite(position.z)) { ready=false; return false; }
        if (!ready) { previous=position; lastMoved=0; ready=true; return false; }
        const float dx=position.x-previous.x, dz=position.z-previous.z;
        if (dx*dx+dz*dz>0.0001f) { previous=position; lastMoved=now; }
        // Native paths update more slowly than rendering. Preserve motion
        // between path ticks, and accumulate small partial-stick steps.
        return lastMoved>0 && now-lastMoved<0.12;
    }
};
struct Input {
    Stick stick{};
    float targetYaw = 0, neutralPitch = DefaultPitch;
    float followSpeed = 3.0f, returnSpeed = 2.0f;
    float yawSpeed = 90.0f, pitchSpeed = 60.0f;
    float minimumPitch = -85.0f, maximumPitch = 60.0f;
    float mouseYaw = 0, mousePitch = DefaultPitch;
    bool mouseActive = false, follow = true, allowed = true, moving = false;
};

inline void Step(State& state, const Input& input, float seconds, double now) {
    if (!input.allowed) return;
    const float dt = std::clamp(seconds, 0.0f, 0.1f);
    const Stick stick = Filter(input.stick);
    if (input.mouseActive || stick.right != 0 || stick.up != 0) {
        if (input.mouseActive) {
            state.yaw = FollowCamera::Wrap(input.mouseYaw);
            state.pitch = std::clamp(input.mousePitch, input.minimumPitch, input.maximumPitch);
        } else {
            state.yaw = FollowCamera::Wrap(state.yaw + stick.right * input.yawSpeed * dt);
            // Positive native Y is stick up: look up by lowering the inclination.
            state.pitch = std::clamp(state.pitch - stick.up * input.pitchSpeed * dt, input.minimumPitch, input.maximumPitch);
        }
        state.manualUntil = now + ReleaseDelay;
        state.waitingForMovement = input.follow;
        state.returning = false; state.returnSeconds=0;
        return;
    }
    if (!input.follow) { state.returning = false; state.waitingForMovement=false; return; }
    if (state.waitingForMovement && !input.moving) return;
    if (now < state.manualUntil) return;
    if (state.waitingForMovement) { state.waitingForMovement=false; state.returning=true; state.returnSeconds=0; }
    // Looking around at rest stays where it was left. Pausing halfway through
    // a return also holds the current view until the character walks again.
    if (state.returning && !input.moving) return;
    const float speed = state.returning && state.returnSeconds<1.0f ? input.returnSpeed : input.followSpeed;
    state.yaw = FollowCamera::Step(state.yaw, input.targetYaw, dt, speed);
    if (state.returning) {
        state.returnSeconds+=dt;
        const float neutralPitch=std::clamp(input.neutralPitch,input.minimumPitch,input.maximumPitch);
        state.pitch += (neutralPitch - state.pitch) * (1.0f - std::exp(-input.returnSpeed * dt));
        const bool centered = std::fabs(FollowCamera::Wrap(input.targetYaw - state.yaw)) < 0.5f;
        // Held sideways walking continually turns the character away again.
        // Do not leave that movement at the faster return rate indefinitely.
        const bool finished = state.returnSeconds>=1.0f;
        if ((centered || finished) && std::fabs(neutralPitch - state.pitch) < 0.15f) state.returning = false;
    }
}
}
