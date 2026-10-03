// Console command:  renderdist
// Hotkey:           F12 (ignored with Shift held, so Shift+F12 switches only 3dcam)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <D2RLPlugin/context.h>
#include <D2RLPlugin/lifecycle.h>

using namespace D2RL;

namespace {

// Settings
constexpr float kMvRadius = 3000.0f;     // model visibility radius (vanilla 150)
constexpr int   kBuildRings = 12;        // extra rings of rooms built around the player
constexpr int   kBuildRate = 16;         // max rooms built per DRLG update
constexpr int   kRenderPoolMB = 64;      // renderer pool size (vanilla 12)
constexpr int   kEntityPoolMB = 280;     // entity (EnTT) pool size (vanilla 70)

const PluginContext* g_ctx = nullptr;
uintptr_t g_base = 0;

constexpr uint64_t RVA_ACTTEST = 0x6AAA70, RVA_DRLG_UPDATE = 0x328A00, RVA_ROOM_INIT = 0x3289A0,
                   RVA_ROOM_RELEASE = 0x3F3AA0, RVA_PLAYER_AROOM = 0x9A180, RVA_MVRADIUS_SET = 0xD90150,
                   RVA_RPOOL_OBJ = 0x2810FF0, RVA_RPOOL_VTBL = 0x1D6D770, RVA_TLSF_INSERT = 0x1215650,
                   RVA_POOL_LOCK = 0x122AD60, RVA_POOL_UNLOCK = 0x122AD90,
                   RVA_EPOOL_OBJ = 0x2677438;   // entity pool object, same class as the renderer pool
const uint8_t kSigActTest[16]     = {0x4C,0x8B,0xDC,0x55,0x56,0x41,0x54,0x41,0x56,0x49,0x8D,0xAB,0xA8,0xFB,0xFF,0xFF};
const uint8_t kSigDrlgUpdate[16]  = {0x40,0x53,0x56,0x57,0x41,0x54,0x48,0x83,0xEC,0x28,0x4C,0x89,0x74,0x24,0x60,0x48};
const uint8_t kSigRoomInit[16]    = {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x42,0x50,0x48,0x8B,0xDA};
const uint8_t kSigRoomRelease[16] = {0x48,0x89,0x5C,0x24,0x20,0x55,0x57,0x41,0x54,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81};
const uint8_t kSigPlayerARoom[5]  = {0x48,0x83,0xEC,0x28,0xE8};
const uint8_t kSigMvRadiusSet[8]  = {0xF3,0x0F,0x5F,0x05,0xAC,0xA7,0xF2,0x00};   // maxss xmm0,[1.0]; movss [radius],xmm0
const uint8_t kSigTlsfInsert[16]  = {0x4C,0x8B,0x42,0x08,0x4C,0x8B,0xCA,0x49,0x83,0xE0,0xFC,0x4C,0x8B,0xD9,0x49,0x81};
const uint8_t kSigPoolLock[3]     = {0x48,0xFF,0x25};                            // jmp [import]
constexpr size_t MGR_ACTDEPTH = 0x1E34, MGR_RECOMPUTE = 0x1E3C;

bool Matches(uint64_t rva, const uint8_t* sig, size_t n) { return memcmp((void*)(g_base + rva), sig, n) == 0; }

void Say(const char* msg) {
    if (!g_ctx) return;
    g_ctx->WriteConsoleMessage(msg, ConsoleMessageKind::Output);
    g_ctx->LogInfo(msg);
}
void Log(const char* msg) { if (g_ctx) g_ctx->LogInfo(msg); }

// State
std::atomic<bool> g_enabled{false};
std::atomic<uintptr_t> g_roomMgr{0};     // HD room manager, captured by the ActTest hook
std::atomic<bool> g_depthApplied{false};
std::atomic<int> g_nativeDepth{-1};      // mgr+0x1E34 before we changed it
bool g_buildOk = false, g_mvOk = false, g_playerRoomOk = false;
std::atomic<bool> g_buildFault{false};

// Room loading
using ActTestFn = char (*)(uintptr_t mgr, uintptr_t room, float* box1, float* box2);
ActTestFn OrigActTest;

char HookActTest(uintptr_t mgr, uintptr_t room, float* box1, float* box2) {
    const char r = OrigActTest(mgr, room, box1, box2);
    g_roomMgr.store(mgr);
    int& depth = *(int*)(mgr + MGR_ACTDEPTH);
    if (!g_enabled.load()) {
        if (g_depthApplied.exchange(false)) {
            depth = g_nativeDepth.load();
            *(uint8_t*)(mgr + MGR_RECOMPUTE) = 1;
        }
        return r;
    }
    if (!g_depthApplied.exchange(true)) {
        if (g_nativeDepth.load() < 0) g_nativeDepth.store(depth);
        depth = std::max(g_nativeDepth.load(), 2 + kBuildRings);
        *(uint8_t*)(mgr + MGR_RECOMPUTE) = 1;
    }
    return 1;
}

constexpr size_t R_NEAR = 0x10, R_NEARN = 0x18, R_SNEXT = 0x38, R_LNEXT = 0x48, R_FLAGS = 0x50,
                 R_ACTIVE = 0x58, R_STATUS = 0x70, R_LEVEL = 0x90;
constexpr size_t L_FIRST = 0x10, L_NEXT = 0x1B8, L_DRLG = 0x1C8;
constexpr size_t D_FLAGS = 0x110, D_BUILT = 0x124, D_LIST0 = 0x130, D_LEVELS = 0x868;
constexpr uint32_t HAS_ROOM = 0x100000;
constexpr int kNativeRings = 2;          // the game itself builds rings 0..2
template <class T> T rd(uintptr_t a) { return *(const T*)a; }

using DrlgUpdateFn = void (*)(uint8_t ctx, uintptr_t drlg);
using RoomInitFn = uintptr_t (*)(uint8_t ctx, uintptr_t room);
using RoomReleaseFn = void (*)(uintptr_t room, int keepRoom);
using PlayerRoomFn = uintptr_t (*)();
DrlgUpdateFn OrigDrlgUpdate;
uintptr_t g_ownedDrlg = 0;
std::unordered_set<uintptr_t> g_owned;
uint64_t g_passes = 0;

void RoomBuilderPass(uint8_t ctx, uintptr_t drlg) {
    if (!drlg || !(rd<uint32_t>(drlg + D_FLAGS) & 1)) return;   // client DRLG only
    if (drlg != g_ownedDrlg) { g_owned.clear(); g_ownedDrlg = drlg; }
    const bool on = g_enabled.load();
    const int maxBuild = on ? kNativeRings + kBuildRings : -1, keepRing = on ? maxBuild + 2 : -1;   // hysteresis before release

    static std::unordered_map<uintptr_t, int> depth;
    static std::vector<uintptr_t> order;
    depth.clear(); order.clear();
    uintptr_t seed = 0;
    if (g_playerRoomOk)
        if (const uintptr_t aroom = ((PlayerRoomFn)(g_base + RVA_PLAYER_AROOM))()) seed = rd<uintptr_t>(aroom + 0x18);
    if (seed && (!rd<uintptr_t>(seed + R_LEVEL) || rd<uintptr_t>(rd<uintptr_t>(seed + R_LEVEL) + L_DRLG) != drlg)) seed = 0;
    if (seed) { depth.emplace(seed, 0); order.push_back(seed); }
    else for (uintptr_t r = rd<uintptr_t>(drlg + D_LIST0 + R_SNEXT); r && r != drlg + D_LIST0 && order.size() < 64; r = rd<uintptr_t>(r + R_SNEXT))
        if (depth.emplace(r, 0).second) order.push_back(r);
    if (order.empty()) return;
    for (size_t i = 0; i < order.size() && order.size() < 8192; ++i) {
        const uintptr_t r = order[i];
        const int d = depth[r];
        if (d >= keepRing) continue;
        const uintptr_t data = rd<uintptr_t>(r + R_NEAR);
        const uint64_t n = rd<uint64_t>(r + R_NEARN);
        if (!data || n == 0 || n > 64) continue;
        for (uint64_t k = 0; k < n; ++k)
            if (const uintptr_t nb = rd<uintptr_t>(data + k * 8); nb && depth.emplace(nb, d + 1).second) order.push_back(nb);
    }

    int built = 0, released = 0, budget = kBuildRate;
    const uint8_t savedBuilt = rd<uint8_t>(drlg + D_BUILT);
    for (const uintptr_t r : order) {
        const int d = depth[r];
        if (d < kNativeRings || d > maxBuild || rd<uintptr_t>(r + R_ACTIVE) || (rd<uint32_t>(r + R_FLAGS) & HAS_ROOM)) continue;
        if (budget-- <= 0) break;
        ((RoomInitFn)(g_base + RVA_ROOM_INIT))(ctx, r);
        if (rd<uintptr_t>(r + R_ACTIVE)) { g_owned.insert(r); ++built; }
    }
    *(uint8_t*)(drlg + D_BUILT) = savedBuilt;

    if (!g_owned.empty() && (g_passes & 7) == 0) {
        static std::unordered_set<uintptr_t> all;         // guard against freed rooms
        all.clear();
        for (uintptr_t lv = rd<uintptr_t>(drlg + D_LEVELS); lv && all.size() < 65536; lv = rd<uintptr_t>(lv + L_NEXT))
            for (uintptr_t r = rd<uintptr_t>(lv + L_FIRST); r && all.size() < 65536; r = rd<uintptr_t>(r + R_LNEXT)) all.insert(r);
        int relBudget = 8;
        for (auto it = g_owned.begin(); it != g_owned.end();) {
            const uintptr_t r = *it;
            if (!all.count(r) || !rd<uintptr_t>(r + R_ACTIVE)) { it = g_owned.erase(it); continue; }
            const auto dIt = depth.find(r);
            if ((dIt != depth.end() && dIt->second <= keepRing) || rd<uint8_t>(r + R_STATUS) < 4 || relBudget <= 0) { ++it; continue; }
            ((RoomReleaseFn)(g_base + RVA_ROOM_RELEASE))(r, 0); --relBudget; ++released;
            it = g_owned.erase(it);
        }
    }
    if (g_passes++ < 3 || built || released) {
        char b[128];
        snprintf(b, sizeof b, "renderdist: %zu rooms in reach, built %d, released %d, own %zu", order.size(), built, released, g_owned.size());
        Log(b);
    }
}

int SehRoomBuilderPass(uint8_t ctx, uintptr_t drlg) {
    __try { RoomBuilderPass(ctx, drlg); return 1; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void HookDrlgUpdate(uint8_t ctx, uintptr_t drlg) {
    OrigDrlgUpdate(ctx, drlg);
    if ((!g_enabled.load() && g_owned.empty()) || !g_buildOk || g_buildFault.load()) return;
    if (!SehRoomBuilderPass(ctx, drlg)) { g_buildFault.store(true); Log("renderdist: fault in the room builder - room building disabled"); }
}

// Grow the fixed TLSF pools by adding extra pools under their own lock, the way
// tlsf_add_pool does it: size header, the game's block_insert, end sentinel.
int GrowPool(uint64_t objRva, uint64_t vanillaBytes, int targetMB) {
    const uintptr_t obj = g_base + objRva;
    const uintptr_t control = rd<uintptr_t>(obj + 0x20);
    if (rd<uintptr_t>(obj) != g_base + RVA_RPOOL_VTBL || !control || rd<uint64_t>(obj + 0x18) != vanillaBytes) return -1;
    if (!Matches(RVA_TLSF_INSERT, kSigTlsfInsert, sizeof kSigTlsfInsert) || !Matches(RVA_POOL_LOCK, kSigPoolLock, 3) ||
        !Matches(RVA_POOL_UNLOCK, kSigPoolLock, 3)) return -1;
    const auto lock = (void (*)(uintptr_t))(g_base + RVA_POOL_LOCK);
    const auto unlock = (void (*)(uintptr_t))(g_base + RVA_POOL_UNLOCK);
    const auto insert = (void (*)(uintptr_t, uintptr_t))(g_base + RVA_TLSF_INSERT);
    int added = 0;
    for (int add = targetMB - (int)(vanillaBytes >> 20); add > 0;) {
        const int mb = std::min(add, 60);
        const size_t bytes = (size_t)mb << 20;
        uint8_t* mem = (uint8_t*)VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!mem) break;
        const uint64_t poolBytes = (bytes - 16) & ~7ull;
        lock(obj + 0x28);
        *(uint64_t*)mem = poolBytes | 1;                       // free block, previous in use
        insert(control, (uintptr_t)mem - 8);
        *(uintptr_t*)(mem + poolBytes) = (uintptr_t)mem - 8;   // sentinel: prev = our block
        *(uint64_t*)(mem + poolBytes + 8) = 2;                 // size 0, previous free
        unlock(obj + 0x28);
        added += mb; add -= mb;
    }
    return added;
}

bool g_poolsGrown = false;
int g_rpoolMB = -1, g_epoolMB = -1;
SRWLOCK g_toggleLock = SRWLOCK_INIT;

// Returns the message to show.
const char* Toggle() {
    static char msg[300];
    AcquireSRWLockExclusive(&g_toggleLock);
    const bool on = !g_enabled.load();
    if (on && !g_poolsGrown) {
        g_rpoolMB = GrowPool(RVA_RPOOL_OBJ, 0xC00000, kRenderPoolMB);
        g_epoolMB = GrowPool(RVA_EPOOL_OBJ, 0x4600000, kEntityPoolMB);
        g_poolsGrown = true;
    }
    if (g_mvOk) ((void (*)(float))(g_base + RVA_MVRADIUS_SET))(on ? kMvRadius : 150.0f);
    g_enabled.store(on);
    if (on)
        snprintf(msg, sizeof msg, "renderdist ON: +%d room rings%s, all candidate rooms render, model radius %.0f%s, renderer pool %d MB, entity pool %d MB",
                 kBuildRings, g_buildOk ? "" : " (UNAVAILABLE)", g_mvOk ? kMvRadius : 150.0f, g_mvOk ? "" : " (setter not found)",
                 g_rpoolMB > 0 ? 12 + g_rpoolMB : 12, g_epoolMB > 0 ? 70 + g_epoolMB : 70);
    else
        snprintf(msg, sizeof msg, "renderdist OFF (built rooms are released as you move; memory pools stay grown)");
    ReleaseSRWLockExclusive(&g_toggleLock);
    return msg;
}

ConsoleCommandResult __cdecl CmdRenderDist(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(Toggle());
    return ConsoleCommandResult::Handled;
}

// F12 polling. Runs outside the game thread, so it only logs. Shift+F12 is ignored here, so
// it switches only 3dcam: a safeguard for areas the extended distance is untested in.
std::atomic<bool> g_quit{false};

DWORD WINAPI KeyThread(void*) {
    for (bool f12Was = false; !g_quit.load(); Sleep(10)) {
        DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        const bool f12 = pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F12) & 0x8000);
        if (f12 && !f12Was && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) Log(Toggle());
        f12Was = f12;
    }
    return 0;
}

}  // namespace

