// AtmosTests: run with no arguments. Exit code 0 means every check passed.
//   AtmosTests --snapshot out.png   also renders the editor to an image
#include "Astro.h"
#include "Bible.h"
#include "ClimateMapper.h"
#include "Core.h"
#include "../Source/BibleFile.h"
#include "../Source/Day.h"
#include "../Source/Globe.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Storage.h"
#include "../Source/WeatherClient.h"
#include "../Source/gui/SceneModel.h"
#include "Sky.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <set>

using namespace atmos;

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                     \
    do                                                       \
    {                                                        \
        ++checks;                                            \
        if (! (cond))                                        \
        {                                                    \
            ++failures;                                      \
            std::printf ("  FAIL %s:%d  ", __FILE__, __LINE__); \
            std::printf (__VA_ARGS__);                       \
            std::printf ("\n");                              \
        }                                                    \
    } while (0)

static double param (const Params& p, const juce::String& k)
{
    static const std::map<juce::String, size_t> idx = {
        { "frame", 0 }, { "spread", 1 }, { "sub", 2 }, { "shimmer", 3 }, { "attack", 4 }, { "decay", 5 }, { "sustain", 6 }, { "release", 7 },
        { "drive", 8 }, { "bits", 9 }, { "cutoff", 10 }, { "q", 11 }, { "tilt", 12 }, { "chorusDepth", 13 }, { "chorusWet", 14 },
        { "chorusRate", 15 }, { "vibDepth", 16 }, { "vibRate", 17 }, { "tremDepth", 18 }, { "tremRate", 19 }, { "delayTime", 20 },
        { "delayFb", 21 }, { "delayWet", 22 }, { "verbDecay", 23 }, { "verbWet", 24 }, { "noise", 25 }, { "noiseFreq", 26 }, { "trim", 27 }
    };
    static_assert (sizeof (Params) == 28 * sizeof (double), "Params layout changed: update the test index");
    return reinterpret_cast<const double*> (&p)[idx.at (k)];
}

static Climate climateFrom (const juce::var& c)
{
    Climate x;
    x.temp = c["temp"];
    x.humidity = c["humidity"];
    x.precip = c["precip"];
    x.wind = c["wind"];
    x.clouds = c["clouds"];
    x.sun = c["sun"];
    x.moon = c["moon"];
    x.pressure = c["pressure"];
    x.seed = (uint32_t) (juce::int64) c["seed"];
    return x;
}

static juce::var loadVectors()
{
    return juce::JSON::parse (juce::File (ATMOS_TEST_VECTORS).loadFileAsString());
}

static void testMapperParity()
{
    std::printf ("Mapping matches lab.html\n");
    const auto v = loadVectors();
    auto* cases = v["cases"].getArray();
    CHECK (cases != nullptr && cases->size() > 100, "vectors.json missing");
    if (cases == nullptr) return;
    double worst = 0;
    juce::String worstWhere;
    for (auto& cs : *cases)
    {
        const Climate c = climateFrom (cs["climate"]);
        Macros m;
        const auto mv = cs["macros"];
        m.tone = mv.hasProperty ("tone") ? (double) mv["tone"] : 0;
        m.bloom = mv.hasProperty ("bloom") ? (double) mv["bloom"] : 0;
        m.space = mv.hasProperty ("space") ? (double) mv["space"] : 0;
        m.motion = mv.hasProperty ("motion") ? (double) mv["motion"] : 0;
        m.intensity = mv.hasProperty ("intensity") ? (double) mv["intensity"] : 0;
        const Params P = resolveLeash (c, m);
        auto* expected = cs["params"].getDynamicObject();
        for (auto& prop : expected->getProperties())
        {
            const double want = prop.value, got = param (P, prop.name.toString());
            const double err = std::abs (got - want) / std::max (1.0, std::abs (want));
            if (err > worst)
            {
                worst = err;
                worstWhere = cs["name"].toString() + " / " + prop.name.toString();
            }
        }
    }
    CHECK (worst < 1e-9, "worst relative error %.3g at %s", worst, worstWhere.toRawUTF8());
    std::printf ("  %d cases, worst relative error %.2g\n", cases->size(), worst);
}

static int64_t utc (int y, int mo, int d, int h, int mi)
{
    return juce::Time (y, mo - 1, d, h, mi, 0, 0, false).toMilliseconds() / 1000;
}

static void testAstro()
{
    std::printf ("Sun, moon and seeds\n");
    const double noonEquinox = sunElevation (0, 0, utc (2024, 3, 20, 12, 7));
    CHECK (std::abs (noonEquinox - 89.5) < 1.5, "equator equinox noon elevation %.2f", noonEquinox);
    const double solstice = sunElevation (0, 0, utc (2024, 6, 21, 12, 2));
    CHECK (std::abs (solstice - 66.56) < 0.8, "equator June solstice noon %.2f", solstice);
    const double trondheimMidnight = sunElevation (63.43, 10.39, utc (2024, 12, 21, 23, 0));
    CHECK (trondheimMidnight < -40, "Trondheim midwinter midnight %.2f", trondheimMidnight);
    const double tromsoMidsummerMidnight = sunElevation (69.65, 18.96, utc (2024, 6, 21, 22, 45));
    CHECK (tromsoMidsummerMidnight > 0, "midnight sun in Tromsø %.2f", tromsoMidsummerMidnight);

    const double newMoon = moonPhase (utc (2024, 4, 8, 18, 21));
    CHECK (newMoon < 0.02 || newMoon > 0.98, "2024-04-08 new moon phase %.3f", newMoon);
    const double fullMoon = moonPhase (utc (2024, 4, 23, 23, 49));
    CHECK (std::abs (fullMoon - 0.5) < 0.03, "2024-04-23 full moon phase %.3f", fullMoon);

    CHECK (localDate (utc (2026, 10, 8, 23, 30), 10.4, 7200, true) == "2026-10-09", "local date rolls over with offset");
    CHECK (localDate (utc (2026, 10, 8, 23, 30), -74, 0, false) == "2026-10-08", "longitude-estimated date");
    const auto s1 = daySeed ("2026-10-08", 63.43, 10.39), s2 = daySeed ("2026-10-08", 63.41, 10.42), s3 = daySeed ("2026-10-09", 63.43, 10.39);
    CHECK (s1 == s2, "same cell same seed");
    CHECK (s1 != s3, "next day new seed");
    CHECK (daySeed ("2026-10-08", 35.68, 139.76) != s1, "other city other seed");

    // The offline estimate must look like weather everywhere
    bool sane = true;
    for (int lat = -85; lat <= 85; lat += 17)
        for (int lon = -180; lon < 180; lon += 45)
            for (int h = 0; h < 24 * 365; h += 997)
            {
                const auto w = simulateWeather (lat, lon, utc (2026, 1, 1, 0, 0) + h * 3600);
                sane &= w.temp > -70 && w.temp < 55 && w.humidity >= 5 && w.humidity <= 100 && w.precip >= 0 && w.precip <= 1 && w.wind >= 0
                        && w.wind < 40 && std::isfinite (w.pressure);
            }
    CHECK (sane, "simulated weather out of range");
    const auto sahara = simulateWeather (23, 5, utc (2026, 7, 15, 13, 0));
    const auto polar = simulateWeather (-78, 166, utc (2026, 7, 15, 13, 0));
    CHECK (sahara.temp > polar.temp + 30, "Sahara July %.1f vs McMurdo July %.1f", sahara.temp, polar.temp);
}

