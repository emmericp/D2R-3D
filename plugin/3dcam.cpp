// Console commands: 3dcam, 3dcam_follow
// Hotkey:           F12 (Shift+F12 also works and leaves renderdistance alone)
// F11 toggles smooth character-facing follow; middle drag temporarily overrides it.
// F10 toggles camera-relative keyboard and controller walking.
// While on: mouse wheel = zoom, middle mouse drag / right stick = orbit.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <set>

#include "follow_camera.h"
#include "camera_orbit.h"
#include "camera_metadata.h"
#include "camera_inspection.h"
#include "movement_camera.h"
#include "camera_frame.h"
#include "camera_picking.h"
#include "npc_picking.h"

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/lifecycle.h>
#include <D2RLPlugin/lifecycle_events.h>
#include <D2RLPlugin/shared_events.h>

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

namespace {

// Defaults
constexpr float kFov = 60.0f;            // perspective vertical FOV (deg)
constexpr float kDist = CameraOrbit::DefaultDistance; // closer third-person distance; the wheel changes it
constexpr float kPitch = CameraOrbit::DefaultPitch;   // lowers the native isometric inclination
constexpr float kHeight = 6.0f;          // pivot lift above the ground look-at point (world up)
constexpr float kShadowSlope = 0.5f;     // min downward slope of shadow-fit rays
constexpr bool  kWheelSwallow = true;    // don't pass the wheel to the game while on

const PluginContext* g_ctx = nullptr;
uintptr_t g_base = 0;
HMODULE g_self = nullptr;

constexpr uint64_t RVA_CAMCOPY = 0x7BBDA0, RVA_COPY_CALL = 0x7BDA2E;   // camera operator=
constexpr uint64_t RVA_GETPROJ = 0xED67A0, RVA_VIEWREBUILD = 0xED70A0, RVA_RAYBUILD = 0xED62B0,
                   RVA_SHADOWFIT = 0xF4E4F0, RVA_SHADOWFIT_END = 0xF4FC7C;
const uint8_t kSigCamCopy[16]     = {0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x1D,0xF5,0x20,0x02,0x48,0x33,0xC4,0x48,0x89};
const uint8_t kSigCopyCall[5]     = {0xE8,0x6D,0xE3,0xFF,0xFF};
const uint8_t kSigGetProj[16]     = {0x48,0x8B,0xC4,0x53,0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x80,0xB9,0x70,0x01,0x00};
const uint8_t kSigViewRebuild[16] = {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x81,0xEC,0xB0,0x00};
const uint8_t kSigRayBuild[16]    = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x55,0x57,0x41,0x56,0x48};

// Verified read-only native getters for build 93847. Never interpret a player
// handle from the SDK as a raw pointer. Disable follow if these signatures change.
constexpr uint64_t RVA_PLAYER_SLOT = 0x8B2D0, RVA_PLAYER_BY_SLOT = 0x9A480,
                   RVA_PATH_FACING = 0x341970;
