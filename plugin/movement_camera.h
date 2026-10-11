#pragma once
#include <cmath>

namespace CameraMovement {
struct Vector { float right, forward; };
struct GroundFrame { float rightX, rightZ, forwardX, forwardZ; };

// Read the actual rendered camera basis instead of assuming that its yaw
// parameter alone describes the view (pitch and view sign can change that).
inline GroundFrame FromView(const float* view, float lookSign) {
    const float rightLength = std::hypot(view[0],view[8]);
    const float forwardLength = std::hypot(view[2],view[10]);
    if (rightLength < 0.0001f || forwardLength < 0.0001f) return {};
    return {view[0]/rightLength,view[8]/rightLength,
            lookSign*view[2]/forwardLength,lookSign*view[10]/forwardLength};
}

// Use the latest view for every input sample, including held keys. A held
// right/left input remains right/left as follow or manual orbit turns the view.
inline Vector Project(Vector input, GroundFrame frame) {
    const float x=input.right*frame.rightX+input.forward*frame.forwardX;
    const float z=input.right*frame.rightZ+input.forward*frame.forwardZ;
    constexpr float h=0.7071067811865475f;
    Vector result{h*(x-z),h*(-x-z)};
    const float magnitude=std::hypot(input.right,input.forward);
    const float length=std::hypot(result.right,result.forward);
    if (length < 0.0001f) return {};
    return {result.right*magnitude/length,result.forward*magnitude/length};
}

inline bool Valid(GroundFrame frame) {
    return std::isfinite(frame.rightX) && std::isfinite(frame.rightZ) && std::isfinite(frame.forwardX) && std::isfinite(frame.forwardZ) &&
           std::hypot(frame.rightX,frame.rightZ)>0.5f && std::hypot(frame.forwardX,frame.forwardZ)>0.5f;
}
}
