#pragma once
#include <cmath>

namespace CameraPicking {
struct Frame {
    float right[3]{}, up[3]{}, back[3]{}, eye[3]{}, projection[4]{};
    float width=0, height=0;
};

// Use one completed render snapshot. The game's live matrices can temporarily
// contain its original isometric view while the next frame is being rebuilt.
inline bool Ray(const Frame& frame, const float* pixels, float* origin, float* direction) {
    if (!pixels || !origin || !direction || !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
        frame.width<=0 || frame.height<=0 || !std::isfinite(pixels[0]) || !std::isfinite(pixels[1])) return false;
    for (float value:frame.projection) if (!std::isfinite(value)) return false;
    if (frame.projection[0]<=0.000001f || frame.projection[1]<=0.000001f) return false;
    const float x=(2*pixels[0]/frame.width-1+frame.projection[2])/frame.projection[0];
    const float y=(1-2*pixels[1]/frame.height+frame.projection[3])/frame.projection[1];
    float ray[3];
    for (int i=0;i<3;++i) {
        if (!std::isfinite(frame.eye[i])) return false;
        ray[i]=x*frame.right[i]+y*frame.up[i]-frame.back[i];
    }
    const float length=std::sqrt(ray[0]*ray[0]+ray[1]*ray[1]+ray[2]*ray[2]);
    if (!std::isfinite(length) || length<0.000001f) return false;
    for (int i=0;i<3;++i) { origin[i]=frame.eye[i]; direction[i]=ray[i]/length; }
    return true;
}
}
