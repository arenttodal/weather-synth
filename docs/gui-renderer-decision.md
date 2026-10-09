# Renderer decision (Phase C)

**Decision: fixed-camera layered 2.5D, drawn with JUCE's existing 2D `Graphics`.** No OpenGL, no WebView, no real-time 3D. The decision was made once, after the vertical slice ran inside the actual plugin (VST3 + Standalone, host check passing), and is not revisited unless profiling on a Mac shows a problem the layered approach cannot fix.

## Why

- **It already looks right.** The Blender scene, rendered from one locked orthographic camera into seven lighting anchors plus an overcast anchor, carries all the modelling and lighting the style needs (flat-shaded facets, soft sun shadow, ambient occlusion). Runtime lighting is a weighted blend of aligned renders, which is enough for a fixed camera; the sky, sea, clouds, weather and lamps are cheap 2D shapes on top. Screenshots: `docs/gui/` (vertical slice) and the Phase H atlas.
- **It fits the editor that exists.** The plugin already uses native JUCE components; the scene is one more `Component` with a timer. No new runtime, no context or lifecycle risk in hosts (OpenGL attachment and WebView lifetimes are the usual sources of host crashes and black editors).
- **The cost is bounded and measurable.** Everything static is cached at physical resolution and rebuilt only when its inputs change; per frame the scene blits two cached layers and draws a bounded number of shapes (≤ 64 wave marks, ≤ 9 clouds in three merged fills, ≤ 400 particles in four batched fills, 2 sprites, 5 small glows).
- **Real-time 3D would buy nothing visible here.** The camera never moves, the building never changes, and continuously moving shadows are not part of the brief. It would add a GPU context, a model loader and a second implementation to keep in sync.

## Evidence (this VM; `docs/gui-phaseC.jsonl`)

Reference machine: 4 vCPU Xeon @ 2.1 GHz, Linux, Xvfb, JUCE 7.0.5 software renderer, 1× display scale, 48 kHz / 256, 6-note chord every 4 s. "GUI" is message-thread CPU per wall second above the harness's own idle loop (24.5 ms/s).

| Scene | Quality | GUI ms/s | fps | update p95 | DSP load |
|---|---|---|---|---|---|
| editor closed | — | ≈ 0 | — | — | 7.6% |
| live default (estimate, partly cloudy dusk) | full (24 fps) | 50 | 25 | 0.004 ms | 7.5% |
| heavy rain, noon | full | 90 | 25 | 0.010 ms | 7.8% |
| fog, night | full | 78 | 25 | 0.004 ms | 7.7% |
| thunderstorm, dusk | full | 83 | 25 | 0.010 ms | 7.8% |
| heavy rain, noon | economy (12 fps) | 47 | 12 | 0.012 ms | 7.7% |
| heavy rain, noon | still | 3.6 | — | — | 7.9% |
| 4 instances, 4 editors, rain | full | 323 | 25 each | 0.009 ms | 28.1% |

Per-frame paint breakdown for heavy rain after optimisation (`ATMOS_PROFILE=1`): background blit 0.42 ms, clouds 1.03, sea 0.26, island 0.53, sensors 0.07, lamps 0.07, precipitation 0.28, fog band 0.19 ≈ 2.9 ms. Before optimisation it was 7.8 ms; the big wins were clipping sprite tints to the sprite (JUCE's software renderer otherwise builds an alpha mask over the whole clip region), baking static fog, the overcast deck and the island veil into the caches, and an opaque RGB background cache.

Editor open: 96 ms cold (first decode of the anchors), 26 ms warm. Audio: no additional overruns with editors open versus closed in any configuration.

## Not verified here

macOS (CoreGraphics renderer, Retina 2× caches) and Windows (Direct2D in JUCE 8) were not available. The 2× cache quadruples blit area; on Apple Silicon this is expected to be well within budget but is **unmeasured**. The manual procedure in `gui-performance.md` repeats these runs with `AtmosGuiBench` on a Mac.

## Escape hatches kept

- Animation quality: full (24 fps), economy (12 fps), still (no timer at all). Reduce motion and no-flash options.
- The scene never blocks controls: it is a non-interactive component under the header and macro band, so even a slow frame cannot delay input handling beyond one paint.
