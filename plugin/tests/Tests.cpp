// AtmosTests: run with no arguments. Exit code 0 means every check passed.
//   AtmosTests --snapshot out.png   also renders the editor to an image
#include "../Source/Astro.h"
#include "../Source/ClimateMapper.h"
#include "../Source/Day.h"
#include "../Source/Engine.h"
#include "../Source/Globe.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Storage.h"
#include "../Source/WeatherClient.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>

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

static RenderStats renderScenario (const Climate& c, const Macros& m, double sr = 48000, int block = 256)
{
    Engine e;
    e.prepare (sr, block);
    e.installFrame (natureParams (c).frame);
    e.setTarget (resolveLeash (c, m));
    juce::AudioBuffer<float> buf (2, block);
    RenderStats st;
    double sum = 0, tail = 0, mid = 0;
    long n = 0, nt = 0, nm = 0;
    const int total = (int) (sr * 22);
    const int chord[] = { 48, 55, 62, 64 }; // C3 G3 D4 E4
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer midi;
        auto at = [&] (double sec) { return (int) (sec * sr) - pos; };
        auto inBlock = [&] (double sec) { const int o = at (sec); return o >= 0 && o < block; };
        for (int i = 0; i < 4; ++i)
        {
            const int note = chord[i];
            if (inBlock (0.05)) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), at (0.05));
            if (inBlock (2.0)) midi.addEvent (juce::MidiMessage::noteOff (1, note), at (2.0));
        }
        for (int i = 0; i < 12; ++i)
        {
            const int note = 60 + (i * 7) % 24;
            const double t0 = 2.3 + i * 0.16;
            if (inBlock (t0)) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 90), at (t0));
            if (inBlock (t0 + 0.12)) midi.addEvent (juce::MidiMessage::noteOff (1, note), at (t0 + 0.12));
        }
        if (inBlock (4.5)) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), at (4.5));
        if (inBlock (5.2)) midi.addEvent (juce::MidiMessage::noteOff (1, 36), at (5.2));
        buf.clear();
        e.render (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < block; ++i)
            {
                const double x = d[i];
                if (! std::isfinite (x)) st.finite = false;
                st.peak = std::max (st.peak, std::abs (x));
                const double t = (pos + i) / sr;
                if (t < 6) sum += x * x, ++n;
                if (t > 21) tail += x * x, ++nt;
                if (t > 12 && t < 13) mid += x * x, ++nm;
            }
        }
    }
    st.rmsDb = 10 * std::log10 (sum / std::max (1L, n) + 1e-20);
    st.tailDb = 10 * std::log10 (tail / std::max (1L, nt) + 1e-20);
    st.midTailDb = 10 * std::log10 (mid / std::max (1L, nm) + 1e-20);
    return st;
}