static void testDayAndStorage()
{
    std::printf ("Days and the almanac\n");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atmos-tests-" + juce::String (juce::Random::getSystemRandom().nextInt()));
    Storage::setFolderOverride (dir);

    Day d = estimateDay (63.43, 10.39, "Trondheim", utc (2026, 10, 8, 18, 0));
    d.country = "NO";
    d.utcOffset = 7200;
    d.utcOffsetKnown = true;
    const Day back = Day::fromVar (juce::JSON::parse (juce::JSON::toString (d.toVar())));
    CHECK (back.placeName == d.placeName && back.observedAt == d.observedAt && back.temp == d.temp && back.utcOffset == d.utcOffset,
           "Day JSON round trip");
    const Climate c1 = d.climate(), c2 = back.climate();
    CHECK (c1.seed == c2.seed && c1.sun == c2.sun && c1.moon == c2.moon, "climate identical after round trip");
    CHECK (d.autoTitle().startsWith (ATMOS_U8 ("2026-10-08 · Trondheim")), "auto title: %s", d.autoTitle().toRawUTF8());

    CHECK (Storage::loadDays().isEmpty(), "fresh almanac is empty");
    Day k = d;
    k.name = "First frost";
    CHECK (Storage::saveDay (k), "save a Day");
    Day k2 = estimateDay (35.68, 139.76, "Tokyo", utc (2026, 10, 9, 3, 0));
    CHECK (Storage::saveDay (k2), "save a second Day");
    auto days = Storage::loadDays();
    CHECK (days.size() == 2 && days[0].placeName == "Tokyo" && days[1].name == "First frost", "almanac sorted newest first");
    CHECK (Storage::deleteDay (days[0].id) && Storage::loadDays().size() == 1, "delete a Day");

    Storage::setRelayUrl ("https://example.up.railway.app/");
    CHECK (Storage::relayUrl() == "https://example.up.railway.app", "relay override trims slash: %s", Storage::relayUrl().toRawUTF8());
    Storage::Home h;
    h.name = "Bergen";
    h.lat = 60.39;
    h.lon = 5.32;
    h.set = true;
    Storage::setHome (h);
    CHECK (Storage::home().set && Storage::home().name == "Bergen", "home place saved");
    Storage::setHome ({});
    CHECK (! Storage::home().set, "home place cleared");

    // A relay reading parses into a Day
    auto json = juce::JSON::parse (R"({"v":1,"temp":-3.5,"humidity":91,"pressure":998,"wind":12.5,"clouds":1,"precip":0.6,
        "condition":"Snow","timezone":3600,"place":{"name":"Oslo","country":"NO","lat":59.9,"lon":10.8}})");
    auto r = WeatherClient::parseSky (json, utc (2026, 1, 15, 9, 0));
    CHECK (r.ok && r.day.placeName == "Oslo" && r.day.conditionText() == "Heavy snow" && r.day.source == "live", "parse relay reply");
    CHECK (! WeatherClient::parseSky (juce::JSON::parse ("{\"error\":\"x\"}"), 0).ok, "reject incomplete reply");

    Storage::setFolderOverride (dir); // keep using the temp folder for the rest of the run
}

struct RenderStats
{
    double peak = 0, rmsDb = -200, tailDb = -200, midTailDb = -200;
    bool finite = true;
};

// Plays a chord, a run and a low note through the engine, sample-accurately, and measures the result
static RenderStats renderPatch (const Patch& patch, double sr = 48000, int block = 256)
{
    Core e;
    e.prepare (sr, block);
    e.setPatch (patch);
    struct Ev { double t; int note; float vel; };
    std::vector<Ev> evs;
    const int chord[] = { 48, 55, 62, 64 }; // C3 G3 D4 E4
    for (int n : chord)
        evs.push_back ({ 0.05, n, 0.8f }), evs.push_back ({ 2.0, n, 0 });
    for (int i = 0; i < 12; ++i)
        evs.push_back ({ 2.3 + i * 0.16, 60 + (i * 7) % 24, 0.7f }), evs.push_back ({ 2.42 + i * 0.16, 60 + (i * 7) % 24, 0 });
    evs.push_back ({ 4.5, 36, 0.86f });
    evs.push_back ({ 5.2, 36, 0 });
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t; });

    std::vector<float> L ((size_t) block), R ((size_t) block);
    RenderStats st;
    double sum = 0, tail = 0, mid = 0;
    long n = 0, nt = 0, nm = 0;
    const long total = (long) (sr * 22);
    size_t next = 0;
    for (long pos = 0; pos < total;)
    {
        // Fire due events, then render up to the next one (or a block)
        while (next < evs.size() && (long) (evs[next].t * sr) <= pos)
        {
            const auto& ev = evs[next++];
            if (ev.vel > 0) e.noteOn (ev.note, ev.vel);
            else e.noteOff (ev.note);
        }
        long len = std::min<long> (block, total - pos);
        if (next < evs.size()) len = std::max<long> (1, std::min<long> (len, (long) (evs[next].t * sr) - pos));
        e.process (L.data(), R.data(), (int) len);
        for (long i = 0; i < len; ++i)
            for (const float* d : { L.data(), R.data() })
            {
                const double x = d[i];
                if (! std::isfinite (x)) st.finite = false;
                st.peak = std::max (st.peak, std::abs (x));
                const double t = (pos + i) / sr;
                if (t < 6) sum += x * x, ++n;
                if (t > 21) tail += x * x, ++nt;
                if (t > 12 && t < 13) mid += x * x, ++nm;
            }
        pos += len;
    }
    st.rmsDb = 10 * std::log10 (sum / std::max (1L, n) + 1e-20);
    st.tailDb = 10 * std::log10 (tail / std::max (1L, nt) + 1e-20);
    st.midTailDb = 10 * std::log10 (mid / std::max (1L, nm) + 1e-20);
    return st;
}

