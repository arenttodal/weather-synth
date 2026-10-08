# Atmospheric

One sound, dealt by your sky. A synth (VST3, AU, standalone) whose single patch is shaped by the weather, sun and moon where you are, so it's a little different for every person on every day. Five knobs let you play it without turning it into a preset browser.

| Path | What it is |
|---|---|
| `plugin/` | The JUCE plugin: engine, Days, globe, tests |
| `server/` | The weather relay for Railway (keeps the OpenWeatherMap key off users' machines) |
| `lab.html` | The tuning lab: audition and rate climates. Its mapping is the source of truth for the plugin |
| `index.html` | The original web prototype |
| `docs/PRODUCT_PLAN.md` | Why the sound maps the way it does, and the three control models |
| `docs/SETUP.md` | Railway setup, installing a build, the admin globe |

## Builds

Every push builds macOS (universal), Windows and Linux on GitHub Actions. Each build runs the unit tests and a host check, plus pluginval (and auval on macOS), then publishes the installers under **Releases → latest**.

## Changing the sound

1. Edit the mapping in `lab.html` (between `NATURE MAPPING` and `SCENARIOS`) and tune it by ear in the lab.
2. Run `node tools/gen-vectors.mjs` to refresh `plugin/tests/vectors.json`.
3. Port the same change to `plugin/Source/ClimateMapper.cpp`. The unit tests fail until the C++ matches the lab exactly, and so does CI.

## Building locally

```
cmake -S plugin -B build -DATMOS_FETCH_JUCE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/AtmosTests_artefacts/Release/AtmosTests
```