static void testEngine()
{
    std::printf ("Engine renders every lab scenario cleanly\n");
    const auto v = loadVectors();
    std::set<juce::String> seen;
    double lo = 0, hi = -200, maxPeak = 0;
    const Macros zero, allUp { 1, 1, 1, 1, 1 }, allDown { -1, -1, -1, -1, -1 };
    for (auto& cs : *v["cases"].getArray())
    {
        const auto name = cs["name"].toString();
        if (seen.count (name)) continue;
        seen.insert (name);
        const Climate c = climateFrom (cs["climate"]);
        for (int mi = 0; mi < 3; ++mi)
        {
            const Macros& m = mi == 0 ? zero : mi == 1 ? allUp : allDown;
            const auto st = renderScenario (c, m);
            const char* tag = mi == 0 ? "nature" : mi == 1 ? "all +1" : "all -1";
            CHECK (st.finite, "%s (%s): non-finite samples", name.toRawUTF8(), tag);
            CHECK (st.peak <= 1.0, "%s (%s): peak %.3f", name.toRawUTF8(), tag, st.peak);
            CHECK (st.rmsDb > -45 && st.rmsDb < -12, "%s (%s): RMS %.1f dB", name.toRawUTF8(), tag, st.rmsDb);
            // Long designed tails are fine (a drenched sky with Space up rings for ~20 s); runaway feedback is not
            CHECK (st.tailDb < -60 || (st.tailDb < -30 && st.tailDb < st.midTailDb - 6), "%s (%s): tail not dying away (%.1f dB at 12 s, %.1f dB at 21 s)",
                   name.toRawUTF8(), tag, st.midTailDb, st.tailDb);
            maxPeak = std::max (maxPeak, st.peak);
            if (mi == 0) CHECK (st.peak < 0.85, "%s: peaks at %.3f, hitting the safety limiter in normal use", name.toRawUTF8(), st.peak);
            if (mi == 0)
            {
                lo = std::min (lo == 0 ? st.rmsDb : lo, st.rmsDb);
                hi = std::max (hi, st.rmsDb);
            }
        }
    }
    std::printf ("  %zu scenarios x 3 macro settings; loudness at nature %.1f .. %.1f dB RMS; highest peak %.2f\n", seen.size(), lo, hi, maxPeak);

    // Bit-exact determinism: the same Day must bounce the same every time
    Climate c;
    c.temp = 38;
    c.precip = 0.4;
    c.seed = 99;
    const auto a = renderScenario (c, {}), b = renderScenario (c, {});
    CHECK (a.rmsDb == b.rmsDb && a.peak == b.peak, "two renders of one Day differ");

    // Sample rates and odd block sizes
    for (double sr : { 44100.0, 96000.0 })
        for (int blk : { 1, 37, 1024 })
        {
            const auto st = renderScenario (c, {}, sr, blk);
            CHECK (st.finite && st.peak <= 1.0 && st.rmsDb > -45, "sr %.0f block %d: rms %.1f", sr, blk, st.rmsDb);
        }

    // A host sending bigger blocks than it promised must still get real audio
    {
        Engine big;
        big.prepare (48000, 256);
        big.installFrame (0.5);
        big.setTarget (resolveLeash (c, {}));
        juce::AudioBuffer<float> b (2, 4096);
        b.clear();
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), 7.0f, 4096); // garbage in
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
        big.render (b, m);
        CHECK (b.getMagnitude (0, 4096) < 1.0f && b.getMagnitude (0, 4096) > 0.0001f, "oversized block: magnitude %.3f", b.getMagnitude (0, 4096));
    }

    // CPU: 12 voices held for 10 s at 48 kHz
    Engine e;
    e.prepare (48000, 512);
    e.installFrame (0.6);
    e.setTarget (resolveLeash (c, { 0.3, 0.3, 0.8, 1, 1 }));
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer on;
    for (int i = 0; i < 12; ++i)
        on.addEvent (juce::MidiMessage::noteOn (1, 48 + i * 2, (juce::uint8) 100), 0);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 48000 * 10 / 512; ++i)
    {
        e.render (buf, on);
        on.clear();
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::printf ("  CPU: 10 s of 12 voices rendered in %.0f ms (%.1f%% of one core)\n", ms, ms / 100.0);
    CHECK (ms < 4000, "too slow: %.0f ms", ms);
}

static void testProcessorState()
{
    std::printf ("Projects reopen with the same Day\n");
    AtmosProcessor a;
    a.prepareToPlay (48000, 256);
    Day d = estimateDay (-33.87, 151.21, "Sydney", utc (2026, 10, 8, 4, 0));
    d.country = "AU";
    a.applyDayForTest (d);
    a.apvts.getParameter ("tone")->setValueNotifyingHost (0.8f);
    a.apvts.getParameter ("intensity")->setValueNotifyingHost (0.1f);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    AtmosProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (b.currentDay().placeName == "Sydney" && b.currentDay().observedAt == d.observedAt, "Day restored: %s",
           b.currentDay().placeName.toRawUTF8());
    CHECK (std::abs (b.apvts.getParameter ("tone")->getValue() - 0.8f) < 1e-4, "macro restored");
    const auto pa = a.currentParams(), pb = b.currentParams();
    CHECK (pa.cutoff == pb.cutoff && pa.verbWet == pb.verbWet && pa.frame == pb.frame, "same parameters after reopening");
    CHECK (b.status() == AtmosProcessor::Status::restored, "restored projects don't fetch today's sky");

    // Garbage state is ignored, not fatal
    const char junk[] = "not a plugin state";
    b.setStateInformation (junk, (int) sizeof junk);
    CHECK (b.currentDay().placeName == "Sydney", "junk state ignored");

    // Process through the processor itself
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    b.prepareToPlay (48000, 256);
    double peak = 0;
    for (int i = 0; i < 400; ++i)
    {
        buf.clear();
        b.processBlock (buf, midi);
        midi.clear();
        peak = std::max (peak, (double) buf.getMagnitude (0, 256));
    }
    CHECK (peak > 0.01 && peak <= 1.0, "processor output peak %.3f", peak);
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    testMapperParity();
    testAstro();
    testDayAndStorage();
    testEngine();
    testProcessorState();
    testGlobe();
    if (argc > 2 && juce::String (argv[1]) == "--snapshot") snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
