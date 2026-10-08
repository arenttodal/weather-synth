// Writes designer/starter-bible.json and plugin/Resources/bible.json: the starting core sounds with boundaries.
// Each parameter's box defaults to a moderate width around its home value;
// a sound overrides the box where its weather should be allowed to go further.
//   node tools/gen-starter-bible.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
// Ranges and scales, read from engine/atmos/Params.h (the single source of truth)
const P = {};
for (const m of fs.readFileSync(path.join(root, "engine/atmos/Params.h"), "utf8").matchAll(
  /\{ "(\w+)", "[^"]*", "[^"]*", ([-\d.]+), ([-\d.]+), ([-\d.]+), Scale::(\w+)/g))
  P[m[1]] = [+m[2], +m[3], +m[4], m[5] === "choice" ? "c" : m[5] === "log" ? "l" : undefined];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
function box(id, home) {
  const [min, max, , kind] = P[id];
  if (kind === "c" || id === "outGain") return [home, home]; // types and level stay fixed
  if (kind === "l") return [clamp(home / 2.2, min, max), clamp(home * 2.2, min, max)];
  const w = (max - min) * 0.2;
  return [clamp(home - w, min, max), clamp(home + w, min, max)];
}
function sound(name, anchor, home, wide = {}) {
  const params = {};
  for (const id of Object.keys(P)) {
    const h = home[id] ?? P[id][2];
    const [lo, hi] = wide[id] ?? box(id, h);
    params[id] = { home: +h.toFixed(4), lo: +Math.min(lo, h).toFixed(4), hi: +Math.max(hi, h).toFixed(4) };
  }
  return { id: name.toLowerCase().replace(/[^a-z]+/g, "-"), name, anchor, params };
}

// Ten analogue starting points (Prophet-5, Memorymoog, OB, Juno lineage). Every sound drifts a little
// (slop), saturates a little (drive) and sits in a real space.
const analogue = { slop: 0.35, drive: 0.15 };
const sounds = [
  sound("Frost Strings", { temp: 0, wet: 0.3, light: 0.7 }, { ...analogue,
    oscAWave: 0, oscBWave: 1, oscBInterval: 1, oscBDetune: 9, mixA: 0.8, mixB: 0.7, mixSub: 0, filtType: 1, cutoff: 2600, resonance: 0.1,
    keyTrack: 0.6, filtEnv: 0.25, fAttack: 0.5, fDecay: 2, fSustain: 0.6, fRelease: 2.5, attack: 0.45, decay: 2, sustain: 0.9, release: 2.2,
    lfoRate: 5, vibDepth: 0.04, movMode: 2, movAmount: 0.45, movA: 0.3, movB: 0.7, spaceType: 1, spaceSend: 0.45, spaceDecay: 5, echoSend: 0.1 }),
  sound("Polar Drone", { temp: -22, wet: 0.19, light: 0 }, { ...analogue,
    oscAWave: 0, oscBWave: 1.6, oscBInterval: 0, oscBDetune: 4, mixA: 0.7, mixB: 0.6, mixSub: 0.5, filtType: 0, cutoff: 380, resonance: 0.35,
    filtEnv: 0.15, fAttack: 2, fDecay: 4, fSustain: 0.5, fRelease: 4, attack: 1.5, decay: 3, sustain: 0.9, release: 5, lfoRate: 0.12, lfoFilter: 0.15,
    movMode: 1, movAmount: 0.25, spaceType: 1, spaceSend: 0.55, spaceDecay: 9, spaceSize: 0.8, kalAmount: 0.3, tilt: -3 }, { spaceDecay: [5, 12] }),
  sound("Fog Pad", { temp: 7, wet: 0.75, light: 0.3 }, { ...analogue,
    oscAWave: 1, oscAPw: 0.3, lfoPwm: 0.45, lfoRate: 0.6, oscBWave: 0, oscBInterval: 3, mixA: 0.6, mixB: 0.45, mixNoise: 0.35, filtType: 2,
    cutoff: 1100, resonance: 0.15, filtEnv: 0.15, fAttack: 1.2, fDecay: 3, fSustain: 0.7, fRelease: 3, attack: 0.9, decay: 2, sustain: 0.85, release: 3.5,
    movMode: 2, movAmount: 0.5, spaceType: 2, spaceSend: 0.5, spaceDecay: 4, echoType: 1, echoSend: 0.2, tilt: -3 }, { mixNoise: [0.15, 0.8] }),
  sound("Drizzle Keys", { temp: 11, wet: 0.55, light: 0.5 }, { ...analogue,
    oscAWave: 0.6, oscAPw: 0.35, lfoPwm: 0.2, lfoRate: 1.2, oscBWave: 1, oscBDetune: 6, mixA: 0.8, mixB: 0.5, filtType: 1, cutoff: 900, resonance: 0.25,
    filtEnv: 0.45, fAttack: 0.002, fDecay: 0.9, fSustain: 0.2, fRelease: 0.6, attack: 0.004, decay: 1.6, sustain: 0.45, release: 0.8, ampVel: 0.6, filtVel: 0.5,
    movMode: 1, movAmount: 0.3, echoType: 0, echoSend: 0.3, echoFeedback: 0.4, echoStereo: 1, spaceType: 0, spaceSend: 0.3, bedLevel: 0.15 }),
  sound("Velvet Brass", { temp: 26, wet: 0.42, light: 0.7 }, { ...analogue, drive: 0.2,
    oscAWave: 0, oscBWave: 1, oscBDetune: 8, mixA: 0.85, mixB: 0.75, mixSub: 0.15, filtType: 1, cutoff: 700, resonance: 0.1, filtEnv: 0.55, keyTrack: 0.5,
    fAttack: 0.09, fDecay: 0.7, fSustain: 0.55, fRelease: 0.4, attack: 0.06, decay: 0.8, sustain: 0.85, release: 0.45, pmEnvA: 0.03, vibDepth: 0.03,
    lfoRate: 5.5, movMode: 2, movAmount: 0.25, spaceType: 2, spaceSend: 0.25, echoSend: 0.08 }),
  sound("Spring Pluck", { temp: 20, wet: 0.3, light: 0.85 }, { ...analogue,
    oscAWave: 0, oscBWave: 2, oscBInterval: 3, mixA: 0.85, mixB: 0.35, filtType: 0, cutoff: 500, resonance: 0.3, filtEnv: 0.7, keyTrack: 0.8, filtVel: 0.7,
    fAttack: 0.001, fDecay: 0.35, fSustain: 0, fRelease: 0.3, attack: 0.001, decay: 1.2, sustain: 0, release: 0.6, ampVel: 0.6,
    echoType: 0, echoSend: 0.35, echoTime: 330, echoStereo: 1, spaceType: 0, spaceSend: 0.25, movMode: 0 }),
  sound("Heat Brass", { temp: 36, wet: 0.1, light: 1 }, { ...analogue, slop: 0.5, drive: 0.35,
    voiceMode: 1, stackDetune: 12, oscAWave: 0, oscBWave: 1, oscBDetune: 10, mixA: 0.9, mixB: 0.8, mixSub: 0.25, filtType: 0, cutoff: 900, resonance: 0.2,
    filtDrive: 0.5, filtEnv: 0.7, fAttack: 0.03, fDecay: 0.5, fSustain: 0.5, fRelease: 0.35, attack: 0.008, decay: 0.5, sustain: 0.8, release: 0.35,
    pmEnvA: 0.05, pmOscB: 0.1, vibDepth: 0.05, movMode: 1, movAmount: 0.2, spaceType: 3, spaceSend: 0.2, echoSend: 0.15, echoAge: 0.6 },
    { drive: [0.15, 0.95], crushBits: [6, 16], crushRate: [0, 0.6], pmOscB: [0, 0.6], slop: [0.3, 1], filtDrive: [0.3, 1] }),
  sound("Thunder Bass", { temp: 16, wet: 0.95, light: 0.15 }, { ...analogue, drive: 0.3,
    voiceMode: 2, stackDetune: 8, oscAWave: 0, oscBWave: 2, oscBInterval: 0, oscBDetune: 3, mixA: 0.85, mixB: 0.7, mixSub: 0.6, filtType: 0, cutoff: 220,
    resonance: 0.35, filtDrive: 0.6, filtEnv: 0.5, keyTrack: 0.4, fAttack: 0.001, fDecay: 0.45, fSustain: 0.25, fRelease: 0.3, attack: 0.002, decay: 1.5,
    sustain: 0.7, release: 0.35, portamento: 0.15, movMode: 0, bedLevel: 0.45, spaceType: 1, spaceSend: 0.25, echoSend: 0.1 },
    { bedLevel: [0.25, 1], resonance: [0.15, 0.7] }),
  sound("Monsoon Kaleidoscope", { temp: 28, wet: 0.9, light: 0.4 }, { ...analogue,
    voiceMode: 1, stackDetune: 14, oscAWave: 0, oscBWave: 1, oscBDetune: 12, mixA: 0.8, mixB: 0.7, filtType: 2, cutoff: 1500, resonance: 0.2, filtEnv: 0.2,
    fAttack: 0.6, fDecay: 2, fSustain: 0.6, fRelease: 3, attack: 0.35, decay: 2, sustain: 0.9, release: 3.5, kalAmount: 0.6, kalSpread: 0.65,
    spaceType: 1, spaceSend: 0.65, spaceDecay: 7, echoType: 1, echoSend: 0.4, echoFeedback: 0.6, bedLevel: 0.35, movMode: 2, movAmount: 0.35 },
    { spaceSend: [0.45, 1], echoFeedback: [0.4, 0.85], kalAmount: [0.4, 1] }),
  sound("Night Bass", { temp: 10, wet: 0.3, light: 0.05 }, { ...analogue,
    oscAWave: 1, oscAPw: 0.5, oscBWave: 0, oscBInterval: 0, mixA: 0.5, mixB: 0.8, mixSub: 0.4, filtType: 0, cutoff: 260, resonance: 0.15, filtDrive: 0.35,
    filtEnv: 0.25, fAttack: 0.002, fDecay: 0.8, fSustain: 0.3, fRelease: 0.4, attack: 0.005, decay: 2, sustain: 0.8, release: 0.4, portamento: 0.1,
    movMode: 1, movAmount: 0.2, spaceType: 0, spaceSend: 0.15, echoSend: 0.05 }),
];

const out = { version: 2, sounds };
fs.writeFileSync(path.join(root, "designer/starter-bible.json"), JSON.stringify(out, null, 1));
fs.mkdirSync(path.join(root, "plugin/Resources"), { recursive: true });
fs.writeFileSync(path.join(root, "plugin/Resources/bible.json"), JSON.stringify(out));
console.log(`${sounds.length} core sounds written`);