static std::vector<CoreSound> builtInBible()
{
    juce::String from;
    auto b = loadBible (from);
    return b;
}

static void testBible()
{
    std::printf ("The sound bible keeps every sound inside its boundaries\n");
    const auto bible = builtInBible();
    CHECK (bible.size() >= 6, "built-in bible has %zu core sounds", bible.size());
    for (const auto& s : bible)
        for (int p = 0; p < kNumParams; ++p)
        {
            const auto& info = paramInfo (p);
            CHECK (s.lo[p] <= s.home[p] && s.home[p] <= s.hi[p] && s.lo[p] >= info.min && s.hi[p] <= info.max, "%s.%s: %g <= %g <= %g in [%g, %g]",
                   s.name.c_str(), info.id, s.lo[p], s.home[p], s.hi[p], info.min, info.max);
        }

    // JSON round trip is exact (Days and projects carry their sound this way)
    for (const auto& s : bible)
    {
        bool ok = false;
        const auto back = soundFromVar (juce::JSON::parse (juce::JSON::toString (soundToVar (s))), ok);
        bool same = ok && back.name == s.name && back.anchorTemp == s.anchorTemp && back.anchorWet == s.anchorWet && back.anchorLight == s.anchorLight;
        for (int p = 0; p < kNumParams; ++p)
            same = same && back.home[p] == s.home[p] && back.lo[p] == s.lo[p] && back.hi[p] == s.hi[p];
        CHECK (same, "%s survives a JSON round trip", s.name.c_str());
    }

    // placeInBounds: 0 is home, +-1 the edges, anything beyond is clamped
    const auto& s0 = bible[0];
    for (int p = 0; p < kNumParams; ++p)
    {
        if (paramInfo (p).scale == Scale::choice) continue;
        CHECK (std::abs (placeInBounds (p, s0.home[p], s0.lo[p], s0.hi[p], 0) - s0.home[p]) < 1e-9, "u=0 is home (%s)", paramInfo (p).id);
        CHECK (std::abs (placeInBounds (p, s0.home[p], s0.lo[p], s0.hi[p], 1) - s0.hi[p]) < 1e-6 * (1 + std::abs (s0.hi[p])), "u=1 is hi (%s)", paramInfo (p).id);
        CHECK (std::abs (placeInBounds (p, s0.home[p], s0.lo[p], s0.hi[p], -5) - s0.lo[p]) < 1e-6 * (1 + std::abs (s0.lo[p])), "u=-5 is lo (%s)", paramInfo (p).id);
    }

    // Any weather, any macros: never outside the box, choice parameters never move
    juce::Random rng (7);
    std::set<int> chosen;
    int outside = 0;
    for (int i = 0; i < 4000; ++i)
    {
        Climate c;
        c.temp = -45 + rng.nextDouble() * 95;
        c.humidity = rng.nextDouble() * 100;
        c.precip = rng.nextDouble();
        c.wind = rng.nextDouble() * 40;
        c.clouds = rng.nextDouble();
        c.sun = -90 + rng.nextDouble() * 180;
        c.moon = rng.nextDouble();
        c.pressure = 950 + rng.nextDouble() * 100;
        c.seed = (uint32_t) rng.nextInt();
        Macros m { rng.nextDouble() * 2 - 1, rng.nextDouble() * 2 - 1, rng.nextDouble() * 2 - 1, rng.nextDouble() * 2 - 1, rng.nextDouble() * 2 - 1 };
        const int k = chooseCoreSound (bible, c, c.precip);
        chosen.insert (k);
        const auto& s = bible[(size_t) k];
        const Patch pt = resolvePatch (s, c, c.precip, m);
        for (int p = 0; p < kNumParams; ++p)
        {
            const double tol = 1e-9 * (1 + std::abs (s.hi[p]));
            if (pt[p] < s.lo[p] - tol || pt[p] > s.hi[p] + tol || ! std::isfinite (pt[p])) ++outside;
            if (paramInfo (p).scale == Scale::choice && pt[p] != s.home[p]) ++outside;
        }
        CHECK (chooseCoreSound (bible, c, c.precip) == k, "choice is deterministic");
    }
    CHECK (outside == 0, "%d parameter values left their boundaries", outside);
    CHECK (chosen.size() * 3 >= bible.size() * 2, "only %zu of %zu core sounds are ever dealt", chosen.size(), bible.size());
    std::printf ("  4000 random skies: %zu of %zu core sounds dealt, 0 values out of bounds\n", chosen.size(), bible.size());

    // Extremes land where they should
    Climate heat;
    heat.temp = 41, heat.humidity = 15, heat.precip = 0, heat.clouds = 0, heat.sun = 60, heat.seed = 5;
    Climate monsoon;
    monsoon.temp = 27, monsoon.humidity = 98, monsoon.precip = 0.9, monsoon.clouds = 1, monsoon.sun = 30, monsoon.seed = 5;
    Climate polar;
    polar.temp = -30, polar.humidity = 70, polar.precip = 0.05, polar.clouds = 0.2, polar.sun = -20, polar.seed = 5;
    const auto& hs = bible[(size_t) chooseCoreSound (bible, heat, 0)];
    const auto& ms = bible[(size_t) chooseCoreSound (bible, monsoon, 0.9)];
    const auto& ps = bible[(size_t) chooseCoreSound (bible, polar, 0.05)];
    std::printf ("  41 C dry: %s  ·  monsoon: %s  ·  polar night: %s\n", hs.name.c_str(), ms.name.c_str(), ps.name.c_str());
    CHECK (hs.anchorTemp >= 25, "heat picks a hot sound (%s)", hs.name.c_str());
    CHECK (ms.anchorWet >= 0.6, "monsoon picks a wet sound (%s)", ms.name.c_str());
    CHECK (ps.anchorTemp <= 0, "polar night picks a cold sound (%s)", ps.name.c_str());
    const Patch hp = resolvePatch (hs, heat, 0, {}), mp = resolvePatch (ms, monsoon, 0.9, {});
    CHECK (hp[drive] > hs.home[drive] || hs.hi[drive] == hs.home[drive], "heat drives harder than home");
    CHECK (mp[spaceSend] > ms.home[spaceSend] || ms.hi[spaceSend] == ms.home[spaceSend], "monsoon is wetter than home");
}

