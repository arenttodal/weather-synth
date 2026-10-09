# GUI audit (Phase A)

Baseline commit: `91f4256` on `claude/vibrant-wright-7kfl4a` (the GUI work continues on this branch, in separate reviewable commits).
Reference machine for every number below: cloud Linux VM, 4 vCPU Intel Xeon @ 2.10 GHz, Ubuntu 24.04, Xvfb + openbox, JUCE 7.0.5 software renderer. **No Mac was available**; Apple Silicon and Intel Mac figures are unverified (see `gui-performance.md`).

## Framework, formats, renderer, build

| Item | Finding |
|---|---|
| Framework | JUCE (8.0.8 in CI via FetchContent, 7.0.5 from Ubuntu locally). C++20. |
| Formats | VST3, AU (macOS), Standalone. `plugin/CMakeLists.txt`, `juce_add_plugin`. |
| Minimum OS | macOS 11 (`CMAKE_OSX_DEPLOYMENT_TARGET`), universal arm64 + x86_64; Windows 10+ (MSVC 2022); Linux (CI only). |
| Renderer | Native JUCE `Component` editor, `juce::Graphics` (CoreGraphics on macOS, Direct2D/software on Windows, software on Linux). No OpenGL, no WebView (`JUCE_WEB_BROWSER=0`). |
| Editor | `AtmosEditor` (`plugin/Source/PluginEditor.*`), fixed 820 × 500, not resizable. Day card animates rain/snow at 30 Hz with its own timer; editor refreshes on processor change messages plus a 30 s timer. |
| Assets | `juce_add_binary_data(AtmosData …)`: `land110m.bin` (globe coastline) and `bible.json`. Nothing is loaded from disk at runtime except the user's optional `bible.json` override. |
| Build | `cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release [-DATMOS_FETCH_JUCE=ON]` then `cmake --build build`. |
| Release pipeline | `.github/workflows/build.yml`: relay tests, engine smoke + WASM, macOS (tests, host check, auval, pluginval, package), Windows, Linux, `latest` pre-release. **Currently blocked**: since 2026-10-08 20:13 UTC no job receives a runner (private repo, Actions minutes). Not changed by this work. |

## The five macros (real model)

Defined in `AtmosProcessor::layout()`; attached with `AudioProcessorValueTreeState::SliderAttachment` (host gestures, automation and undo come from APVTS). All are `AudioParameterFloat`, ID version 1, range −1…1, step 0.001, default 0, linear normalisation `(v + 1) / 2`.

| ID | Name | Meaning in the engine (`macroLean`, `engine/atmos/Bible.cpp`) |
|---|---|---|
| `tone` | Tone | Cutoff (×1.5), brightness tilt (×0.8), filter-envelope amount (×0.5) |
| `bloom` | Bloom | Amp attack/release (×1.5), sustain (×0.8), filter attack/release |
| `space` | Space | Reverb send (×1.5), echo send (×1.0), reverb decay (×1.0) |
| `motion` | Motion | Movement amount (×1.5), vibrato (×1.2), PWM, LFO to filter, Kaleidoscope spread |
| `intensity` | Intensity | Scales how hard the weather pulls every parameter: 0 = nature, +1 = ×1.6, −1 = tamed to the core sound's home |

The concept art's **WARMTH / SHAPE / MOTION / ECHO / SPACE are placeholders**. The GUI labels the real five: TONE, BLOOM, MOTION, SPACE, INTENSITY. No DSP was added or rerouted.

**Weather anchor.** A macro's value is a *lean* added on top of the weather's own lean, and 0 always means "where nature put it". The weather anchor is therefore the 0 position on every track; reset-to-anchor = set to 0 = the parameter default (the two coincide in this engine). The resulting musical values stay inside each core sound's lo/hi boundaries (`placeInBounds`), so no macro can leave the designed box whatever the weather does. Bounds never move, so parameter IDs and normalised semantics are stable.

## Weather state ownership

