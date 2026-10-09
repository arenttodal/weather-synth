#include "Environment.h"
#include "Sky.h"
#include <algorithm>
#include <cmath>

namespace atmos::gui
{
namespace
{
    double clamp01 (double x) { return std::isfinite (x) ? std::clamp (x, 0.0, 1.0) : 0.0; }
    double smooth (double a, double b, double x)
    {
        const double t = clamp01 ((x - a) / (b - a));
        return t * t * (3 - 2 * t);
    }
    double lerp (double a, double b, double t) { return a + (b - a) * t; }
    double lerpAngle (double a, double b, double t)
    {
        double d = std::fmod (b - a + 540.0, 360.0) - 180.0;
        return std::fmod (a + d * t + 360.0, 360.0);
    }

    using P = Conditions::Precip;

    // Rate for an OpenWeather code, and what it means. Returns false for codes we don't know.
    bool decode (int id, Conditions& c, P& kind, double& rate)
    {
        kind = P::none;
        rate = 0;
        if (id >= 200 && id < 300)
        {
            c.thunder = true;
            switch (id)
            {
                case 200: kind = P::rain; rate = 0.35; break;
                case 201: kind = P::rain; rate = 0.6; break;
                case 202: kind = P::rain; rate = 0.9; break;
                case 230: kind = P::drizzle; rate = 0.25; break;
                case 231: kind = P::drizzle; rate = 0.35; break;
                case 232: kind = P::drizzle; rate = 0.5; break;
                case 210: case 211: case 212: case 221: break; // thunder without supplied precipitation
                default: return false;
            }
            return true;
        }
        if (id >= 300 && id < 400)
        {
            static const int ids[] = { 300, 301, 302, 310, 311, 312, 313, 314, 321 };
            static const double r[] = { 0.2, 0.3, 0.45, 0.3, 0.35, 0.5, 0.5, 0.6, 0.5 };
            for (int i = 0; i < 9; ++i)
                if (ids[i] == id)
                {
                    kind = P::drizzle;
                    rate = r[i];
                    return true;
                }
            return false;
        }
        if (id >= 500 && id < 600)
        {
            if (id == 511) // freezing rain: before the generic rain family
            {
                kind = P::freezingRain;
                rate = 0.5;
                return true;
            }
            static const int ids[] = { 500, 501, 502, 503, 504, 520, 521, 522, 531 };
            static const double r[] = { 0.3, 0.55, 0.8, 0.95, 1.0, 0.4, 0.6, 0.85, 0.7 };
            for (int i = 0; i < 9; ++i)
                if (ids[i] == id)
                {
                    kind = P::rain;
                    rate = r[i];
                    return true;
                }
            return false;
        }
        if (id >= 600 && id < 700)
        {
            switch (id)
            {
                case 600: kind = P::snow; rate = 0.3; break;
                case 601: kind = P::snow; rate = 0.55; break;
                case 602: kind = P::snow; rate = 0.85; break;
                case 611: kind = P::sleet; rate = 0.5; break;
                case 612: kind = P::sleet; rate = 0.4; break; // light shower sleet
                case 613: kind = P::sleet; rate = 0.6; break; // shower sleet
                case 615: kind = P::sleet; rate = 0.4; break; // light rain and snow
                case 616: kind = P::sleet; rate = 0.6; break; // rain and snow
                case 620: kind = P::snow; rate = 0.35; break;
                case 621: kind = P::snow; rate = 0.55; break;
                case 622: kind = P::snow; rate = 0.85; break;
                default: return false;
            }
            return true;
        }
        switch (id)
        {
            case 701: c.mist = std::max (c.mist, 0.5); return true;
            case 711: c.smoke = std::max (c.smoke, 0.6); return true;
            case 721: c.haze = std::max (c.haze, 0.5); return true;
            case 731: case 761: c.dust = std::max (c.dust, 0.55); return true;
            case 741: c.fog = std::max (c.fog, 0.85); return true;
            case 751: c.dust = std::max (c.dust, 0.65); return true; // sand
            case 762: c.ash = std::max (c.ash, 0.6); return true;
            case 771: c.squall = true; return true;
            case 781: c.tornado = true; return true;
            case 800: c.cloud = std::max (c.cloud, 0.0); return true;
            case 801: c.cloud = std::max (c.cloud, 0.18); return true;
            case 802: c.cloud = std::max (c.cloud, 0.4); return true;
            case 803: c.cloud = std::max (c.cloud, 0.7); return true;
            case 804: c.cloud = std::max (c.cloud, 0.95); return true;
            default: return false;
        }
    }