static void testEngine()
{
    std::printf ("Engine renders every core sound, edge and lab scenario cleanly\n");
    const auto bible = builtInBible();
    double lo = 0, hi = -200, maxPeak = 0;
    int renders = 0;
    auto judge = [&] (const RenderStats& st, const juce::String& what, bool normal) {
        ++renders;
        CHECK (st.finite, "%s: non-finite samples", what.toRawUTF8());
        CHECK (st.peak <= 1.0, "%s: peak %.3f", what.toRawUTF8(), st.peak);
        CHECK (st.rmsDb > -48 && st.rmsDb < -10, "%s: RMS %.1f dB", what.toRawUTF8(), st.rmsDb);
        // Long designed tails are fine (a drenched sky rings for ~20 s); runaway feedback is not
        CHECK (st.tailDb < -60 || (st.tailDb < -30 && st.tailDb < st.midTailDb - 6), "%s: tail not dying away (%.1f dB at 12 s, %.1f dB at 21 s)",
               what.toRawUTF8(), st.midTailDb, st.tailDb);
        maxPeak = std::max (maxPeak, st.peak);
        if (normal)
        {
            lo = std::min (lo == 0 ? st.rmsDb : lo, st.rmsDb);
            hi = std::max (hi, st.rmsDb);
        }
    };

    // Every core sound at home and at both corners of its box
    for (const auto& s : bible)
    {
        judge (renderPatch (s.home), juce::String (s.name) + " home", true);
        judge (renderPatch (s.lo), juce::String (s.name) + " all-low", false);
        judge (renderPatch (s.hi), juce::String (s.name) + " all-high", false);
    }

    // Every lab weather scenario, dealt from the bible, with macros centred and at both extremes
    const auto v = loadVectors();
    std::set<juce::String> seen;
    const Macros zero, allUp { 1, 1, 1, 1, 1 }, allDown { -1, -1, -1, -1, -1 };
    for (auto& cs : *v["cases"].getArray())
    {
        const auto name = cs["name"].toString();
        if (seen.count (name)) continue;
        seen.insert (name);
        const Climate c = climateFrom (cs["climate"]);
        const auto& s = bible[(size_t) chooseCoreSound (bible, c, c.precip)];
        for (int mi = 0; mi < 3; ++mi)
        {
            const Macros& m = mi == 0 ? zero : mi == 1 ? allUp : allDown;
            const auto st = renderPatch (resolvePatch (s, c, c.precip, m));
            judge (st, name + " / " + juce::String (s.name) + (mi == 0 ? " (nature)" : mi == 1 ? " (all +1)" : " (all -1)"), mi == 0);
            if (mi == 0) CHECK (st.peak < 0.9, "%s: peaks at %.3f, hitting the safety limiter in normal use", name.toRawUTF8(), st.peak);
        }
    }
    std::printf ("  %d renders; loudness at home/nature %.1f .. %.1f dB RMS; highest peak %.2f\n", renders, lo, hi, maxPeak);

    // Bit-exact determinism: the same Day must bounce the same every time
    Climate c;
    c.temp = 38;
    c.precip = 0.4;
    c.seed = 99;
    const Patch hot = resolvePatch (bible[(size_t) chooseCoreSound (bible, c, c.precip)], c, c.precip, {});
    const auto a = renderPatch (hot), b = renderPatch (hot);
    CHECK (a.rmsDb == b.rmsDb && a.peak == b.peak, "two renders of one Day differ");

    // Sample rates and odd block sizes
    for (double sr : { 44100.0, 96000.0 })
        for (int blk : { 1, 37, 1024 })
        {
            const auto st = renderPatch (hot, sr, blk);
            CHECK (st.finite && st.peak <= 1.0 && st.rmsDb > -48, "sr %.0f block %d: rms %.1f", sr, blk, st.rmsDb);
        }

    // CPU: 12 voices of the heaviest-looking sound held for 10 s at 48 kHz
    for (const auto& s : bible)
    {
        Core e;
        e.prepare (48000, 512);
        e.setPatch (s.hi);
        std::vector<float> L (512), R (512);
        for (int i = 0; i < 12; ++i)
            e.noteOn (48 + i * 2, 0.8f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 48000 * 10 / 512; ++i)
            e.process (L.data(), R.data(), 512);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("  CPU %-18s 10 s of 12 voices in %4.0f ms (%.1f%% of one core)\n", s.name.c_str(), ms, ms / 100.0);
        CHECK (ms < 5000, "%s too slow: %.0f ms", s.name.c_str(), ms);
    }
}

