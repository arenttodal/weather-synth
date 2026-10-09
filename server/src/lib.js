// Pure helpers for the Atmospheric weather relay. No I/O here, so it is easy to test.

export const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// Round to a ~11 km grid cell. Everyone in a cell shares one cached reading
// (and one daily seed in the plugin), and exact coordinates are never stored.
export function cellOf(lat, lon) {
  const r = (x) => Math.round(x * 10) / 10;
  return { lat: r(lat), lon: r(lon), key: `${r(lat).toFixed(1)},${r(lon).toFixed(1)}` };
}

export function validCoords(lat, lon) {
  return Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180;
}

// OpenWeatherMap reports rain/snow in mm over the last hour; the synth wants 0..1.
// Condition codes lift the floor (a thunderstorm with a dry gauge is still a storm).
export function precipIntensity(w) {
  const mm = (w.rain?.["1h"] ?? 0) + (w.snow?.["1h"] ?? 0);
  let p = clamp(Math.sqrt(mm / 10), 0, 1);
  const id = w.weather?.[0]?.id ?? 800;
  if (id >= 200 && id < 300) p = Math.max(p, 0.85); // thunderstorm
  else if (id >= 300 && id < 400) p = Math.max(p, 0.15); // drizzle
  else if (id >= 502 && id <= 504) p = Math.max(p, 0.8); // heavy rain
  else if (id >= 500 && id < 600) p = Math.max(p, 0.35); // rain
  else if (id >= 600 && id < 700) p = Math.max(p, id >= 602 ? 0.7 : 0.4); // snow
  return +p.toFixed(3);
}

// Normalise an OWM /data/2.5/weather response into the plugin's climate snapshot.
function finiteOrNull(v) {
  return typeof v === "number" && Number.isFinite(v) ? v : null;
}

export function toSnapshot(w, cell) {
  return {
    temp: w.main?.temp ?? 10,
    humidity: w.main?.humidity ?? 70,
    pressure: w.main?.pressure ?? 1013,
    wind: w.wind?.speed ?? 0,
    clouds: clamp((w.clouds?.all ?? 0) / 100, 0, 1),
    precip: precipIntensity(w),
    condition: w.weather?.[0]?.main ?? "Clear",
    description: w.weather?.[0]?.description ?? "",
    // Visual-only extras for the plugin's scene (null = the provider didn't say)
    conditionIds: Array.isArray(w.weather) ? w.weather.map((c) => c?.id).filter((id) => Number.isInteger(id)) : [],
    windDeg: finiteOrNull(w.wind?.deg),
    gust: finiteOrNull(w.wind?.gust),
    visibility: finiteOrNull(w.visibility),
    rain1h: finiteOrNull(w.rain?.["1h"]),
    snow1h: finiteOrNull(w.snow?.["1h"]),
    observedAt: w.dt ?? Math.floor(Date.now() / 1000),
    timezone: w.timezone ?? 0,
    sunrise: w.sys?.sunrise ?? null,
    sunset: w.sys?.sunset ?? null,
    place: {
      name: w.name || "",
      country: w.sys?.country || "",
      lat: cell.lat,
      lon: cell.lon,
    },
  };
}

// First public address in X-Forwarded-For (Railway's edge appends the client).
export function clientIp(headers, socketAddr = "") {
  const xff = String(headers["x-forwarded-for"] || "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  const ip = xff[0] || socketAddr || "";
  return ip.replace(/^::ffff:/, "");
}

export function isPrivateIp(ip) {
  return (
    !ip ||
    ip === "::1" ||
    /^127\./.test(ip) ||
    /^10\./.test(ip) ||
    /^192\.168\./.test(ip) ||
    /^172\.(1[6-9]|2\d|3[01])\./.test(ip) ||
    /^100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])\./.test(ip) ||
    /^f[cd]/i.test(ip) ||
    /^fe80/i.test(ip)
  );
}

// Small TTL cache with a size cap (oldest entries evicted first).
export class TtlCache {
  constructor(ttlMs, max = 5000, now = () => Date.now()) {
    this.ttl = ttlMs;
    this.max = max;
    this.now = now;
    this.map = new Map();
  }
  get(k) {
    const e = this.map.get(k);
    if (!e) return undefined;
    if (this.now() > e.exp) {
      this.map.delete(k);
      return undefined;
    }
    return e.v;
  }
  set(k, v) {
    if (this.map.has(k)) this.map.delete(k);
    this.map.set(k, { v, exp: this.now() + this.ttl });
    while (this.map.size > this.max) this.map.delete(this.map.keys().next().value);
  }
}

// Fixed-window limiter per key.
export class RateLimiter {
  constructor(limit, windowMs, now = () => Date.now()) {
    this.limit = limit;
    this.window = windowMs;
    this.now = now;
    this.hits = new Map();
  }
  allow(k) {
    const t = this.now();
    let e = this.hits.get(k);
    if (!e || t - e.start >= this.window) {
      e = { start: t, n: 0 };
      this.hits.set(k, e);
    }
    e.n++;
    if (this.hits.size > 20000) this.hits.clear();
    return e.n <= this.limit;
  }
}
