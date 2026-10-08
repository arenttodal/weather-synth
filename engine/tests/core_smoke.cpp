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
    const char* src[] = { "Wavetable", "FM", "Supersaw", "Pluck" };
    const char* mov[] = { "Off", "Tape", "Chorus", "Pulse", "Shaper" };
    const char* spc[] = { "Room", "Hall", "Plate", "Spring" };
    int fails = 0; double lo = 0, hi = -200;
    for (int s = 0; s < 4; ++s)
        for (int m = 0; m < 5; ++m)
            for (int sp = 0; sp < 4; ++sp)
            {
                Patch p;
                p[srcType] = s; p[movMode] = m; p[movAmount] = 0.7; p[spaceType] = sp; p[spaceSend] = 0.6;
                p[echoSend] = 0.5; p[echoType] = (s + m) % 2; p[kalAmount] = (sp % 2) ? 0.8 : 0.2;
                p[filtType] = (s + sp) % 5; p[drive] = s == 2 ? 0.6 : 0.1; p[crushBits] = s == 1 ? 8 : 16;
                p[bedLevel] = 0.4; p[srcSub] = 0.3; p[srcShimmer] = 0.2; p[srcBreath] = 0.2;
                const auto st = render (p);
                const bool ok = st.finite && st.peak <= 0.9 && st.rms > -50 && st.rms < -8;
                if (! ok) { ++fails; std::printf ("FAIL %s/%s/%s rms %.1f peak %.3f tail %.1f finite %d\n", src[s], mov[m], spc[sp], st.rms, st.peak, st.tail, st.finite); }
                lo = lo == 0 ? st.rms : std::min (lo, st.rms); hi = std::max (hi, st.rms);
            }
    // Defaults per source, for level calibration
    for (int s = 0; s < 4; ++s) { Patch p; p[srcType] = s; auto st = render (p); std::printf ("default %-9s rms %.1f dB peak %.2f\n", src[s], st.rms, st.peak); }
    std::printf ("80 combinations, rms %.1f .. %.1f dB, %d failed\n", lo, hi, fails);
    return fails ? 1 : 0;
}
