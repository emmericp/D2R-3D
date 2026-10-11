#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace NpcPicking {
struct Body { float x,y,z; float radius=1.6f, height=8.0f; };

// Intersect a vertical body volume in world units, so its on-screen size
// naturally follows camera pitch, zoom, and viewport resolution.
inline float Intersect(const float* eye,const float* ray,Body body) {
    const float x=eye[0]-body.x,z=eye[2]-body.z;
    const float a=ray[0]*ray[0]+ray[2]*ray[2],b=x*ray[0]+z*ray[2];
    const float c=x*x+z*z-body.radius*body.radius;
    float start=0,end=std::numeric_limits<float>::infinity();
    if (a<0.000001f) { if (c>0) return -1; }
    else {
        const float discriminant=b*b-a*c;
        if (discriminant<0) return -1;
        const float root=std::sqrt(discriminant);
        start=std::max(0.0f,(-b-root)/a); end=(-b+root)/a;
    }
    if (std::abs(ray[1])<0.000001f) {
        if (eye[1]<body.y || eye[1]>body.y+body.height) return -1;
    } else {
        const float t0=(body.y-eye[1])/ray[1],t1=(body.y+body.height-eye[1])/ray[1];
        start=std::max(start,std::min(t0,t1)); end=std::min(end,std::max(t0,t1));
    }
    return std::isfinite(start) && start<=end && end>0?start:-1;
}

// Native NPC picking already works over the lower body. Only redirect an
// upper-body hit to that existing target; ground and lower-body rays stay put.
inline bool LowerBodyRay(const float* eye,const float* ray,Body body,float* direction,float* distance) {
    const float hit=Intersect(eye,ray,body);
    if (hit<0 || eye[1]+ray[1]*hit<=body.y+3.0f) return false;
    float aim[3]={body.x-eye[0],body.y+1.5f-eye[1],body.z-eye[2]};
    const float length=std::sqrt(aim[0]*aim[0]+aim[1]*aim[1]+aim[2]*aim[2]);
    if (!std::isfinite(length) || length<0.01f) return false;
    for (int i=0;i<3;++i) direction[i]=aim[i]/length;
    *distance=hit;
    return true;
}
}