static void testProcessorState()
{
    std::printf ("Projects and kept Days reopen with the same Day and the same sound\n");
    AtmosProcessor a;
    a.prepareToPlay (48000, 256);
    CHECK (a.getBible().size() >= 6 && a.bibleSource() == "built in", "processor loads the built-in bible (%s)", a.bibleSource().toRawUTF8());
    Day d = estimateDay (-33.87, 151.21, "Sydney", utc (2026, 10, 8, 4, 0));
    d.country = "AU";
    a.applyDayForTest (d);
    CHECK (a.currentDay().sound.isObject(), "a new Day takes a core sound from the bible");
    a.apvts.getParameter ("tone")->setValueNotifyingHost (0.8f);
    a.apvts.getParameter ("intensity")->setValueNotifyingHost (0.1f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    AtmosProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.currentDay().placeName == "Sydney" && b.currentDay().observedAt == d.observedAt, "Day restored: %s",
           b.currentDay().placeName.toRawUTF8());
    CHECK (std::abs (b.apvts.getParameter ("tone")->getValue() - 0.8f) < 1e-4, "macro restored");
    CHECK (a.currentSound().name == b.currentSound().name, "same core sound after reopening");
    {
        const auto pa = a.currentPatch(), pb = b.currentPatch();
        bool same = true;
        for (int p = 0; p < kNumParams; ++p)
            same = same && pa[p] == pb[p];
        CHECK (same, "same parameters after reopening");
    }
    CHECK (b.status() == AtmosProcessor::Status::restored, "restored projects don't fetch today's sky");

    // A project made with an older bible keeps its sound even after the bible changes
    {
        CoreSound custom = a.getBible()[0];
        custom.name = "Retired sound";
        custom.home[cutoff] = custom.lo[cutoff] = custom.hi[cutoff] = 777;
        Day old = d;
        old.sound = soundToVar (custom);
        a.applyDayForTest (old);
        juce::MemoryBlock st2;
        a.getStateInformation (st2);
        AtmosProcessor c;
        c.setStateInformation (st2.getData(), (int) st2.getSize());
        CHECK (c.currentSound().name == "Retired sound" && std::abs (c.currentPatch()[cutoff] - 777) < 1e-6, "project keeps a sound the bible no longer has (%s)",
               c.currentSound().name.c_str());
        // ... and so does a kept Day
        CHECK (c.keepDay ("Old friend"), "keep the Day");
        const auto kept = Storage::loadDays();
        bool found = false;
        for (const auto& k : kept)
            if (k.name == "Old friend")
            {
                found = true;
                AtmosProcessor e2;
                e2.loadDay (k);
                CHECK (e2.currentSound().name == "Retired sound", "kept Day reloads with its sound (%s)", e2.currentSound().name.c_str());
            }
        CHECK (found, "kept Day is in the almanac");
    }

    // A weather reply that lands after a project was restored must not replace the project's Day
    {
        Storage::setRelayUrl ("http://127.0.0.1:9"); // refused at once: the deal falls back to an estimate
        AtmosProcessor late;
        late.dealToday();
        late.setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
        CHECK (late.currentDay().placeName == "Sydney" && late.status() == AtmosProcessor::Status::restored,
               "late weather reply overwrote the restored project (now %s)", late.currentDay().placeName.toRawUTF8());
    }

    // Garbage state is ignored, not fatal
    const char junk[] = "not a plugin state";
    b.setStateInformation (junk, (int) sizeof junk);
    CHECK (b.currentDay().placeName == "Sydney", "junk state ignored");

    // Process through the processor itself, mono and stereo, including blocks bigger than promised
    for (int chans : { 2, 1 })
    {
        juce::AudioBuffer<float> buf (chans, 4096);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 3);
        midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), 100);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 120);
        b.prepareToPlay (48000, 256);
        double peak = 0;
        for (int i = 0; i < 400; ++i)
        {
            const int n = i % 3 == 0 ? 4096 : 256; // hosts sometimes exceed the size they announced
            buf.setSize (chans, n, false, false, true);
            for (int ch = 0; ch < chans; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 7.0f, n); // garbage in
            b.processBlock (buf, midi);
            midi.clear();
            peak = std::max (peak, (double) buf.getMagnitude (0, n));
        }
        CHECK (peak > 0.01 && peak <= 1.0, "%d-channel processor output peak %.3f", chans, peak);
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        b.processBlock (buf, off);
    }
}

static void testGlobe()
{
    std::printf ("Globe projection\n");
    Globe g;
    g.setSize (400, 400);
    g.centreOn (45, 10);
    double worst = 0;
    for (double lat = -60; lat <= 80; lat += 7)
        for (double lon = -60; lon <= 80; lon += 9)
        {
            juce::Point<float> p;
            if (! g.project (lat, lon, p)) continue;
            double la, lo;
            if (g.unproject (p, la, lo)) worst = std::max (worst, std::abs (la - lat) + std::abs (lo - lon));
        }
    CHECK (worst < 0.5, "project/unproject round trip error %.3f°", worst);
    double la, lo;
    CHECK (! g.unproject ({ 2, 2 }, la, lo), "corner is off the globe");
}

static void snapshot (const juce::File& out)
{
    AtmosProcessor p;
    p.applyDayForTest (estimateDay (63.43, 10.39, "Trondheim", utc (2026, 10, 8, 16, 30)));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os (out);
    png.writeImageToStream (img, os);
    if (auto* e = dynamic_cast<AtmosEditor*> (ed.get()))
    {
        e->toggleGlobe();
        auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os2 (out.getSiblingFile (out.getFileNameWithoutExtension() + "-globe.png"));
        os2.setPosition (0);
        png.writeImageToStream (img2, os2);
    }
    std::printf ("Editor snapshot written to %s\n", out.getFullPathName().toRawUTF8());
}

// ---------------------------------------------------------------- GUI behaviour
namespace G = atmos::gui;

static bool envFinite (const G::Environment& e)
{
    const double v[] = { e.sunAlt, e.sunAz, e.moonAlt, e.moonAz, e.moonFraction, e.moonPhase, e.daylight, e.cloud, e.overcast, e.drizzle, e.rain, e.snow,
                         e.sleet, e.hail, e.freezing, e.thunder, e.squall, e.tornado, e.mist, e.haze, e.smoke, e.dust, e.ash, e.visibility, e.windMps,
                         e.gustMps, e.waves, e.accent, e.accentIce, e.lamps, e.stars };
    for (double x : v)
        if (! std::isfinite (x)) return false;
    const double unit[] = { e.moonFraction, e.moonPhase, e.daylight, e.cloud, e.overcast, e.drizzle, e.rain, e.snow, e.sleet, e.hail, e.freezing,
                            e.thunder, e.mist, e.haze, e.smoke, e.dust, e.ash, e.visibility, e.waves, e.accent, e.accentIce, e.lamps, e.stars };
    for (double x : unit)
        if (x < 0 || x > 1) return false;
    return true;
}

static G::Snapshot snapAt (double lat, double lon, int64_t t, std::vector<int> ids, double temp = 10, double cloud = 0.5, double precip = 0, double wind = 4)
{
    G::Snapshot s;
    s.lat = lat;
    s.lon = lon;
    s.observedAt = t;
    s.conditionIds = std::move (ids);
    s.tempC = temp;
    s.cloud = cloud;
    s.precip = precip;
    s.windMps = wind;
    return s;
}

