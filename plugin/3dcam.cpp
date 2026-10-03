// Console command:  3dcam
// Hotkey:           F12 (Shift+F12 also works and leaves renderdistance alone)
// While on: mouse wheel = zoom, middle mouse drag = orbit (yaw/pitch).

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

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/lifecycle.h>

#pragma intrinsic(_ReturnAddress)

using namespace D2RL;

namespace {

// Defaults
constexpr float kFov = 60.0f;            // perspective vertical FOV (deg)
constexpr float kDist = 45.0f;           // default camera-to-pivot distance (world units); the wheel changes it
constexpr float kPitch = 22.0f;          // default pitch (deg)
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
std::atomic<bool> g_enabled{false};
std::atomic<float> g_dist{kDist}, g_pitch{kPitch}, g_yaw{0.0f};
std::atomic<uint32_t> g_gen{1};
std::atomic<uintptr_t> g_master{0};

struct Cam { uintptr_t ptr; float proj0[16]; float extW, extH, nearZ; int type; bool haveBase, written; uint32_t lastGen; float lookSign; };
SRWLOCK g_camLock = SRWLOCK_INIT;
Cam g_cams[8];
int g_camCount = 0;

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
    const bool wasDirty = F<uint8_t>(cam, C_PDIRTY) != 0;
    const uintptr_t ret = OrigGetProj(cam);
    AcquireSRWLockExclusive(&g_camLock);
    Cam* c = FindCam(cam);
    if (!c && g_camCount < 8) { c = &g_cams[g_camCount++]; memset(c, 0, sizeof *c); c->ptr = cam; c->lookSign = -1.0f; }
    if (c) {
        if (wasDirty || !c->haveBase) {
            memcpy(c->proj0, (void*)(cam + C_PROJ), 64); c->haveBase = true;
            c->type = F<int32_t>(cam, C_TYPE); c->extW = F<float>(cam, C_EXTW); c->extH = F<float>(cam, C_EXTH); c->nearZ = F<float>(cam, C_NEAR);
        }
        const uint32_t gen = g_gen.load();
        if (c->lastGen != gen) { F<uint8_t>(cam, C_VDIRTY) = 1; c->lastGen = gen; }
        if (g_enabled.load() && c->ptr == g_master.load()) {
            float M[16]; BuildProj(*c, M);
            memcpy((void*)(cam + C_PROJ), M, 64);
            c->written = true;
        } else if (c->written) {
            F<uint8_t>(cam, C_PDIRTY) = 1; c->written = false;
        }
    }
    ReleaseSRWLockExclusive(&g_camLock);
    return ret;
}

void HookViewRebuild(uintptr_t cam) {
    OrigViewRebuild(cam);
    if (!cam || !g_enabled.load()) return;
    AcquireSRWLockExclusive(&g_camLock);
    Cam* c = FindCam(cam);
    if (c && c->haveBase && c->ptr == g_master.load()) {
        float view[16]; memcpy(view, (void*)(cam + C_VIEW), 64);
        float P[4] = {0, 0, -Radius(*c), 1};
        if (F<uint8_t>(cam, C_LOOKAT_OK)) {   // pivot = look-at point lifted to body height (world y-up)
            const float L[4] = {F<float>(cam, C_LOOKAT), F<float>(cam, C_LOOKAT + 4) + kHeight, F<float>(cam, C_LOOKAT + 8), 1};
            Vec4Mat(L, view, P);
        }
        const float sgn = P[2] <= 0.0f ? -1.0f : 1.0f;
        const float d2r = 3.14159265f / 180.0f;
        float R[16], Ry[16], RR[16];
        AxisRot4(R, 1, 0, 0, g_pitch.load() * d2r);
        const float un = sqrtf(view[4]*view[4] + view[5]*view[5] + view[6]*view[6]);   // world up in view space
        if (g_yaw.load() != 0.0f && un > 1e-6f) { AxisRot4(Ry, view[4]/un, view[5]/un, view[6]/un, g_yaw.load() * d2r); Mul4(Ry, R, RR); memcpy(R, RR, 64); }
        float A[16]; memcpy(A, R, 64);
        A[12] = -(P[0]*R[0] + P[1]*R[4] + P[2]*R[8]);
        A[13] = -(P[0]*R[1] + P[1]*R[5] + P[2]*R[9]);
        A[14] = sgn * Radius(*c) - (P[0]*R[2] + P[1]*R[6] + P[2]*R[10]);
        float folded[16], inv[16];
        Mul4(view, A, folded);
        memcpy((void*)(cam + C_VIEW), folded, 64);
        if (Invert4(folded, inv)) memcpy((void*)(cam + C_INVVIEW), inv, 64);
        c->lookSign = sgn;
    }
    ReleaseSRWLockExclusive(&g_camLock);
}

