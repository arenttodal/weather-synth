#pragma once
// Audition renderer for the location browser: the same phrase for every candidate, played by
// a separate instance of the real engine with the candidate's patch, on a worker thread.
// Nothing here touches the playing instrument; the finished buffer goes to the processor's
// audition slot only when the user presses Preview.
#include "PluginProcessor.h"

namespace atmos
{
class AuditionRenderer
{
public:
    using Clip = AtmosProcessor::AuditionClip;
    static constexpr double seconds = 3.0; // phrase plus release tail
    static constexpr const char* phraseLabel = "3-second phrase";

    AuditionRenderer();
    ~AuditionRenderer();

    // 'done' runs on the message thread with the token it was given, unless cancelled or
    // superseded by a later render (only the newest token is delivered)
    void render (const Patch&, double sampleRate, int token, std::function<void (std::shared_ptr<const Clip>, int token)> done);
    void cancel();
    bool busy() const { return pending.load() > 0; }

    // The phrase itself, synchronously (tests and the worker)
    static std::shared_ptr<Clip> renderPhrase (const Patch&, double sampleRate);

private:
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<int>> latest; // shared with jobs: outlives this object safely
    std::atomic<int> pending { 0 };
};
} // namespace atmos