static void testGuiEnvironment()
{
    std::printf ("Scene state: fixtures, codes, astronomy, labels\n");
    // Every fixture at every time: deterministic, finite, clamped, and with a lighting blend that sums to 1
    int combos = 0;
    for (auto& f : G::fixtures())
        for (auto& t : G::timesOfDay())
        {
            auto a = G::computeEnvironment (f.snapshot), b = G::computeEnvironment (f.snapshot);
            G::applyTime (a, t);
            G::applyTime (b, t);
            CHECK (envFinite (a), "%s@%s: environment finite and clamped", f.name, t.name);
            CHECK (a.rain == b.rain && a.cloud == b.cloud && a.sunAlt == b.sunAlt && a.accent == b.accent, "%s@%s: deterministic", f.name, t.name);
            double sum = 0;
            for (auto& w : G::anchorWeights (a))
                sum += w.weight;
            CHECK (std::abs (sum - 1) < 1e-9, "%s@%s: lighting weights sum to %.6f", f.name, t.name, sum);
            ++combos;
        }
    std::printf ("  %d fixture x time combinations\n", combos);

    const int64_t t = utc (2026, 10, 9, 12, 0);
    // Unknown and missing codes: benign, no invented weather
    auto u = snapAt (60, 10, t, { 999, 123 }, 12, 0.3);
    const auto uc = G::classify (u);
    const auto ue = G::computeEnvironment (u);
    CHECK (uc.unknownCode && envFinite (ue) && ue.rain == 0 && ue.snow == 0 && ue.thunder == 0, "unknown codes fall back without inventing weather");
    auto none = snapAt (60, 10, t, {}, 12, 0.7);
    none.conditionMain = "";
    CHECK (envFinite (G::computeEnvironment (none)), "no codes and no category is still a valid sky");
    auto older = snapAt (60, 10, t, {}, 8, 0.9, 0.6);
    older.conditionMain = "Rain";
    CHECK (G::computeEnvironment (older).rain > 0, "older Days without codes use OpenWeather's category word");

    // Temperature alone never fabricates frost, snow on the ground, rain or a tornado
    for (double temp : { -35.0, -2.0, 0.0, 44.0 })
    {
        const auto e = G::computeEnvironment (snapAt (60, 10, t, { 800 }, temp, 0.0));
        CHECK (e.accent == 0 && e.snow == 0 && e.rain == 0 && e.tornado == 0 && e.dust == 0, "clear sky at %.0f C shows no invented weather", temp);
    }
    CHECK (G::computeEnvironment (snapAt (60, 10, t, { 601 }, -3, 1, 0.5)).accent > 0, "reported snow at -3 C settles");
    CHECK (G::computeEnvironment (snapAt (60, 10, t, { 601 }, 6, 1, 0.5)).accent == 0, "reported snow at +6 C does not settle");

    // Combinations: independent modifiers, not one giant switch
    const auto rf = G::computeEnvironment (snapAt (60, 10, t, { 500, 741 }, 8, 1, 0.4));
    CHECK (rf.rain > 0 && rf.mist > 0 && rf.visibility < 0.5, "rain and fog together");
    const auto ws = G::computeEnvironment (snapAt (60, 10, t, { 601 }, -4, 1, 0.5, 15));
    CHECK (ws.snow > 0 && ws.waves > 0.6, "wind and snow together");
    const auto th = G::computeEnvironment (snapAt (60, 10, t, { 211 }, 15, 0.9, 0.0));
    CHECK (th.thunder > 0 && th.rain == 0 && th.drizzle == 0, "thunder without supplied rain shows no rain");
    const auto fr = G::computeEnvironment (snapAt (60, 10, t, { 511 }, 3, 1, 0.5));
    CHECK (fr.freezing > 0 && fr.rain == 0 && fr.accentIce > 0, "511 is freezing rain, decided before generic rain");
    const auto sl = G::computeEnvironment (snapAt (60, 10, t, { 615 }, 1, 1, 0.4));
    CHECK (sl.sleet > 0 && sl.snow == 0, "615 is mixed rain and snow");
    CHECK (G::classify (snapAt (60, 10, t, { 202 }, 15, 1, 0.9)).precip != G::Conditions::Precip::hail, "thunderstorms never infer hail");
    const auto vis = snapAt (60, 10, t, { 800 }, 10, 0);
    auto foggy = vis;
    foggy.visibilityM = 400;
    CHECK (G::computeEnvironment (foggy).visibility < G::computeEnvironment (vis).visibility, "reported visibility shortens the view");
    auto night = snapAt (63.43, 10.39, utc (2026, 10, 9, 0, 0), { 741 }, 5, 1);
    night.visibilityM = 300;
    const auto ne = G::computeEnvironment (night);
    CHECK (ne.daylight < 0.05 && ne.visibility < 0.3 && ne.lamps > 0.8, "fog at night: dark, short view, lamps on");

    // Day, night, polar day and polar night come from the sun's position, never NaN
    const auto noon = G::computeEnvironment (snapAt (63.43, 10.39, utc (2026, 6, 21, 11, 0), { 800 }));
    const auto midnight = G::computeEnvironment (snapAt (63.43, 10.39, utc (2026, 12, 21, 23, 0), { 800 }));
    CHECK (noon.daylight > 0.95 && midnight.daylight < 0.05, "Trondheim: midsummer noon is day, midwinter midnight is night");
    const auto polarDay = G::computeEnvironment (snapAt (69.65, 18.96, utc (2026, 6, 21, 22, 30), { 800 }));
    const auto polarNight = G::computeEnvironment (snapAt (69.65, 18.96, utc (2026, 12, 21, 11, 0), { 800 }));
    CHECK (polarDay.sunAlt > 0 && envFinite (polarDay), "Tromso midnight sun: sun above the horizon at 00:30 local (%.1f)", polarDay.sunAlt);
    CHECK (polarNight.sunAlt < 0 && polarNight.sunAlt > -10 && envFinite (polarNight), "Tromso polar night: noon is twilight (%.1f)", polarNight.sunAlt);
    for (auto* e : { &polarDay, &polarNight })
        CHECK (! G::anchorWeights (*e).empty(), "polar light still has lighting anchors");

    // Moon: phase is a date quantity, visibility depends on place; it can share the sky with the sun
    const auto ml = atmos::sky::moonLight (utc (2026, 10, 9, 12, 0));
    const auto m1 = atmos::sky::moon (60, 10, utc (2026, 10, 9, 12, 0)), m2 = atmos::sky::moon (-33.9, 151.2, utc (2026, 10, 9, 12, 0));
    CHECK (ml.fraction >= 0 && ml.fraction <= 1 && std::abs (m1.altitudeDeg - m2.altitudeDeg) > 1, "moon phase is global, moon position local");
    bool both = false;
    for (int h = 0; h < 24 * 30 && ! both; h += 3)
    {
        const auto tt = utc (2026, 10, 1, 0, 0) + h * 3600;
        both = atmos::sky::sun (45, 0, tt).altitudeDeg > 10 && atmos::sky::moon (45, 0, tt).altitudeDeg > 10;
    }
    CHECK (both, "sun and moon can be up together");

    // Header: local time from the provider's offset (DST included), "~" when estimated
    auto lt = snapAt (59.9, 10.75, utc (2026, 7, 1, 10, 0), { 800 });
    lt.utcOffset = 7200;
    lt.offsetKnown = true;
    CHECK (G::timeLabel (lt) == "12:00", "summer time label %s", G::timeLabel (lt).toRawUTF8());
    lt.utcOffset = 3600;
    CHECK (G::timeLabel (lt) == "11:00", "winter offset label %s", G::timeLabel (lt).toRawUTF8());
    lt.offsetKnown = false;
    CHECK (G::timeLabel (lt).startsWith ("~"), "estimated offset is marked");

    // Source labels describe reality
    Day d = estimateDay (63.43, 10.39, "Trondheim", juce::Time::currentTimeMillis() / 1000);
    d.source = "live";
    const auto now = juce::Time::currentTimeMillis() / 1000;
    CHECK (G::makeSnapshot (d, G::FeedStatus::live, true, now).source == G::Source::live, "fresh reading is LIVE");
    CHECK (G::makeSnapshot (d, G::FeedStatus::live, true, now + 7200).source == G::Source::stale, "two-hour-old reading is STALE");
    CHECK (G::makeSnapshot (d, G::FeedStatus::live, false, now).source == G::Source::stale, "yesterday's reading is STALE");
    CHECK (G::makeSnapshot (d, G::FeedStatus::restored, true, now).source == G::Source::savedDay, "restored project is SAVED DAY");
    d.source = "estimate";
    const auto est = G::makeSnapshot (d, G::FeedStatus::estimate, true, now);
    CHECK (est.source == G::Source::estimated && est.offline, "network failure: OFFLINE, ESTIMATED (never LIVE)");
    CHECK (juce::String (G::sourceLabel (G::Source::live)) == "LIVE" && juce::String (G::sourceLabel (G::Source::savedDay)) == "SAVED DAY", "label text");
}

