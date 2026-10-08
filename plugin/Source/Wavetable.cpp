#include "Wavetable.h"
#include <algorithm>
#include <cmath>

namespace atmos
{
const std::array<Frame, 5>& frames()
{
    static const std::array<Frame, 5> f = { {
        { "Glass", { 1, 0, 0.5f, 0, 0, 0, 0.35f, 0, 0, 0, 0.25f, 0, 0, 0.15f, 0, 0 } },
        { "Breath", { 1, 0.25f, 0.08f, 0.03f, 0.01f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Reed", { 1, 0, 0.33f, 0, 0.2f, 0, 0.14f, 0, 0.11f, 0, 0.09f, 0, 0.07f, 0, 0.06f, 0 } },
        { "String", { 1, 1 / 2.f, 1 / 3.f, 1 / 4.f, 1 / 5.f, 1 / 6.f, 1 / 7.f, 1 / 8.f, 1 / 9.f, 1 / 10.f, 1 / 11.f, 1 / 12.f, 1 / 13.f, 1 / 14.f, 1 / 15.f, 1 / 16.f } },
        { "Brass", { 0.6f, 0.8f, 1, 0.9f, 0.7f, 0.5f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f, 0.01f } },
    } };
    return f;
}

std::array<float, 16> framePartials (double pos)
{
    const auto& F = frames();
    const double x = std::clamp (pos, 0.0, 1.0) * (double) (F.size() - 1);
    const int i = std::min ((int) F.size() - 2, (int) std::floor (x));
    const float t = (float) (x - i);
    std::array<float, 16> out {};
    for (size_t k = 0; k < 16; ++k)
        out[k] = F[(size_t) i].partials[k] + (F[(size_t) i + 1].partials[k] - F[(size_t) i].partials[k]) * t;
    return out;
}

const char* frameName (double pos)
{
    const auto& F = frames();
    const int i = (int) std::lround (std::clamp (pos, 0.0, 1.0) * (double) (F.size() - 1));
    return F[(size_t) i].name;
}

WavetableBank::WavetableBank (const std::array<float, 16>& partials, double sampleRate)
{
    const double twoPi = 6.283185307179586;
    tables.resize (kOctaves);
    float scale = 1.0f;
    for (int oct = 0; oct < kOctaves; ++oct)
    {
        const double maxFundamental = 27.5 * std::pow (2.0, oct + 1);
        const int maxHarm = std::clamp ((int) std::floor (0.45 * sampleRate / maxFundamental), 1, 16);
        auto& tb = tables[(size_t) oct];
        tb.assign (kSize + 1, 0.0f);
        for (int n = 0; n < kSize; ++n)
        {
            double s = 0;
            for (int h = 1; h <= maxHarm; ++h)
                if (partials[(size_t) h - 1] != 0.0f)
                    s += partials[(size_t) h - 1] * std::sin (twoPi * h * n / kSize);
            tb[(size_t) n] = (float) s;
        }
        if (oct == 0)
        {
            // Normalise once from the fullest table so every octave keeps the same level
            float peak = 0;
            for (float v : tb)
                peak = std::max (peak, std::abs (v));
            scale = peak > 0 ? 1.0f / peak : 1.0f;
        }
        for (auto& v : tb)
            v *= scale;
        tb[kSize] = tb[0];
    }
}

int WavetableBank::tableFor (double f) const noexcept
{
    if (f <= 55.0) return 0;
    const int t = (int) std::floor (std::log2 (f / 27.5));
    return std::clamp (t, 0, kOctaves - 1);
}
} // namespace atmos
