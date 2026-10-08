// Builds plugin/Resources/land110m.bin (Natural Earth 1:110m land, public domain)
// for the globe. One-off: npm i world-atlas@2.0.2 topojson-client@3.1.0, then
//   node tools/gen-land.mjs <path to node_modules>
// Format (little-endian): int32 ringCount, then per ring int32 n and n pairs
// of int16 (lon*100, lat*100).
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const modules = path.resolve(process.argv[2] || "node_modules");
const require = createRequire(path.join(modules, "noop.js"));
const topo = require("topojson-client");
const land = JSON.parse(fs.readFileSync(path.join(modules, "world-atlas/land-110m.json"), "utf8"));
const geo = topo.feature(land, land.objects.land);

const rings = [];
for (const f of geo.features) {
  const polys = f.geometry.type === "Polygon" ? [f.geometry.coordinates] : f.geometry.coordinates;
  for (const poly of polys) for (const ring of poly) rings.push(ring);
}
let size = 4;
for (const r of rings) size += 4 + r.length * 4;
const buf = Buffer.alloc(size);
let o = buf.writeInt32LE(rings.length, 0);
for (const r of rings) {
  o = buf.writeInt32LE(r.length, o);
  for (const [lon, lat] of r) {
    o = buf.writeInt16LE(Math.round(lon * 100), o);
    o = buf.writeInt16LE(Math.round(lat * 100), o);
  }
}
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
fs.writeFileSync(path.join(root, "plugin/Resources/land110m.bin"), buf);
console.log(`${rings.length} rings, ${rings.reduce((a, r) => a + r.length, 0)} points, ${size} bytes`);
