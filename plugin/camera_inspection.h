#pragma once
#include <cmath>
#include <charconv>
#include <cstdint>
#include <sstream>
#include <string>

// Optional local camera-only inspection protocol. No movement, interaction,
// character editing, console execution, or launch commands are accepted.
namespace CameraInspection {
struct Pose { float pitch=0, distance=28, yaw=0; };
enum class Action { Status, Hold, Restore };
struct Request { uint64_t sequence=0, session=0; Action action=Action::Status; Pose pose{}; };
inline bool Valid(Pose p) {
    return std::isfinite(p.pitch) && p.pitch>=-85 && p.pitch<=60 &&
        std::isfinite(p.distance) && p.distance>=3.5f && p.distance<=1500 &&
        std::isfinite(p.yaw) && p.yaw>=-180 && p.yaw<=180;
}
inline bool Parse(const std::string& text,Request& output) {
    if (text.empty() || text.size()>256 || text.front()<'0' || text.front()>'9') return false;
    std::istringstream fields(text); Request request{}; std::string sequence,session,command,extra;
    if (!(fields>>sequence>>session>>command)) return false;
    const auto number=[](const std::string& token,uint64_t& value) {
        const auto result=std::from_chars(token.data(),token.data()+token.size(),value);
        return result.ec==std::errc{} && result.ptr==token.data()+token.size() && value!=0;
    };
    if (!number(sequence,request.sequence) || !number(session,request.session)) return false;
    if (command=="status") request.action=Action::Status;
    else if (command=="restore") request.action=Action::Restore;
    else if (command=="hold") {
        request.action=Action::Hold;
        if (!(fields>>request.pose.pitch>>request.pose.distance>>request.pose.yaw) || !Valid(request.pose)) return false;
    } else return false;
    if (fields>>extra) return false;
    output=request; return true;
}
struct State {
    Pose requested{},saved{};
    bool active=false;
    bool Hold(Pose pose,Pose current) {
        if (!Valid(pose) || !Valid(current)) return false;
        if (!active) saved=current;
        requested=pose; active=true; return true;
    }
    bool Restore(Pose& output) {
        if (!active) return false;
        output=saved; active=false; return true;
    }
    void Clear() { active=false; }
};
}
