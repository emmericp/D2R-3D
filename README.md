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
F12 enables both, Shift+F12 only enables the camera.

### Local character-follow camera

The camera plugin adds smooth third-person follow using the local player's native facing angle. Follow is enabled by default when the 3D camera is enabled. **F11** switches between follow and free orbit and saves the choice. The mouse wheel zooms; middle-mouse dragging temporarily overrides follow, which resumes 0.75 seconds after release. Turning while stationary is supported.

Settings are stored in `d2rloader/config/d2r-3d-3dcam.toml`. Set `follow_enabled` to `true` or `false` and adjust `follow_speed` (0.5–20.0, default 6.0) to change how quickly the camera catches up. Restart after manually editing settings. The optional developer-console command `3dcam_follow` toggles the same setting.

This extension has been tested with D2RLoader 1.3.1-beta, PluginSDK 0.3.0 / ABI 4, and game build 93847. Native facing getters are checked against expected code signatures before use; unsupported getters disable follow without replacing the original camera controls. The character movement controls remain those supplied by the game.

For a CMake build, put a PluginSDK checkout beside this repository:

```text
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The DLLs are written to `build/Release`. Put them in the game's `d2rloader/plugins` folder while the game is closed. Tests cover camera alignment, direction wraparound, smoothing across frame rates, and pause handling.
