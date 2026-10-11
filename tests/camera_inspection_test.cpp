#include "camera_inspection.h"
#include <iostream>
#include <stdexcept>
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    try {
        using namespace CameraInspection;
        Request r{};
        Require(Parse("1 123 status",r) && r.action==Action::Status,"Status rejected");
        Require(Parse("2 123 hold -12 28 90",r) && r.pose.yaw==90,"Exact inspection pose rejected");
        for(const char* bad:{"0 123 status","1 0 status","1 123 walk","1 123 hold -12 2 90",
            "1 123 hold 61 28 90","1 123 hold -12 28 181","1 123 hold nan 28 90",
            "1 123 status trailing","1 123 restore trailing","-1 123 status","1 -1 status",
            "18446744073709551616 123 status"})
            Require(!Parse(bad,r),"Invalid or non-camera request accepted");
        State state{};const Pose original{-12,28,45};
        Require(state.Hold({-20,35,90},original),"Hold failed");
        Require(state.Hold({-15,40,-90},{-20,35,90}),"Second hold failed");
        Pose restored{};
        Require(state.Restore(restored) && restored.pitch==original.pitch && restored.distance==original.distance &&
            restored.yaw==original.yaw,"Repeated inspections replaced the original view");
        Require(!state.Restore(restored),"Inactive restore changed the view");
        Require(!state.Hold({0,28,INFINITY},original) && !state.active,"Invalid hold changed inspection state");
        state.Hold({0,28,0},original);state.Clear();
        Require(!state.active && !state.Restore(restored),"Manual takeover or game exit retained inspection state");
        std::cout<<"Strict camera-only requests, bounded poses, repeated holds and restoration passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
