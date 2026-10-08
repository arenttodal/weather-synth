// Writes designer/starter-bible.json: nine starting core sounds with boundaries.
// Each parameter's box defaults to a moderate width around its home value;
// a sound overrides the box where its weather should be allowed to go further.
//   node tools/gen-starter-bible.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
// Mirror of engine/atmos/Params.h (ranges and scales)
const P = {
  srcType: [0, 3, 0, "c"], srcFrame: [0, 1, 0.35], fmRatio: [0.5, 8, 2, "l"], fmIndex: [0, 10, 2.5], fmFeedback: [0, 1, 0],
  sawVoices: [1, 7, 5], sawDetune: [0, 60, 18], pluckDamp: [0, 1, 0.4], pluckBright: [0, 1, 0.6], srcSpread: [0, 40, 6],
  srcSub: [0, 1, 0.2], srcShimmer: [0, 1, 0], srcBreath: [0, 1, 0], attack: [0.002, 4, 0.08, "l"], decay: [0.05, 6, 0.8, "l"],
  sustain: [0, 1, 0.75], release: [0.05, 12, 1.2, "l"], vibDepth: [0, 1, 0.05], vibRate: [0.1, 10, 4.5, "l"], glide: [0, 1, 0.15],
  drive: [0, 1, 0], crushBits: [4, 16, 16], crushRate: [0, 1, 0], filtType: [0, 4, 0, "c"], cutoff: [40, 18000, 2400, "l"],
  resonance: [0, 0.9, 0.12], filtDrive: [0, 1, 0.1], filtEnv: [-1, 1, 0.15], filtEnvDecay: [0.02, 4, 0.6, "l"], tilt: [-12, 6, 0],
  kalAmount: [0, 1, 0], kalFocus: [0, 1, 0.5], kalSpread: [0, 1, 0.5], movMode: [0, 4, 2, "c"], movAmount: [0, 1, 0.3],
  movA: [0, 1, 0.45], movB: [0, 1, 0.6], movC: [0, 1, 0.6], echoType: [0, 1, 0, "c"], echoSend: [0, 1, 0.1], echoTime: [40, 1200, 375, "l"],
  echoFeedback: [0, 1, 0.4], echoTone: [0, 1, 0.5], echoAge: [0, 1, 0.35], echoStereo: [0, 2, 1, "c"], spaceType: [0, 3, 2, "c"],
  spaceSend: [0, 1, 0.3], spaceDecay: [0.2, 12, 2.5, "l"], spaceSize: [0, 1, 0.5], spaceDamping: [0, 1, 0.4], spaceMod: [0, 1, 0.4],
  spacePreDelay: [0, 250, 10], bedLevel: [0, 1, 0], bedColour: [200, 8000, 1800, "l"], outGain: [-24, 6, 0],
};
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

