#include "camera_picking.h"
#include "npc_picking.h"
#include <iostream>
#include <limits>
#include <stdexcept>

void Check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
float Dot(const float* a,const float* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
int main() {
    try {
        for (float width:{1920.0f,3440.0f,5120.0f}) for (float pitch:{5.0f,30.0f,67.0f}) for (float yaw:{0.0f,45.0f,179.0f,-179.0f}) {
            const float r=3.14159265358979323846f/180;
            const float cp=std::cos(pitch*r),sp=std::sin(pitch*r),cy=std::cos(yaw*r),sy=std::sin(yaw*r);
            CameraPicking::Frame frame{{-sy,0,cy},{-sp*cy,cp,-sp*sy},{cp*cy,sp,cp*sy},{500,20,1200},{1.732f/(width/1080),1.732f,0.12f,-0.08f},width,1080};
            for (float u:{0.05f,0.5f,0.95f}) for (float v:{0.05f,0.5f,0.95f}) {
                const float pixels[2]={u*frame.width,v*frame.height}; float origin[3],direction[3];
                Check(CameraPicking::Ray(frame,pixels,origin,direction),"Valid render frame rejected");
                Check(std::abs(Dot(direction,direction)-1)<0.00001f,"Ray not normalized");
                for (int i=0;i<3;++i) Check(origin[i]==frame.eye[i],"Ray not at rendered eye");
                const float z=Dot(direction,frame.back),x=Dot(direction,frame.right),y=Dot(direction,frame.up);
                Check(z<0,"Ray points behind view");
                const float px=((x*frame.projection[0]/-z-frame.projection[2])+1)*frame.width/2;
                const float py=(1-(y*frame.projection[1]/-z-frame.projection[3]))*frame.height/2;
                Check(std::abs(px-pixels[0])<0.002f && std::abs(py-pixels[1])<0.002f,"Ray misses its projected pixel");
            }
        }
        CameraPicking::Frame invalid{}; float pixels[2]{},origin[3]{},direction[3]{};
        Check(!CameraPicking::Ray(invalid,pixels,origin,direction),"Zero viewport accepted");
        invalid.width=1920; invalid.height=1080; invalid.projection[0]=invalid.projection[1]=1;
        invalid.back[2]=1; invalid.eye[0]=std::numeric_limits<float>::quiet_NaN();
        Check(!CameraPicking::Ray(invalid,pixels,origin,direction),"Nonfinite eye accepted");
        const NpcPicking::Body npc{0,0,-20};
        const float torsoEye[3]={0,6,0},forward[3]={0,0,-1}; float hit=0;
        Check(NpcPicking::LowerBodyRay(torsoEye,forward,npc,direction,&hit),"NPC torso hover was not accepted");
        Check(direction[1]<0 && std::abs(direction[0])<0.00001f,"NPC ray did not aim at lower body");
        const float distanceToNpc=-20/direction[2];
        Check(std::abs(torsoEye[1]+direction[1]*distanceToNpc-1.5f)<0.00001f,"NPC correction missed native lower-body target");
        const float groundEye[3]={0,2,0},aboveEye[3]={0,9,0},sideEye[3]={3,6,0},backward[3]={0,0,1};
        Check(!NpcPicking::LowerBodyRay(groundEye,forward,npc,direction,&hit),"Working lower-body ray was redirected");
        Check(!NpcPicking::LowerBodyRay(aboveEye,forward,npc,direction,&hit),"Empty space above NPC was selected");
        Check(!NpcPicking::LowerBodyRay(sideEye,forward,npc,direction,&hit),"Ray beside NPC was selected");
        Check(!NpcPicking::LowerBodyRay(torsoEye,backward,npc,direction,&hit),"NPC behind camera was selected");
        const float overheadEye[3]={0,12,-20},down[3]={0,-1,0};
        Check(std::abs(NpcPicking::Intersect(overheadEye,down,npc)-4)<0.00001f,"Vertical NPC ray intersection failed");
        std::cout<<"Perspective picking round trips and NPC torso/lower-body/empty-space ray cases passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