const uint8_t kSigPlayerSlot[] = {0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3};
const uint8_t kSigPlayerBySlot[] = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08,0x0F,0x83,0x85,0x00,0x00,0x00};
const uint8_t kSigPathFacing[] = {0x48,0x85,0xC9,0x75,0x04,0x0F,0x57,0xC0,0xC3,0xF3,0x0F,0x10,0x81,0x80,0x00,0x00,0x00,0xC3};
constexpr uint64_t RVA_KEY_MOVE = 0x8A960;
const uint8_t kSigKeyMove[] = {0x48,0x83,0xEC,0x48,0x0F,0x29,0x74,0x24,0x30,0x0F,0x57,0xF6,0x0F,0x29,0x7C,0x24,0x20};
constexpr uint64_t RVA_CONTROLLER_MOVE=0x13CF10, RVA_CONTROLLER_WALK_CALL=0x14C642;
const uint8_t kSigControllerMove[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
// This caller joins the same float2 movement path as RVA_KEY_MOVE. Other
// callers of the stick getter serve menus and targeting and stay unchanged.
const uint8_t kSigControllerWalk[]={0xE8,0xC9,0x08,0xFF,0xFF,0xF3,0x0F,0x10,0x00,0xF3,0x0F,0x10,0x48,0x04};
constexpr uint64_t RVA_CONTROLLER_LOOK=0x13CFF0;
const uint8_t kSigControllerLook[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
// Verified second stick, distinct from walking's handler+0x20 float2. The
// getter keeps the game's focus/chat gates and only reads handler+0x28.
const uint8_t kSigControllerLookSource[]={0x48,0x69,0x4C,0x24,0x38,0xC8,0x01,0x00,0x00,0x48,0x83,0xC1,0x28,0x48,0x03,0xCE};
// Client unit lookup's table base and monster metadata verified on build 93847.
constexpr uint64_t RVA_CLIENT_LOOKUP=0x9A5D0, RVA_CLIENT_MONSTERS=0x2A23D10;
const uint8_t kSigClientLookup[]={0x4C,0x63,0xCA,0x48,0x8D,0x05,0x36,0x93,0x98,0x02,0x8B,0xD1,0x44,0x8B,0xC1};

constexpr size_t C_VIEW = 0x10, C_INVVIEW = 0x50, C_PROJ = 0x90, C_LOOKAT = 0x11C, C_LOOKAT_OK = 0x128,
                 C_EXTW = 0x148, C_EXTH = 0x14C, C_VPW = 0x150, C_VPH = 0x154, C_NEAR = 0x158,
                 C_TYPE = 0x168, C_PDIRTY = 0x170, C_VDIRTY = 0x171;

template <class T> inline T& F(uintptr_t a, size_t off) { return *(T*)(a + off); }

void Say(const char* msg) {
    if (!g_ctx) return;
    g_ctx->WriteConsoleMessage(msg, ConsoleMessageKind::Output);
    g_ctx->LogInfo(msg);
}

// Matrix math helpers (row-major)
void Mul4(const float* A, const float* B, float* C) {
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c)
        C[r*4+c] = A[r*4]*B[c] + A[r*4+1]*B[4+c] + A[r*4+2]*B[8+c] + A[r*4+3]*B[12+c];
}
void Vec4Mat(const float* v, const float* M, float* o) {
    for (int j = 0; j < 4; ++j) o[j] = v[0]*M[j] + v[1]*M[4+j] + v[2]*M[8+j] + v[3]*M[12+j];
}
bool Invert4(const float* m, float* out) {
    double a[4][8];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { a[i][j] = m[i*4+j]; a[i][j+4] = i == j; }
    for (int col = 0; col < 4; ++col) {
        int piv = col;
        for (int r = col + 1; r < 4; ++r) if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
        if (fabs(a[piv][col]) < 1e-12) return false;
        for (int j = 0; j < 8; ++j) std::swap(a[col][j], a[piv][j]);
        const double d = a[col][col];
        for (int j = 0; j < 8; ++j) a[col][j] /= d;
        for (int r = 0; r < 4; ++r) if (r != col) { const double f = a[r][col]; for (int j = 0; j < 8; ++j) a[r][j] -= f * a[col][j]; }
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out[i*4+j] = (float)a[i][j+4];
    return true;
}
void AxisRot4(float* M, float ux, float uy, float uz, float t) {   // rotation about unit axis u
    const float c = cosf(t), s = sinf(t), k = 1.0f - c;
    const float R[16] = {c + ux*ux*k, uy*ux*k + uz*s, uz*ux*k - uy*s, 0,
                         ux*uy*k - uz*s, c + uy*uy*k, uz*uy*k + ux*s, 0,
                         ux*uz*k + uy*s, uy*uz*k - ux*s, c + uz*uz*k, 0,
                         0, 0, 0, 1};
    memcpy(M, R, sizeof R);
}

// State
std::atomic<bool> g_enabled{true};
std::atomic<float> g_dist{kDist}, g_pitch{kPitch}, g_yaw{0.0f};
std::atomic<uint32_t> g_gen{1};
std::atomic<uintptr_t> g_master{0};
std::atomic<bool> g_follow{true}, g_followFault{false}, g_mmbDown{false};
std::atomic<float> g_followYaw{0.0f};
std::atomic<uint32_t> g_mouseOrbitEpoch{0};
std::atomic<uint32_t> g_followEpoch{1};
std::atomic<bool> g_relativeMovement{true}, g_cameraReady{false};
SRWLOCK g_movementFrameLock = SRWLOCK_INIT;
CameraMovement::GroundFrame g_movementFrame{};
bool g_movementOk = false;
std::atomic<int> g_controllerHookState{0}; // 0: awaiting code, 1: installed, 2: hook failed
std::atomic<bool> g_controllerLookOk{false}, g_controllerLookFault{false};
std::atomic<ULONGLONG> g_controllerTurnUntil{0};
struct ControllerCameraSample { CameraOrbit::Stick stick{}; ULONGLONG timestamp = 0; };
SRWLOCK g_controllerCameraLock = SRWLOCK_INIT;
ControllerCameraSample g_controllerCamera{};
bool g_facingOk = false;
float g_followSpeed = 3.0f;
float g_defaultPitch = kPitch, g_defaultDistance = kDist;
float g_controllerTurnScale = CameraOrbit::ControllerTurnScale;
float g_stickYawSpeed = 90.0f, g_stickPitchSpeed = 60.0f, g_recenterSpeed = 2.0f;
std::atomic<bool> g_npcTargeting{true},g_pickWorld{false},g_pickMenu{false};
std::atomic<bool> g_inspectionEnabled{false},g_inspectionTakeover{false};
bool g_npcLayoutOk=false;
std::set<std::string> g_pickPanels; // SDK UI thread only.

constexpr const char* kFollowConfig =
    "# F12: 3D camera. F11: follow. F10: camera-relative movement.\n"
    "[camera]\nenabled = true\nfollow_enabled = true\nfollow_speed = 3.0\n"
    "movement_camera_relative = true\nnpc_body_targeting = true\n"
    "default_distance = 28.0\ndefault_pitch = -12.0\ncontroller_turn_scale = 0.5\n"
    "stick_yaw_speed = 90.0\nstick_pitch_speed = 60.0\nrecenter_speed = 2.0\ninspection_enabled = false\n";

void LoadFollowConfig() {
    if (!g_ctx || !g_ctx->EnsureConfig(kFollowConfig)) return;
    char config[4096]{};
    if (!g_ctx->ReadConfig(config, sizeof config)) return;
    std::istringstream lines(config);
    std::string line;
    bool cameraSection = false;
    while (std::getline(lines, line)) {
        line = line.substr(0, line.find('#'));
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        line = line.substr(start, line.find_last_not_of(" \t\r") - start + 1);
        if (line.front() == '[') { cameraSection = line == "[camera]"; continue; }
        if (!cameraSection) continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0) continue;
        const auto name = line.substr(0, line.find_last_not_of(" \t", equals - 1) + 1);
        const auto valueStart = line.find_first_not_of(" \t", equals + 1);
        if (valueStart == std::string::npos) continue;
        const auto value = line.substr(valueStart);
        if (name == "enabled" && (value == "true" || value == "false")) g_enabled.store(value == "true");
        else if (name == "follow_enabled" && (value == "true" || value == "false")) g_follow.store(value == "true");
        else if (name == "movement_camera_relative" && (value == "true" || value == "false")) g_relativeMovement.store(value == "true");
        else if (name == "npc_body_targeting" && (value == "true" || value == "false")) g_npcTargeting.store(value == "true");
        else if (name == "inspection_enabled" && (value == "true" || value == "false")) g_inspectionEnabled.store(value == "true");
        else {
            char* end = nullptr;
            const float number = strtof(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0' || !std::isfinite(number)) continue;
            if (name == "follow_speed") g_followSpeed = std::clamp(number, 0.5f, 20.0f);
            else if (name == "default_distance") g_defaultDistance = std::clamp(number, 3.5f, 1500.0f);
            else if (name == "default_pitch") g_defaultPitch = std::clamp(number, -85.0f, 60.0f);
            else if (name == "controller_turn_scale") g_controllerTurnScale = std::clamp(number, 0.1f, 2.0f);
            else if (name == "stick_yaw_speed") g_stickYawSpeed = std::clamp(number, 10.0f, 360.0f);
            else if (name == "stick_pitch_speed") g_stickPitchSpeed = std::clamp(number, 10.0f, 180.0f);
            else if (name == "recenter_speed") g_recenterSpeed = std::clamp(number, 0.5f, 20.0f);
        }
    }
    g_dist.store(g_defaultDistance); g_pitch.store(g_defaultPitch);
}

void SaveFollowConfig() {
    char config[1024];
    snprintf(config, sizeof config, "# F12: 3D. F11: follow. F10: movement. Restart after editing numeric settings.\n[camera]\nenabled = %s\nfollow_enabled = %s\nfollow_speed = %.3f\nmovement_camera_relative = %s\nnpc_body_targeting = %s\ndefault_distance = %.3f\ndefault_pitch = %.3f\ncontroller_turn_scale = %.3f\nstick_yaw_speed = %.3f\nstick_pitch_speed = %.3f\nrecenter_speed = %.3f\ninspection_enabled = %s\n",
             g_enabled.load()?"true":"false", g_follow.load()?"true":"false", g_followSpeed,
             g_relativeMovement.load()?"true":"false", g_npcTargeting.load()?"true":"false",
             g_defaultDistance, g_defaultPitch, g_controllerTurnScale, g_stickYawSpeed, g_stickPitchSpeed, g_recenterSpeed,
             g_inspectionEnabled.load()?"true":"false");
    if (g_ctx && !g_ctx->WriteConfig(config)) g_ctx->LogError("3dcam: could not save follow settings");
}

// Invoked on the camera hook's thread, with no changes to player data. Validate
// path ownership and angle range; a teardown fault disables only follow mode.
bool ReadPlayerFacing(float* facing, uintptr_t* identity, CameraOrbit::Position* position) {
    if (!g_facingOk || g_followFault.load()) return false;
    __try {
        const auto getSlot = (uint32_t (*)())(g_base + RVA_PLAYER_SLOT);
        const auto getPlayer = (uintptr_t (*)(uint32_t))(g_base + RVA_PLAYER_BY_SLOT);
        const auto getFacing = (float (*)(uintptr_t))(g_base + RVA_PATH_FACING);
        const uint32_t slot = getSlot();
        if (slot >= 8) return false;
        const uintptr_t player = getPlayer(slot);
        if (!player || F<uint32_t>(player, 0) != 0) return false;
        const uintptr_t path = F<uintptr_t>(player, 0x38);
        if (!path || F<uintptr_t>(path, 0x40) != player) return false;
        const float direction = getFacing(path);
        if (!std::isfinite(direction) || direction < 0.0f || direction >= 64.0f) return false;
        *facing = direction; *identity = player;
        *position={F<uint32_t>(path,0)/32768.0f,F<uint32_t>(path,4)/32768.0f};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_followFault.exchange(true) && g_ctx) g_ctx->LogError("3dcam: facing read fault; follow disabled for this session");
        return false;
    }
}

struct Cam {
    uintptr_t ptr; float proj0[16]; float extW, extH, nearZ; int type;
    bool haveBase, written; uint32_t lastGen; float lookSign;
    uint32_t followEpoch; uintptr_t followPlayer; LARGE_INTEGER followTick;
    bool followReady; CameraOrbit::State orbit; CameraOrbit::Motion motion; uint32_t mouseOrbitEpoch;
    CameraMetadata::Projection nativeProjection; CameraMetadata::Eye eye;
};
SRWLOCK g_camLock = SRWLOCK_INIT;
Cam g_cams[8];
int g_camCount = 0;
D2R3DCameraFrame g_renderFrame{};
CameraPicking::Frame g_pickingFrame{};
uintptr_t g_pickingCamera=0;
float g_pickingGround=0,g_pickingNear=0;
CameraInspection::State g_inspection{}; // guarded by g_camLock
CameraMetadata::Projection g_renderMetadata{};
uint64_t g_inspectionSequence=0,g_inspectionRendered=0;
std::filesystem::path g_inspectionRequestPath,g_inspectionStatusPath;
uint64_t g_inspectionSession=0,g_inspectionLastSequence=0;
uint64_t g_inspectionPendingSequence=0; // inspection worker only
FILETIME g_inspectionLastWrite{}; // inspection worker only
SRWLOCK g_inspectionFileLock=SRWLOCK_INIT;

void __cdecl OnPickingGameplay(const PluginContext*,const Lifecycle::GameplayEvent* event,void*) noexcept {
    if (!event) return;
    if (event->kind==Lifecycle::GameplayEventKind::GameLeft) {
        g_pickWorld.store(false); g_pickMenu.store(false); g_pickPanels.clear();
        AcquireSRWLockExclusive(&g_camLock); g_inspection.Clear(); g_inspectionSequence=0; g_inspectionRendered=0; ReleaseSRWLockExclusive(&g_camLock);
    }
    else if (event->kind==Lifecycle::GameplayEventKind::GameJoined || event->kind==Lifecycle::GameplayEventKind::LocalPlayerReady) g_pickWorld.store(true);
}
SharedEvents::UiMessageAction __cdecl OnPickingUi(const PluginContext*,const SharedEvents::UiMessageEvent* event,void*) noexcept {
    if (!event || !event->target || strcmp(event->target,"PanelManager") || !event->command) return SharedEvents::UiMessageAction::Continue;
    const std::string name=event->text?event->text:"";
    if (!strcmp(event->command,"OpenPanel") || !strcmp(event->command,"OpenExclusivePanel")) g_pickPanels.insert(name);
    else if (!strcmp(event->command,"ClosePanel") || !strcmp(event->command,"UnloadPanel")) g_pickPanels.erase(name);
    else if (!strcmp(event->command,"CloseAll") || !strcmp(event->command,"UnloadAll")) g_pickPanels.clear();
    bool blocked=false;
    for (const auto& panel:g_pickPanels) for (const char* part:{"Inventory","Bank","Stash","Vendor","Horadric","Pause","Options","QuestLog","CharacterStats","SkillsTree","Waypoint","Trade","Gamble","Dialogue","Npc","NPC"})
        if (panel.find(part)!=std::string::npos) blocked=true;
    g_pickMenu.store(blocked);
    return SharedEvents::UiMessageAction::Continue;
}

// Read-only client units, never SDK handles or server units. The ray still
// goes through native collision/selection; no hovered-unit globals are written.
bool NpcBodyRay(const CameraPicking::Frame& frame,float ground,float nearPlane,const float* origin,float* direction) {
    if (!g_npcLayoutOk || !g_npcTargeting.load() || !g_pickWorld.load() || g_pickMenu.load()) return false;
    __try {
        float best=100000.0f,selected[3]{};
        unsigned budget=512;
        for (unsigned bucket=0;bucket<128 && budget;++bucket) {
            uintptr_t unit=F<uintptr_t>(g_base+RVA_CLIENT_MONSTERS,bucket*sizeof(uintptr_t));
            for (unsigned chain=0;unit && chain<128 && budget; ++chain,--budget) {
                const uintptr_t next=F<uintptr_t>(unit,0x158);
                const uint32_t kind=F<uint32_t>(unit,0),klass=F<uint32_t>(unit,4),mode=F<uint32_t>(unit,0xC);
                if (kind==1 && (mode==1 || mode==2)) {
                    const uintptr_t path=F<uintptr_t>(unit,0x38),data=F<uintptr_t>(unit,0x10);
                    if (path && data && F<uintptr_t>(path,0x40)==unit) {
                        const uintptr_t stats=F<uintptr_t>(data,0);
                        // NPC + interact flags. Guards and hirelings are excluded.
                        if (stats && F<uint16_t>(stats,0)==klass && (F<uint32_t>(stats,0x3C)&0x300)==0x300) {
                            NpcPicking::Body body{F<uint32_t>(path,0)/32768.0f,ground,F<uint32_t>(path,4)/32768.0f};
                            const float dx=body.x-origin[0],dz=body.z-origin[2];
                            const float viewZ=dx*frame.back[0]+(ground+4-origin[1])*frame.back[1]+dz*frame.back[2];
                            if (dx*dx+dz*dz<160*160 && viewZ<-std::max(nearPlane,0.1f)) {
                                float ray[3],distance;
                                if (NpcPicking::LowerBodyRay(origin,direction,body,ray,&distance) && distance<best) {
                                    best=distance; memcpy(selected,ray,sizeof selected);
                                }
                            }
                        }
                    }
                }
                if (next==unit) break;
                unit=next;
            }
        }
        if (best<100000) { memcpy(direction,selected,sizeof selected); return true; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}

Cam* FindCam(uintptr_t c) { for (int i = 0; i < g_camCount; ++i) if (g_cams[i].ptr == c) return &g_cams[i]; return nullptr; }

// Camera-to-pivot distance.
float Radius(const Cam&) { return g_dist.load(); }

// Infinite reverse-Z perspective, same form as the game's own perspective builder
// (proj[10] = 0, proj[11] = -1, proj[14] = near), keeping the ortho view's screen offset.
void BuildProj(const Cam& c, float* M) {
    const float aspect = c.extH > 0 && c.extW / c.extH > 0.1f && c.extW / c.extH < 10.0f ? c.extW / c.extH : 16.0f / 9.0f;
    const float f = 1.0f / tanf(kFov * 3.14159265f / 360.0f);
    const float radius = Radius(c);
    const float nearZ = c.nearZ > 0.0f && c.nearZ < radius * 0.5f ? c.nearZ : radius * 0.05f;
    memset(M, 0, 64);
    M[0] = f / aspect; M[5] = f;
    M[8] = -c.proj0[12]; M[9] = -c.proj0[13];
    M[11] = -1.0f; M[14] = nearZ;
}

// Hooks
using GetProjFn = uintptr_t (*)(uintptr_t);
using ViewRebuildFn = void (*)(uintptr_t);
using RayBuildFn = void (*)(uintptr_t, float*, float*, float*);
GetProjFn OrigGetProj; ViewRebuildFn OrigViewRebuild; RayBuildFn OrigRayBuild;
using KeyMoveFn = uint64_t (*)();
KeyMoveFn OrigKeyMove = nullptr;
using ControllerMoveFn = float* (*)(void*,float*,uint32_t);
ControllerMoveFn OrigControllerMove=nullptr;
bool GameFocused();

bool RotateMovement(CameraMovement::Vector input,CameraMovement::Vector* output) {
    if (!g_enabled.load() || !g_relativeMovement.load() || !g_cameraReady.load() || !GameFocused()) {
        return false;
    }
    if (!std::isfinite(input.right) || !std::isfinite(input.forward)) return false;
    AcquireSRWLockShared(&g_movementFrameLock);
    const auto frame = g_movementFrame;
    ReleaseSRWLockShared(&g_movementFrameLock);
    if (!CameraMovement::Valid(frame)) return false;
    *output=CameraMovement::Project(input,frame);
    return true;
}

uint64_t HookKeyMove() {
    // Preserve the game's configured bindings and native input gates.
    const uint64_t native=OrigKeyMove();
    CameraMovement::Vector input{},output{};
    static_assert(sizeof(input)==sizeof(native));
    memcpy(&input,&native,sizeof input);
    if (!RotateMovement(input,&output)) return native;
    if (input.right*input.right+input.forward*input.forward > 0.0001f) g_controllerTurnUntil.store(0);
    uint64_t packed;
    memcpy(&packed, &output, sizeof packed);
    return packed;
}

void SampleControllerCamera(void* handler, uint32_t player) {
    if (!g_controllerLookOk.load() || g_controllerLookFault.load()) return;
    float stick[2]{};
    __try {
        const auto getLook=(ControllerMoveFn)(g_base+RVA_CONTROLLER_LOOK);
        getLook(handler,stick,player); // Read only; no change to shared/native right-stick input.
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if (!g_controllerLookFault.exchange(true) && g_ctx) g_ctx->LogError("3dcam: right-stick read fault; stick orbit disabled for this session");
        return;
    }
    if (!std::isfinite(stick[0]) || !std::isfinite(stick[1]) || std::fabs(stick[0])>1.05f || std::fabs(stick[1])>1.05f) return;
    AcquireSRWLockExclusive(&g_controllerCameraLock);
    g_controllerCamera={{stick[0],stick[1]},GetTickCount64()};
    ReleaseSRWLockExclusive(&g_controllerCameraLock);
}

float* HookControllerMove(void* handler,float* output,uint32_t player) {
    const uintptr_t caller=(uintptr_t)_ReturnAddress();
    float* native=OrigControllerMove(handler,output,player);
    if (!native || caller!=g_base+RVA_CONTROLLER_WALK_CALL+5 || !g_pickWorld.load() ||
        g_pickMenu.load() || !g_facingOk || player>=8) return native;
    const auto getSlot=(uint32_t (*)())(g_base+RVA_PLAYER_SLOT);
    if (player!=getSlot()) return native;
    const CameraMovement::Vector input{native[0],native[1]};
    if (g_enabled.load() && GameFocused()) {
        SampleControllerCamera(handler,player);
        if (std::isfinite(input.right) && std::isfinite(input.forward) &&
            input.right*input.right+input.forward*input.forward>0.0001f)
            g_controllerTurnUntil.store(GetTickCount64()+250);
    }
    CameraMovement::Vector rotated{};
    if (RotateMovement(input,&rotated)) {
        // Change only this call's returned float2. The shared handler's stick
        // state, native dead zone, analog magnitude, and other consumers remain intact.
        native[0]=rotated.right; native[1]=rotated.forward;
    }
    return native;
}

void InstallControllerMovementHook() {
    if (!g_ctx) return;
    if (!g_controllerLookOk.load() && !g_controllerLookFault.load()) {
        bool lookReady=false;
        __try {
            lookReady=!memcmp((void*)(g_base+RVA_CONTROLLER_LOOK),kSigControllerLook,sizeof kSigControllerLook) &&
                !memcmp((void*)(g_base+RVA_CONTROLLER_LOOK+0x4E),kSigControllerLookSource,sizeof kSigControllerLookSource);
        } __except(EXCEPTION_EXECUTE_HANDLER) { lookReady=false; }
        if (lookReady) {
            g_controllerLookOk.store(true);
            g_ctx->LogInfo("3dcam: native right-stick getter verified; camera orbit available");
        }
    }
    if (g_controllerHookState.load()!=0) return;
    bool ready=false;
    __try {
        ready=!memcmp((void*)(g_base+RVA_CONTROLLER_MOVE),kSigControllerMove,sizeof kSigControllerMove) &&
              !memcmp((void*)(g_base+RVA_CONTROLLER_WALK_CALL),kSigControllerWalk,sizeof kSigControllerWalk);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    // Some native code pages become available only after their first use.
    // Keep checking quietly rather than patching an unverified function.
    if (!ready) return;
    const bool installed=g_ctx->InstallInlineHook(RVA_CONTROLLER_MOVE,kSigControllerMove,sizeof kSigControllerMove,
        (void*)&HookControllerMove,(void**)&OrigControllerMove);
    g_controllerHookState.store(installed?1:2);
    g_ctx->LogInfo(installed?"3dcam: camera-relative controller walking hook installed":"3dcam: controller walking hook failed; native stick retained");
}

using CamCopyFn = uintptr_t (*)(uintptr_t dst, uintptr_t src);
CamCopyFn OrigCamCopy;
std::atomic<bool> g_masterLogged{false};

uintptr_t HookCamCopy(uintptr_t dst, uintptr_t src) {
    const uintptr_t r = OrigCamCopy(dst, src);
    if ((uintptr_t)_ReturnAddress() == g_base + RVA_COPY_CALL + 5) g_master.store(src);
    return r;
}

uintptr_t HookGetProj(uintptr_t cam) {
    if (!cam) return OrigGetProj(cam);
    AcquireSRWLockExclusive(&g_camLock);
    if (Cam* previous=FindCam(cam); previous && previous->written) {
        CameraMetadata::RestoreProjection((void*)cam,previous->nativeProjection);
        if (!g_enabled.load()) { previous->eye.Restore((void*)cam); F<uint8_t>(cam,C_PDIRTY)=1; }
    }
    ReleaseSRWLockExclusive(&g_camLock);
    const bool wasDirty = F<uint8_t>(cam, C_PDIRTY) != 0;
    const uintptr_t ret = OrigGetProj(cam);
    AcquireSRWLockExclusive(&g_camLock);
    Cam* c = FindCam(cam);
    if (!c && g_camCount < 8) { c = &g_cams[g_camCount++]; memset(c, 0, sizeof *c); c->ptr = cam; c->lookSign = -1.0f; }
    if (c) {
        if (wasDirty || !c->haveBase) {
            memcpy(c->proj0, (void*)(cam + C_PROJ), 64); c->haveBase = true;
            c->type = F<int32_t>(cam, C_TYPE); c->extW = F<float>(cam, C_EXTW); c->extH = F<float>(cam, C_EXTH); c->nearZ = F<float>(cam, C_NEAR);
            c->nativeProjection=CameraMetadata::ReadProjection((void*)cam);
        }
        const uint32_t gen = g_gen.load();
        if (c->lastGen != gen) { F<uint8_t>(cam, C_VDIRTY) = 1; c->lastGen = gen; }
        // Native view caching must not freeze a smoothing turn when the player
        // stops moving. Let the original code rebuild a clean view each frame.
        if (g_enabled.load() && c->ptr == g_master.load()) F<uint8_t>(cam, C_VDIRTY) = 1;
        if (g_enabled.load() && c->ptr == g_master.load()) {
            float M[16]; BuildProj(*c, M);
            memcpy((void*)(cam + C_PROJ), M, 64);
            CameraMetadata::PublishProjection((void*)cam,M,kFov);
            g_renderMetadata=CameraMetadata::ReadProjection((void*)cam);
            c->written = true;
        } else if (c->written) {
            F<uint8_t>(cam, C_PDIRTY) = 1; c->written = false;
        }
    }
    ReleaseSRWLockExclusive(&g_camLock);
    return ret;
}

void HookViewRebuild(uintptr_t cam) {
    if (cam) {
        AcquireSRWLockExclusive(&g_camLock);
        if (Cam* previous=FindCam(cam)) previous->eye.Restore((void*)cam);
        ReleaseSRWLockExclusive(&g_camLock);
    }
    OrigViewRebuild(cam);
    if (!cam || !g_enabled.load()) return;
    AcquireSRWLockExclusive(&g_camLock);
    Cam* c = FindCam(cam);
    if (c && c->haveBase && c->ptr == g_master.load()) {
        if (g_inspectionTakeover.exchange(false)) g_inspection.Clear();
        if (g_inspection.active) g_dist.store(g_inspection.requested.distance);
        float view[16]; memcpy(view, (void*)(cam + C_VIEW), 64);
        float P[4] = {0, 0, -Radius(*c), 1};
        if (F<uint8_t>(cam, C_LOOKAT_OK)) {   // pivot = look-at point lifted to body height (world y-up)
            const float L[4] = {F<float>(cam, C_LOOKAT), F<float>(cam, C_LOOKAT + 4) + kHeight, F<float>(cam, C_LOOKAT + 8), 1};
            Vec4Mat(L, view, P);
        }
        const float sgn = P[2] <= 0.0f ? -1.0f : 1.0f;
        const float d2r = 3.14159265f / 180.0f;
        float facing=0; uintptr_t player=0; CameraOrbit::Position position{};
        const bool haveFacing=ReadPlayerFacing(&facing,&player,&position);
        LARGE_INTEGER tick,frequency; QueryPerformanceCounter(&tick); QueryPerformanceFrequency(&frequency);
        const uint32_t epoch=g_followEpoch.load(), mouseEpoch=g_mouseOrbitEpoch.load();
        if (!c->followReady || c->followEpoch!=epoch || c->followPlayer!=player) {
            c->orbit={g_yaw.load(),g_pitch.load()}; c->followTick=tick; c->followReady=true;
            c->motion={};
            c->followEpoch=epoch; c->followPlayer=player; c->mouseOrbitEpoch=mouseEpoch;
        }
        const float seconds=(float)((double)(tick.QuadPart-c->followTick.QuadPart)/frequency.QuadPart);
        c->followTick=tick;
        const ULONGLONG now=GetTickCount64();
        CameraOrbit::Input controls{};
        controls.allowed=g_pickWorld.load() && !g_pickMenu.load() && GameFocused();
        AcquireSRWLockShared(&g_controllerCameraLock);
        const auto controller=g_controllerCamera;
        ReleaseSRWLockShared(&g_controllerCameraLock);
        if (now-controller.timestamp<100 && !g_controllerLookFault.load()) controls.stick=controller.stick;
        controls.follow=g_follow.load() && haveFacing;
        controls.moving=haveFacing && c->motion.Observe(position,now/1000.0);
        controls.targetYaw=haveFacing?FollowCamera::TargetYaw(facing,view[2],view[10],sgn):c->orbit.yaw;
        controls.neutralPitch=g_defaultPitch; controls.returnSpeed=g_recenterSpeed;
        const float nativeInclination=std::atan2(view[6],view[5])*FollowCamera::RadiansToDegrees;
        const float floorInclination=std::max(-5.0f,std::asin(std::clamp((2.0f-kHeight)/Radius(*c),-1.0f,1.0f))*FollowCamera::RadiansToDegrees);
        // Keep manual look above the player's ground plane. This is a tilt
        // bound, not wall/terrain collision; native geometry remains unchanged.
        controls.minimumPitch=std::clamp(floorInclination-nativeInclination,-85.0f,60.0f);
        controls.maximumPitch=std::max(controls.minimumPitch,std::clamp(75.0f-nativeInclination,-85.0f,60.0f));
        controls.followSpeed=g_followSpeed*(now<g_controllerTurnUntil.load()?g_controllerTurnScale:1.0f);
        controls.yawSpeed=g_stickYawSpeed; controls.pitchSpeed=g_stickPitchSpeed;
        controls.mouseActive=g_mmbDown.load() || c->mouseOrbitEpoch!=mouseEpoch;
        controls.mouseYaw=g_yaw.load(); controls.mousePitch=g_pitch.load(); c->mouseOrbitEpoch=mouseEpoch;
        // A live physical camera input takes control immediately. Inspection
        // otherwise holds only the view, without changing follow preferences.
        const auto look=CameraOrbit::Filter(controls.stick);
        if (g_inspection.active && (g_mmbDown.load() || look.right!=0 || look.up!=0)) g_inspection.Clear();
        if (g_inspection.active) {
            controls.allowed=true; controls.follow=false; controls.stick={}; controls.mouseActive=true;
            controls.mouseYaw=g_inspection.requested.yaw; controls.mousePitch=g_inspection.requested.pitch;
        }
        CameraOrbit::Step(c->orbit,controls,seconds,now/1000.0);
        const float yaw=c->orbit.yaw;
        g_followYaw.store(yaw); g_pitch.store(c->orbit.pitch);
        if (!controls.mouseActive) g_yaw.store(yaw);
        float R[16], Ry[16], RR[16];
        AxisRot4(R, 1, 0, 0, c->orbit.pitch * d2r);
        const float un = sqrtf(view[4]*view[4] + view[5]*view[5] + view[6]*view[6]);   // world up in view space
        if (yaw != 0.0f && un > 1e-6f) { AxisRot4(Ry, view[4]/un, view[5]/un, view[6]/un, yaw * d2r); Mul4(Ry, R, RR); memcpy(R, RR, 64); }
        float A[16]; memcpy(A, R, 64);
        A[12] = -(P[0]*R[0] + P[1]*R[4] + P[2]*R[8]);
        A[13] = -(P[0]*R[1] + P[1]*R[5] + P[2]*R[9]);
        A[14] = sgn * Radius(*c) - (P[0]*R[2] + P[1]*R[6] + P[2]*R[10]);
        float folded[16], inv[16];
        Mul4(view, A, folded);
        memcpy((void*)(cam + C_VIEW), folded, 64);
        if (Invert4(folded, inv)) {
            g_inspectionRendered=g_inspectionSequence;
            memcpy((void*)(cam + C_INVVIEW), inv, 64);
            c->eye.Publish((void*)cam,inv+12);
            g_renderFrame.active = 1;
            g_renderFrame.timestampMs = GetTickCount64();
            memcpy(g_renderFrame.right, inv, sizeof g_renderFrame.right);
            memcpy(g_renderFrame.up, inv + 4, sizeof g_renderFrame.up);
            memcpy(g_renderFrame.back, inv + 8, sizeof g_renderFrame.back);
            g_renderFrame.projection[0] = F<float>(cam, C_PROJ);
            g_renderFrame.projection[1] = F<float>(cam, C_PROJ + 5 * sizeof(float));
            g_renderFrame.projection[2] = F<float>(cam, C_PROJ + 8 * sizeof(float));
            g_renderFrame.projection[3] = F<float>(cam, C_PROJ + 9 * sizeof(float));
            memcpy(g_renderFrame.eye, inv + 12, sizeof g_renderFrame.eye);
            memcpy(g_pickingFrame.right,g_renderFrame.right,sizeof g_pickingFrame.right);
            memcpy(g_pickingFrame.up,g_renderFrame.up,sizeof g_pickingFrame.up);
            memcpy(g_pickingFrame.back,g_renderFrame.back,sizeof g_pickingFrame.back);
            memcpy(g_pickingFrame.eye,g_renderFrame.eye,sizeof g_pickingFrame.eye);
            memcpy(g_pickingFrame.projection,g_renderFrame.projection,sizeof g_pickingFrame.projection);
            g_pickingFrame.width=F<float>(cam,C_VPW); g_pickingFrame.height=F<float>(cam,C_VPH);
            g_pickingCamera=cam;
            g_pickingGround=F<float>(cam,C_LOOKAT+4); g_pickingNear=F<float>(cam,C_PROJ+14*sizeof(float));
            g_renderFrame.reserved = F<uint8_t>(cam, C_LOOKAT_OK) ? 1 : 0;
            if (g_renderFrame.reserved) memcpy(g_renderFrame.anchor, (void*)(cam + C_LOOKAT), sizeof g_renderFrame.anchor);
        } else g_renderFrame.active = 0;
        const auto movementFrame = CameraMovement::FromView(folded,sgn);
        AcquireSRWLockExclusive(&g_movementFrameLock);
        g_movementFrame = movementFrame;
        ReleaseSRWLockExclusive(&g_movementFrameLock);
        g_cameraReady.store(CameraMovement::Valid(movementFrame));
        c->lookSign = sgn;
    }
    ReleaseSRWLockExclusive(&g_camLock);
}

void HookRayBuild(uintptr_t cam, float* mouse, float* origin, float* dir) {
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    OrigRayBuild(cam, mouse, origin, dir);
    if (!cam || !mouse || !origin || !dir || !g_enabled.load()) return;
    bool world = false;
    CameraPicking::Frame frame{};
    float ground=0,nearPlane=0;
    AcquireSRWLockShared(&g_camLock);
    world=cam==g_master.load() && cam==g_pickingCamera && g_renderFrame.active &&
          GetTickCount64()-g_renderFrame.timestampMs<500;
    if (world) { frame=g_pickingFrame; ground=g_pickingGround; nearPlane=g_pickingNear; }
    ReleaseSRWLockShared(&g_camLock);
    float o[3], d[3];
    const bool shadow=ra>=g_base+RVA_SHADOWFIT && ra<g_base+RVA_SHADOWFIT_END;
    if (world && CameraPicking::Ray(frame, mouse, o, d)) {
        if (!shadow) NpcBodyRay(frame,ground,nearPlane,o,d);
        memcpy(origin, o, 12); memcpy(dir, d, 12);
    }
    // The directional shadow fits its box by intersecting screen-corner rays with the ground;
    // at shallow pitch those rays run nearly flat and the box explodes. Clamp their slope.
    if (shadow && dir[1] > -kShadowSlope) {
        float hx = dir[0], hz = dir[2], hl = sqrtf(hx*hx + hz*hz);
        if (hl < 1e-6f) { hx = 0; hz = 1; hl = 1; }
        const float k = sqrtf(1.0f - kShadowSlope * kShadowSlope);
        dir[0] = hx / hl * k; dir[1] = -kShadowSlope; dir[2] = hz / hl * k;
    }
}

// Mouse
HHOOK g_mouseHook = nullptr;
DWORD g_mouseThreadId = 0;
HANDLE g_mouseThread = nullptr;
POINT g_mmbLast{};

bool GameFocused() {
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

// Wheel = zoom (camera distance)
// Middle mouse drag = yaw/pitch around the player.
LRESULT CALLBACK MouseProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && g_enabled.load() && GameFocused()) {
        const auto* ms = (const MSLLHOOKSTRUCT*)l;
        if (w == WM_MOUSEWHEEL) {
            g_inspectionTakeover.store(true);
            const int steps = (short)HIWORD(ms->mouseData) / WHEEL_DELTA;
            g_dist.store(std::clamp(g_dist.load() / powf(1.12f, (float)steps), 3.5f, 1500.0f));
            g_gen.fetch_add(1);
            if (kWheelSwallow) return 1;
        } else if (w == WM_MBUTTONDOWN) {
            g_inspectionTakeover.store(true);
            if (g_follow.load()) g_yaw.store(g_followYaw.load());
            g_mmbDown.store(true); g_mmbLast = ms->pt;
        }
        else if (w == WM_MBUTTONUP) g_mmbDown.store(false);
        else if (w == WM_MOUSEMOVE && g_mmbDown.load()) {
            const int dx = ms->pt.x - g_mmbLast.x, dy = ms->pt.y - g_mmbLast.y;
            g_mmbLast = ms->pt;
            float yaw = g_yaw.load() + dx * 0.25f;
            yaw -= 360.0f * floorf((yaw + 180.0f) / 360.0f);
            g_yaw.store(yaw);
            g_pitch.store(std::clamp(g_pitch.load() + dy * 0.15f, -85.0f, 60.0f));
            g_mouseOrbitEpoch.fetch_add(1);
            g_gen.fetch_add(1);
        }
    } else if (code == HC_ACTION && w == WM_MBUTTONUP) g_mmbDown.store(false);
    if (code == HC_ACTION && !GameFocused()) g_mmbDown.store(false);
    return CallNextHookEx(g_mouseHook, code, w, l);
}

bool g_hooksOk = false;

const char* Toggle() {
    if (!g_hooksOk) return "3dcam: unavailable - the camera hooks did not install (see log)";
    const bool on = !g_enabled.load();
    g_enabled.store(on);
    if (!on) g_cameraReady.store(false);
    g_gen.fetch_add(1);
    g_followEpoch.fetch_add(1);
    SaveFollowConfig();
    if (on && !g_master.load()) return "3dcam ON - waiting for the world camera (enter a game)";
    return on ? "3dcam ON (F11 = follow, F10 = camera-relative movement, wheel = zoom, middle drag/right stick = orbit)" : "3dcam OFF";
}

const char* ToggleFollow() {
    if (!g_facingOk || g_followFault.load()) return "3dcam follow unavailable on this game build (see log)";
    const bool on = !g_follow.load();
    if (!on) g_yaw.store(g_followYaw.load()); // Keep the current view when returning to free orbit.
    g_follow.store(on); g_followEpoch.fetch_add(1); g_gen.fetch_add(1);
    SaveFollowConfig();
    return on ? "3dcam follow ON (smoothly stays behind character; F11 = free orbit)" : "3dcam follow OFF (free orbit; F11 = follow)";
}

ConsoleCommandResult __cdecl CmdFollow(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(ToggleFollow());
    return ConsoleCommandResult::Handled;
}

const char* ToggleMovement() {
    if (!g_movementOk && g_controllerHookState.load()!=1) return "3dcam: camera-relative movement unavailable on this game build";
    const bool on = !g_relativeMovement.load();
    g_relativeMovement.store(on); g_followEpoch.fetch_add(1);
    SaveFollowConfig();
    return on ? "3dcam movement ON (keyboard and controller walking follow the camera)" : "3dcam movement OFF (original isometric directions)";
}

ConsoleCommandResult __cdecl CmdMovement(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(ToggleMovement());
    return ConsoleCommandResult::Handled;
}

ConsoleCommandResult __cdecl CmdNpcTargeting(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    const bool enabled=!g_npcTargeting.load(); g_npcTargeting.store(enabled); SaveFollowConfig();
    Say(enabled?"NPC body targeting ON":"NPC body targeting OFF (native lower-body area)");
    return ConsoleCommandResult::Handled;
}

ConsoleCommandResult __cdecl Cmd3dcam(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(Toggle());
    return ConsoleCommandResult::Handled;
}

// Useful for repeatable visual checks when a middle-button orbit is unavailable.
// Applies only to this session, just like wheel zoom and mouse orbit.
ConsoleCommandResult __cdecl CmdView(D2R::Game::Client*, const ConsoleCommandContext* command, void*) noexcept {
    float pitch=0, distance=0; char extra=0;
    if (!command || !command->args || sscanf_s(command->args,"%f %f %c",&pitch,&distance,&extra,1)!=2 ||
        !std::isfinite(pitch) || !std::isfinite(distance) || pitch < -85 || pitch > 60 || distance < 3.5f || distance > 1500) {
        Say("Usage: 3dcam_view <pitch -85..60> <distance 3.5..1500>");
        return ConsoleCommandResult::Handled;
    }
    g_pitch.store(pitch); g_dist.store(distance); g_gen.fetch_add(1); g_followEpoch.fetch_add(1);
    Say("3dcam: session view updated");
    return ConsoleCommandResult::Handled;
}

std::string InspectionStatus(uint64_t sequence=0,bool ok=true,const char* error="") {
    char status[1400];
    AcquireSRWLockShared(&g_camLock);
    const bool fresh=g_renderFrame.active && g_enabled.load() && g_pickWorld.load() &&
        GetTickCount64()-g_renderFrame.timestampMs<500;
    snprintf(status,sizeof status,
        "{\"version\":\"1.4.2\",\"process_id\":%lu,\"session\":%llu,\"sequence\":%llu,\"ok\":%s,\"error\":\"%s\","
        "\"inspection_enabled\":%s,\"held\":%s,\"rendered_sequence\":%llu,\"world\":%s,\"camera_enabled\":%s,\"fresh\":%s,"
        "\"follow_enabled\":%s,\"movement_camera_relative\":%s,\"pitch\":%.4f,\"distance\":%.4f,\"yaw\":%.4f,"
        "\"depth_min\":%.4f,\"depth_max\":%.4f,\"near_plane\":%.4f,\"eye\":[%.4f,%.4f,%.4f]}\n",
        GetCurrentProcessId(),(unsigned long long)g_inspectionSession,(unsigned long long)sequence,ok?"true":"false",error,
        g_inspectionEnabled.load()?"true":"false",g_inspection.active?"true":"false",(unsigned long long)g_inspectionRendered,
        g_pickWorld.load()?"true":"false",g_enabled.load()?"true":"false",fresh?"true":"false",g_follow.load()?"true":"false",
        g_relativeMovement.load()?"true":"false",g_pitch.load(),g_dist.load(),g_followYaw.load(),
        g_renderMetadata.depthMin,g_renderMetadata.depthMax,g_renderMetadata.nearPlane,
        g_renderFrame.eye[0],g_renderFrame.eye[1],g_renderFrame.eye[2]);
    ReleaseSRWLockShared(&g_camLock);
    return status;
}
bool WriteInspectionStatus(uint64_t sequence=0,bool ok=true,const char* error="") {
    const auto status=InspectionStatus(sequence,ok,error);
    const auto temporary=g_inspectionStatusPath.wstring()+L".tmp";
    AcquireSRWLockExclusive(&g_inspectionFileLock);
    bool wrote=false;
    { std::ofstream output(temporary,std::ios::binary); output<<status; wrote=(bool)output; }
    const bool okWrite=wrote && MoveFileExW(temporary.c_str(),g_inspectionStatusPath.c_str(),MOVEFILE_REPLACE_EXISTING)!=0;
    ReleaseSRWLockExclusive(&g_inspectionFileLock); return okWrite;
}
void InitializeInspection() {
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,std::size(executable));
    const auto directory=std::filesystem::path(executable).parent_path()/L"d2rloader"/L"inspection";
    const auto name=L"camera-"+std::to_wstring(GetCurrentProcessId());
    g_inspectionRequestPath=directory/(name+L"-request.txt");
    g_inspectionStatusPath=directory/(name+L"-status.json");
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    g_inspectionSession=((uint64_t)time.dwHighDateTime<<32)|time.dwLowDateTime;
    if (g_inspectionEnabled.load()) {
        std::error_code error; std::filesystem::create_directories(directory,error);
        if (error || !WriteInspectionStatus()) { g_inspectionEnabled.store(false); g_ctx->LogError("3dcam: optional inspection files unavailable"); }
    }
}
bool ApplyInspection(const CameraInspection::Request& request) {
    if (request.action==CameraInspection::Action::Status) return true;
    AcquireSRWLockExclusive(&g_camLock);
    bool ok=false;
    if (request.action==CameraInspection::Action::Restore) {
        CameraInspection::Pose saved{};
        ok=g_inspection.Restore(saved);
        if (ok) {
            g_pitch.store(saved.pitch); g_dist.store(saved.distance); g_yaw.store(saved.yaw);
            g_mouseOrbitEpoch.fetch_add(1); g_gen.fetch_add(1); g_inspectionSequence=request.sequence;
        }
    } else if (g_enabled.load() && g_pickWorld.load() && g_renderFrame.active &&
        GetTickCount64()-g_renderFrame.timestampMs<500) {
        const CameraInspection::Pose current{g_pitch.load(),g_dist.load(),g_followYaw.load()};
        ok=g_inspection.Hold(request.pose,current);
        if (ok) {
            g_inspectionTakeover.store(false);
            g_pitch.store(request.pose.pitch); g_dist.store(request.pose.distance); g_yaw.store(request.pose.yaw);
            g_gen.fetch_add(1); g_inspectionSequence=request.sequence;
        }
    }
    ReleaseSRWLockExclusive(&g_camLock); return ok;
}
void PollInspection() {
    if (!g_inspectionEnabled.load()) return;
    if (g_inspectionPendingSequence) {
        AcquireSRWLockShared(&g_camLock);
        const bool rendered=g_inspectionRendered>=g_inspectionPendingSequence;
        ReleaseSRWLockShared(&g_camLock);
        if (rendered) { WriteInspectionStatus(g_inspectionPendingSequence); g_inspectionPendingSequence=0; }
    }
    WIN32_FILE_ATTRIBUTE_DATA file{};
    if (!GetFileAttributesExW(g_inspectionRequestPath.c_str(),GetFileExInfoStandard,&file) ||
        file.nFileSizeHigh || file.nFileSizeLow>256 || CompareFileTime(&file.ftLastWriteTime,&g_inspectionLastWrite)==0) return;
    g_inspectionLastWrite=file.ftLastWriteTime;
    char text[257]{}; std::ifstream input(g_inspectionRequestPath,std::ios::binary);
    input.read(text,256); CameraInspection::Request request{};
    if (!CameraInspection::Parse(std::string(text,(size_t)input.gcount()),request)) { WriteInspectionStatus(0,false,"Invalid camera-only request"); return; }
    if (request.session!=g_inspectionSession || request.sequence<=g_inspectionLastSequence) {
        WriteInspectionStatus(request.sequence,false,"Stale session or request"); return;
    }
    g_inspectionLastSequence=request.sequence;
    g_inspectionPendingSequence=0;
    const bool ok=ApplyInspection(request);
    if (ok && request.action!=CameraInspection::Action::Status) g_inspectionPendingSequence=request.sequence;
    WriteInspectionStatus(request.sequence,ok,ok?"":"No active 3D view or saved inspection pose");
}
ConsoleCommandResult __cdecl CmdStatus(D2R::Game::Client*,const ConsoleCommandContext*,void*) noexcept {
    Say(InspectionStatus().c_str()); return ConsoleCommandResult::Handled;
}
ConsoleCommandResult __cdecl CmdInspection(D2R::Game::Client*,const ConsoleCommandContext*,void*) noexcept {
    const bool on=!g_inspectionEnabled.load();
    if (on) {
        std::error_code error; std::filesystem::create_directories(g_inspectionStatusPath.parent_path(),error);
        if (error) { Say("3dcam: inspection directory unavailable"); return ConsoleCommandResult::Handled; }
    } else {
        CameraInspection::Request request{}; request.action=CameraInspection::Action::Restore; ApplyInspection(request);
    }
    g_inspectionEnabled.store(on); SaveFollowConfig(); WriteInspectionStatus();
    Say(on?"3dcam: optional camera-only inspection ON":"3dcam: camera-only inspection OFF");
    return ConsoleCommandResult::Handled;
}

// Mouse hook + F12 polling. F12 runs outside the game thread, so it only logs.
DWORD WINAPI MouseThread(void*) {
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, g_self, 0);
    SetTimer(nullptr, 0, 10, nullptr);
    bool f12Was = false, f11Was = false, f10Was = false;
    ULONGLONG controllerCheck=0,inspectionCheck=0;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            const ULONGLONG now=GetTickCount64();
            if (now-controllerCheck>=1000) { controllerCheck=now; InstallControllerMovementHook(); }
            if (now-inspectionCheck>=100) { inspectionCheck=now; PollInspection(); }
            const bool f12 = GameFocused() && (GetAsyncKeyState(VK_F12) & 0x8000);
            if (f12 && !f12Was && g_ctx) g_ctx->LogInfo(Toggle());
            f12Was = f12;
            const bool f11 = GameFocused() && (GetAsyncKeyState(VK_F11) & 0x8000);
            if (f11 && !f11Was && g_ctx) g_ctx->LogInfo(ToggleFollow());
            f11Was = f11;
            const bool f10 = GameFocused() && (GetAsyncKeyState(VK_F10) & 0x8000);
            if (f10 && !f10Was && g_ctx) g_ctx->LogInfo(ToggleMovement());
            f10Was = f10;
            if (g_master.load() && !g_masterLogged.exchange(true) && g_ctx) {
                char b[96];
                snprintf(b, sizeof b, "3dcam: world camera %p (projection type %d)", (void*)g_master.load(), F<int32_t>(g_master.load(), C_TYPE));
                g_ctx->LogInfo(b);
            }
            continue;
        }
        DispatchMessageW(&m);
    }
    if (g_mouseHook) UnhookWindowsHookEx(g_mouseHook);
    return 0;
}

}  // namespace

