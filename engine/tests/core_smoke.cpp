// Native smoke test for the shared engine: every source x movement x space type,
// checking for NaN, clipping, silence and runaway tails.
#include "atmos/Core.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace atmos;

struct Stats { double rms, peak, tail; bool finite; };

static Stats render (Patch p, double sr = 48000)
{
    Core c;
    c.prepare (sr, 512);
    c.setPatch (p);
    const int total = (int) (sr * 10), blk = 256;
    std::vector<float> L (blk), R (blk);
    double sum = 0, peak = 0, tail = 0; long n = 0, nt = 0; bool finite = true;
    const int chord[] = { 48, 55, 62, 64 };
    for (int pos = 0; pos < total; pos += blk)
    {
        const double t = pos / sr;
        if (pos == 0) for (int k : chord) c.noteOn (k, 0.8f);
        if (t >= 2.0 && t < 2.0 + blk / sr) for (int k : chord) c.noteOff (k);
        for (int i = 0; i < 8; ++i)
        {
            const double t0 = 2.4 + i * 0.18;
            if (t >= t0 && t < t0 + blk / sr) c.noteOn (60 + (i * 5) % 19, 0.7f);
            if (t >= t0 + 0.12 && t < t0 + 0.12 + blk / sr) c.noteOff (60 + (i * 5) % 19);
        }
        c.process (L.data(), R.data(), blk);
        for (int i = 0; i < blk; ++i)
            for (float x : { L[i], R[i] })
            {
                if (! std::isfinite (x)) finite = false;
                peak = std::max (peak, (double) std::abs (x));
                if (t < 4.5) { sum += x * x; ++n; }
                if (t > 9.0) { tail += x * x; ++nt; }
            }
    }
    return { 10 * std::log10 (sum / n + 1e-20), peak, 10 * std::log10 (tail / std::max (1L, nt) + 1e-20), finite };
}

int main()
{
    const char* flt[] = { "Moog", "Prophet", "SEM-LP", "SEM-BP" };
    const char* vm[] = { "Poly", "Stack2", "Stack4" };
    const char* mov[] = { "Off", "Tape", "Chorus", "Pulse", "Shaper" };
    int fails = 0, count = 0; double lo = 0, hi = -200;
    for (int f = 0; f < 4; ++f)
        for (int v = 0; v < 3; ++v)
            for (int m = 0; m < 5; ++m)
            {
                Patch p;
                p[filtType] = f; p[voiceMode] = v; p[movMode] = m; p[movAmount] = 0.7;
                p[spaceType] = (f + m) % 4; p[spaceSend] = 0.6; p[echoSend] = 0.5; p[echoType] = (v + m) % 2;
                p[kalAmount] = (m % 2) ? 0.8 : 0.2; p[resonance] = (m % 3) * 0.48; p[filtDrive] = v * 0.45;
                p[oscSync] = m == 3; p[pmEnvA] = m == 3 ? 0.5 : 0; p[pmOscB] = f == 1 ? 0.6 : 0;
                p[oscAWave] = (m % 2) ? 0.7 : 0; p[oscBWave] = (f + v) % 3; p[mixNoise] = m == 4 ? 0.8 : 0.1; p[mixSub] = 0.6;
                p[drive] = v == 2 ? 0.8 : 0.15; p[crushBits] = v == 2 ? 8 : 16; p[bedLevel] = 0.4; p[slop] = 1;
                p[portamento] = v == 1 ? 0.5 : 0; p[lfoPwm] = 0.6; p[lfoFilter] = m * 0.2; p[lfoShape] = m % 4;
                const auto st = render (p);
                ++count;
                const bool ok = st.finite && st.peak <= 0.9 && st.rms > -50 && st.rms < -8;
                if (! ok) { ++fails; std::printf ("FAIL %s/%s/%s rms %.1f peak %.3f tail %.1f finite %d\n", flt[f], vm[v], mov[m], st.rms, st.peak, st.tail, st.finite); }
                lo = lo == 0 ? st.rms : std::min (lo, st.rms); hi = std::max (hi, st.rms);
            }
    // Self-oscillation must stay bounded
    for (int f = 0; f < 4; ++f) { Patch p; p[filtType] = f; p[resonance] = 1; p[filtDrive] = 1; p[cutoff] = 300; auto st = render (p); ++count;
        if (! st.finite || st.peak > 0.9) { ++fails; std::printf ("FAIL self-oscillating %s peak %.3f\n", flt[f], st.peak); } }
    // Defaults per filter, for level calibration
    for (int f = 0; f < 4; ++f) { Patch p; p[filtType] = f; auto st = render (p); std::printf ("default %-8s rms %.1f dB peak %.2f\n", flt[f], st.rms, st.peak); }
    std::printf ("%d combinations, rms %.1f .. %.1f dB, %d failed\n", count, lo, hi, fails);
    return fails ? 1 : 0;
}