- **Snapshot**: `atmos::Day` (`plugin/Source/Day.h`): place, lat/lon, `observedAt` (unix), UTC offset (+ known flag), source (`live` / `estimate` / `globe` / `globe estimate`), condition (OWM `main` string), temp, humidity, precip 0–1, wind m/s, clouds 0–1, pressure, `mappingVersion`, and `sound` (the core-sound snapshot). `Day::climate()` adds sun elevation, moon phase and the daily seed (`engine/atmos/Astro.cpp`).
- **Owner**: `AtmosProcessor` (`day`, under `dayLock`; the audio thread receives plain patch data through a SpinLock + version counter, never the Day).
- **Fetch**: `WeatherClient` (thread pool, HTTPS to the Railway relay, 7 s timeout, results posted to the message thread, `alive` flag guards destroyed clients). Relay: `server/src/app.js`, caches per ~11 km cell for 20 min, rate-limited, holds the OWM key.
- **Refresh policy**: a Day is dealt **once** when the plugin starts without a restored project (400 ms grace timer), and again only when the user presses *Hear today / Refresh sky*. There is no periodic refresh, so the sound never changes during playback by itself. The GUI must keep it that way.
- **Accepted replies**: generation counter `dayGeneration` (added 2026-10-08): a reply is applied only if no restore / Keep Day load / globe pick / preview end happened since its request. Restores apply immediately even off the message thread (`setStateInformation`), status text is posted to the message thread.
- **Saved state**: XML `Atmospheric` v2: `Day` child (JSON of `Day::toVar`, including the sound snapshot) + APVTS `MACROS` child. Older `mappingVersion` Days draw a fresh core sound.
- **Stale / offline**: on network failure the processor plays `estimateDay` (simulated weather for the last known place) with status `estimate` and an error message. A live Day from an earlier local date is detected by `dayIsCurrent()`.
- **Location**: IP-located by the relay, or a home place chosen in *Place* (geocoding through the relay), stored in `settings.json`. Admin globe (Cmd+Shift+G / Cmd+W / triple-click) previews any point.
- **Time zone**: OWM's UTC offset (seconds) at observation time. It already includes DST for that moment; there is no IANA zone ID in the pipeline. Estimates without a provider offset use longitude/15 h, flagged `utcOffsetKnown = false`.
- **Astronomy**: `sunElevation` (NOAA low precision, ~0.5°), `moonPhase` (mean synodic month). No sun azimuth or moon position yet; both are needed for the sky and will be added as *visual-only* functions so the sound (which uses elevation + phase) is untouched.
- **Keep Day**: `keepDay(name)` copies the current Day (with its sound) into `days.json` with a new UUID; `loadDay` restores it and bumps the generation.

### Gaps for the GUI

The relay snapshot has no condition **ID**, wind direction, gust, visibility, or rain/snow mm. The plan forbids substring matching of descriptions, so the relay and `Day` get additive, visual-only fields (old Days and old relay replies still load; the sound ignores them).

## Tests and tooling

| Check | What it covers |
|---|---|
| `AtmosTests` | mapping parity with `lab.html`, astronomy, storage, bible bounds, 102 renders, CPU, processor state, late replies, globe projection; `--snapshot file.png` renders the editor |
| `AtmosHostCheck` | loads the built VST3 (and AU on macOS) like a host: audio at 3 rates × 4 block sizes, state round trip, editor open/close, editor automation, pluginval-style 10 s editor automation |
| pluginval (CI) | strictness 7, macOS full, Windows non-GUI (known Windows-only pluginval exit issue, editor covered by host check) |
| auval (CI, macOS) | `aumu Atm1 Atmo` |
| relay `npm test` | 11 tests |
| `engine/tests/core_smoke.cpp` | 68 engine configurations |
| **new** `AtmosGuiBench` | DSP vs GUI CPU per thread, block-time percentiles, overruns, RSS across open/close cycles |

Accessibility: JUCE's default accessibility handlers for sliders and buttons; the Day card has none. No reduced-motion handling. No screenshot regression tooling beyond `--snapshot`.

Pre-existing issues (not GUI): GitHub Actions blocked (billing/minutes); Windows pluginval exit code outside a debugger; on this 4-vCPU VM, eight instances each holding a 6-note chord overrun 3 of ~1900 blocks (DSP, not GUI; same with editors closed).

## Baseline measurements (current editor, before the GUI work)

`docs/gui-baseline.jsonl`, 10 s each, 48 kHz / 256 samples, 6-note chord every 4 s. "GUI ms/s" is message-thread CPU per wall second; the harness's own idle loop costs **24.5 ms/s** (measured with zero instances), so subtract it.

| Configuration | DSP load (audio thread) | GUI ms/s | GUI over idle | RSS |
|---|---|---|---|---|
| 0 instances (harness floor) | 0.7% | 24.5 | — | 12 MiB |
| 1 instance, editor closed | 7.9% | 24.2 | ≈0 | 19 MiB |
| 1 instance, editor open | 7.7% | 62.3 | **+38 (3.8% of a core)** | 26 MiB |
| 4 instances, 4 editors | 27.7% | 151.9 | +127 | 41 MiB |
| 4 instances, 2 editors | 28.1% | 90.3 | +66 | 37 MiB |
| 8 instances, editors closed | 52.9% | 24.9 | ≈0 | 41 MiB |
| 50 open/close cycles | — | — | cold open 47 ms, warm 1.1 ms | +8.4 MiB after cycles (JUCE font/peer caches; recheck after the new editor) |

Screenshot: `docs/gui/baseline-editor.png`.

## Decisions taken in Phase A

- Keep the native JUCE renderer; no WebView, no OpenGL until profiling says otherwise (see `gui-renderer-decision.md`).
- Blender **4.0.2** (Ubuntu 24.04 package) is the pinned authoring version; it runs headless here (EEVEE, Cycles and Workbench all render under Xvfb).
- Reference images: `lighting-states.png` is in `design/references/`. The hero, sky & rain, wind & winter and visibility & fallback sheets were shared in the conversation but not delivered as files; they should be added to `design/references/` with the names in the plan before final art sign-off.
