#include "follow_camera.h"
#include "movement_camera.h"
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char* description) {
    if (!condition) throw std::runtime_error(description);
}
bool Near(float a, float b, float tolerance = 0.001f) { return std::fabs(a - b) < tolerance; }

int main() {
    try {
        const float rad = 3.14159265358979323846f / 180.0f;
        // Geometric invariant: eye-to-player direction agrees with character
        // facing for every native direction, base camera yaw, and view Z sign.
        for (float base : {-135.0f, -45.0f, 0.0f, 45.0f, 123.0f}) {
            for (float sign : {-1.0f, 1.0f}) {
                for (int direction = 0; direction < 64; ++direction) {
                    const float yaw = FollowCamera::TargetYaw((float)direction, std::cos(base*rad), std::sin(base*rad), sign);
                    const float forwardX = sign * std::cos((base + yaw)*rad);
                    const float forwardZ = sign * std::sin((base + yaw)*rad);
                    const float facing = (45.0f + direction * 360.0f/64.0f)*rad;
                    Require(forwardX*std::cos(facing) + forwardZ*std::sin(facing) > 0.99999f, "Camera is not behind facing direction");
                }
            }
        }
        // Cardinal mappings measured in the running game: 56 is +X, 8 is +Z.
        Require(Near(FollowCamera::TargetYaw(56, 1, 0, -1), -180), "East-facing calibration failed");
        Require(Near(FollowCamera::TargetYaw(8, 1, 0, -1), -90), "South-facing calibration failed");
        const float crossing = FollowCamera::Step(179, -179, 0.01f, 6);
        Require(FollowCamera::Wrap(crossing - 179) > 0 && FollowCamera::Wrap(crossing - 179) < 2, "Turn took long route across angle seam");
        float twenty = 0, sixty = 0;
        for (int i=0; i<20; ++i) twenty=FollowCamera::Step(twenty, 100, 1.0f/20, 6);
        for (int i=0; i<60; ++i) sixty=FollowCamera::Step(sixty, 100, 1.0f/60, 6);
        Require(Near(twenty, sixty), "Smoothing changes with frame rate");
        Require(Near(FollowCamera::Step(23, 90, 0, 6), 23), "Zero elapsed time moved camera");
        Require(Near(FollowCamera::Step(23, 90, -1, 6), 23), "Backward clock moved camera");
        Require(Near(FollowCamera::Step(0, 100, 50, 6), FollowCamera::Step(0, 100, 0.1f, 6)), "Pause caused instant snap");
        // Compare movement after the game's original screen-to-world mapping
        // with the expected camera forward/right vectors at several orbit angles.
        constexpr float rootHalf = 0.7071067811865475f;
        // Keep each input unchanged across two full camera rotations. In
        // particular, held left/right must never become screen-forward after
        // the first 90-degree turn, which was the input-anchor regression.
        for (float yaw=-720; yaw<=720; yaw+=15) {
            const float c=std::cos(yaw*rad), s=std::sin(yaw*rad);
            const CameraMovement::GroundFrame live{rootHalf*(c+s),rootHalf*(s-c),rootHalf*(s-c),-rootHalf*(s+c)};
            for (CameraMovement::Vector input : {CameraMovement::Vector{0,1}, {0,-1}, {1,0}, {-1,0}, {rootHalf,rootHalf}, {0,0},
                                                 {0,0.25f}, {0.2f,-0.3f}, {-0.6f,0.8f}, {0.005f,0.003f}}) {
                const auto output = CameraMovement::Project(input,live);
                const float worldX = rootHalf*(output.right-output.forward);
                const float worldZ = rootHalf*(-output.right-output.forward);
                const float baseX = rootHalf*(input.right-input.forward);
                const float baseZ = rootHalf*(-input.right-input.forward);
                Require(Near(worldX,c*baseX-s*baseZ) && Near(worldZ,s*baseX+c*baseZ), "Movement does not follow camera orientation");
                Require(Near(output.right*output.right+output.forward*output.forward,input.right*input.right+input.forward*input.forward), "Rotation changed movement speed");
                Require(Near(worldX*live.rightX+worldZ*live.rightZ,input.right) && Near(worldX*live.forwardX+worldZ*live.forwardZ,input.forward), "Held input changed its direction relative to the camera");
            }
        }
        float view[16]{}; view[0]=rootHalf;view[8]=-rootHalf;view[2]=-0.3f;view[10]=-0.3f;
        const auto frame=CameraMovement::FromView(view,1);
        Require(CameraMovement::Valid(frame), "Valid camera ground basis was rejected");
        const auto projected=CameraMovement::Project({0,1},frame);
        Require(Near(projected.right,0) && Near(projected.forward,1), "Actual rendered ground basis mapped forward incorrectly");
        Require(!CameraMovement::Valid({}), "Invalid camera basis was accepted");
        std::cout << "Follow geometry, smoothing, keyboard/analog movement magnitudes, and continuous held directions passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
