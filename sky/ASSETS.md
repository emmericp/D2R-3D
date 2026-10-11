# Sky assets

Act 1 uses [Overcast Soil (Pure Sky)](https://polyhaven.com/a/overcast_soil_puresky) by Sergej Majboroda, sky-only edits by Jarod Guest, downloaded from Poly Haven. The distributed texture is the original tonemapped panorama, sampled as an equirectangular sky. [CC0 license](https://polyhaven.com/license).

- File: `textures/act1-overcast.jpg`
- Publisher download: `https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/overcast_soil_puresky.jpg`
- Publisher/download MD5: `6e690e8cd4e7aef6f5c04cca62619331`

The optional graphics backend is ReShade 6.8.0, extracted from the official add-on installer. Its BSD license is in `third_party/reshade/LICENSE.md`. The backend is loaded explicitly by the sky plugin during a D2RLoader launch; no graphics-proxy DLL is placed in the game root.

The other panoramas are also original Poly Haven tonemapped JPGs, under the same CC0 license. Daytime panoramas render at 4096 × 2048; night retains 8192 × 4096 for small star points on wide displays. No photographed terrain is sampled. The five GPU textures use approximately 256 MiB total.

| Use | Local file | Source and artists | Original MD5 |
| --- | --- | --- | --- |
| Act 2 daylight | `textures/act2-sunny.jpg` | [Qwantani Noon (Pure Sky)](https://polyhaven.com/a/qwantani_noon_puresky), Greg Zaal / Jarod Guest | `3fe80fac290d4417837b5cb8bf5cb189` |
| Act 2 night; faint Act 4 stars | `textures/starry-night.jpg` | [Qwantani Night (Pure Sky)](https://polyhaven.com/a/qwantani_night_puresky), Greg Zaal / Jarod Guest | `70c24e45fe8db819641c76068675c4a2` |
| Act 3 | `textures/act3-hazy.jpg` | [Kloofendal 28d Misty (Pure Sky)](https://polyhaven.com/a/kloofendal_28d_misty_puresky), Jenelle van Heerden / Greg Zaal | `317d52731388ff6506c6fcc374d21fdf` |
| Act 5 | `textures/act5-cold-clouds.jpg` | [Snow Field (Pure Sky)](https://polyhaven.com/a/snow_field_puresky), Sergej Majboroda / Jarod Guest | `eb5e574743a8bcfc157957c180c440ee` |

All originals are from `https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/<asset-id>.jpg`, with asset IDs matching the linked source pages. Download digests were checked against the publisher's API metadata. Act 4 uses the Act 1 cloud shapes with a very dark red horizon and faint stars; this appearance is computed by our shader. Night grading suppresses the photo's distant artificial lights along the horizon.

Interior rock, brick, and ice materials are original procedural shader patterns and allocate no additional texture assets. Arcane Sanctuary reuses the credited Qwantani Night panorama as a faint starry void.