    // OpenWeather's category word, only when no codes arrived (older Days, estimates)
    std::vector<int> fromMain (const std::string& main, double precip, double cloud)
    {
        if (main == "Thunderstorm") return { precip > 0.05 ? 201 : 211 };
        if (main == "Drizzle") return { 301 };
        if (main == "Rain") return { precip > 0.75 ? 502 : precip > 0.35 ? 501 : 500 };
        if (main == "Snow") return { precip > 0.7 ? 602 : precip > 0.35 ? 601 : 600 };
        if (main == "Mist") return { 701 };
        if (main == "Smoke") return { 711 };
        if (main == "Haze") return { 721 };
        if (main == "Dust") return { 761 };
        if (main == "Fog") return { 741 };
        if (main == "Sand") return { 751 };
        if (main == "Ash") return { 762 };
        if (main == "Squall") return { 771 };
        if (main == "Tornado") return { 781 };
        if (main == "Clear") return { 800 };
        if (main == "Clouds") return { cloud > 0.85 ? 804 : cloud > 0.55 ? 803 : cloud > 0.3 ? 802 : 801 };
        return {};
    }

    int precipRank (P p)
    {
        switch (p)
        {
            case P::hail: return 6;
            case P::freezingRain: return 5;
            case P::sleet: return 4;
            case P::snow: return 3;
            case P::rain: return 2;
            case P::drizzle: return 1;
            case P::none: return 0;
        }
        return 0;
    }
} // namespace

const char* sourceLabel (Source s)
{
    switch (s)
    {
        case Source::live: return "LIVE";
        case Source::stale: return "STALE";
        case Source::savedDay: return "SAVED DAY";
        case Source::estimated: return "ESTIMATED";
        case Source::preview: return "GLOBE PREVIEW";
        case Source::loading: return "READING SKY";
    }
    return "";
}

Conditions classify (const Snapshot& s)
{
    Conditions c;
    c.cloud = s.cloud >= 0 ? clamp01 (s.cloud) : 0.0;
    auto ids = s.conditionIds;
    if (ids.empty()) ids = fromMain (s.conditionMain, s.precip, c.cloud);

    // Policy for several codes: the most significant code (first) that carries precipitation
    // decides its kind; a later code only replaces it with a more severe kind. Visibility,
    // thunder, squall and tornado modifiers from every code combine.
    bool first = true;
    for (int id : ids)
    {
        P kind;
        double rate;
        if (! decode (id, c, kind, rate))
        {
            c.unknownCode = true;
            continue;
        }
        if (kind != P::none && (c.precip == P::none || (! first && precipRank (kind) > precipRank (c.precip))))
        {
            c.precip = kind;
            c.rate = rate;
        }
        if (kind != P::none) first = false;
    }
    // Measured intensity refines the code's typical rate (it never invents precipitation)
    if (c.precip != P::none && s.precip > 0.02) c.rate = std::clamp (0.5 * c.rate + 0.5 * s.precip, 0.1, 1.0);
    if (s.fixtureHail)
    {
        c.precip = P::hail;
        c.rate = std::max (c.rate, 0.6);
    }
    c.frost = s.fixtureFrost; // temperature alone never establishes frost
    // Precipitation needs clouds even if the cover figure lags
    if (c.precip != P::none) c.cloud = std::max (c.cloud, 0.6 + 0.3 * c.rate);
    if (c.thunder) c.cloud = std::max (c.cloud, 0.85);
    return c;
}

Environment computeEnvironment (const Snapshot& s)
{
    Environment e;
    const auto c = classify (s);
    const auto sunPos = sky::sun (s.lat, s.lon, s.observedAt);
    const auto later = sky::sun (s.lat, s.lon, s.observedAt + 1200);
    const auto moonPos = sky::moon (s.lat, s.lon, s.observedAt);
    const auto ml = sky::moonLight (s.observedAt);
    e.sunAlt = sunPos.altitudeDeg;
    e.sunAz = sunPos.azimuthDeg;
    e.evening = later.altitudeDeg < sunPos.altitudeDeg;
    e.moonAlt = moonPos.altitudeDeg;
    e.moonAz = moonPos.azimuthDeg;
    e.moonFraction = clamp01 (ml.fraction);
    e.moonPhase = clamp01 (ml.phase);
    e.moonAngle = std::isfinite (ml.angleDeg) ? ml.angleDeg : 0.0;
    e.daylight = smooth (-9.0, 7.0, e.sunAlt);

    e.cloud = c.cloud;
    const double r = c.rate;
    switch (c.precip)
    {
        case Conditions::Precip::drizzle: e.drizzle = r; break;
        case Conditions::Precip::rain: e.rain = r; break;
        case Conditions::Precip::snow: e.snow = r; break;
        case Conditions::Precip::sleet: e.sleet = r; break;
        case Conditions::Precip::freezingRain: e.freezing = r; break;
        case Conditions::Precip::hail: e.hail = r; break;
        case Conditions::Precip::none: break;
    }
    e.thunder = c.thunder ? 1.0 : 0.0;
    e.squall = c.squall ? 1.0 : 0.0;
    e.tornado = c.tornado ? 1.0 : 0.0;
    e.mist = std::max (c.mist, c.fog);
    e.haze = c.haze;
    e.smoke = c.smoke;
    e.dust = c.dust;
    e.ash = c.ash;

    const double wet = std::max ({ e.rain, e.drizzle * 0.6, e.freezing, e.sleet, e.snow * 0.7, e.hail });
    e.overcast = std::max ({ smooth (0.5, 0.95, e.cloud), wet * 0.8, e.mist * 0.7, e.thunder * 0.9 });

    // Visibility: the provider's figure when known (10 km+ clear, 200 m very short), else the veils
    double vis = 1 - std::max ({ e.mist, e.haze * 0.6, e.smoke * 0.7, e.dust * 0.7, e.ash * 0.7, wet * 0.35 });
    if (s.visibilityM >= 0) vis = std::min (vis, clamp01 (std::log10 (std::max (s.visibilityM, 200.0) / 200.0) / std::log10 (50.0)));
    e.visibility = clamp01 (vis);

    e.windMps = std::clamp (std::isfinite (s.windMps) ? s.windMps : 0.0, 0.0, 60.0);
    e.gustMps = s.gustMps >= 0 ? std::clamp (s.gustMps, e.windMps, 80.0) : e.windMps;
    e.windFromDeg = s.windFromDeg >= 0 && s.windFromDeg <= 360 ? s.windFromDeg : -1;
    if (c.squall) e.gustMps = std::max (e.gustMps, e.windMps + 10);
    // Symbolic sea state from wind (not a marine measurement)
    e.waves = clamp01 (0.08 + std::pow (e.windMps / 22.0, 0.8) + 0.25 * e.squall + 0.15 * e.thunder);

    // Surface accents follow reported conditions, never temperature alone
    if (e.snow > 0 && s.tempC <= 2) e.accent = 0.35 + 0.4 * e.snow;
    if (e.sleet > 0 && s.tempC <= 2) e.accent = std::max (e.accent, 0.25);
    if (e.freezing > 0) { e.accent = std::max (e.accent, 0.45); e.accentIce = 1; }
    if (c.frost) { e.accent = std::max (e.accent, 0.4); e.accentIce = std::max (e.accentIce, 0.7); }
    if (e.hail > 0) e.accent = std::max (e.accent, 0.2);

    e.lamps = clamp01 (1.0 - 0.8 * e.daylight + 0.25 * e.overcast * e.daylight + 0.2 * e.thunder);
    e.stars = clamp01 ((1 - e.daylight) * (1 - e.cloud) * e.visibility);
    return e;
}

Environment blend (const Environment& a, const Environment& b, double t)
{
    t = clamp01 (t);
    Environment e = b;
#define L(f) e.f = lerp (a.f, b.f, t)
    L (sunAlt); L (moonAlt); L (moonFraction); L (moonAngle); L (daylight); L (cloud); L (overcast); L (drizzle); L (rain); L (snow);
    L (sleet); L (hail); L (freezing); L (thunder); L (squall); L (tornado); L (mist); L (haze); L (smoke); L (dust); L (ash); L (visibility);
    L (windMps); L (gustMps); L (waves); L (accent); L (accentIce); L (lamps); L (stars);
#undef L
    e.sunAz = lerpAngle (a.sunAz, b.sunAz, t);
    e.moonAz = lerpAngle (a.moonAz, b.moonAz, t);
    e.moonPhase = std::abs (b.moonPhase - a.moonPhase) > 0.5 ? b.moonPhase : lerp (a.moonPhase, b.moonPhase, t);
    if (a.windFromDeg >= 0 && b.windFromDeg >= 0) e.windFromDeg = lerpAngle (a.windFromDeg, b.windFromDeg, t);
    e.evening = t < 0.5 ? a.evening : b.evening;
    return e;
}

std::vector<AnchorWeight> anchorWeights (const Environment& e)
{
    // Altitude keys along the morning and evening halves of the day. The anchors are art
    // direction, not fixed hours: polar days simply never reach "noon", polar nights stay
    // between "night" and "dusk"/"dawn".
    struct Key
    {
        double alt;
        const char* name;
    };
    static const Key morning[] = { { -14, "night" }, { -4, "dawn" }, { 14, "morning" }, { 42, "noon" } };
    static const Key evening[] = { { -14, "night" }, { -6, "dusk" }, { 1, "sunset" }, { 18, "late_afternoon" }, { 42, "noon" } };
    const Key* k = e.evening ? evening : morning;
    const int n = e.evening ? 5 : 4;
    std::vector<AnchorWeight> w;
    const double a = std::isfinite (e.sunAlt) ? e.sunAlt : 0.0;
    if (a <= k[0].alt) w.push_back ({ k[0].name, 1.0 });
    else if (a >= k[n - 1].alt) w.push_back ({ k[n - 1].name, 1.0 });
    else
        for (int i = 0; i < n - 1; ++i)
            if (a >= k[i].alt && a < k[i + 1].alt)
            {
                const double t = (a - k[i].alt) / (k[i + 1].alt - k[i].alt);
                w.push_back ({ k[i].name, 1 - t });
                w.push_back ({ k[i + 1].name, t });
            }
    // Overcast replaces part of the sunlit look with diffuse light (daytime only)
    const double oc = clamp01 (e.overcast * e.daylight * 0.85);
    if (oc > 0.01)
    {
        for (auto& x : w)
            x.weight *= 1 - oc;
        w.push_back ({ "overcast", oc });
    }
    return w;
}

SkyColours skyColours (const Environment& e)
{
    struct K
    {
        double alt;
        unsigned zen, hor, sea, deep, amb;
    };
    // Picked from the light-and-time reference sheet
    static const K morning[] = { { -14, 0x1A2547, 0x2E3F6C, 0x1E4660, 0x15344A, 0x6F7FA8 }, { -4, 0x56609A, 0xF19C7E, 0x3E5E8A, 0x2C4870, 0xD9B8C0 },
                                 { 14, 0x5DA0DA, 0xCDE5F4, 0x2E86A8, 0x1F6688, 0xF2F2F0 }, { 42, 0x3F92D8, 0xA4D3F1, 0x2699AA, 0x1B7486, 0xFFFFFF } };
    static const K evening[] = { { -14, 0x1A2547, 0x2E3F6C, 0x1E4660, 0x15344A, 0x6F7FA8 }, { -6, 0x3C3F78, 0x8E70A8, 0x2D4670, 0x223558, 0x9C98C8 },
                                 { 1, 0x6A5A8E, 0xF5865A, 0x3A5C80, 0x2A4466, 0xE8B49A }, { 18, 0x6A90C2, 0xF0C99C, 0x3A7494, 0x295873, 0xF6E2C8 },
                                 { 42, 0x3F92D8, 0xA4D3F1, 0x2699AA, 0x1B7486, 0xFFFFFF } };
    const K* k = e.evening ? evening : morning;
    const int n = e.evening ? 5 : 4;
    int i = 0;
    double t = 0;
    if (e.sunAlt <= k[0].alt) i = 0, t = 0;
    else if (e.sunAlt >= k[n - 1].alt) i = n - 2, t = 1;
    else
        for (int j = 0; j < n - 1; ++j)
            if (e.sunAlt >= k[j].alt && e.sunAlt < k[j + 1].alt) i = j, t = (e.sunAlt - k[j].alt) / (k[j + 1].alt - k[j].alt);
    auto mix = [&] (unsigned a, unsigned b, float* out) {
        for (int c = 0; c < 3; ++c)
        {
            const double ca = ((a >> (16 - 8 * c)) & 0xff) / 255.0, cb = ((b >> (16 - 8 * c)) & 0xff) / 255.0;
            out[c] = (float) lerp (ca, cb, t);
        }
    };
    SkyColours s {};
    mix (k[i].zen, k[i + 1].zen, s.zenith);
    mix (k[i].hor, k[i + 1].hor, s.horizon);
    mix (k[i].sea, k[i + 1].sea, s.sea);
    mix (k[i].deep, k[i + 1].deep, s.seaDeep);
    mix (k[i].amb, k[i + 1].amb, s.ambient);

    // Overcast and storms: towards a grey that keeps the time of day's brightness
    const double grey = clamp01 (e.overcast * 0.85 + e.thunder * 0.1);
    const double dark = clamp01 (0.35 * std::max ({ e.rain, e.freezing, e.hail, e.sleet * 0.8 }) + 0.25 * e.thunder);
    auto toGrey = [&] (float* c, double tone) {
        const double l = 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
        const double g[3] = { l * 0.94, l * 0.98, l * 1.06 }; // slightly cool grey
        for (int j = 0; j < 3; ++j)
            c[j] = (float) (lerp (c[j], g[j] * tone, grey) * (1 - dark));
    };
    toGrey (s.zenith, 0.95);
    toGrey (s.horizon, 0.92);
    toGrey (s.sea, 0.95);
    toGrey (s.seaDeep, 0.95);
    for (float& c : s.ambient)
        c = (float) (c * (1 - 0.35 * grey) * (1 - 0.5 * dark));
    return s;
}
} // namespace atmos::gui
