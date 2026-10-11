#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <cctype>

namespace SkyModel {
enum class Interior { None=0, Cave=1, Masonry=2, Sandstone=3, Sewer=4, Jungle=5, Ice=6, Infernal=7, Arcane=8 };
struct Scene { int act=0; bool outdoors=false; Interior interior=Interior::None; float ceiling=32; };

inline Scene FromBiome(std::string_view path) {
    const auto slash=path.find_last_of("/\\");
    if (slash!=std::string_view::npos) path.remove_prefix(slash+1);
    path=path.substr(0,path.find('.'));
    std::string name(path);
    for (char& c:name) c=(char)std::tolower((unsigned char)c);
    if (name.find("frontend")!=std::string::npos) return {};
    Scene scene;
    for (int act=1;act<=4;++act) if (name.starts_with("act"+std::to_string(act))) scene.act=act;
    if (name.starts_with("expansion") || name.starts_with("act5")) scene.act=5;
    constexpr std::string_view outdoor[]={
        "act1_outdoors","act1_tristram","act1_court",
        "act2_outdoors","act2_town",
        "act3_jungle","act3_docktown","act3_kurast","act3_travincal_outdoors",
        "act4_mesa","act4_lava","act4_diab",
        "expansion_town","expansion_siege","expansion_mountaintop",
        "expansion_ruins","expansion_ruins_snow","expansion_wildtemple_tempenter"
    };
    for (const auto key:outdoor) if (name==key) scene.outdoors=true;
    // Deliberately recognize dungeon families, rather than putting a ceiling
    // over every unknown biome. Outdoor classification always takes priority.
    if (!scene.outdoors && scene.act) {
        const auto has=[&](std::string_view key){ return name.find(key)!=std::string::npos; };
        if (has("arcane")) scene.interior=Interior::Arcane;
        else if (has("ice") || has("frozen")) { scene.interior=Interior::Ice; scene.ceiling=40; }
        else if (has("lava") || has("hell")) { scene.interior=Interior::Infernal; scene.ceiling=48; }
        else if (has("cave") || has("lair")) { scene.interior=Interior::Cave; scene.ceiling=28; }
        else if (has("sewer")) scene.interior=Interior::Sewer;
        else if (has("tomb") || has("palace") || has("temple")) { scene.interior=scene.act==3?Interior::Jungle:Interior::Sandstone; scene.ceiling=40; }
        else if (has("crypt") || has("catacomb") || has("barracks") || has("cathedral") || has("jail") || has("dungeon") || has("keep") || has("baal")) {
            scene.interior=scene.act==3?Interior::Jungle:Interior::Masonry;
            scene.ceiling=has("cathedral") || has("keep") || has("baal")?52.0f:36.0f;
        }
    }
    return scene;
}

inline bool IsSupportedInterior(Scene scene, bool inWorld, bool cameraActive, bool menuOpen, bool enabled) {
    return enabled && inWorld && cameraActive && !menuOpen && !scene.outdoors && scene.interior!=Interior::None && scene.act>=1 && scene.act<=5;
}

inline float Daylight(int nativeLight) {
    if (nativeLight<0 || nativeLight>255) return 1;
    return std::clamp((nativeLight-32.0f)/96.0f,0.0f,1.0f);
}

inline bool IsSupportedScene(Scene scene, bool inWorld, bool cameraActive, bool menuOpen, bool enabled) {
    return enabled && inWorld && cameraActive && !menuOpen && scene.outdoors && scene.act>=1 && scene.act<=5;
}
}