static void testGuiEditor()
{
    std::printf ("Editor: assets, lifecycle, controls, late replies\n");
    {
        juce::SharedResourcePointer<G::SceneAssets> assets;
        CHECK (assets->valid(), "scene assets compiled in and manifest parsed");
        for (auto* a : { "dawn", "morning", "noon", "late_afternoon", "sunset", "dusk", "night", "overcast" })
        {
            const auto img = assets->lighting (a);
            CHECK (img.isValid() && std::abs (img.getWidth() / 2.0f - assets->artBox().getWidth()) < 1.5f, "lighting %s decodes at the art box size", a);
        }
        CHECK (assets->decodedBytes() < 64u * 1024 * 1024, "decoded scene art %.1f MiB (budget 32-64)", assets->decodedBytes() / 1048576.0);
        CHECK (assets->vaneFrames() == 16 && assets->anemometerFrames() == 6 && assets->accentMask().isValid(), "sprites and masks present");
        CHECK (assets->isLand (assets->marker ("MarkerCabinetCentre")) && ! assets->isLand ({ 5, 455 }), "island coverage for splashes");
    }

    AtmosProcessor p;
    p.prepareToPlay (48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<AtmosEditor*> (ed.get());
    CHECK (e != nullptr, "editor is the scene editor");
    if (e == nullptr) return;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    CHECK (! e->sceneView().animating(), "an editor that isn't on screen runs no animation timer");

    // Layout at minimum, default and maximum size: everything inside, nothing overlapping the scene
    for (auto sz : { juce::Point<int> (820, 546), juce::Point<int> (1024, 682), juce::Point<int> (2048, 1364) })
    {
        ed->setSize (sz.x, sz.y);
        bool inside = true, clear = true;
        for (auto* c : ed->getChildren())
            if (c->isVisible() && ! ed->getLocalBounds().contains (c->getBounds()))
            {
                inside = false;
                std::printf ("    outside: %s %s\n", c->getName().toRawUTF8(), c->getBounds().toString().toRawUTF8());
            }
        for (auto* t : e->macroTracks())
            clear &= ! t->getBounds().intersects (e->sceneView().getBounds()) && t->slider.getHeight() >= 0.1 * sz.y;
        CHECK (inside && clear, "layout at %dx%d: controls inside, below the scene, tracks tall enough", sz.x, sz.y);
    }
    ed->setSize (820, 546);
    CHECK (std::abs (e->sceneView().getWidth() / (double) e->sceneView().getHeight() - 1024.0 / 460.0) < 0.02, "scene keeps its aspect at minimum size");

    // Particles stay bounded and finite for every weather, even after a long stall
    int worst = 0;
    bool finite = true;
    for (auto& f : G::fixtures())
    {
        e->applyBenchOptions (juce::String (f.name) + "@noon", "full");
        for (int i = 0; i < 40; ++i)
            worst = juce::jmax (worst, e->sceneView().stepForTest (i == 20 ? 3600.0 : 1.0 / 24));
        finite &= e->sceneView().particlesFinite();
    }
    e->applyBenchOptions ("rain@noon", "full");
    CHECK (e->headerBar().getDescription().contains ("FIXTURE") && ! e->headerBar().getDescription().contains ("LIVE"),
           "fixtures never claim LIVE: %s", e->headerBar().getDescription().toRawUTF8());
    CHECK (worst <= G::SceneView::maxParticles && worst > 100, "particle pool bounded (peak %d of %d)", worst, G::SceneView::maxParticles);
    CHECK (finite, "particles finite and on screen after an hour-long stall (delta is clamped)");

    // Macros: real parameters, host gestures for every change, reset to the weather's anchor
    struct Gestures : juce::AudioProcessorListener
    {
        int begins = 0, ends = 0, changes = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
    } gestures;
    p.addListener (&gestures);
    auto& tracks = e->macroTracks();
    CHECK (tracks.size() == 5, "five macro tracks");
    const char* ids[] = { "tone", "bloom", "motion", "space", "intensity" };
    for (int i = 0; i < tracks.size(); ++i)
    {
        auto* t = tracks[i];
        auto* param = p.apvts.getParameter (ids[i]);
        t->keyPressed (juce::KeyPress (juce::KeyPress::upKey), &t->slider);
        CHECK (std::abs (p.apvts.getRawParameterValue (ids[i])->load() - 0.02f) < 1e-4f, "%s: up arrow nudges +2%%", ids[i]);
        t->keyPressed (juce::KeyPress (juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0), &t->slider);
        CHECK (std::abs (p.apvts.getRawParameterValue (ids[i])->load() - 0.018f) < 1e-4f, "%s: shift-down is a fine step", ids[i]);
        t->setValueFromText ("-40");
        CHECK (std::abs (p.apvts.getRawParameterValue (ids[i])->load() + 0.4f) < 1e-4f, "%s: typed value -40 lands at -40%%", ids[i]);
        CHECK (t->slider.getTextFromValue (-0.4) == juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "40%", "%s: readout", ids[i]);
        CHECK (t->slider.isDoubleClickReturnEnabled() && t->slider.getDoubleClickReturnValue() == 0.0, "%s: double-click returns to the weather anchor (0)", ids[i]);
        t->keyPressed (juce::KeyPress (juce::KeyPress::homeKey), &t->slider);
        CHECK (p.apvts.getRawParameterValue (ids[i])->load() == 0.0f, "%s: Home key resets to nature", ids[i]);
        // Host automation moves the control
        param->setValueNotifyingHost (param->convertTo0to1 (0.5f));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (std::abs (t->slider.getValue() - 0.5) < 1e-3, "%s: host automation updates the track", ids[i]);
        param->setValueNotifyingHost (param->convertTo0to1 (0.0f));
        CHECK (t->slider.getTitle().isNotEmpty() && t->slider.getTooltip().contains ("limits"), "%s: accessible name and tooltip", ids[i]);
    }
    CHECK (gestures.begins >= 15 && gestures.begins == gestures.ends, "every GUI change is wrapped in a host gesture (%d begins, %d ends)", gestures.begins, gestures.ends);
    p.removeListener (&gestures);

    // The scene follows the processor's Day: Keep Day / load, late replies, restores
    Storage::setRelayUrl ("http://127.0.0.1:9");
    auto kept = estimateDay (-33.87, 151.21, "Sydney", utc (2026, 10, 8, 4, 0));
    p.dealToday();
    p.loadDay (kept);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);
    CHECK (p.currentDay().placeName == "Sydney", "a late weather reply cannot replace a loaded Day (now %s)", p.currentDay().placeName.toRawUTF8());
    e->applyBenchOptions ("live", "full");
    CHECK (e->headerBar().getDescription().contains ("Sydney") && e->headerBar().getDescription().contains ("SAVED DAY"),
           "header shows the loaded Day as SAVED DAY: %s", e->headerBar().getDescription().toRawUTF8());
    CHECK (std::abs (e->sceneView().shownEnvironment().sunAlt - atmos::sky::sun (kept.lat, kept.lon, kept.observedAt).altitudeDeg) < 0.01,
           "the scene's sky is the kept Day's own sky (immediately, no transition)");

    // Closing and reopening: no timers left behind, shared art survives another editor closing
    std::unique_ptr<juce::AudioProcessorEditor> second (p.createEditor());
    ed.reset();
    {
        juce::SharedResourcePointer<G::SceneAssets> assets;
        CHECK (assets->lighting ("noon").isValid(), "art still available to the remaining editor");
    }
    second.reset();
}

