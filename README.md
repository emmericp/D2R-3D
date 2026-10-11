# Diablo II: Resurrected in 3D

Adds camera control to Diablo II: Resurrected.
Press F12 to activate, use the mouse wheel to zoom, and hold the middle mouse button and drag to rotate the camera.

![](img/diablo.jpg)
![](img/bloodraven.jpg)
![](img/pindleskin.jpg)

## Standalone version

**This will trigger Diablo II's integrity check and the game will crash after a minute.**

### Binaries

Download from [GitHub releases](https://github.com/emmericp/D2R-3D/releases) or from [3d.diablo.deadlybossmods.com](https://3d.diablo.deadlybossmods.com/releases/d2r-3d-latest.zip)

Drop both the .exe and .dll file directly into your D2R folder (next to D2R.exe).
Press F12 to activate in the game.

### Build from source

Install [Bazel](https://bazel.build/install/windows).

```
bazel build //standalone:dist
```
Build output will be in `bazel-bin/standalone/dist/d2r-3d.{exe,dll}`.

## Plugin version

`bazel build //plugin:dist` builds two plugins: one for the camera, one for the render distance.
The local 1.4.2 camera starts in 3D by default. Render distance 1.0.1 enables its extension when that default world view first becomes available. F12 toggles both; Shift+F12 only toggles the camera. The ordinary Battle.net launcher retains the normal game view.

### Local character-follow camera

The camera plugin adds smooth third-person follow using the local player's native facing angle. The 3D view, character follow, and camera-relative movement are enabled by default. The closer view uses distance 28 and pitch offset -12 degrees from the native inclination, looking at a pivot 6 world units above the ground. **F11** switches between follow and free orbit and saves the choice. The mouse wheel zooms; middle-mouse dragging and the controller's right stick temporarily override follow. Right-stick up looks up; right-stick right orbits right. Both work while standing still or moving. In camera 1.4.1, releasing manual look holds its angle until the character actually moves, then gently returns yaw behind the character and tilt to the configured default. Stopping pauses that return. A short release delay prevents immediate tugging while walking and looking. Turning while stationary is supported.

**F10** switches camera-relative keyboard and controller walking on or off and saves the choice. It uses the game's existing movement bindings, including custom keys. With it enabled, forward moves into the camera view and left/right remain left/right relative to the rendered view on every input sample, even while a key or stick direction is held and follow rotates the camera. Forward must be selected to go straight after a sideways turn. The camera continues following the character during movement. Native input gates still handle chat and focus.

Camera 1.3.4 applies the same rotation to the controller's walking vector while preserving analog magnitude and the native dead zone. Only the verified walking call receives the rotated return value; menus and other controller-targeting callers keep their normal input. The movement setting and F10 control both input types. Controller hooks check both the native getter and its walking caller; code that is unavailable at startup is checked again once a second until the verified hook can be installed. Local user testing confirmed controller-relative walking.

Camera 1.4.0 reads the separate right-stick getter from that verified local walking call without writing shared controller state or changing right-stick values returned to other native consumers. Its prologue and handler+0x28 source are checked against build 93847. A small radial dead zone suppresses resting-stick drift and partial tilt controls orbit speed. Manual tilt is bounded to keep the camera above the local player's ground plane at the current zoom; this does not add wall or terrain collision. Focus, gameplay, menu, and fresh-sample gates suspend stick orbit where it should not apply. This also keeps left-stick analog walking independent of look input; walking uses the current rendered camera direction. Automated tests cover manual-look priority, frame-rate independence, angle seams, drift, tilt limits, release delay, return timing, and focus/menu gates. The 1.4.0 right-stick feel and new default framing still need human gameplay testing; no virtual controller or input injection is used for validation.

Settings are stored in `d2rloader/config/d2r-3d-3dcam.toml`. `enabled`, `follow_enabled`, and `movement_camera_relative` save the F12/F11/F10 choices. `default_distance` and `default_pitch` choose the initial framing. `follow_speed` (0.5–20.0, default 3.0) preserves the previously reduced keyboard turn rate. `controller_turn_scale` (default 0.5) halves that rate again for controller steering, without changing walking speed. `stick_yaw_speed` and `stick_pitch_speed` (defaults 90 and 60 degrees/second) control manual right-stick look; `recenter_speed` (default 2.0, reduced from 6.0) controls the gentler post-look return. Its yaw override lasts at most about one second of actual walking, so continually held sideways walking returns to its normal steering rate. Movement detection reads the native dynamic path; holding a key or stick against a wall does not trigger a return. Small displacement accumulates and a 0.12-second motion grace period bridges native path ticks. With follow off, manual orbit remains where you leave it. Restart after manually editing settings. The optional developer-console command `3dcam_follow` toggles follow.

Camera 1.4.2 keeps native rendering metadata consistent with the perspective matrices: the camera-position field receives the eye from the completed inverse view, and projection type/FOV/near plane match the rendered projection. It also changes the separate linear-depth encoding range from its native 550..1060 bounds to 0..1060, retaining the native far bound while including nearby third-person geometry. The offsets and setter were verified in build 93847; these encoding bounds are separate from projection clipping planes. The original eye is restored before each native view rebuild, without overwriting a newer native camera position, and original projection metadata and depth range are restored before native projection reconstruction or disabling 3D. Automated tests cover render/native transitions, repeated publication, preservation of native motion, valid nearby depth, and invalid data. All six test targets pass. The update is installed locally, with the corrected range confirmed in a live Act 4 camera snapshot. The grey band was absent in two held camera views and the restored view near the Pandemonium Fortress stairs; the original screenshot's exact character position has not been revisited since restarting.

For repeatable visual inspection, `3dcam_status` reports the live camera settings and depth range. The optional camera-only file interface is off by default (`inspection_enabled = false`); `3dcam_inspection` toggles it. With it enabled, PowerShell 7 can run `tools/control-camera.ps1 -Action Status`, `-Action Hold -Pitch -12 -Distance 28 -Yaw 90`, or `-Action Restore`. A hold changes only the session's camera, retaining the first pre-inspection view for restoration. Existing tilt limits still apply, and the status reports the resulting angle. Mouse orbit, wheel zoom, or right-stick look takes control immediately; leaving the game clears the hold. Requests select a particular loader process and startup session, reject stale or out-of-range input, and acknowledge a held/restored view only after a camera frame has been rebuilt. They cannot move a character, interact with objects, run console commands, or edit saves. Disable inspection after testing.

Camera 1.3.2 builds mouse picking rays from a completed render snapshot, including its viewport dimensions, projection offsets, eye, and orientation. This avoids reading the temporary isometric matrices written during the next native camera rebuild. The game's original targeting and interaction code still handles the resulting ray. CPU tests check projection/picking round trips at several resolutions, pitches, yaw angles, and screen offsets. Local user testing confirmed the reduced turning rate and a substantial improvement in targeting.

Camera 1.3.3 adds `npc_body_targeting = true`. Interactable NPCs receive an approximate world-space body volume; an upper-body hover redirects the native query to the NPC's existing lower-body target. Working lower-body rays, waypoints, enemies, hirelings, and non-interactive town guards retain their ordinary targeting. Native collision and interaction checks still process the query. Menus, frontend screens, and shadow fitting do not receive this assistance. NPC metadata is read only and gated by the verified build 93847 client layout. The optional console command `3dcam_npc_targeting` toggles and saves the setting for comparison.

The body approximation is a cylinder 8 world units high, using the local player's ground elevation. It is not the NPC's exact animated mesh; chest/shoulder targeting and other acts still need live testing. Tests cover torso hits, preservation of lower-body rays, misses above/beside the body, and NPCs behind the camera.

This extension has been tested with D2RLoader 1.3.1-beta, PluginSDK 0.3.0 / ABI 4, and game build 93847. Native facing getters are checked against expected code signatures before use; unsupported getters disable follow without replacing the original camera controls. The character movement controls remain those supplied by the game.

For a CMake build, put a PluginSDK checkout beside this repository:

```text
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The DLLs are written to `build/Release`. Put them in the game's `d2rloader/plugins` folder while the game is closed. Tests cover camera alignment, direction wraparound, smoothing across frame rates, and pause handling.

### Outdoor skies

The CMake build also produces `d2rl-sky.dll`. This companion uses a ReShade 6.8.0 add-on backend and the camera's copied view/projection data to render a panoramic sky into empty reverse-Z depth pixels. It follows camera rotation, keeps foreground geometry intact, and suspends the pass for stock menus and inventories. Known outdoor biomes receive skies; recognized interiors receive the enclosures described below. Unknown biomes, loading screens, and the character frontend keep their original background.

Act 1 uses gray overcast clouds. Act 2 uses a sunny sky, blending to stars as native environmental light falls, including the Claw Viper eclipse. Act 3 uses misty daylight, Act 4 a nearly black sky with a restrained red horizon and faint stars, and Act 5 cool blue overcast clouds. The other daylight skies dim with native nighttime lighting. [Asset sources and licenses](sky/ASSETS.md) are recorded with original checksums.

With the game closed, install `d2rl-sky.dll` in `d2rloader/plugins`, copy this repository's `sky` folder to `d2rloader/sky`, and place the official full-add-on ReShade64.dll from [ReShade](https://reshade.me/) inside that sky folder. Preserve the accompanying ReShade license. The sky plugin loads this backend explicitly; no `dxgi.dll` proxy belongs in the game root, so the normal Battle.net launcher does not load the skies.

**F9** toggles skies and interiors and saves the setting in `d2rloader/config/d2r-3d-sky.toml`. `brightness` scales the outdoor appearance (default 0.7). Config edits take effect within about one second. The 3D camera still uses F12, F11, F10, the wheel, and middle drag.

For development previews, `preview_act = 1..5` selects a palette while staying in the current outdoor level, and `preview_night = 0..1` controls the night blend. These only change appearance. Reset to `preview_act = 0` and `preview_night = -1` for automatic selection. The optional console command `3dcam_view <pitch> <distance>` changes the view for the current session without changing camera defaults. F9 can suspend the pass while working in the loader's developer console.

The outdoor sky does not extend the game's terrain. Below-horizon gaps in unloaded outdoor terrain stay dark. Native biome hooks are signature checked on build 93847; a mismatch leaves the automatic backgrounds off. CPU tests cover biome classification, menu/camera gating, and daylight endpoints.

Local validation on build 93847: both CMake tests passed; the final shader compiled in-game; Act 1 rendered in Rogue Encampment with foreground geometry and HUD preserved; inventory and pause menus suspended the pass; F9 toggled and persisted correctly. All five act palettes and Act 2's 8K night were previewed in the same outdoor scene without changing quests or act state. The native environmental-light reader returned changing daylight values, and automatic Act 1 selection was restored afterward. Actual travel through Acts 2–5 and the Claw Viper quest trigger still need gameplay validation.

### Interior backgrounds

Interior backgrounds use an original procedural ceiling and distant enclosure rather than a panoramic room photograph. The copied camera eye and player anchor provide perspective; world-space rock or brick sampling gives movement parallax. The effect only fills empty black pixels, so the game's existing walls, characters, torch glows, and other depth surfaces remain visible. Subdued warm fill and distance haze keep enclosed levels dark. No additional material textures are needed.

Recognized biome names select rough brown caves, gray masonry vaults, sandy tombs/palaces, greenish sewers, damp jungle interiors, blue ice caves, or dark red infernal caves. Arcane Sanctuary uses a faint starry void without a roof. These are family-wide visual approximations: the ceiling does not follow the exact room layout, cast native shadows, or add collision. A wide camera orbit can still pass through native walls; zooming closer helps in narrow rooms.

Set `interiors_enabled = false` to disable indoor backgrounds while retaining outdoor skies. `interior_brightness` scales indoor materials (default 1.3, accepted range 0.05–2.0). Development-only `preview_interior` selects 1=cave, 2=masonry, 3=sandstone, 4=sewer, 5=jungle, 6=ice, 7=infernal, or 8=arcane in the current level. Restore `preview_interior = 0` for automatic selection.

Install camera 1.3.1 or later and sky 1.1.0 together: they exchange version 2 camera snapshots with eye and anchor positions. The previous camera/sky pair used version 1; mixing snapshot versions disables the backgrounds. Interior validation used a separate copied save in Normal difficulty: Catacombs selected masonry automatically, rendered a vaulted ceiling, and correctly suspended/resumed for inventory. Cave, ice, and arcane materials were also previewed; other interior families still need visits in their actual levels. The shader compiled and the Release CPU tests passed. Original character save hashes were unchanged.
