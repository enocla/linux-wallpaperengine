# Development

How to build, run, debug and iterate on this fork (NixOS-first, works anywhere Nix is available).

## Environment

The flake provides everything:

- `nix develop`: dev shell with the build dependencies, the runtime libraries CEF needs (`LD_LIBRARY_PATH`), and the helper tools (`clang-format`, `gdb`, `imagemagick`, `jq`, `mpv` + `mpv-mpris`).
- `nix build`: sandboxed release package (reuses nixpkgs' `linux-wallpaperengine` derivation with this checkout as source).

Wallpaper Engine itself must be installed through Steam so the `assets` folder and workshop content are found (see the README).

## Build loop

```bash
nix develop
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON ..   # first time only; CMake downloads CEF into build/cef
make -j"$(nproc)" linux-wallpaperengine tests
./output/tests                                          # Catch2, src/WallpaperEngine/Testing/Cases
```

Re-run `cmake ..` when `CMakeLists.txt` or `src/WallpaperEngine/Scripting/resources/builtins.js` changes (the builtins are embedded at configure time).

## Running and capturing

`tools/dev/run-windowed.sh <id|path> [seconds] [screenshot.png] [extra args]` runs a background in a 1280x720 window, saves a screenshot and prints the log grouped by frequency, which turns per-frame spam into one line with a count:

```bash
WPE_DELAY=300 tools/dev/run-windowed.sh 3000562427 20 /tmp/shot.png
tools/dev/run-windowed.sh 3000562427 20 /tmp/day.png --set-property daystarttime=1 --set-property nightstarttime=23
```

- `--screenshot-delay` is in frames (minimum 5), so set `WPE_DELAY` high enough for scripts and animations to settle.
- The screenshot is read from the scene framebuffer at full scene resolution. Crop/zoom areas with `magick shot.png -crop WxH+X+Y +repage -scale 300% out.png`.
- `--list-properties <id>` shows the user properties, and `--set-property name=value` overrides them.
- Isolate layers with `--render-debug object=<id>`, `skip-object=<id>`, `skip-effect=<id>`, `base-only`, or `pass-log` (logs every render pass).

## Inspecting a wallpaper

```bash
tools/dev/extract-pkg.py ~/.local/share/Steam/steamapps/workshop/content/431960/<id>/scene.pkg /tmp/<id>
jq '.objects[] | {id, name}' /tmp/<id>/scene.json
```

The extractor also writes every SceneScript to `/tmp/<id>/scripts/NN.js`, each headed by the layer/property paths that use it. Read these first: they tell you which engine APIs (`thisLayer`, `thisScene.getLayer`, `engine.*`, media events, `Vec3`...) the wallpaper depends on.

## Media integration (MPRIS)

Any MPRIS player works: browsers, Spotify, mpv. To get deterministic metadata without touching your real player:

```bash
tools/dev/fake-mpris-player.sh /tmp/test.mp3   # the script header shows how to generate a track with cover art
busctl --user list | grep mpris
pkill -x mpv
```

A player that is already playing wins over the fake one. Check `busctl --user get-property <name> /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player Metadata` to see exactly what a player reports.

## Scripting engine notes

`src/WallpaperEngine/Scripting/ScriptEngine.cpp` loads every property script as an ES module once the scene is built, with this lifecycle:

1. `init(value)`
2. `applyUserProperties(allProperties)`
3. `update(value)` every frame
4. `media*Changed(event)` on changes

- `thisLayer`/`thisObject` are bound per module.
- Layer properties are live: reads and writes go straight to the `DynamicValue`s that the renderers use.
- Angles are exposed in degrees.
- Script errors are logged at most 3 times per script.

Debugging approach that worked well:

- Temporarily dump each layer's properties every N frames from `ScriptEngine::tick()` (gated on an env var) to see what scripts actually computed.
- Compare that output with what is rendered, to tell a script problem from a renderer problem.
- Remove the dump before committing.

Gotchas:

- A value that exists but isn't fed into a uniform or the model matrix won't change anything on screen.
- `CPass` uniforms must be registered by pointer to follow script updates.

## Before committing

```bash
clang-format -i $(git diff --name-only -- '*.cpp' '*.h')
cd build && make -j"$(nproc)" linux-wallpaperengine tests && ./output/tests
```

- Run the target wallpaper once more and check that the log has no repeated errors.
- Shut it down with SIGTERM (the run script's `timeout` does this) and confirm there's no core dump: `coredumpctl list | tail`.
- After bumping a git submodule, run `tools/dev/update-flake-submodules.py` and commit `flake.nix`/`flake.lock`. The package pins submodules as flake inputs because `self.submodules` would also fetch quickjs' huge nested `test262`.

## Installing the local build

```bash
nix build -o ~/.local/share/linux-wallpaperengine/result     # the out-link is a GC root
ln -sfn ~/.local/share/linux-wallpaperengine/result/bin/linux-wallpaperengine ~/.local/bin/linux-wallpaperengine
```

`~/nix-config` (niri session) runs `~/.local/bin/linux-wallpaperengine`. Rebuild with the same command after pulling changes, then restart the session or the `niri-wallpaperengine` process.