// Visual matrix: every fixture at every time of day, rendered through the real editor.
//   AtmosTests --atlas out/ [fixtures=a,b] [times=noon,night] [width=1024]
// Writes out/<fixture>@<time>.png and a contact sheet out/atlas.png (fixtures down, times across).
static int atlas (const juce::File& dir, const juce::StringArray& args)
{
    juce::StringArray fx, tm;
    int width = 1024;
    float scale = 1.0f;
    for (auto& a : args)
    {
        if (a.startsWith ("fixtures=")) fx.addTokens (a.fromFirstOccurrenceOf ("=", false, false), ",", "");
        if (a.startsWith ("times=")) tm.addTokens (a.fromFirstOccurrenceOf ("=", false, false), ",", "");
        if (a.startsWith ("width=")) width = a.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        if (a.startsWith ("scale=")) scale = a.fromFirstOccurrenceOf ("=", false, false).getFloatValue(); // 2 = Retina
    }
    if (fx.isEmpty())
        for (auto& f : atmos::gui::fixtures())
            fx.add (f.name);
    if (tm.isEmpty())
        for (auto& t : atmos::gui::timesOfDay())
            tm.add (t.name);
    dir.createDirectory();
    Storage::setFolderOverride (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atmos-atlas"));
    AtmosProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<AtmosEditor*> (ed.get());
    ed->setSize (width, width * 682 / 1024);
    const int tw = 340, th = 227;
    juce::Image sheet (juce::Image::RGB, tw * tm.size(), th * fx.size(), true);
    juce::Graphics sg (sheet);
    juce::PNGImageFormat png;
    int n = 0;
    for (int r = 0; r < fx.size(); ++r)
        for (int c = 0; c < tm.size(); ++c)
        {
            e->applyBenchOptions (fx[r] + "@" + tm[c], "still");
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (fx[r] + "@" + tm[c] + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
            sg.drawImage (img, juce::Rectangle<float> ((float) (c * tw), (float) (r * th), (float) tw, (float) th));
            ++n;
        }
    auto f = dir.getChildFile ("atlas.png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    png.writeImageToStream (sheet, os);
    std::printf ("Atlas: %d images (%d fixtures x %d times) in %s\n", n, fx.size(), tm.size(), dir.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--atlas")
    {
        juce::StringArray rest;
        for (int i = 3; i < argc; ++i)
            rest.add (argv[i]);
        return atlas (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), rest);
    }
    testMapperParity();
    testAstro();
    testDayAndStorage();
    testBible();
    testEngine();
    testProcessorState();
    testGlobe();
    testGuiEnvironment();
    testGuiEditor();
    if (argc > 2 && juce::String (argv[1]) == "--snapshot") snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