static const PluginInfo g_info = {
    PluginInfoSize, D2RL_PLUGIN_API_VERSION, "d2r-3d-renderdistance", "renderdistance", "1.0.0", "Tandanu",
    "Extended render distance: builds and renders far rooms and their models. Press F12 to enable.",
    PluginFlags::Shared | PluginFlags::NativeHooks, {0, 0, 0, 0},
};

D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &g_info; }

D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const PluginContext* ctx) noexcept {
    g_ctx = ctx;
    if (!ctx) return true;
    g_base = ctx->exeBase ? ctx->exeBase : (uintptr_t)GetModuleHandleW(nullptr);
    const bool hAct = ctx->InstallInlineHook(RVA_ACTTEST, kSigActTest, sizeof kSigActTest, (void*)&HookActTest, (void**)&OrigActTest);
    const bool hUpd = ctx->InstallInlineHook(RVA_DRLG_UPDATE, kSigDrlgUpdate, sizeof kSigDrlgUpdate, (void*)&HookDrlgUpdate, (void**)&OrigDrlgUpdate);
    g_buildOk = hUpd && Matches(RVA_ROOM_INIT, kSigRoomInit, sizeof kSigRoomInit) && Matches(RVA_ROOM_RELEASE, kSigRoomRelease, sizeof kSigRoomRelease);
    g_playerRoomOk = Matches(RVA_PLAYER_AROOM, kSigPlayerARoom, sizeof kSigPlayerARoom);
    g_mvOk = Matches(RVA_MVRADIUS_SET, kSigMvRadiusSet, sizeof kSigMvRadiusSet);
    char b[200];
    snprintf(b, sizeof b, "renderdistance loaded: actTest=%s drlgUpdate=%s roomBuilder=%s playerRoom=%s mvRadiusSetter=%s",
             hAct ? "ok" : "FAIL", hUpd ? "ok" : "FAIL", g_buildOk ? "ok" : "FAIL", g_playerRoomOk ? "ok" : "FAIL", g_mvOk ? "ok" : "FAIL");
    ctx->LogInfo(b);
    if (HANDLE h = CreateThread(nullptr, 0, KeyThread, nullptr, 0, nullptr)) CloseHandle(h);
    if (!ctx->RegisterConsoleCommand("renderdist", &CmdRenderDist, "renderdist - switch the extended render distance on/off (also F12)")) ctx->LogError("renderdist: could not register the console command");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    g_quit.store(true);
    g_enabled.store(false);
    g_ctx = nullptr;
}
