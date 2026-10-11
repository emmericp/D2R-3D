#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <array>
#include <filesystem>
#include <sstream>
#include <set>
#include <cstring>
#include <cstdio>
#include <D2RLPlugin/api.h>
#include <reshade.hpp>
#include "camera_frame.h"
#include "sky_model.h"

using namespace D2RL;
extern "C" __declspec(dllexport) const char* NAME="D2R-3D skies";
extern "C" __declspec(dllexport) const char* DESCRIPTION="Skies and dungeon enclosures synchronized with the D2R-3D camera.";

namespace {
const PluginContext* g_ctx=nullptr;
HMODULE g_self=nullptr,g_graphics=nullptr;
uintptr_t g_base=0;
std::filesystem::path g_skyRoot;
HANDLE g_stop=nullptr,g_worker=nullptr;
std::atomic<bool> g_enabled{true},g_world{false},g_outdoors{false},g_menu{false};
std::atomic<int> g_act{0},g_previewAct{0},g_interior{0},g_previewInterior{0};
std::atomic<float> g_brightness{0.7f},g_previewNight{-1};
std::atomic<bool> g_interiorsEnabled{true};
std::atomic<float> g_interiorBrightness{1.3f},g_ceiling{32};
std::atomic<uintptr_t> g_environmentSlot{0};
std::set<std::string> g_panels; // SDK UI thread only.
bool g_registered=false;
D2R3DGetCameraFrameFn g_camera=nullptr;

constexpr uint64_t kBiomeRva=0xE68C20;
constexpr uint8_t kBiomeSig[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x56,0x57,0x41,0x57,0x48,0x81,0xEC,0xC0,0x00};
using BiomeFn=uintptr_t (*)(const char*);
BiomeFn OriginalBiome=nullptr;

void Log(const char* message) { if (g_ctx) g_ctx->LogInfo(message); }
bool CopySafe(void* out,const void* source,size_t bytes) {
    __try { memcpy(out,source,bytes); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t OnBiome(const char* source) {
    char name[160]{};
    if (source) for (size_t i=0;i+1<sizeof name;++i) if (!CopySafe(name+i,source+i,1) || !name[i]) break;
    const auto scene=SkyModel::FromBiome(name);
    g_act.store(scene.act); g_outdoors.store(scene.outdoors);
    g_interior.store((int)scene.interior); g_ceiling.store(scene.ceiling);
    char message[224]; snprintf(message,sizeof message,"sky biome: %.150s (act=%d outdoors=%d)",name,scene.act,scene.outdoors);
    Log(message);
    return OriginalBiome(source);
}

// Find the read-only environment pointer from the native debug formatter's
// unique instruction pattern. The environment's light includes the game's
// daylight transitions; it is never modified by this plugin.
void FindEnvironment() {
    if (g_environmentSlot.load()) return;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!CopySafe(&dos,(void*)g_base,sizeof dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
        !CopySafe(&nt,(void*)(g_base+dos.e_lfanew),sizeof nt) || nt.Signature!=IMAGE_NT_SIGNATURE) return;
    constexpr uint8_t pattern[]={0x48,0x8B,0x0D,0,0,0,0,0xE8,0,0,0,0,0x48,0x63,0xC8,0x4C,0x8D,0x05,0,0,0,0,0xBA,0x32,0,0,0,0x48,0x8D,0x05};
    constexpr char mask[]="xxx????x????xxxxxx????xxxxxxxx";
    constexpr size_t chunk=65536;
    std::array<uint8_t,chunk+sizeof pattern> bytes{};
    const uintptr_t end=g_base+nt.OptionalHeader.SizeOfImage;
    for (uintptr_t start=g_base+4096;start<end;start+=chunk) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery((void*)start,&info,sizeof info) || info.State!=MEM_COMMIT || (info.Protect&PAGE_GUARD) || (info.Protect&0xFF)==PAGE_NOACCESS) continue;
        const size_t size=std::min<size_t>(bytes.size(),end-start);
        if (!CopySafe(bytes.data(),(void*)start,size)) continue;
        for (size_t i=0;i+sizeof pattern<=size;++i) {
            if (bytes[i]!=pattern[0] || bytes[i+1]!=pattern[1]) continue;
            size_t k=0; while (k<sizeof pattern && (mask[k]=='?' || bytes[i+k]==pattern[k])) ++k;
            if (k!=sizeof pattern) continue;
            int32_t relative=0; memcpy(&relative,bytes.data()+i+3,sizeof relative);
            g_environmentSlot.store(start+i+7+relative);
            Log("sky: native day/night environment found"); return;
        }
    }
}

int NativeLight() {
    uintptr_t holder=0,environment=0; int32_t fields[4]{};
    const auto slot=g_environmentSlot.load();
    if (!slot || !CopySafe(&holder,(void*)slot,sizeof holder) || holder<65536 ||
        !CopySafe(&environment,(void*)(holder+8),sizeof environment) || environment<65536 ||
        !CopySafe(fields,(void*)environment,sizeof fields) || fields[0]<0 || fields[0]>5 || fields[3]<0 || fields[3]>255) return -1;
    return fields[3];
}

constexpr const char* DefaultConfig="[sky]\nenabled = true\nbrightness = 0.7\ninteriors_enabled = true\ninterior_brightness = 1.3\n# Appearance previews only; zero selects the real area.\npreview_act = 0\npreview_night = -1\npreview_interior = 0\n";
void ReadConfig() {
    char text[2048]{};
    if (!g_ctx || !g_ctx->ReadConfig(text,sizeof text)) return;
    std::istringstream lines(text); std::string line;
    while (std::getline(lines,line)) {
        line=line.substr(0,line.find('#'));
        std::istringstream fields(line); std::string key,equals,value;
        if (!(fields>>key>>equals>>value) || equals!="=") continue;
        if (key=="enabled" && (value=="true" || value=="false")) g_enabled.store(value=="true");
        else if (key=="interiors_enabled" && (value=="true" || value=="false")) g_interiorsEnabled.store(value=="true");
        else {
            char* end=nullptr; const float number=strtof(value.c_str(),&end);
            if (end==value.c_str() || *end || !std::isfinite(number)) continue;
            if (key=="brightness") g_brightness.store(std::clamp(number,0.05f,2.0f));
            if (key=="preview_act") g_previewAct.store((int)std::clamp(number,0.0f,5.0f));
            if (key=="preview_night") g_previewNight.store(number<0?-1:std::clamp(number,0.0f,1.0f));
            if (key=="interior_brightness") g_interiorBrightness.store(std::clamp(number,0.05f,2.0f));
            if (key=="preview_interior") g_previewInterior.store((int)std::clamp(number,0.0f,8.0f));
        }
    }
}
DWORD WINAPI Worker(void*) {
    unsigned tick=0;
    while (WaitForSingleObject(g_stop,1000)==WAIT_TIMEOUT) {
        ReadConfig();
        if (g_world.load() && ++tick%5==0) FindEnvironment();
    }
    return 0;
}

void __cdecl OnGameplay(const PluginContext*,const Lifecycle::GameplayEvent* event,void*) noexcept {
    if (!event) return;
    if (event->kind==Lifecycle::GameplayEventKind::GameLeft) { g_world.store(false); g_menu.store(false); g_panels.clear(); }
    else if (event->kind==Lifecycle::GameplayEventKind::GameJoined || event->kind==Lifecycle::GameplayEventKind::LocalPlayerReady) g_world.store(true);
}
SharedEvents::UiMessageAction __cdecl OnUi(const PluginContext*,const SharedEvents::UiMessageEvent* event,void*) noexcept {
    if (!event || !event->target || strcmp(event->target,"PanelManager") || !event->command) return SharedEvents::UiMessageAction::Continue;
    const std::string panel=event->text?event->text:"";
    if (!strcmp(event->command,"OpenPanel") || !strcmp(event->command,"OpenExclusivePanel")) g_panels.insert(panel);
    else if (!strcmp(event->command,"ClosePanel") || !strcmp(event->command,"UnloadPanel")) g_panels.erase(panel);
    else if (!strcmp(event->command,"UnloadAll") || !strcmp(event->command,"CloseAll")) g_panels.clear();
    bool blocked=false;
    for (const auto& name:g_panels) for (const char* part:{"Inventory","Bank","Stash","Vendor","Horadric","Pause","Options","QuestLog","CharacterStats","SkillsTree","Waypoint","Trade","Gamble","Dialogue"})
        if (name.find(part)!=std::string::npos) blocked=true;
    g_menu.store(blocked);
    return SharedEvents::UiMessageAction::Continue;
}

void SetFloat(reshade::api::effect_runtime* runtime,const char* name,const float* value,size_t count) {
    const auto variable=runtime->find_uniform_variable("D2RSky.fx",name);
    if (variable.handle) runtime->set_uniform_value_float(variable,value,count);
}
void OnEffects(reshade::api::effect_runtime* runtime,reshade::api::command_list*,reshade::api::resource_view,reshade::api::resource_view) {
    if (runtime->get_device()->get_api()!=reshade::api::device_api::d3d12) return;
    const auto technique=runtime->find_technique("D2RSky.fx","D2R3DSky");
    if (!technique.handle) return;
    if (!g_camera) {
        const HMODULE camera=GetModuleHandleW(L"d2rl-3dcam.dll");
        if (camera) g_camera=(D2R3DGetCameraFrameFn)GetProcAddress(camera,"D2R3DGetCameraFrame");
    }
    D2R3DCameraFrame frame{};
    const bool cameraReady=g_camera && g_camera(&frame,sizeof frame) && frame.version==2 && frame.active;
    const int preview=g_previewAct.load();
    const int interiorPreview=g_previewInterior.load();
    SkyModel::Scene scene{preview?preview:g_act.load(),g_outdoors.load(),(SkyModel::Interior)g_interior.load(),g_ceiling.load()};
    if (interiorPreview) {
        scene.outdoors=false; scene.interior=(SkyModel::Interior)interiorPreview;
        scene.ceiling=interiorPreview==1?28.0f:interiorPreview==6?40.0f:36.0f;
    }
    const bool indoors=SkyModel::IsSupportedInterior(scene,g_world.load(),cameraReady && frame.reserved,g_menu.load(),g_enabled.load() && g_interiorsEnabled.load());
    bool on=indoors || SkyModel::IsSupportedScene(scene,g_world.load(),cameraReady,g_menu.load(),g_enabled.load());
    const auto depth=runtime->find_texture_variable("D2RSky.fx","SceneDepth");
    reshade::api::resource_view depthView{};
    if (depth.handle) runtime->get_texture_binding(depth,&depthView,nullptr);
    on=on && depthView.handle!=0;
    runtime->set_technique_state(technique,on);
    const auto enable=runtime->find_uniform_variable("D2RSky.fx","SkyEnabled");
    if (enable.handle) runtime->set_uniform_value_bool(enable,&on,1);
    // Report why the pass is gated, including changes while it stays off.
    const int state=(on?1:0)|(scene.act<<1)|(g_world.load()?16:0)|
        (cameraReady?32:0)|(scene.outdoors?64:0)|(g_menu.load()?128:0)|(depthView.handle?256:0)|((int)scene.interior<<10);
    static int previous=-1;
    if (state!=previous) { previous=state; char message[180]; snprintf(message,sizeof message,"background pass %s (act=%d world=%d camera=%d outdoor=%d interior=%d menu=%d depth=%d)",on?"ON":"OFF",scene.act,g_world.load(),cameraReady,scene.outdoors,(int)scene.interior,g_menu.load(),depthView.handle!=0); Log(message); }
    if (!on) return;
    SetFloat(runtime,"CameraRight",frame.right,3); SetFloat(runtime,"CameraUp",frame.up,3);
    SetFloat(runtime,"CameraBack",frame.back,3); SetFloat(runtime,"CameraProjection",frame.projection,4);
    SetFloat(runtime,"CameraEye",frame.eye,3); SetFloat(runtime,"CameraAnchor",frame.anchor,3);
    const float interior=indoors?(float)scene.interior:0;
    SetFloat(runtime,"InteriorProfile",&interior,1); SetFloat(runtime,"InteriorHeight",&scene.ceiling,1);
    const float interiorBrightness=g_interiorBrightness.load(); SetFloat(runtime,"InteriorBrightness",&interiorBrightness,1);
    const float brightness=g_brightness.load(); SetFloat(runtime,"SkyBrightness",&brightness,1);
    const float act=(float)scene.act; SetFloat(runtime,"SkyAct",&act,1);
    const float previewNight=g_previewNight.load();
    const int light=NativeLight();
    const float daylight=previewNight>=0?1-previewNight:SkyModel::Daylight(light);
    const float night=1-daylight;
    SetFloat(runtime,"SkyNightMix",&night,1);
    static int lastAct=0,lastLight=-2;
    if (scene.act!=lastAct || (light!=lastLight && (light==32 || light==128))) {
        lastAct=scene.act; lastLight=light;
        char message[100]; snprintf(message,sizeof message,"sky appearance: act=%d native light=%d night=%.2f",scene.act,light,night); Log(message);
    }
}

Input::ActionResult __cdecl Toggle(const PluginContext*,const Input::ActionEvent* event,void*) noexcept {
    if (!event || event->kind!=Input::ActionEventKind::Pressed) return Input::ActionResult::Ignored;
    const bool enabled=!g_enabled.load(); g_enabled.store(enabled);
    char config[512]; snprintf(config,sizeof config,"[sky]\nenabled = %s\nbrightness = %.3f\ninteriors_enabled = %s\ninterior_brightness = %.3f\npreview_act = %d\npreview_night = %.3f\npreview_interior = %d\n",enabled?"true":"false",g_brightness.load(),g_interiorsEnabled.load()?"true":"false",g_interiorBrightness.load(),g_previewAct.load(),g_previewNight.load(),g_previewInterior.load());
    if (g_ctx && !g_ctx->WriteConfig(config)) g_ctx->LogError("sky: settings could not be saved");
    Log(enabled?"sky enabled (F9 toggles)":"sky disabled (F9 toggles)");
    return Input::ActionResult::Handled;
}
}

