# State coverage

Every state is produced by shared systems (sky colours, lighting-anchor blend, clouds, waves, particle pool, veils, surface accents, lamps), never by a per-state picture. Fixtures live in `plugin/Source/gui/SceneModel.cpp`; contact sheets for all 18 fixtures × 9 times are in `docs/gui/atlas/` (regenerate with `AtmosTests --atlas <dir>`).

## Light and time (continuous, from the sun's altitude)

| Reference card | How it's produced |
|---|---|
| Dawn, morning, noon | morning half: night → dawn (−4°) → morning (14°) → noon (42°+) anchors blended by altitude |
| Late afternoon, sunset, dusk, night | evening half: noon → late afternoon (18°) → sunset (1°) → dusk (−6°) → night (−14°−) |
| Polar day | a day whose sun never reaches the noon key: stays between dawn/morning or sunset/late-afternoon light; tested at Tromsø, 21 June 00:30 |
| Polar night | noon twilight (−8°): dusk/dawn and night anchors; tested at Tromsø, 21 December noon |

Morning vs evening comes from whether the sun is rising (altitude 20 minutes later). Sun and moon positions: SunCalc formulas (`engine/atmos/Sky.cpp`), tested against SunCalc's own reference values. The moon's lit fraction is from the date; its position from the place; it's drawn only above the horizon, can share the sky with the sun, and is hidden by clouds, fog or the cabinet.

## Environments

| # | State | Live trigger (OpenWeather) | Shared mechanisms |
|---|---|---|---|
| 1 | Clear | 800 | open sky, sun/moon/stars, quiet waves |
| 2 | Partly cloudy | 801–802 or cover 20–55% | a few faceted clouds drifting with the wind |
| 3 | Overcast | 803–804 or cover > 85% | diffuse "overcast" lighting anchor, cloud deck, grey sky; no rain unless reported |
| 4 | Drizzle | 3xx, 230–232 | thin short streaks |
| 5 | Rain / showers | 500–501, 520–521, 531 | streaks + splashes on open water |
| 6 | Heavy rain | 502–504, 522 | dense streaks, darker sky and island, more foam |
| 7 | Thunderstorm | 2xx | storm palette, small distant bolt with a local glow (none if "No lightning flashes"); rain only if the code says so (210–221 have none) |
| 8 | Wind / squalls | wind speed; 771 | faster clouds, wind-slanted precipitation, whitecaps, vane turns into the wind, anemometer speeds up; wind alone never adds rain |
| 9 | Snow | 600–602, 620–622 | drifting square flakes; roof and rock accents only when reported snow meets ≤ 2 °C |
| 10 | Sleet / mixed | 611–613, 615–616 | mixed pellets and flakes |
| 11 | Freezing rain | 511 (decided before generic rain) | icy-tinted streaks, ice accent on roof and ledges |
| 12 | Hail | **fixture only**: OpenWeather has no hail code; thunderstorms never imply hail | white pellets that bounce once |
| 13 | Mist / fog | 701, 741; visibility | sky-coloured wash and horizon bands, clamped so the cabinet stays readable |
| 14 | Haze / smoke | 721, 711 | warm-grey sky and veil; no fire is drawn |
| 15 | Dust / sand / ash | 731, 751, 761, 762 | ochre (or ash-grey) sky and veil, drifting specks; same island |
| 16 | Tornado / extreme wind | 781 | small distant funnel on the horizon; no camera shake; extreme wind alone shows no funnel |
| 17 | Frost | **fixture only**: temperature alone never establishes frost | sparse pale accent on upward facets |
| — | Offline / saved day | network failure → ESTIMATED; restored project or kept Day → SAVED DAY | header source label and icon; scene shows that Day's own sky; controls unaffected |

Modifiers combine: rain with fog, snow with wind, fog at night (all tested). Several codes: the first code that carries precipitation decides its kind, a later code can only make it more severe; visibility, thunder, squall and tornado modifiers from every code combine. Unknown codes are ignored with a flag (no crash, no invented weather). Missing values stay unknown (negative), never zero.

## Source labels

| Label | Meaning |
|---|---|
| ● LIVE | an accepted reading less than an hour old, from today |
| ○ STALE | a live reading older than an hour or from an earlier date (press Refresh to deal today) |
| ■ SAVED DAY | a project's or a kept Day's own snapshot, sky included |
| △ ESTIMATED | the defined offline fallback (simulated sky); always shown with OFFLINE |
| ◆ GLOBE PREVIEW | the admin globe |
| ··· READING SKY | first reading in progress |
| ◇ FIXTURE | developer fixture (tests, benchmark, `ATMOS_DEV=1` builds); never real data |
| ╱ OFFLINE | refresh unavailable; can accompany any of the above |

Fixtures always show "Fixture: …" as the place and FIXTURE as the source (the offline/saved-day fixture shows its own labels), and are only reachable from tests, the benchmark, or a build started with `ATMOS_DEV=1` (Cmd+Alt+Shift+F / T).
