#pragma once
#include <array>
#include <vector>

namespace atmos
{
// Five harmonic frames, 16 partials each, ordered cold -> hot (same as lab.html)
struct Frame
{
    const char* name;
    std::array<float, 16> partials;
};
const std::array<Frame, 5>& frames();
std::array<float, 16> framePartials (double position);
const char* frameName (double position);

// Band-limited single-cycle tables, one per octave so high notes don't alias.
// Built off the audio thread, read-only afterwards.
class WavetableBank
{
public:
    static constexpr int kSize = 2048;
    static constexpr int kOctaves = 11; // fundamentals from 27.5 Hz up

    WavetableBank (const std::array<float, 16>& partials, double sampleRate);

    // Linear-interpolated lookup; phase in [0, 1)
    inline float lookup (int table, double phase) const noexcept
    {
        const double x = phase * kSize;
        const int i = (int) x;
        const float t = (float) (x - i);
        const float* tb = tables[(size_t) table].data();
        return tb[i] + t * (tb[i + 1] - tb[i]);
    }

    int tableFor (double freqHz) const noexcept;

private:
    std::vector<std::vector<float>> tables; // kSize + 1 samples (wraparound guard)
};
} // namespace atmos
