#include "sky_model.h"
#include "camera_frame.h"
#include <iostream>
#include <stdexcept>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<D2R3DCameraFrame>);
void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        for (const auto entry:{std::pair{"data/hd/env/ACT1_OUTDOORS.json",1},
                              {"act2_town",2},{"act3_jungle",3},{"act4_lava",4},{"expansion_ruins_snow",5}}) {
            const auto scene=SkyModel::FromBiome(entry.first);
            Check(scene.act==entry.second && scene.outdoors,"Outdoor act selection failed");
            Check(SkyModel::IsSupportedScene(scene,true,true,false,true),"Outdoor sky was disabled");
            Check(!SkyModel::IsSupportedScene(scene,false,true,false,true),"Sky enabled outside gameplay");
            Check(!SkyModel::IsSupportedScene(scene,true,false,false,true),"Sky enabled without 3D camera");
            Check(!SkyModel::IsSupportedScene(scene,true,true,true,true),"Sky overlays an open menu");
        }
        Check(!SkyModel::FromBiome("act1_caves").outdoors,"Cave received an outdoor sky");
        for (const auto entry:{std::pair{"act1_caves",SkyModel::Interior::Cave},
                              {"act1_crypt",SkyModel::Interior::Masonry},{"act1_cathedral",SkyModel::Interior::Masonry},
                              {"act2_tomb",SkyModel::Interior::Sandstone},{"act2_sewer",SkyModel::Interior::Sewer},
                              {"act3_dungeon",SkyModel::Interior::Jungle},{"expansion_icecaves",SkyModel::Interior::Ice},
                              {"act2_arcane",SkyModel::Interior::Arcane}}) {
            const auto scene=SkyModel::FromBiome(entry.first);
            Check(scene.interior==entry.second,"Wrong interior material family");
            Check(SkyModel::IsSupportedInterior(scene,true,true,false,true),"Known interior disabled");
            Check(!SkyModel::IsSupportedScene(scene,true,true,false,true),"Interior received outdoor sky");
            Check(!SkyModel::IsSupportedInterior(scene,true,true,true,true),"Interior overlays menu");
            Check(!SkyModel::IsSupportedInterior(scene,false,true,false,true),"Interior outside game");
            Check(!SkyModel::IsSupportedInterior(scene,true,false,false,true),"Interior without camera");
            Check(!SkyModel::IsSupportedInterior(scene,true,true,false,false),"Interior ignores toggle");
        }
        Check(SkyModel::FromBiome("expansion_wildtemple_tempenter").interior==SkyModel::Interior::None,"Outdoor temple enclosed");
        Check(SkyModel::FromBiome("act2_unknown").interior==SkyModel::Interior::None,"Unknown level enclosed");
        Check(D2R3DCameraFrame{}.version==2,"Interior camera position ABI mismatch");
        Check(!SkyModel::FromBiome("act2_frontend_biome").act,"Frontend received a sky");
        Check(!SkyModel::FromBiome("unknown").outdoors,"Unknown biome received a sky");
        Check(SkyModel::Daylight(0)==0 && SkyModel::Daylight(32)==0,"Quest/night darkness did not map to night");
        Check(SkyModel::Daylight(128)==1 && SkyModel::Daylight(255)==1,"Day did not map to day");
        Check(SkyModel::Daylight(-1)==1,"Missing environment did not use a safe day fallback");
        std::cout<<"Sky biome selection, indoor/menu gating, daylight extremes, and camera ABI passed.\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