const sounds = [
  sound("Frost Glass", { temp: -12, wet: 0.3, light: 0.6 },
    { srcFrame: 0, srcShimmer: 0.35, srcSub: 0.1, attack: 0.6, release: 3, sustain: 0.8, filtType: 1, cutoff: 3500, kalAmount: 0.25, kalFocus: 0.7,
      movMode: 2, movAmount: 0.2, movA: 0.2, spaceType: 1, spaceSend: 0.45, spaceDecay: 6, echoSend: 0.15, vibDepth: 0.02 }),
  sound("Polar Night", { temp: -22, wet: 0.2, light: 0.0 },
    { srcFrame: 0.14, srcSub: 0.5, srcShimmer: 0.15, attack: 1.2, release: 4, cutoff: 700, filtEnv: 0.05, spaceType: 1, spaceSend: 0.55,
      spaceDecay: 9, spaceSize: 0.8, kalAmount: 0.4, movMode: 1, movAmount: 0.2, tilt: -4 }, { spaceDecay: [5, 12] }),
  sound("Fog Choir", { temp: 8, wet: 0.75, light: 0.3 },
    { srcFrame: 0.86, srcSpread: 12, attack: 0.4, release: 2.5, filtType: 1, cutoff: 900, tilt: -4, kalAmount: 0.55, movMode: 2, movAmount: 0.4,
      spaceType: 2, spaceSend: 0.5, spaceDecay: 4, echoType: 1, echoSend: 0.25, srcBreath: 0.15 }),
  sound("Drizzle Reed", { temp: 10, wet: 0.6, light: 0.5 },
    { srcFrame: 0.43, cutoff: 1800, resonance: 0.2, filtEnv: 0.3, attack: 0.03, decay: 0.6, sustain: 0.6, release: 0.9, movMode: 1, movAmount: 0.3,
      echoType: 0, echoSend: 0.35, echoFeedback: 0.45, echoStereo: 1, spaceType: 0, spaceSend: 0.3, bedLevel: 0.2 }),
  sound("Mild Breath", { temp: 16, wet: 0.35, light: 0.7 },
    { srcFrame: 0.2, attack: 0.05, release: 1, cutoff: 2600, movMode: 2, movAmount: 0.25, spaceType: 2, spaceSend: 0.25, srcBreath: 0.1 }),
  sound("Spring Pluck", { temp: 20, wet: 0.3, light: 0.85 },
    { srcType: 3, pluckDamp: 0.35, pluckBright: 0.6, filtType: 1, cutoff: 6000, attack: 0.002, decay: 1.5, sustain: 0.0, release: 1.2,
      echoSend: 0.3, echoTime: 330, spaceType: 0, spaceSend: 0.25, movMode: 0, srcSub: 0.1 }),
  sound("Monsoon Kaleidoscope", { temp: 28, wet: 0.9, light: 0.4 },
    { srcType: 2, sawVoices: 5, sawDetune: 14, cutoff: 1400, kalAmount: 0.7, kalSpread: 0.65, spaceType: 1, spaceSend: 0.7, spaceDecay: 7,
      echoType: 1, echoSend: 0.4, echoFeedback: 0.6, bedLevel: 0.35, movMode: 2, movAmount: 0.35, attack: 0.25, release: 3 },
    { spaceSend: [0.45, 1], echoFeedback: [0.4, 0.85], kalAmount: [0.45, 1] }),
  sound("Heat Brass", { temp: 38, wet: 0.1, light: 1.0 },
    { srcFrame: 1.0, drive: 0.35, cutoff: 5000, filtEnv: 0.35, attack: 0.01, decay: 0.4, sustain: 0.65, release: 0.5, movMode: 3, movAmount: 0.3,
      spaceType: 3, spaceSend: 0.2, echoSend: 0.2, echoAge: 0.6, vibDepth: 0.1, srcSpread: 10 },
    { drive: [0.15, 0.9], crushBits: [6, 16], crushRate: [0, 0.6], movAmount: [0.15, 0.9], movA: [0.4, 1] }),
  sound("Storm FM", { temp: 15, wet: 0.95, light: 0.2 },
    { srcType: 1, fmRatio: 2, fmIndex: 4, fmFeedback: 0.2, cutoff: 1600, resonance: 0.35, movMode: 1, movAmount: 0.4, spaceType: 1,
      spaceSend: 0.6, bedLevel: 0.5, echoType: 1, echoSend: 0.4, attack: 0.02, release: 1.5 },
    { bedLevel: [0.25, 1], resonance: [0.15, 0.7], movAmount: [0.2, 0.9] }),
];

const out = { version: 1, sounds };
fs.writeFileSync(path.join(root, "designer/starter-bible.json"), JSON.stringify(out, null, 1));
fs.mkdirSync(path.join(root, "plugin/Resources"), { recursive: true });
fs.writeFileSync(path.join(root, "plugin/Resources/bible.json"), JSON.stringify(out));
console.log(`${sounds.length} core sounds written`);
