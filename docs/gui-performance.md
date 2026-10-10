# GUI performance

All numbers come from `AtmosGuiBench` (`plugin/tests/GuiBench.cpp`); raw results in `docs/gui-baseline.jsonl` (old editor), `docs/gui-phaseC.jsonl` and `docs/gui-phaseH.jsonl` (final).

## Tested configuration

| | |
|---|---|
| Machine | Cloud VM, 4 vCPU Intel Xeon @ 2.10 GHz, no GPU |
| OS / display | Ubuntu 24.04, Xvfb 1920×1080×24 with openbox (`tools/with-wm.sh`), 1× display scale |
| Renderer | JUCE 7.0.5 software renderer (the Linux path) |
| Plugin | the processor and editor compiled into the bench (same code as the VST3), Release |
| Audio | 48 kHz, 256-sample blocks, paced in real time on its own thread; a 6-note chord every 4 s (≈6 voices of the default core sound) |
| Quality | full = 24 fps, economy = 12 fps, still = no timer |

**Not measured:** Windows, Retina at runtime, a real DAW, an Intel Mac. Apple Silicon numbers from CI are in the next section, with their limits.

## Apple Silicon (GitHub CI runner)

GitHub's macOS runner: Apple M1 (virtual), macOS 14.8.9, JUCE 8.0.8 (CoreGraphics renderer), universal Release build, no physical display (1× scale). Same bench and audio load as above; run [38006209426](https://github.com/arenttodal/weather-synth/actions/runs/38006209426), raw lines in `docs/gui-mac.jsonl`. The macOS bench's idle floor is ≈ 0 (0.04 ms/s), so nothing is subtracted.

| Configuration | Quality | GUI ms/s | fps reached | DSP load | Overruns / 10 s | RSS (resident size) |
|---|---|---|---|---|---|---|
| 1 instance, editor closed | — | 0.1 | — | 6.6% | 0 | 24 MiB |
| clear, noon | full | 20.4 | 9.8 | 5.7% | 0 | 71 MiB |
| heavy rain, noon | full | 17.3 | 9.0 | 5.7% | 0 | 87 MiB |
| heavy rain, noon | economy | 15.4 | 6.7 | 5.9% | 0 | 89 MiB |
| heavy rain, noon | still | 10.1 | 0.1 | 5.6% | 1 (7.4 ms block) | 84 MiB |
| 4 instances, 4 editors, rain | economy | 34.4 | 7.0 each | 23.5% | 0 | 109 MiB |

Read with care:
- **The runner never reached the target frame rate** (24 or 12 fps). It has no display, so macOS paces window repaints itself; the GUI cost is therefore for fewer frames than a real screen would ask for. Per frame it is about 2 ms (20.4 ms/s ÷ 9.8 fps at full), which at 24 fps would be ≈ 50 ms/s (5% of a core), in line with the Linux software renderer. A Mac with a screen is still needed for the real figure.
- The one overrun (still mode) is a single 7.4 ms block on a shared virtual machine; the p99 block time stayed at 0.68 ms.
- **No leak on editor open/close.** The RSS column above climbed with every open/close (18 → 440 MiB over 50 cycles), but macOS's `leaks --atExit` after 10 cycles found **0 leaked bytes**, and the physical footprint (Activity Monitor's number) was 63 MB, below the 82 MB peak with an editor open. On macOS the resident size keeps counting freed window surfaces and reusable malloc pages. The bench now reports physical footprint on macOS; the RSS figures in this table and in `gui-mac.jsonl` are the older, resident-size ones.

## Method

- **GUI cost** = CPU time of the message thread (`CLOCK_THREAD_CPUTIME_ID`) per wall-clock second, minus the bench's own idle loop (24.5 ms/s measured with zero instances). Reported as ms/s; 10 ms/s = 1% of one core.
- **DSP cost** = CPU time of the audio thread / wall time; per-block CPU percentiles; an *overrun* is a block whose processing took longer than its 5.33 ms duration.
- **Update** = the per-frame simulation step (transitions, particles, sensors), timed separately from drawing. **Paint** breakdown via `ATMOS_PROFILE=1`.
- **Memory** = process RSS before/after opening editors and after N open/close cycles.

## Results (final build)

