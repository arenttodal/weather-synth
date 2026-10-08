// C API over the shared engine for the browser build (see build.sh).
// One engine per module instance; the AudioWorklet owns it.
#include "atmos/Astro.h"
#include "atmos/Bible.h"
#include "atmos/Core.h"
#include <cstdio>
#include <string>

#define EXPORT(name) extern "C" __attribute__ ((export_name (#name)))

namespace
{
atmos::Core* core = nullptr;
std::vector<atmos::CoreSound> bible;
atmos::Climate climate;
double precip = 0;
atmos::Macros macros;
float left[1024], right[1024];
std::string scratch;
} // namespace

EXPORT (atmos_init) void atmos_init (double sampleRate)
{
    if (! core) core = new atmos::Core();
    core->prepare (sampleRate, 1024);
}

EXPORT (atmos_param_count) int atmos_param_count() { return atmos::kNumParams; }

// The schema as JSON, so the designer never hard-codes ranges
EXPORT (atmos_schema) const char* atmos_schema()
{
    scratch = "{\"params\":[";
    char buf[512];
    for (int i = 0; i < atmos::kNumParams; ++i)
    {
        const auto& p = atmos::paramInfo (i);
        const char* sc = p.scale == atmos::Scale::log ? "log" : p.scale == atmos::Scale::choice ? "choice" : "linear";
        std::snprintf (buf, sizeof buf, "%s{\"id\":\"%s\",\"name\":\"%s\",\"group\":\"%s\",\"min\":%g,\"max\":%g,\"def\":%g,\"scale\":\"%s\",\"choices\":\"%s\"}",
                       i ? "," : "", p.id, p.name, p.group, p.min, p.max, p.def, sc, p.choices ? p.choices : "");
        scratch += buf;
    }
    scratch += "]}";
    return scratch.c_str();
}

EXPORT (atmos_set_param) void atmos_set_param (int i, double v) { if (core) core->setParam (i, v); }
EXPORT (atmos_get_param) double atmos_get_param (int i) { return core && i >= 0 && i < atmos::kNumParams ? core->patch()[i] : 0.0; }
EXPORT (atmos_note_on) void atmos_note_on (int note, double vel) { if (core) core->noteOn (note, (float) vel); }
EXPORT (atmos_note_off) void atmos_note_off (int note) { if (core) core->noteOff (note); }
EXPORT (atmos_all_off) void atmos_all_off() { if (core) core->allNotesOff(); }
EXPORT (atmos_active_voices) int atmos_active_voices() { return core ? core->activeVoices() : 0; }

EXPORT (atmos_render) void atmos_render (int n)
{
    if (! core) return;
    core->process (left, right, n > 1024 ? 1024 : n);
}
EXPORT (atmos_left) float* atmos_left() { return left; }
EXPORT (atmos_right) float* atmos_right() { return right; }

// ---- The sound bible, set from the designer
EXPORT (atmos_bible_resize) void atmos_bible_resize (int n) { bible.resize ((size_t) (n < 0 ? 0 : n)); }
EXPORT (atmos_bible_set) void atmos_bible_set (int s, int which, int p, double v)
{
    if (s < 0 || s >= (int) bible.size() || p < 0 || p >= atmos::kNumParams) return;
    auto& cs = bible[(size_t) s];
    (which == 0 ? cs.home : which == 1 ? cs.lo : cs.hi)[p] = v;
}
EXPORT (atmos_bible_anchor) void atmos_bible_anchor (int s, double temp, double wet, double light)
{
    if (s < 0 || s >= (int) bible.size()) return;
    bible[(size_t) s].anchorTemp = temp;
    bible[(size_t) s].anchorWet = wet;
    bible[(size_t) s].anchorLight = light;
}

EXPORT (atmos_set_climate) void atmos_set_climate (double temp, double humidity, double pr, double wind, double clouds, double sun, double moon,
                                                   double pressure, double seed)
{
    climate.temp = temp;
    climate.humidity = humidity;
    climate.precip = pr;
    climate.wind = wind;
    climate.clouds = clouds;
    climate.sun = sun;
    climate.moon = moon;
    climate.pressure = pressure;
    climate.seed = (uint32_t) seed;
    precip = pr;
}
EXPORT (atmos_set_macros) void atmos_set_macros (double tone, double bloom, double space, double motion, double intensity)
{
    macros = { tone, bloom, space, motion, intensity };
}

// Choose today's core sound and load the resolved patch; returns the chosen index
EXPORT (atmos_resolve) int atmos_resolve()
{
    const int i = atmos::chooseCoreSound (bible, climate, precip);
    if (i < 0 || ! core) return -1;
    core->setPatch (atmos::resolvePatch (bible[(size_t) i], climate, precip, macros));
    return i;
}

// The patch currently loaded (after atmos_resolve, the weather's choice)
EXPORT (atmos_patch) const double* atmos_patch()
{
    static double out[atmos::kNumParams];
    for (int i = 0; i < atmos::kNumParams; ++i)
        out[i] = core ? core->patch()[i] : 0.0;
    return out;
}

// Forces, for the designer's readout: energy heat cold wet drench humid motion gale light fullness dark low
EXPORT (atmos_forces) const double* atmos_forces()
{
    static double out[12];
    const auto f = atmos::forces (climate, 1.0);
    const double v[12] = { f.energy, f.heat, f.cold, f.wet, f.drench, f.humid, f.motion, f.gale, f.light, f.fullness, f.dark, f.low };
    for (int i = 0; i < 12; ++i)
        out[i] = v[i];
    return out;
}

// Sun and moon for a place and moment, for the designer's "real sky" button
EXPORT (atmos_sun) double atmos_sun (double lat, double lon, double unix) { return atmos::sunElevation (lat, lon, (int64_t) unix); }
EXPORT (atmos_moon) double atmos_moon (double unix) { return atmos::moonPhase ((int64_t) unix); }

// libc++ in this toolchain is built without exceptions but still references the throw hooks
extern "C" void* __cxa_allocate_exception (unsigned long) { __builtin_trap(); }
extern "C" void __cxa_throw (void*, void*, void (*) (void*)) { __builtin_trap(); }
