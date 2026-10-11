#include "camera_metadata.h"
#include <array>
#include <iostream>
#include <stdexcept>

void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        using namespace CameraMetadata;
        std::array<unsigned char,0x174> camera{};
        const Projection original{1,35,10}; RestoreProjection(camera.data(),original);
        const float nativeEye[3]={10816,459,10681},renderEye[3]={10250,39,10125};
        std::memcpy(camera.data()+PositionOffset,nativeEye,sizeof nativeEye);
        Eye eye{};
        Require(eye.Publish(camera.data(),renderEye),"Valid rendered eye rejected");
        Require(!std::memcmp(camera.data()+PositionOffset,renderEye,sizeof renderEye),"Native lighting eye disagrees with rendered view");
        const float nextRenderEye[3]={10251,39,10126}; eye.Publish(camera.data(),nextRenderEye);
        eye.Restore(camera.data());
        Require(!std::memcmp(camera.data()+PositionOffset,nativeEye,sizeof nativeEye),"Repeated renders corrupted original native camera position");
        eye.Publish(camera.data(),renderEye);
        const float nextNativeEye[3]={10820,459,10682}; std::memcpy(camera.data()+PositionOffset,nextNativeEye,sizeof nextNativeEye);
        eye.Restore(camera.data());
        Require(!std::memcmp(camera.data()+PositionOffset,nextNativeEye,sizeof nextNativeEye),"Restore overwrote native camera motion");
        float projection[16]{}; projection[0]=0.73f; projection[5]=1.73f; projection[11]=-1; projection[14]=1.4f;
        Require(PublishProjection(camera.data(),projection,60),"Rendered perspective metadata rejected");
        const auto perspective=ReadProjection(camera.data());
        Require(perspective.type==0 && perspective.fov==60 && perspective.nearPlane==1.4f,"Lighting still sees isometric projection metadata");
        Require(perspective.depthMin==0 && perspective.depthMax==original.depthMax,"Linear depth still excludes nearby third-person objects");
        const float heroDepth=(28-perspective.depthMin)/(perspective.depthMax-perspective.depthMin);
        Require(heroDepth>0 && heroDepth<1,"Hero falls outside the native linear-depth encoding range");
        RestoreProjection(camera.data(),original);
        const auto restored=ReadProjection(camera.data());
        Require(restored.type==original.type && restored.fov==original.fov && restored.nearPlane==original.nearPlane &&
            restored.depthMin==original.depthMin && restored.depthMax==original.depthMax,"Native projection/depth encoding did not restore");
        const auto saved=camera;
        const float invalid[3]={NAN,0,0}; Require(!eye.Publish(camera.data(),invalid),"Invalid eye accepted");
        projection[14]=NAN; Require(!PublishProjection(camera.data(),projection,60),"Invalid near plane accepted");
        Require(camera==saved,"Invalid rendering metadata modified camera");
        projection[14]=1.4f; Write(camera.data(),DepthMaxOffset,NAN); const auto badRange=camera;
        Require(!PublishProjection(camera.data(),projection,60) && camera==badRange,"Invalid native depth range modified camera");
        std::cout<<"Rendered eye/projection consistency, native restoration, repeated rebuilds, and invalid data passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