| Scene | Quality | GUI ms/s | fps | Update p95 | DSP load | Overruns / 10 s |
|---|---|---|---|---|---|---|
| editor closed | — | ≈ 0 | — | — | 7.7% | 0 |
| live default (estimate, dusk) | full | 59 | 25 | 0.005 ms | 7.5% | 0 |
| clear, noon | full | 52 | 25 | 0.004 ms | 7.8% | 1* |
| heavy rain, noon (worst) | full | 101 | 25 | 0.012 ms | 7.6% | 0 |
| thunderstorm, dusk | full | 100 | 25 | 0.009 ms | 7.8% | 0 |
| tornado + rain, noon | full | 99 | 25 | 0.009 ms | 7.5% | 0 |
| fog, night | full | 94 | 25 | 0.004 ms | 7.5% | 0 |
| snow, night | full | 93 | 25 | 0.011 ms | 7.9% | 0 |
| dust, noon | full | 62 | 25 | 0.005 ms | 7.3% | 0 |
| heavy rain, noon | economy | 53 | 12.5 | 0.012 ms | 7.8% | 0 |
| heavy rain, noon | still | 5 | — | — | 8.0% | 0 |
| 4 instances, 4 editors, heavy rain | full | 384 (96 each) | 25 | 0.010 ms | 29.4% | 1* |
| 4 instances, 2 editors, rain | full | 185 | 25 | 0.009 ms | 27.2% | 1* |
| 8 instances, editors closed | — | ≈ 0 | — | — | 55.5% | 21* |

\* Overruns occur with and without editors (8 closed instances: 21) and track DSP load on this shared 4-vCPU VM; the GUI runs on the message thread and adds no work to the audio callback (one relaxed atomic store per block). Single-instance runs show 0–1 overruns per 10 s regardless of editor state.

Paint breakdown, heavy rain, per frame: background blit 0.42 ms, clouds 1.03, sea 0.26, island 0.53, sensors 0.07, lamps 0.07, precipitation 0.28, fog band 0.19 (≈2.9 ms before the particle count was raised to 300; ≈3.4 ms now).

### Budgets

| Budget | Result | Verdict |
|---|---|---|
| 24–30 fps normal; 12–15 economy; still available | 24 / 12 / 0 fps | met |
| Controls independent of scene pacing | scene is a non-interactive component; controls are separate components | met |
| Closed editor: no frames or timers | ≈ 0 ms/s; test asserts no timer when not showing | met |
| Assets on disk 10–25 MB | 8.7 MB art + 0.3 MB font (VST3 bundle 13 MB total) | met (below range) |
| Decoded assets 32–64 MB active | ≤ 3 anchors × 4.9 MB + sprites + accent mask ≈ 20 MiB shared per process; per-editor caches 0.4 MB (1×) to 5.5 MB (2×) | met |
| Particles ≈200 normal, cap 400 | 300 at full intensity (economy 160, reduced motion 140); hard cap 400 tested | **deviation: 300 normal** (heavy rain needed density to read like the reference; cost +0.1 ms/frame) |
| Update < 1 ms p95 | ≤ 0.012 ms p95 | met |
| Editor open < 500 ms | cold 86–133 ms, warm 27–32 ms | met |
| No added xruns, no RT-unsafe work | no change with editors open; audio path gained one atomic store | met on this VM |
| 50 open/close cycles stable | RSS 28.9 / 28.7 / 29.0 MiB after 10 / 50 / 150 cycles | met (no growth) |

**GUI CPU is higher than the old editor** (38 ms/s → 52–101 ms/s on this software renderer). It is the price of a full-window animated scene at 24 fps; economy mode halves it and still mode removes it. Worth checking on a Mac before release: CoreGraphics may be faster or slower than JUCE's software renderer for these blits.

## How to reproduce

```
cmake --build build --target AtmosGuiBench
tools/with-wm.sh build/AtmosGuiBench_artefacts/Release/AtmosGuiBench --seconds 10 --editors 1 --fixture heavy_rain@noon
# options: --instances N --editors N --quality full|economy|still --cycles N --fixture <name>@<time>
# per-layer paint times: ATMOS_PROFILE=1 (prints every 150 frames)
```
(`tools/with-wm.sh` only matters under Xvfb; run the binary directly on a desktop.)

## Manual procedure for a Mac (unverified here)

1. Build Release universal (`cmake -S plugin -B build -DATMOS_FETCH_JUCE=ON -DCMAKE_BUILD_TYPE=Release`, `cmake --build build --config Release`).
2. Run `AtmosGuiBench` with the table's configurations, on the built-in Retina display (2×) and on a 1× external display if available. Record the machine model, macOS version and display scale.
3. In a DAW (Logic AU, Ableton/Reaper VST3), 48 kHz / 128 samples, 16 voices held: open the editor with the heavy-rain fixture (`ATMOS_DEV=1`, Cmd+Alt+Shift+F) and watch the DAW's CPU and dropout counters for 2 minutes in full, economy and still. Compare with the editor closed.
4. Open and close the editor 50 times; check Activity Monitor memory.
5. Sleep the Mac with the editor open, wake it, and confirm the scene resumes without a burst (delta is clamped to 0.1 s).
