#include "Audition.h"

namespace atmos
{
AuditionRenderer::AuditionRenderer() : latest (std::make_shared<std::atomic<int>> (0)) {}

AuditionRenderer::~AuditionRenderer()
{
    latest->store (-1);
    pool.removeAllJobs (true, 2000);
}

void AuditionRenderer::cancel() { latest->store (latest->load() + 1000000); }

std::shared_ptr<AuditionRenderer::Clip> AuditionRenderer::renderPhrase (const Patch& patch, double sr)
{
    auto clip = std::make_shared<Clip>();
    clip->sampleRate = sr;
    const int total = (int) std::ceil (seconds * sr), block = 512;
    clip->audio.setSize (2, total);
    clip->audio.clear();
    Core core;
    core.prepare (sr, block);
    core.setPatch (patch);
    // An arpeggiated chord, held, then released: the same notes for every candidate
    struct Ev
    {
        double t;
        int note;
        bool on;
    };
    const Ev evs[] = { { 0.0, 48, true }, { 0.22, 55, true }, { 0.44, 60, true }, { 0.66, 64, true },
                       { 2.1, 48, false }, { 2.1, 55, false }, { 2.1, 60, false }, { 2.1, 64, false } };
    size_t next = 0;
    for (int pos = 0; pos < total;)
    {
        while (next < std::size (evs) && (int) (evs[next].t * sr) <= pos)
        {
            if (evs[next].on) core.noteOn (evs[next].note, 0.8f);
            else core.noteOff (evs[next].note);
            ++next;
        }
        int len = juce::jmin (block, total - pos);
        if (next < std::size (evs)) len = juce::jmin (len, juce::jmax (1, (int) (evs[next].t * sr) - pos));
        core.process (clip->audio.getWritePointer (0, pos), clip->audio.getWritePointer (1, pos), len);
        pos += len;
    }
    // The tail is cut with a gentle fade so every phrase ends cleanly at the same length
    const int fade = (int) (0.35 * sr);
    for (int c = 0; c < 2; ++c)
        clip->audio.applyGainRamp (c, total - fade, fade, 1.0f, 0.0f);
    return clip;
}

void AuditionRenderer::render (const Patch& patch, double sr, int token, std::function<void (std::shared_ptr<const Clip>, int)> done)
{
    latest->store (token);
    ++pending;
    auto keep = latest;
    auto* counter = &pending;
    pool.addJob ([patch, sr, token, done, keep, counter] {
        std::shared_ptr<const Clip> clip;
        if (keep->load() == token) clip = renderPhrase (patch, sr);
        juce::MessageManager::callAsync ([clip, token, done, keep, counter] {
            if (keep->load() == -1) return; // the renderer is gone
            --(*counter);
            if (keep->load() == token && clip != nullptr) done (clip, token);
        });
    });
}
} // namespace atmos
