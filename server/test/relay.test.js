import test from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { createApp } from "../src/app.js";
import { precipIntensity, cellOf, clientIp, isPrivateIp, TtlCache, RateLimiter } from "../src/lib.js";

const owmBody = {
  weather: [{ id: 501, main: "Rain", description: "moderate rain" }, { id: 701, main: "Mist", description: "mist" }],
  visibility: 4000,
  main: { temp: 6.2, humidity: 88, pressure: 1004 },
  wind: { speed: 7.5, deg: 250, gust: 12.1 },
  clouds: { all: 90 },
  rain: { "1h": 2.5 },
  dt: 1791470000,
  timezone: 7200,
  sys: { country: "NO", sunrise: 1791440000, sunset: 1791478000 },
  name: "Trondheim",
};

function fakeFetch(calls) {
  return async (url) => {
    calls.push(url);
    if (url.includes("ip-api.com")) return { ok: true, json: async () => ({ status: "success", lat: 63.43, lon: 10.39, city: "Trondheim", countryCode: "NO" }) };
    if (url.includes("/geo/1.0/direct")) return { ok: true, json: async () => [{ name: "Tokyo", country: "JP", lat: 35.6828, lon: 139.759 }] };
    if (url.includes("appid=bad")) return { ok: false, status: 401, json: async () => ({}) };
    return { ok: true, json: async () => owmBody };
  };
}

async function withServer(opts, fn) {
  const calls = [];
  const srv = http.createServer(createApp({ owmKey: "k", fetchImpl: fakeFetch(calls), ...opts }));
  await new Promise((r) => srv.listen(0, r));
  const base = `http://127.0.0.1:${srv.address().port}`;
  try {
    await fn(base, calls);
  } finally {
    srv.close();
  }
}
const get = (base, path, headers = {}) => fetch(base + path, { headers }).then(async (r) => ({ status: r.status, body: await r.json() }));

test("precip intensity maps mm/h and condition codes", () => {
  assert.equal(precipIntensity({ weather: [{ id: 800 }] }), 0);
  assert.equal(precipIntensity({ weather: [{ id: 211 }] }), 0.85);
  assert.ok(precipIntensity({ weather: [{ id: 500 }], rain: { "1h": 10 } }) === 1);
  assert.ok(precipIntensity({ weather: [{ id: 300 }] }) >= 0.15);
  assert.ok(precipIntensity({ weather: [{ id: 601 }], snow: { "1h": 0.2 } }) >= 0.4);
});

test("cells round to 0.1 degree", () => {
  assert.deepEqual(cellOf(63.4305, 10.3951), { lat: 63.4, lon: 10.4, key: "63.4,10.4" });
});

test("client IP takes the first forwarded address", () => {
  assert.equal(clientIp({ "x-forwarded-for": "81.2.3.4, 10.0.0.1" }), "81.2.3.4");
  assert.equal(clientIp({}, "::ffff:127.0.0.1"), "127.0.0.1");
  assert.ok(isPrivateIp("192.168.1.2") && isPrivateIp("100.64.0.3") && !isPrivateIp("81.2.3.4"));
});

test("cache expires and limiter resets per window", () => {
  let t = 0;
  const c = new TtlCache(100, 2, () => t);
  c.set("a", 1);
  c.set("b", 2);
  c.set("c", 3);
  assert.equal(c.get("a"), undefined); // evicted by size cap
  t = 150;
  assert.equal(c.get("b"), undefined); // expired
  const l = new RateLimiter(2, 1000, () => t);
  assert.ok(l.allow("x") && l.allow("x") && !l.allow("x"));
  t = 2000;
  assert.ok(l.allow("x"));
});

test("/v1/sky by coordinates returns a snapshot and caches per cell", async () => {
  await withServer({}, async (base, calls) => {
    const a = await get(base, "/v1/sky?lat=63.4305&lon=10.3951");
    assert.equal(a.status, 200);
    assert.equal(a.body.located, "coords");
    assert.equal(a.body.temp, 6.2);
    assert.equal(a.body.precip, 0.5);
    assert.equal(a.body.clouds, 0.9);
    assert.equal(a.body.place.name, "Trondheim");
    assert.equal(a.body.place.lat, 63.4);
    assert.deepEqual(a.body.conditionIds, [501, 701]);
    assert.equal(a.body.windDeg, 250);
    assert.equal(a.body.gust, 12.1);
    assert.equal(a.body.visibility, 4000);
    assert.equal(a.body.rain1h, 2.5);
    assert.equal(a.body.snow1h, null);
    const b = await get(base, "/v1/sky?lat=63.41&lon=10.42");
    assert.equal(b.body.cached, true);
    assert.equal(calls.filter((u) => u.includes("/data/2.5/weather")).length, 1);
    assert.ok(!JSON.stringify(a.body).includes("appid"), "never leaks the key");
  });
});

test("/v1/sky without coordinates locates by IP", async () => {
  await withServer({}, async (base, calls) => {
    const r = await get(base, "/v1/sky", { "x-forwarded-for": "81.2.3.4" });
    assert.equal(r.status, 200);
    assert.equal(r.body.located, "ip");
    assert.ok(calls.some((u) => u.includes("ip-api.com/json/81.2.3.4")));
  });
});

test("/v1/sky from a private IP asks for a city", async () => {
  await withServer({}, async (base) => {
    const r = await get(base, "/v1/sky");
    assert.equal(r.status, 404);
    assert.equal(r.body.error, "location_unknown");
  });
});

test("/v1/geocode returns trimmed results", async () => {
  await withServer({}, async (base) => {
    const r = await get(base, "/v1/geocode?q=Tokyo");
    assert.equal(r.status, 200);
    assert.deepEqual(r.body.results[0], { name: "Tokyo", state: "", country: "JP", lat: 35.683, lon: 139.759 });
  });
});

test("missing key answers 503, bad key 502, health reports config", async () => {
  await withServer({ owmKey: "" }, async (base) => {
    assert.equal((await get(base, "/v1/sky?lat=1&lon=1")).status, 503);
    assert.deepEqual((await get(base, "/health")).body, { ok: true, keyConfigured: false });
  });
  await withServer({ owmKey: "bad" }, async (base) => {
    const r = await get(base, "/v1/sky?lat=1&lon=1");
    assert.equal(r.status, 502);
    assert.match(r.body.message, /rejected/);
  });
});

test("rate limit kicks in per IP", async () => {
  await withServer({}, async (base) => {
    let last;
    for (let i = 0; i < 61; i++) last = await get(base, "/v1/sky?lat=1&lon=1", { "x-forwarded-for": "81.2.3.9" });
    assert.equal(last.status, 429);
    assert.equal((await get(base, "/v1/sky?lat=1&lon=1", { "x-forwarded-for": "81.2.3.10" })).status, 200);
  });
});

test("serves the sound designer and refuses paths outside it", async () => {
  await withServer({ owmKey: "" }, async (base) => {
    const r = await fetch(base + "/designer/");
    assert.equal(r.status, 200);
    assert.match(r.headers.get("content-type"), /text\/html/);
    assert.match(await r.text(), /^<!doctype html>.*Sound Designer/s);
    const w = await fetch(base + "/designer/atmos.wasm");
    assert.equal(w.headers.get("content-type"), "application/wasm");
    assert.equal((await fetch(base + "/designer/..%2F..%2Fpackage.json")).status, 404);
    assert.equal((await fetch(base + "/designer", { redirect: "manual" })).status, 301);
  });
});
