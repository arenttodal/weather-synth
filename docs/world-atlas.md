# World Atlas and Explore Locations

Addendum: "Weather Synth: World Atlas and Location Audition", 11 October 2026. Concept references: `design/references/atlas-browser.png` (the browser) and `design/references/world-atlas.png` (the twelve families). Concept art with illustrative conditions, not live weather or production assets.

![Explore Locations](gui/explore-locations.png)

## How it behaves

The header's place button opens **Explore Locations**. The instrument keeps playing and the header keeps showing what is loaded.

1. Search a city or region (OpenWeather geocoding through the relay, up to five results, debounced) or type coordinates (`59.91, 10.75`, `33.9 S 18.4 E`), or click the globe.
2. The candidate's weather is read (or the defined offline estimate, labelled ESTIMATED), its landscape resolved, and the card shows place, exact coordinates, temperature, condition, humidity, local time, source and age, the core sound it would play, and a still thumbnail of its scene.
3. **Preview sound** renders a fixed 3-second phrase with a separate instance of the real engine on a worker thread, then plays it through the host output at a fixed level. **Stop** fades it.
4. **Cancel** (or Esc, or the close button) drops the candidate. **Load location** commits exactly the Day that was shown and auditioned, swaps the whole scene, and makes that place the source of later "today" readings.

## Two domains

| Committed instrument | Browser candidate |
|---|---|
| `AtmosProcessor` Day, core sound, macros, scene, header, saved state | `ExploreBrowser::Candidate`: coordinates, label, prepared Day, thumbnail, audition clip |
| Changes only through `commitLocation` (Load) | Changes on every search, pin or reply |

- One function prepares a Day for both paths (`AtmosProcessor::prepareDay`): the candidate's core sound and the loaded one are the same by construction, and `patchFor` gives the exact patch with the current macros. Load does not re-query the weather.
- Each selection bumps a generation; late weather, geocoding, thumbnail and audition results from an earlier pin, a Cancel or a closed browser are dropped.
- The rendered phrase is keyed to the patch and sample rate; moving a macro invalidates it and the next Preview re-renders.
- Audition playback: the clip is published to the audio thread with a hazard-pointer hand-off (no locks, no allocation, no disk or network in `processBlock`). It is never saved, never plays in an offline render (the browser says so), and stops before Load, on Cancel and when the editor closes. A clip rendered for another sample rate does not play.
- Keep Day is untouched by browsing.

Tests (`testExplore`, run in CI): coordinate parsing; land/ocean lookup including the dateline; globe click and pin agree at the dateline and poles; deterministic, clean phrase; candidate selection, preview, save-during-preview and Cancel leave the instrument, saved state and header unchanged; rapid A/B keeps only B; a restore while a request is pending wins; offline rendering drops the audition; Load applies the auditioned snapshot, sound and patch.

## Landscape atlas

Twelve families plus neutral and open water (`gui/Landscape.cpp`): city, temperate, boreal, tropical, grassland, savanna, desert, Mediterranean, alpine, tundra/polar, coast/islands, wetland/delta. Each is a few flat, faceted silhouette bands drawn procedurally into the cached sky layer and tinted by the sky, so every weather and time of day applies to every family without extra art. Measured cost per frame: none (cached).

**Resolution** (`gui/LandscapeResolver.cpp`, version 1), geography only:

| Source | Rule | Confidence |
|---|---|---|
| curated | about 45 hand-picked places, within 40 km | 0.8 |
| ocean | Natural Earth 110 m land outline (public domain, already shipped for the globe) finds no land within about half a degree | 0.7 |
| none | everything else | neutral, 0 |

Never from temperature, humidity or precipitation. The user's **Scene style** (display settings) overrides the picture only.

## Hero fidelity

Goal: match `design/references/balloon-hero.png` element by element, not approximate it.

**One 3D scene** (`design/blender/hero_env.py`). The hero's environment is built as a single lit scene with a perspective camera, not 2D sprites:
- **Placement:** each element is placed by where it appears in the hero (pixel x, y) and how far away it is, then lifted along the camera ray.
- **Depth:** every material fades with distance towards the sky behind it or the city haze (aerial perspective). That is where the hero's depth comes from.
- **Clouds:** lofted from silhouettes measured on the hero, in its two tiers (a lower body bulging forward, the dome set back, the crease between them), lit by a warm key from the right and a cool fill from above.
- **City:** ridges in three layers, far, middle and near towers with lit window pairs, the cable-stayed bridge, and treetops along the bottom, all at the hero's measured positions.
- **Matched numerically:** mean colours per region (sky top, middle and low; ridges; towers; near city; cloud tops and undersides; sun) were brought to within a few levels of the hero.

**Lighting anchors.** The same eight anchors as the craft:
- **Dusk** is the hero itself.
- **Dawn, noon, golden hour (sunset), night and overcast** are measured on `design/references/balloon-lighting.png`.
- **Morning and late afternoon** sit between their neighbours.
- Each anchor sets the sky ramp, the haze, key and fill light, cloud colours and window glow.

**Export.** `hero_env.py -- --export assets/weather_scene/hero` renders, per anchor:
- `city_<anchor>.png`: ridges, city and bridge, haze baked in.
- `bands_<anchor>.png`: the flat cloud bands over the horizon.
- `cloud<k>_<anchor>.png`: the hero's five clouds, each with its box.
- `hero.json`: the boxes, plus the sky as colour rows down the scene.

It runs one Blender process per layer and crops with `tools/crop_alpha.py` (Pillow).

**In the plugin.** Everything is blended by the same anchor weights as the craft, so time of day and overcast move through all layers together:
- **Cached background:** the sky ramp, then the sun and moon (still placed astronomically by the plugin), the bands, then the city layer. Families other than city stay procedural for now.
- **Clouds:** they sit where the hero has them and appear in its order as cover grows (all five at broken cloud). They drift with the wind and are re-lit only when the light changes.

**Craft** (`build_scene.py`, `scene_config.json`):
- **Camera:** perspective, level, at cabinet height and yawed to the cabinet's left side, so the envelope's underside shows.
- **Envelope and suspension:** the hero's envelope profile; a wide collar with a hanging orange burner; short black brackets for the dish and wind mast, each with a line from the envelope.
- **Cabinet:** the hero's face layout (ring knobs, slim cream fader caps in black slots, the patch field, grilled speakers, cables), with a larger lit service window.
- **Dusk light:** warm white from the front left (the hero's craft is not lit pink).

Stills: `xvfb-run -a blender -b --factory-startup --python design/blender/hero_env.py -- --out still.png [--anchor noon] [--fast]`.

## Not done yet (honest list)

- Composable overlays (city + coast + terrain together) and season/ground-cover inputs; today one family per place.
- A worldwide classifier: needs a validated source (for example a build-time, size-measured class grid from land cover plus elevation), a licence review and a version; not shipped.
- Globe/Search tabs at the smallest sizes (the panel scales instead), a 2D map fallback, keyboard arrow navigation through results (Tab works).
- Pausing the hidden scene's animation while the browser is open.
- Blender-authored family layers: **city is done** (Hero fidelity, above); the other eleven families are still procedural and follow one at a time, each checked side by side against the reference.
- Host checks of the audition (realtime playback, stopped transport, offline bounce) in real DAWs.
