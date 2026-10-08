// HTTP handler for the relay. Dependencies are injected so tests can run offline.
import { cellOf, validCoords, toSnapshot, clientIp, isPrivateIp, TtlCache, RateLimiter } from "./lib.js";

const OWM = "https://api.openweathermap.org";

export function createApp({ owmKey, ipinfoToken = "", fetchImpl = fetch, now = () => Date.now(), log = () => {} }) {
  const weatherCache = new TtlCache(20 * 60 * 1000, 5000, now);
  const ipCache = new TtlCache(6 * 60 * 60 * 1000, 20000, now);
  const geoCache = new TtlCache(24 * 60 * 60 * 1000, 2000, now);
  const limiter = new RateLimiter(60, 5 * 60 * 1000, now);

  async function getJson(url, timeoutMs = 6000) {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), timeoutMs);
    try {
      const r = await fetchImpl(url, { signal: ctl.signal, headers: { "user-agent": "atmospheric-relay/1" } });
      if (!r.ok) throw Object.assign(new Error(`upstream ${r.status}`), { status: r.status });
      return await r.json();
    } finally {
      clearTimeout(t);
    }
  }

  async function weatherAt(lat, lon) {
    const cell = cellOf(lat, lon);
    const hit = weatherCache.get(cell.key);
    if (hit) return { ...hit, cached: true };
    const w = await getJson(`${OWM}/data/2.5/weather?lat=${cell.lat}&lon=${cell.lon}&units=metric&appid=${encodeURIComponent(owmKey)}`);
    const snap = toSnapshot(w, cell);
    weatherCache.set(cell.key, snap);
    return { ...snap, cached: false };
  }

  // City-level location from the caller's IP. Never stored beyond the cache.
  async function locate(ip) {
    if (isPrivateIp(ip)) return null;
    const hit = ipCache.get(ip);
    if (hit !== undefined) return hit;
    let loc = null;
    try {
      if (ipinfoToken) {
        const j = await getJson(`https://ipinfo.io/${encodeURIComponent(ip)}/json?token=${encodeURIComponent(ipinfoToken)}`, 4000);
        const [la, lo] = String(j.loc || "").split(",").map(Number);
        if (validCoords(la, lo)) loc = { lat: la, lon: lo, city: j.city || "", country: j.country || "" };
      } else {
        // ip-api.com free tier: HTTP only, fine for a free non-commercial app
        const j = await getJson(`http://ip-api.com/json/${encodeURIComponent(ip)}?fields=status,lat,lon,city,countryCode`, 4000);
        if (j.status === "success" && validCoords(j.lat, j.lon)) loc = { lat: j.lat, lon: j.lon, city: j.city || "", country: j.countryCode || "" };
      }
    } catch (e) {
      log("locate failed", e.message);
    }
    ipCache.set(ip, loc);
    return loc;
  }

  async function geocode(q) {
    const k = q.toLowerCase();
    const hit = geoCache.get(k);
    if (hit) return hit;
    const j = await getJson(`${OWM}/geo/1.0/direct?q=${encodeURIComponent(q)}&limit=5&appid=${encodeURIComponent(owmKey)}`);
    const out = (Array.isArray(j) ? j : []).map((g) => ({ name: g.name, state: g.state || "", country: g.country, lat: +g.lat.toFixed(3), lon: +g.lon.toFixed(3) }));
    geoCache.set(k, out);
    return out;
  }

  function send(res, status, body) {
    const data = JSON.stringify(body);
    res.writeHead(status, {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      "access-control-allow-origin": "*",
    });
    res.end(data);
  }

  return async function handle(req, res) {
    const url = new URL(req.url, "http://relay");
    if (req.method === "OPTIONS") return send(res, 204, {});
    if (url.pathname === "/health") return send(res, 200, { ok: true, keyConfigured: !!owmKey });
    if (req.method !== "GET") return send(res, 405, { error: "method_not_allowed" });

    const ip = clientIp(req.headers, req.socket?.remoteAddress);
    if (!limiter.allow(ip)) return send(res, 429, { error: "rate_limited", message: "Too many requests, try again in a few minutes" });
    if (!owmKey) return send(res, 503, { error: "not_configured", message: "OWM_API_KEY is not set on the server" });

    try {
      if (url.pathname === "/v1/sky") {
        let lat = parseFloat(url.searchParams.get("lat"));
        let lon = parseFloat(url.searchParams.get("lon"));
        let located = "coords";
        let city = null;
        if (!validCoords(lat, lon)) {
          const loc = await locate(ip);
          if (!loc) return send(res, 404, { error: "location_unknown", message: "Couldn't place you from your connection. Set a city in the plugin." });
          ({ lat, lon } = loc);
          located = "ip";
          city = loc;
        }
        const snap = await weatherAt(lat, lon);
        if (city && !snap.place.name) snap.place.name = city.city;
        return send(res, 200, { v: 1, located, ...snap });
      }
      if (url.pathname === "/v1/geocode") {
        const q = (url.searchParams.get("q") || "").trim().slice(0, 80);
        if (q.length < 2) return send(res, 400, { error: "bad_query" });
        return send(res, 200, { v: 1, results: await geocode(q) });
      }
      return send(res, 404, { error: "not_found" });
    } catch (e) {
      log("request failed", url.pathname, e.message);
      const status = e.status === 401 ? 502 : e.name === "AbortError" ? 504 : 502;
      return send(res, status, { error: "upstream_failed", message: e.status === 401 ? "Weather provider rejected the server's key" : "Weather provider unavailable" });
    }
  };
}
