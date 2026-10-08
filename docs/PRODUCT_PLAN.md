# Atmospheric: one sound, decided by the sky

Product direction for taking the web prototype to a JUCE plugin (VST3/AU) and standalone app.
The tuning lab (`lab.html`) implements everything described in sections 2 and 3, so each claim here can be heard and rated.

## 1. The premise

- One sound per person per day. No patch browser, ever.
- The sound is "dealt" by the place and the moment: temperature, humidity, rain, wind, cloud, sun angle, moon phase and air pressure, plus a small daily seed so two cities with identical weather still differ.
- Four or five macros keep it playable as an instrument without turning it into a preset hunt.
- A day can be kept: it is saved as a dated "Day" (e.g. `2026-10-08 · Trondheim · Drizzle 6 °C · Waxing gibbous`) with an optional name.

What the reference products teach:

- **Spitfire LABS**: a single sound with a couple of expressive controls (expression, dynamics, one big knob) is enough for people to make music with. Constraint is the selling point.
- **GRAZE**, a game that writes a new song every day from a date seed: its developer found that key, tempo and chord changes were almost inaudible day to day, and that **timbre and rhythm/motion are what make two days sound different**. So weather should drive timbre, space and motion. It should not drive the musical key.
- **That's No Moon** (a VCV Rack sequencer) dealt one daily sequence to everyone, and its developer hit random-number inconsistencies between instances. Our mapping has to be deterministic: same inputs give the same sound on every machine, in every DAW, on every bounce.
- **Synplant** and **WX** show that "you don't pick, you grow/discover" works commercially, and that people still want a few familiar handles afterwards.

## 2. What nature controls (shared by all three models)

The rule of thumb behind every mapping: **temperature is energy, water is space, wind is motion, light is brightness, pressure is weight, and the moon is a night-time glow.**

| Climate input | Sound | Why |
|---|---|---|
| Temperature | Waveform position (cold Glass → Breath → Reed → String → hot Brass), attack time, modulation speed, saturation | Hot air is fast and harmonically rich; cold is slow, sparse and crystalline |
| Above 32 °C, peaking at 45 °C | Overdrive, bit reduction, fast deep tremolo, snappy attack, short tail | Your "40 °C should be breaking apart" request |
| Below −10 °C | Crystalline octave shimmer, long empty reverb, slower attack | Frozen, alien stillness |
| Humidity | Unison spread, chorus, release length, reverb decay | Moist air smears and sustains |
| Precipitation | Delay amount and feedback, reverb amount, rain-noise bed (snow gives a darker, softer bed) | Rain is literally echoes |
| Humid and raining together | "Drench": reverb and delay are forced high | Your "drenched out" request |
| Wind | Vibrato depth and rate, filter resonance, gust-modulated noise | Instability and movement |
| Gale (14 m/s and up) | Tremolo, strong resonance | Buffeting |
| Sun angle, cloud cover | High-shelf brightness tilt (−10 dB at night to +3 dB at high sun) | Darkness as a shelf, not a filter (your instinct) |
| Moon phase | At night: octave-up shimmer near full moon, more sub near new moon | Moonlight only matters when the sun is down |
| Air pressure | Sub-octave weight | Storms (low pressure) feel heavy |
| Daily seed (date + ~10 km location cell) | Small offset in waveform position, delay rhythm, chorus rate | The lottery part; same for everyone in that place that day |

### Answers to your specific questions

- **Filter cutoff:** user-controlled. Time of day sets *brightness* through a high shelf, and also where the filter "rests" by default. You keep a filter at night; the night is just darker underneath it. The two never fight because they are separate stages.
- **Rain and wet/dry:** the climate sets the *character* of the space (decay length, delay feedback, damping) and a *floor*. On a drenched day the Space macro cannot go fully dry; on a desert day it can go very wet, but the tail stays short and bright. So dry/wet is not fixed. It is a user control inside limits the weather sets.
- **Heat:** warmer means more saturation, faster modulation and a drier, shorter tail (hot and dry). Hot *and* humid (a rainforest) keeps the drive but gets the wet space, because humidity is a separate axis.
- **Day vs night:** brightness/darkness through the shelf, as above, plus the moon glow at night.
- **ADSR:** no four-slider envelope. One **Bloom** macro morphs pluck ↔ swell (attack, release and sustain together). Climate sets where Bloom rests: cold means slow swell, heat means instant pluck.
- **Waveform/wavetable:** locked to nature. It is the strongest identity of the day's sound and the "ticket" you were dealt. Model C lets you reach other waveforms indirectly by asking "what if it were warmer?".

## 3. Three control models

All three share section 2. They differ only in what the 4–5 macros are allowed to do.

### A · Sealed ticket
Nature sets everything. Four macros (Tone, Bloom, Space, Motion) are performance trims of about ±1 octave on the filter and ±25 % elsewhere.

- **For:** strongest concept and story ("today's sound, take it or leave it"). Least to design and explain.
- **Against:** on a mild grey day you get a mild grey sound and can't escape it. Extremes (45 °C breakup) are unusable in a mix with no way to tame them. People will open it, smile, and not use it in a track.
- **Best for:** an art piece, an app that is listened to more than produced with.

### B · Leash *(recommended)*
Each macro starts at nature's position for today (shown as a marker on the knob) and can travel far from it: filter ±3 octaves, Bloom ±3 octaves of envelope time, Space/Motion roughly double or half. The weather limits how far some can go (the drench floor), and a fifth macro, **Intensity**, scales only the extremes, from tamed (0×) to exaggerated (1.6×).

