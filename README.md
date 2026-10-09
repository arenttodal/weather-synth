# Atmospheric

One sound, dealt by your sky. A synth (VST3, AU, standalone) whose single patch is shaped by the weather, sun and moon where you are, so it's a little different for every person on every day. Five knobs let you play it without turning it into a preset browser.

| Path | What it is |
|---|---|
| `plugin/` | The JUCE plugin: processor, Days, globe, the island-scene editor (`Source/gui`), tests |
| `design/blender/`, `assets/` | The editor's scene: reproducible Blender scripts and the exported art compiled into the plugin |
| `engine/` | The sound engine, plain C++ shared by the plugin and the browser: sources, bible, nature rules, and OSP's effects (Kaleidoscope, Space, Echo, Movement, Character filter) in `engine/osp` |
| `designer/` | The sound designer: the real engine in WebAssembly, for dialling in core sounds and their boundaries |
| `server/` | The weather relay for Railway (keeps the OpenWeatherMap key off users' machines) |
| `lab.html` | The first tuning lab (superseded by the designer); still the reference for the weather "forces" |
| `index.html` | The original web prototype |
| `docs/PRODUCT_PLAN.md` | Why the sound maps the way it does, and the three control models |
| `docs/SETUP.md` | Railway setup, installing a build, the admin globe |

## Builds

Every push builds macOS (universal), Windows and Linux on GitHub Actions. Each build runs the unit tests and a host check, plus pluginval (and auval on macOS), then publishes the installers under **Releases → latest**.

## Changing the sound

The plugin plays one **core sound** a day, drawn from the **bible**: a handful of designed sounds, each with a home value and low/high boundaries for every parameter, and an anchor climate (temperature, wetness, light). The day's weather picks the nearest sound (with a little daily luck) and then leans every parameter inside its boundaries. Nothing leaves the box you approved.

1. Open the designer (`designer/index.html` through a local web server, or `/designer/` on the relay) and shape the sounds. Use the Low/High edge and Weather audition modes to hear the extremes.
2. Export, and save the file as `plugin/Resources/bible.json` to build it into the plugin.
   To try a bible without rebuilding, drop the export as `bible.json` into the app data folder (macOS `~/Library/Application Support/Atmospheric`, Windows `%APPDATA%\Atmospheric`) and reopen the plugin.
3. Days and projects store the core sound they were made with, so changing the bible never changes a saved Day.

After changing anything in `engine/`, run `tools/sync-designer.sh` so the designer and the relay play the same engine as the plugin.

## The editor

A low-poly synthesizer house on an island whose sky, sea and weather show the Day the sound was dealt from. See `docs/gui-handoff.md` (how it works, rebuilding the art, known issues), `docs/gui-state-coverage.md`, `docs/gui-performance.md` and the contact sheets in `docs/gui/atlas/`.

## Building locally

```
cmake -S plugin -B build -DATMOS_FETCH_JUCE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/AtmosTests_artefacts/Release/AtmosTests
```