// Mouse ray through the camera's live (our) matrices: origin at the eye, unit direction.
bool UnprojectRay(uintptr_t cam, const float* mouse, float lookSign, float* origin, float* dir) {
    float V[16], P[16], invV[16], invP[16];
    memcpy(V, (void*)(cam + C_VIEW), 64); memcpy(P, (void*)(cam + C_PROJ), 64);
    const float W = F<float>(cam, C_VPW), H = F<float>(cam, C_VPH);
    if (!(W > 0 && H > 0) || !Invert4(V, invV) || !Invert4(P, invP)) return false;
    const float nx = 2.0f * mouse[0] / W - 1.0f, ny = 1.0f - 2.0f * mouse[1] / H;
    float a[4], b[4];
    const float ca[4] = {nx, ny, 1.0f, 1}, cb[4] = {nx, ny, 0.25f, 1};   // two depths on the ray
    Vec4Mat(ca, invP, a); Vec4Mat(cb, invP, b);
    if (fabsf(a[3]) < 1e-12f || fabsf(b[3]) < 1e-12f) return false;
    float d[4] = {b[0]/b[3] - a[0]/a[3], b[1]/b[3] - a[1]/a[3], b[2]/b[3] - a[2]/a[3], 0};
    if (d[2] * lookSign < 0) { d[0] = -d[0]; d[1] = -d[1]; d[2] = -d[2]; }
    const float eye[4] = {0, 0, 0, 1};
    float ow[4], dw[4]; Vec4Mat(eye, invV, ow); Vec4Mat(d, invV, dw);
    const float n = sqrtf(dw[0]*dw[0] + dw[1]*dw[1] + dw[2]*dw[2]);
    if (!(n > 1e-9f) || fabsf(ow[3]) < 1e-12f) return false;
    for (int i = 0; i < 3; ++i) { origin[i] = ow[i] / ow[3]; dir[i] = dw[i] / n; }
    return true;
}

void HookRayBuild(uintptr_t cam, float* mouse, float* origin, float* dir) {
    const uintptr_t ra = (uintptr_t)_ReturnAddress();
    OrigRayBuild(cam, mouse, origin, dir);
    if (!cam || !mouse || !origin || !dir || !g_enabled.load()) return;
    bool world = false; float lookSign = -1.0f;
    AcquireSRWLockShared(&g_camLock);
    if (const Cam* c = FindCam(cam)) { world = c->ptr == g_master.load(); lookSign = c->lookSign; }
    ReleaseSRWLockShared(&g_camLock);
    float o[3], d[3];
    if (world && UnprojectRay(cam, mouse, lookSign, o, d)) { memcpy(origin, o, 12); memcpy(dir, d, 12); }
    // The directional shadow fits its box by intersecting screen-corner rays with the ground;
    // at shallow pitch those rays run nearly flat and the box explodes. Clamp their slope.
    if (ra >= g_base + RVA_SHADOWFIT && ra < g_base + RVA_SHADOWFIT_END && dir[1] > -kShadowSlope) {
        float hx = dir[0], hz = dir[2], hl = sqrtf(hx*hx + hz*hz);
        if (hl < 1e-6f) { hx = 0; hz = 1; hl = 1; }
        const float k = sqrtf(1.0f - kShadowSlope * kShadowSlope);
        dir[0] = hx / hl * k; dir[1] = -kShadowSlope; dir[2] = hz / hl * k;
    }
}

// Mouse
HHOOK g_mouseHook = nullptr;
DWORD g_mouseThreadId = 0;
bool g_mmbDown = false;
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
            const int steps = (short)HIWORD(ms->mouseData) / WHEEL_DELTA;
            g_dist.store(std::clamp(g_dist.load() / powf(1.12f, (float)steps), 3.5f, 1500.0f));
            g_gen.fetch_add(1);
            if (kWheelSwallow) return 1;
        } else if (w == WM_MBUTTONDOWN) { g_mmbDown = true; g_mmbLast = ms->pt; }
        else if (w == WM_MBUTTONUP) g_mmbDown = false;
        else if (w == WM_MOUSEMOVE && g_mmbDown) {
            const int dx = ms->pt.x - g_mmbLast.x, dy = ms->pt.y - g_mmbLast.y;
            g_mmbLast = ms->pt;
            float yaw = g_yaw.load() + dx * 0.25f;
            yaw -= 360.0f * floorf((yaw + 180.0f) / 360.0f);
            g_yaw.store(yaw);
            g_pitch.store(std::clamp(g_pitch.load() + dy * 0.15f, -85.0f, 60.0f));
            g_gen.fetch_add(1);
        }
    } else if (code == HC_ACTION && w == WM_MBUTTONUP) g_mmbDown = false;
    return CallNextHookEx(g_mouseHook, code, w, l);
}