- **For:** keeps "every day is different" (the waveform, the space's character and the brightness are still dealt) while every sound is mixable. Intensity solves the outlier problem: 45 °C can be fully broken for fun or held back for a track. A knob marker showing where nature put it is a strong, simple UI story.
- **Against:** a determined user can steer two different days toward each other. That's acceptable; the waveform and the space's character still differ.
- **Best for:** a plugin people return to in real sessions.

### C · What if
The macros are climate axes, not synth parameters: Warmer (±15 °C), Wetter, Windier, Later (moves the sun), plus Tone as the one direct control. Every move goes through the same nature mapping, so you can only ever land on sounds the matrix approves of.

- **For:** the most original idea, and the most on-brand ("what would today sound like at noon in a heatwave?"). It never produces an untested parameter combination, because every result is a real climate.
- **Against:** less predictable. Turning "Warmer" changes waveform, drive, attack and modulation at once, which is exciting for exploring and frustrating for mixing. It also weakens the "you get what nature gives" story.
- **Best for:** an "explore" mode, or as a secondary page inside B.

### Recommendation
Ship **B** as the instrument. Consider C's "Later" and "Warmer" as a hidden second page for exploring, since it reuses the same mapping for free. Decide after a lab session: rate the deck once per model and compare the matrices.

## 4. Days, saving and DAW recall

- A new plugin instance or app launch deals **today**: it fetches current conditions and stores the climate snapshot plus the seed. It never stores raw synth parameters.
- The plugin state saved in a project is that snapshot and the macro positions. **Opening a project tomorrow gives yesterday's sound**, and offline bounces are deterministic. A small banner offers "Today sounds different. Hear today?" and only swaps on request.
- **Keep this Day** saves to a personal almanac with an automatic name (date, place, conditions, moon) and an optional user name. The almanac only contains days you lived; there is no browsing other people's days. (Possible later feature: send a Day to a friend.)
- Live drift (re-fetching weather every 30 minutes while playing) is optional, off by default in a DAW, and always frozen during offline render.
- Saving the climate snapshot rather than parameters means future mapping improvements can be applied to old Days. A mapping version number is stored so old projects can be pinned to the mapping they were made with.

## 5. Getting weather without a user API key

An API key compiled into a plugin is public: anyone can pull it out of the binary in minutes. The working pattern is a tiny relay you own:

```
Plugin ──HTTPS──▶ your relay (e.g. Cloudflare Worker) ──▶ OpenWeatherMap
                   • holds the API key as a secret
                   • works out location from the request IP (no permission prompt in a DAW)
                   • rounds location to ~10 km and caches each cell for 15–30 min
                   • returns the climate snapshot + daily seed
```

- **Location:** plugins can't use browser geolocation. IP-based location from the relay is good enough at city level (Cloudflare Workers expose request city and coordinates), with an optional "set my city" override for privacy or VPN users.
- **Cost scales with places, not users:** with a 10 km grid and a 20-minute cache, 100,000 users in the same cities cost about the same as 1,000.
- **Moon phase and sun angle** are computed locally from date, time and coordinates. No API needed.
- **Offline:** use the last snapshot, or fall back to a climate-normals simulation (the existing web app already has one per location).
- **Licensing:** OpenWeather's free plan allows commercial use under ODbL with attribution (60 calls/min, 1M/month), but One Call 3.0 adds CC BY-SA terms. For a closed, paid product, check their business licence (Professional and higher) before launch. Open-Meteo needs no key but its free tier is non-commercial only; its paid plans are an alternative.

## 6. JUCE build plan

1. **Port the mapping first.** `natureParams()` and `resolve()` in `lab.html` are pure functions with no audio or UI. Port them 1:1 to a C++ `ClimateMapper` and keep a shared JSON test vector (climate in, parameters out) that both implementations must pass.
2. **DSP voice.** A wavetable oscillator morphing between the five harmonic frames (or a real wavetable later) with two detuned layers, a sub-octave and an octave shimmer. Then waveshaper drive, bit reducer, 24 dB ladder or SVF low-pass, low and high shelves, chorus, vibrato, tremolo, feedback delay, algorithmic reverb (use an FDN, not convolution, so decay can move smoothly) and a limiter.
3. **Loudness.** The lab's static compensation keeps the 24 deck scenarios within about 6 dB (measured). The plugin should add a slow RMS-follower auto-gain (±6 dB, seconds-long) so extremes don't jump in level.
4. **Network.** A `juce::URL` fetch on a background thread at instance creation, never on the audio thread. Parameter changes from new weather are smoothed over 2–5 s.
5. **State.** `getStateInformation` writes the snapshot, seed, macro values and mapping version.
6. **UI.** One screen: the day card (place, conditions, moon), five macros with nature markers, Keep this Day, and the almanac.
7. **Formats.** VST3, AU and Standalone from one JUCE project. AAX later, which needs PACE signing.

## 7. How to run the one-hour tuning session

1. Open the lab and press Play (Chords is the most revealing pattern; try Arpeggio for the delay and envelopes).
2. Work through the **Curated deck** (24 places, including the outliers) with model B. Press 1/2/3 for each, add tags when something is off, and write a note when you know what would fix it. With auto-advance on, a scenario takes 30–60 seconds.
3. Press **Fill coverage gaps** until every cell in the matrix has at least two verdicts.
4. Use **Sweep** on temperature and humidity to find where the sound tips from good to bad.
5. Switch to model A and C for ten minutes each to judge the control philosophy, not the sound.
6. Tell me you're done. I'll read the verdicts, tags and notes and retune the mapping where cells are red, then repeat until the matrix is green.
