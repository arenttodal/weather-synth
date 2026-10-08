// Generates plugin/tests/vectors.json from the mapping code inside lab.html.
// The C++ ClimateMapper must reproduce these numbers, so the lab stays the
// single source of truth while the sound is being tuned.
//   node tools/gen-vectors.mjs
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const html = fs.readFileSync(path.join(root, "lab.html"), "utf8");

const start = html.indexOf('"use strict";');
const end = html.indexOf("  const TEMP_BANDS");
if (start < 0 || end < 0) throw new Error("mapping markers not found in lab.html");
const code = html.slice(start, end);
const api = new Function(`${code}; return { natureParams, resolve, forces, DECK };`)();

const MACRO_SETS = [
  {},
  { tone: 1, bloom: 1, space: 1, motion: 1, intensity: 1 },
  { tone: -1, bloom: -1, space: -1, motion: -1, intensity: -1 },
  { tone: 0.35, bloom: -0.6, space: 0.8, motion: -0.25, intensity: 0.5 },
];

const cases = [];
for (const [name, place, c] of api.DECK) {
  for (const seed of [1, 777, 4242424242]) {
    for (const macros of MACRO_SETS) {
      const climate = { ...c, seed };
      cases.push({ name, climate, macros, params: api.resolve("B", climate, macros).final });
    }
  }
}
const out = path.join(root, "plugin", "tests", "vectors.json");
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, JSON.stringify({ model: "B", cases }, null, 0));
console.log(`wrote ${cases.length} cases to ${path.relative(root, out)}`);