bool g_hooksOk = false;

const char* Toggle() {
    if (!g_hooksOk) return "3dcam: unavailable - the camera hooks did not install (see log)";
    const bool on = !g_enabled.load();
    g_enabled.store(on);
    g_gen.fetch_add(1);
    if (on && !g_master.load()) return "3dcam ON - waiting for the world camera (enter a game)";
    return on ? "3dcam ON (mouse wheel = zoom, middle mouse drag = orbit)" : "3dcam OFF";
}

ConsoleCommandResult __cdecl Cmd3dcam(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(Toggle());
    return ConsoleCommandResult::Handled;
}

// Mouse hook + F12 polling. F12 runs outside the game thread, so it only logs.
DWORD WINAPI MouseThread(void*) {
    g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, g_self, 0);
    SetTimer(nullptr, 0, 10, nullptr);
    bool f12Was = false;
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_TIMER) {
            const bool f12 = GameFocused() && (GetAsyncKeyState(VK_F12) & 0x8000);
            if (f12 && !f12Was && g_ctx) g_ctx->LogInfo(Toggle());
            f12Was = f12;
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
    PluginInfoSize, D2RL_PLUGIN_API_VERSION, "d2r-3d-3dcam", "3dcam", "1.0.0", "Tandanu",
    "Perspective 3D camera with orbit, zoom, corrected targeting and shadow fit. Press F12 to enable.",
    PluginFlags::Shared | PluginFlags::NativeHooks, {0, 0, 0, 0},
};

D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &g_info; }

D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const PluginContext* ctx) noexcept {
    g_ctx = ctx;
    if (!ctx) return true;
    g_base = ctx->exeBase ? ctx->exeBase : (uintptr_t)GetModuleHandleW(nullptr);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Cmd3dcam, &g_self);
    const bool hc = memcmp((void*)(g_base + RVA_COPY_CALL), kSigCopyCall, sizeof kSigCopyCall) == 0 &&
                    ctx->InstallInlineHook(RVA_CAMCOPY, kSigCamCopy, sizeof kSigCamCopy, (void*)&HookCamCopy, (void**)&OrigCamCopy);
    const bool h1 = ctx->InstallInlineHook(RVA_GETPROJ, kSigGetProj, sizeof kSigGetProj, (void*)&HookGetProj, (void**)&OrigGetProj);
    const bool h2 = ctx->InstallInlineHook(RVA_VIEWREBUILD, kSigViewRebuild, sizeof kSigViewRebuild, (void*)&HookViewRebuild, (void**)&OrigViewRebuild);
    const bool h3 = ctx->InstallInlineHook(RVA_RAYBUILD, kSigRayBuild, sizeof kSigRayBuild, (void*)&HookRayBuild, (void**)&OrigRayBuild);
    g_hooksOk = hc && h1 && h2;
    char b[160];
    snprintf(b, sizeof b, "3dcam loaded: camCopy=%s getProj=%s viewRebuild=%s rayBuild=%s%s", hc ? "ok" : "FAIL", h1 ? "ok" : "FAIL", h2 ? "ok" : "FAIL", h3 ? "ok" : "FAIL",
             h3 ? "" : " (targeting + shadow fit unavailable)");
    ctx->LogInfo(b);
    if (HANDLE h = CreateThread(nullptr, 0, MouseThread, nullptr, 0, &g_mouseThreadId)) CloseHandle(h);
    if (!ctx->RegisterConsoleCommand("3dcam", &Cmd3dcam, "3dcam - switch the perspective 3D camera on/off")) ctx->LogError("3dcam: could not register the console command");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    g_enabled.store(false);
    if (g_mouseThreadId) PostThreadMessageW(g_mouseThreadId, WM_QUIT, 0, 0);
    g_ctx = nullptr;
}
