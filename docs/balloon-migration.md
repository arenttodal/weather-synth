# Balloon observatory migration (GUI only)

Brief: "Weather Synth — floating observatory GUI migration", 11 October 2026. References: `design/references/balloon-hero.png` (proportions and framing), `balloon-climates.png` (regional backdrops), `balloon-lighting.png` (palettes).

**Rollback point:** commit `a245b49` on `claude/vibrant-wright-7kfl4a` is the last island version (all checks green on macOS CI, run 38013658251). `git checkout a245b49 -- design/blender assets/weather_scene plugin/Source/gui` restores the island scene. The island builder stays in `build_scene.py` behind `"variant": "island"` in `scene_config.json`.

## What changes

| Area | File | Change |
|---|---|---|
| Blender scene | `design/blender/scene_config.json`, `build_scene.py` | `variant: balloon`: no island, rocks, steps, sea or shore. New: lathed low-poly envelope (cream/terracotta gores), collar, burner, cradle frame, four suspension lines, dish on a left outrigger, wind mast with vane and anemometer on a right outrigger, sensor pod on a short cable. The cabinet is the same mesh; its doorway becomes a small lit service window. The camera fits the whole craft automatically (`fit` in the config). |
| Export | `design/blender/export_layers.py` | Same anchors, accent mask and mechanism sprites; no shoreline polygons; new markers (burner, pod, dish, service light). |
| Art | `assets/weather_scene/**` | Re-exported. |
| Renderer | `plugin/Source/gui/SceneView.*` | Sea, wave marks, shore foam and splashes removed. New: regional backdrop band (procedural low-poly silhouettes in the cached background), whole-craft bob (1–2 px, off in still and reduced-motion), pod pulse once per newly accepted snapshot, burner and service-window lamps. |
| Landscape | `plugin/Source/gui/Landscape.*` (new), `Environment.h` | Nine visual-only profiles: neutral, urban, arid, green hills, alpine, tropical, plains, polar, coastal. No trustworthy region data exists in the product, so the default is **neutral**, with a **Scene style** override in the display settings (global, like the other display settings; never in projects, never touches sound). Fixtures can name a profile for demos. |
| Assets | `plugin/Source/gui/SceneAssets.*` | Shore data optional; the island coverage grid goes. |
| Editor | `plugin/Source/PluginEditor.cpp`, `Storage.*` | Scene style menu and preference. |
| Tests, docs | `plugin/tests/Tests.cpp`, `docs/gui-*.md` | Updated for the new scene; new matrix. |

**Not touched:** the engine, parameters and IDs, weather-to-sound mapping, saved state format, Keep Day, late-reply protection, relay. `PluginProcessor` gained only what the World Atlas browser needs (`prepareDay`, shared with the existing load path; `commitLocation`; the audition slot), see `world-atlas.md`.

## Decisions taken

- The antenna/dish and pod are narrative. The header keeps saying where data comes from (LIVE / STALE / SAVED DAY / ESTIMATED); nothing says "measured by the pod".
- Landscape never follows current weather (a hot day is not a desert). Without a data source, neutral is the honest default.
- Proportions follow the hero image within the existing 3:2 layout (header and macro band unchanged); the envelope is never cropped.

## Status

- Done: craft, export, renderer, twelve landscape families plus neutral and open water (World Atlas addendum), geography-only resolver, Scene style override, region fixtures, Explore Locations browser (`world-atlas.md`). Screenshots: `gui/balloon-families-noon.png`, `gui/balloon-families-dusk.png`, `gui/explore-locations.png`.
- The art is 3.8 MB (was 8.7 MB). The ground band is 20 % of the scene (horizon at 0.80).
- Pending: Apple Silicon benchmark of the new scene (CI), full atlas regeneration in `docs/gui/atlas/`, art sign-off.
