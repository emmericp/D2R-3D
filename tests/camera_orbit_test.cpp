#include "camera_orbit.h"
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool Near(float a, float b, float epsilon = 0.001f) { return std::fabs(a-b) < epsilon; }

int main() {
    try {
        using namespace CameraOrbit;
        Require(Filter({0.06f, 0.06f}).right == 0, "Resting stick drift moved camera");
        Require(Filter({NAN, 1}).up == 0, "Invalid stick data moved camera");
        Require(Near(Filter({1, 1}).right, 0.70710678f), "Diagonal camera input exceeds full tilt");
        Require(Filter({0.5f, 0}).right > 0 && Filter({0.5f, 0}).right < Filter({1, 0}).right,
                "Partial tilt lost analog camera speed");
        // Manual look has priority over follow, including when the character turns.
        State twenty{}, sixty{};
        Input look{}; look.stick = {1, 0}; look.targetYaw = -100;
        for (int i=1; i<=20; ++i) Step(twenty, look, 1.0f/20, i/20.0);
        for (int i=1; i<=60; ++i) Step(sixty, look, 1.0f/60, i/60.0);
        Require(Near(twenty.yaw, 90) && Near(twenty.yaw, sixty.yaw), "Right-stick orbit is not frame-rate independent");
        look.stick = {}; look.targetYaw = 0;
        Step(sixty, look, 1.0f/60, 1.1);
        Require(Near(sixty.yaw, 90), "Camera snapped back immediately on release");
        look.targetYaw=-100;
        for (int i=1; i<=600; ++i) Step(sixty,look,1.0f/60,1.1+i/60.0);
        Require(Near(sixty.yaw,90) && sixty.waitingForMovement, "Manual look did not stay put while character stood still");
        look.moving=true; look.targetYaw=0;
        Step(sixty, look, 1.0f/60, 12);
        Require(sixty.yaw < 90 && sixty.yaw > 85, "Return did not start gently when character moved");
        const float pausedYaw=sixty.yaw, pausedPitch=sixty.pitch;
        look.moving=false; Step(sixty,look,0.1f,13);
        Require(Near(sixty.yaw,pausedYaw) && Near(sixty.pitch,pausedPitch), "Return kept moving camera after character stopped");
        look.moving=true;
        for (int i=1; i<=180; ++i) Step(sixty, look, 1.0f/60, 13+i/60.0);
        Require(std::fabs(sixty.yaw) < 0.5f && !sixty.returning, "Return did not settle behind moving character");
        State moving{};
        look.stick={1, 0}; Step(moving,look,0.1f,10); look.stick={};
        for (int i=1; i<=100; ++i) {
            look.targetYaw=FollowCamera::Wrap(moving.yaw+90);
            Step(moving,look,0.02f,10+i*0.02);
        }
        Require(!moving.returning, "Continuous sideways walking kept the faster recenter rate indefinitely");
        Motion motion{};
        Require(!motion.Observe({100,100},1), "First position falsely counted as movement");
        Require(!motion.Observe({100.002f,100},2), "Tiny stationary jitter counted as movement");
        Require(motion.Observe({100.02f,100},3), "Walking displacement was missed");
        Require(motion.Observe({100.02f,100},3.05), "Camera return flickered between native path ticks");
        Require(!motion.Observe({100.02f,100},3.2), "Stopped character still counted as moving");
        Require(!motion.Observe({NAN,100},4), "Invalid path position counted as movement");
        State seam{179, 30, 0, true}; look.targetYaw = -179;
        Step(seam, look, 0.01f, 5);
        Require(FollowCamera::Wrap(seam.yaw-179) > 0, "Return crossed angle seam the long way");
        Require(seam.pitch < 30 && seam.pitch > DefaultPitch, "Tilt return snapped or went the wrong way");
        State slow{}, full{}; look.targetYaw = 90; look.followSpeed = 3;
        Step(full, look, 0.01f, 5);
        look.followSpeed *= ControllerTurnScale;
        Step(slow, look, 0.01f, 5);
        Require(slow.yaw/full.yaw > 0.5f && slow.yaw/full.yaw < 0.51f, "Controller follow is not half the existing rate");
        State free{40, 25}; look.follow = false; look.stick = {-1, 1};
        Step(free, look, 0.1f, 5);
        Require(free.yaw < 40 && free.pitch < 25, "Free orbit ignored right stick or vertical look is inverted");
        const float savedYaw=free.yaw, savedPitch=free.pitch;
        look.stick = {}; Step(free, look, 0.1f, 6);
        Require(Near(free.yaw, savedYaw) && Near(free.pitch, savedPitch), "Free orbit unexpectedly recentered");
        look.allowed=false; look.stick={1, 1}; Step(free, look, 0.1f, 7);
        Require(Near(free.yaw, savedYaw) && Near(free.pitch, savedPitch), "Menu/focus gate ignored");
        look.allowed=true; look.mouseActive=true; look.mouseYaw=125; look.mousePitch=15;
        Step(free, look, 0.1f, 8);
        Require(Near(free.yaw,125) && Near(free.pitch,15), "Mouse orbit lost priority over stick");
        look.mouseActive=false; look.stick={1, 0};
        const float pauseYaw=free.yaw;
        Step(free, look, 0, 8);
        Require(Near(free.yaw,pauseYaw), "Zero elapsed time moved camera");
        Step(free, look, 10, 20);
        Require(Near(FollowCamera::Wrap(free.yaw-pauseYaw),9), "Pause caused huge manual camera jump");
        look.stick={0, 1}; look.minimumPitch=-30; look.maximumPitch=45;
        for (int i=0; i<100; ++i) Step(free,look,0.1f,21+i*0.1);
        Require(Near(free.pitch,-30), "Looking up exceeded the ground-height limit");
        look.stick={0,-1};
        for (int i=0; i<100; ++i) Step(free,look,0.1f,31+i*0.1);
        Require(Near(free.pitch,45), "Looking down exceeded the inclination limit");
        std::cout << "Analog orbit, half-speed controller follow, release delay, smooth recenter, focus/menu gates, and frame rates passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