static const PluginInfo Info={PluginInfoSize,D2RL_PLUGIN_ABI_VERSION,"d2r-3d-sky","D2R-3D skies","1.1.0","Local D2R-3D extension",
    "Depth-aware skies and dungeon enclosures. F9 toggles. Requires the D2R-3D camera.",PluginFlags::Client|PluginFlags::NativeHooks,{0,0,0,0}};
D2RL_PLUGIN_EXPORT const PluginInfo* D2RLoaderGetPluginInfo() noexcept { return &Info; }
D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const PluginContext* context) noexcept {
    if (!context) return false;
    g_ctx=context; g_base=context->exeBase;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&D2RLoaderGetPluginInfo,&g_self);
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,std::size(executable));
    g_skyRoot=std::filesystem::path(executable).parent_path()/L"d2rloader"/L"sky";
    if (!context->EnsureConfig(DefaultConfig)) context->LogError("sky: default settings could not be created");
    ReadConfig();
    const LifecycleService* lifecycle=nullptr;
    if (context->QueryService(&lifecycle)==ServiceQueryResult::Success) for (auto kind:{Lifecycle::GameplayEventKind::GameJoined,Lifecycle::GameplayEventKind::GameLeft,Lifecycle::GameplayEventKind::LocalPlayerReady}) {
        Lifecycle::GameplayEventListener listener{}; listener.structSize=sizeof listener; listener.kind=kind; listener.callback=OnGameplay;
        Lifecycle::ListenerHandle handle=0; lifecycle->registerGameplayEventListener(context,&listener,&handle);
    }
    const SharedEventService* events=nullptr;
    if (context->QueryService(&events)==ServiceQueryResult::Success) {
        SharedEvents::UiMessageListener listener{}; listener.structSize=sizeof listener; listener.callback=OnUi;
        SharedEvents::ListenerHandle handle=0; events->registerUiMessageListener(context,&listener,&handle);
    }
    const InputService* input=nullptr;
    if (context->QueryService(&input)==ServiceQueryResult::Success) {
        Input::ActionRegistration action{}; action.structSize=sizeof action; action.logicalId="sky-toggle";
        action.displayName="Toggle skies and interiors"; action.category="D2R-3D"; action.defaultPrimary.key=Input::Key::F9; action.callback=Toggle;
        Input::ActionHandle handle=0; input->registerAction(context,&action,&handle);
    }
    if (!context->InstallInlineHook(kBiomeRva,kBiomeSig,sizeof kBiomeSig,(void*)&OnBiome,(void**)&OriginalBiome)) context->LogError("sky: native biome hook unavailable; skies remain off");
    wchar_t previousBase[32768]{};
    const DWORD previousSize=GetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",previousBase,std::size(previousBase));
    SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",g_skyRoot.c_str());
    g_graphics=LoadLibraryW((g_skyRoot/L"ReShade64.dll").c_str());
    SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",previousSize?previousBase:nullptr);
    if (!g_graphics || !reshade::register_addon(g_self,g_graphics)) { context->LogError("sky: graphics backend could not load; camera remains available"); return true; }
    g_registered=true;
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(&OnEffects);
    Log("sky: ReShade backend loaded only by the modded launcher; sky add-on registered");
    g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    g_worker=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    return true;
}
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (g_registered) { reshade::unregister_addon(g_self); g_registered=false; }
    if (g_stop) SetEvent(g_stop);
    if (g_worker) { WaitForSingleObject(g_worker,5000); CloseHandle(g_worker); }
    if (g_stop) CloseHandle(g_stop);
    g_ctx=nullptr;
}