static const PluginInfo g_info = {
    PluginInfoSize, D2RL_PLUGIN_ABI_VERSION, "d2r-3d-3dcam", "3dcam", "1.4.2", "Tandanu (local third-person extension)",
    "Third-person follow and camera-relative walking enabled by default. Right stick: orbit. F12: camera. F11: follow. F10: movement. Wheel: zoom.",
    PluginFlags::Shared | PluginFlags::NativeHooks, {0, 0, 0, 0},
};

D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &g_info; }

D2RL_PLUGIN_EXPORT bool D2R3DIsCameraEnabled() noexcept { return g_enabled.load() && g_hooksOk; }

D2RL_PLUGIN_EXPORT bool D2R3DGetCameraFrame(D2R3DCameraFrame* output, uint32_t size) noexcept {
    if (!output || size < sizeof(D2R3DCameraFrame)) return false;
    AcquireSRWLockShared(&g_camLock);
    *output = g_renderFrame;
    ReleaseSRWLockShared(&g_camLock);
    output->active = output->active && g_enabled.load() && GetTickCount64() - output->timestampMs < 500;
    return true;
}

D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const PluginContext* ctx) noexcept {
    g_ctx = ctx;
    if (!ctx) return true;
    g_base = ctx->exeBase ? ctx->exeBase : (uintptr_t)GetModuleHandleW(nullptr);
    g_controllerHookState.store(0);
    LoadFollowConfig();
    g_npcLayoutOk=ctx->CheckExpectedBytes(RVA_CLIENT_LOOKUP,kSigClientLookup,sizeof kSigClientLookup);
    ctx->LogInfo(g_npcLayoutOk?"3dcam: NPC client layout verified; body targeting available":"3dcam: NPC layout mismatch; original targeting retained");
    const LifecycleService* pickingLifecycle=nullptr;
    if (ctx->QueryService(&pickingLifecycle)==ServiceQueryResult::Success) for (auto kind:{Lifecycle::GameplayEventKind::GameJoined,Lifecycle::GameplayEventKind::GameLeft,Lifecycle::GameplayEventKind::LocalPlayerReady}) {
        Lifecycle::GameplayEventListener listener{}; listener.structSize=sizeof listener; listener.kind=kind; listener.callback=OnPickingGameplay;
        Lifecycle::ListenerHandle handle=0; pickingLifecycle->registerGameplayEventListener(ctx,&listener,&handle);
    }
    const SharedEventService* pickingEvents=nullptr;
    if (ctx->QueryService(&pickingEvents)==ServiceQueryResult::Success) {
        SharedEvents::UiMessageListener listener{}; listener.structSize=sizeof listener; listener.callback=OnPickingUi;
        SharedEvents::ListenerHandle handle=0; pickingEvents->registerUiMessageListener(ctx,&listener,&handle);
    }
    g_facingOk = ctx->CheckExpectedBytes(RVA_PLAYER_SLOT, kSigPlayerSlot, sizeof kSigPlayerSlot) &&
                 ctx->CheckExpectedBytes(RVA_PLAYER_BY_SLOT, kSigPlayerBySlot, sizeof kSigPlayerBySlot) &&
                 ctx->CheckExpectedBytes(RVA_PATH_FACING, kSigPathFacing, sizeof kSigPathFacing);
    ctx->LogInfo(g_facingOk ? "3dcam: native character-facing getters verified" : "3dcam: facing getter mismatch; follow unavailable");
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Cmd3dcam, &g_self);
    const bool hc = memcmp((void*)(g_base + RVA_COPY_CALL), kSigCopyCall, sizeof kSigCopyCall) == 0 &&
                    ctx->InstallInlineHook(RVA_CAMCOPY, kSigCamCopy, sizeof kSigCamCopy, (void*)&HookCamCopy, (void**)&OrigCamCopy);
    const bool h1 = ctx->InstallInlineHook(RVA_GETPROJ, kSigGetProj, sizeof kSigGetProj, (void*)&HookGetProj, (void**)&OrigGetProj);
    const bool h2 = ctx->InstallInlineHook(RVA_VIEWREBUILD, kSigViewRebuild, sizeof kSigViewRebuild, (void*)&HookViewRebuild, (void**)&OrigViewRebuild);
    const bool h3 = ctx->InstallInlineHook(RVA_RAYBUILD, kSigRayBuild, sizeof kSigRayBuild, (void*)&HookRayBuild, (void**)&OrigRayBuild);
    g_hooksOk = hc && h1 && h2;
    if (!g_hooksOk) g_enabled.store(false);
    char b[160];
    snprintf(b, sizeof b, "3dcam loaded: camCopy=%s getProj=%s viewRebuild=%s rayBuild=%s%s", hc ? "ok" : "FAIL", h1 ? "ok" : "FAIL", h2 ? "ok" : "FAIL", h3 ? "ok" : "FAIL",
             h3 ? "" : " (targeting + shadow fit unavailable)");
    ctx->LogInfo(b);
    g_movementOk = ctx->InstallInlineHook(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove, (void*)&HookKeyMove, (void**)&OrigKeyMove);
    ctx->LogInfo(g_movementOk ? "3dcam: camera-relative native keyboard movement hook installed" : "3dcam: keyboard movement hook unavailable; original movement retained");
    InstallControllerMovementHook();
    if (g_controllerHookState.load()==0) ctx->LogInfo("3dcam: controller walking hook waiting for verified native code");
    ctx->LogInfo(g_follow.load() ? "3dcam follow enabled by config (F11 toggles)" : "3dcam free orbit enabled by config (F11 toggles)");
    snprintf(b,sizeof b,"3dcam: default %s, distance %.1f, pitch %.1f, movement %s, controller turn %.0f%%",
        g_enabled.load()?"ON":"OFF",g_defaultDistance,g_defaultPitch,g_relativeMovement.load()?"camera-relative":"isometric",g_controllerTurnScale*100);
    ctx->LogInfo(b);
    InitializeInspection();
    g_mouseThread=CreateThread(nullptr,0,MouseThread,nullptr,0,&g_mouseThreadId);
    if (!ctx->RegisterConsoleCommand("3dcam", &Cmd3dcam, "3dcam - switch the perspective 3D camera on/off")) ctx->LogError("3dcam: could not register the console command");
    if (!ctx->RegisterConsoleCommand("3dcam_view", &CmdView, "3dcam_view <pitch> <distance> - set this session's camera view")) ctx->LogError("3dcam: could not register the view command");
    if (!ctx->RegisterConsoleCommand("3dcam_status",&CmdStatus,"3dcam_status - report live camera settings and depth range")) ctx->LogError("3dcam: could not register status command");
    if (!ctx->RegisterConsoleCommand("3dcam_inspection",&CmdInspection,"3dcam_inspection - toggle optional local camera-only inspection files")) ctx->LogError("3dcam: could not register inspection command");
    if (!ctx->RegisterConsoleCommand("3dcam_follow", &CmdFollow, "3dcam_follow - toggle smooth character-facing follow (F11)")) ctx->LogError("3dcam: could not register the follow command");
    if (!ctx->RegisterConsoleCommand("3dcam_movement", &CmdMovement, "3dcam_movement - toggle camera-relative keyboard/controller walking (F10)")) ctx->LogError("3dcam: could not register the movement command");
    if (!ctx->RegisterConsoleCommand("3dcam_npc_targeting", &CmdNpcTargeting, "3dcam_npc_targeting - toggle NPC body hover assistance")) ctx->LogError("3dcam: could not register NPC targeting command");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    g_enabled.store(false);
    g_cameraReady.store(false);
    if (g_mouseThreadId) PostThreadMessageW(g_mouseThreadId, WM_QUIT, 0, 0);
    if (g_mouseThread) { WaitForSingleObject(g_mouseThread,5000); CloseHandle(g_mouseThread); g_mouseThread=nullptr; }
    g_ctx = nullptr;
}
