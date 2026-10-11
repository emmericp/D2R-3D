#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Camera metadata layout verified in native build 93847. Rendering reads the
// eye and projection properties as well as matrices. Keep those consistent,
// while restoring the native eye before its next view rebuild.
namespace CameraMetadata {
constexpr size_t PositionOffset=0x110, FovOffset=0x13C, NearOffset=0x158,
    DepthMinOffset=0x160, DepthMaxOffset=0x164, TypeOffset=0x168;
template<class T> inline T Read(const void* camera,size_t offset) {
    T value; std::memcpy(&value,(const char*)camera+offset,sizeof value); return value;
}
template<class T> inline void Write(void* camera,size_t offset,T value) {
    std::memcpy((char*)camera+offset,&value,sizeof value);
}
struct Projection {
    int32_t type=1;
    float fov=60, nearPlane=10, depthMin=550, depthMax=1060;
};
inline Projection ReadProjection(const void* camera) {
    return {Read<int32_t>(camera,TypeOffset),Read<float>(camera,FovOffset),Read<float>(camera,NearOffset),
        Read<float>(camera,DepthMinOffset),Read<float>(camera,DepthMaxOffset)};
}
inline void RestoreProjection(void* camera,Projection native) {
    Write(camera,TypeOffset,native.type); Write(camera,FovOffset,native.fov); Write(camera,NearOffset,native.nearPlane);
    Write(camera,DepthMinOffset,native.depthMin); Write(camera,DepthMaxOffset,native.depthMax);
}
inline bool PublishProjection(void* camera,const float* projection,float fov) {
    if (!std::isfinite(fov) || fov<=0 || fov>=180 || !std::isfinite(projection[14]) ||
        projection[14]<=0 || projection[11]!=-1 || projection[15]!=0) return false;
    const float depthMax=Read<float>(camera,DepthMaxOffset);
    if (!std::isfinite(depthMax) || depthMax<=projection[14]) return false;
    // These are the separate linear-depth encoding bounds, not projection
    // clipping planes. The native world constructor sets 550..1060 for its
    // distant eye (ED7020 writes +160/+164). Retaining 550 excludes the hero
    // in third person from the range used by fog, reflections and lighting.
    // Include nearby geometry while retaining the game's far depth limit.
    RestoreProjection(camera,{0,fov,projection[14],0,depthMax});
    return true;
}
struct Eye {
    float native[3]{}, rendered[3]{};
    bool written=false;
    void Restore(void* camera) {
        auto* position=(char*)camera+PositionOffset;
        // A native camera update may already have written this frame's position.
        // Preserve it; restore only a position still equal to our last write.
        if (written && !std::memcmp(position,rendered,sizeof rendered)) std::memcpy(position,native,sizeof native);
        written=false;
    }
    bool Publish(void* camera,const float* eye) {
        for (unsigned i=0;i<3;++i) if (!std::isfinite(eye[i])) return false;
        auto* position=(char*)camera+PositionOffset;
        if (!written || std::memcmp(position,rendered,sizeof rendered)) std::memcpy(native,position,sizeof native);
        std::memcpy(rendered,eye,sizeof rendered); std::memcpy(position,rendered,sizeof rendered);
        written=true;
        return true;
    }
};
}
