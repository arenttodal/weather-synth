// AtmosGuiBench: separates DSP cost (audio thread) from GUI cost (message thread).
//
//   AtmosGuiBench [--seconds 10] [--instances 1] [--editors 0] [--fixture name] [--quality normal|economy|still]
//                 [--cycles N]   (open/close every editor N times first, then report memory)
//                 [--hold N]     (stay alive N s after printing, for heap/vmmap)
//
// Prints one JSON object. CPU times come from per-thread CPU clocks, so they are
// independent of how busy the machine is; "load" is CPU time divided by wall time.
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"
#include "../Source/Storage.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <thread>
#include <vector>
#if JUCE_MAC
 #include <mach/mach.h>
#endif

namespace
{
double threadCpuSeconds()
{
    timespec ts {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Linux: resident set. macOS: physical footprint (what Activity Monitor shows); the resident
// size there keeps counting freed window surfaces and reusable malloc pages, so it climbed with
// every editor open/close while `leaks` found nothing leaked and the footprint stayed flat.
double residentMiB()
{
#if JUCE_MAC
    task_vm_info_data_t info {};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info (mach_task_self(), TASK_VM_INFO, (task_info_t) &info, &count) == KERN_SUCCESS) return info.phys_footprint / 1048576.0;
    return 0;
#elif JUCE_LINUX
    long pages = 0, rss = 0;
    if (FILE* f = std::fopen ("/proc/self/statm", "r"))
    {
        if (std::fscanf (f, "%ld %ld", &pages, &rss) != 2) rss = 0;
        std::fclose (f);
    }
    return rss * (double) sysconf (_SC_PAGESIZE) / 1048576.0;
#else
    return 0;
#endif
}

juce::String arg (const juce::StringArray& a, const juce::String& key, const juce::String& def)
{
    const int i = a.indexOf (key);
    return i >= 0 && i + 1 < a.size() ? a[i + 1] : def;
}

struct AudioRunner
{
    std::vector<AtmosProcessor*> procs;
    std::atomic<bool> run { true };
    double cpu = 0, wall = 0, worstBlockMs = 0;
    std::vector<double> blockMs;
    int xruns = 0; // blocks whose processing took longer than their duration
    std::thread t;

    void start()
    {
        t = std::thread ([this] {
            constexpr double sr = 48000;
            constexpr int block = 256;
            const double blockDur = block / sr;
            juce::AudioBuffer<float> buf (2, block);
            const auto c0 = threadCpuSeconds();
            const auto w0 = juce::Time::getMillisecondCounterHiRes();
            double next = w0;
            int n = 0;
            while (run.load())
            {
                juce::MidiBuffer midi;
                if (n % 750 == 0) // a new six-note chord every 4 s
                    for (int k = 0; k < 6; ++k)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + k * 4 + (n / 750) % 5, (juce::uint8) 96), 0);
                if (n % 750 == 600)
                    midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                const double b0 = threadCpuSeconds();
                for (auto* p : procs)
                {
                    juce::MidiBuffer m (midi);
                    p->processBlock (buf, m);
                }
                const double ms = (threadCpuSeconds() - b0) * 1000.0;
                blockMs.push_back (ms);
                worstBlockMs = std::max (worstBlockMs, ms);
                if (ms > blockDur * 1000.0) ++xruns;
                ++n;
                next += blockDur * 1000.0;
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (next > now) std::this_thread::sleep_for (std::chrono::microseconds ((int) ((next - now) * 1000)));
            }
            cpu = threadCpuSeconds() - c0;
            wall = (juce::Time::getMillisecondCounterHiRes() - w0) / 1000.0;
        });
    }
    void stop()
    {
        run = false;
        if (t.joinable()) t.join();
    }
};
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray a;
    for (int i = 1; i < argc; ++i)
        a.add (argv[i]);
    const double seconds = arg (a, "--seconds", "10").getDoubleValue();
    const int instances = juce::jmax (0, arg (a, "--instances", "1").getIntValue());
    const int editors = juce::jlimit (0, instances, arg (a, "--editors", "0").getIntValue());
    const int cycles = arg (a, "--cycles", "0").getIntValue();
    const juce::String fixture = arg (a, "--fixture", "");
    const juce::String quality = arg (a, "--quality", "");

    // Never touch the user's real settings or the network
    atmos::Storage::setFolderOverride (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atmos-guibench"));
    atmos::Storage::setRelayUrl ("http://127.0.0.1:9");

    const double rss0 = residentMiB();
    juce::OwnedArray<AtmosProcessor> procs;
    for (int i = 0; i < instances; ++i)
    {
        auto* p = procs.add (new AtmosProcessor());
        p->prepareToPlay (48000, 256);
    }
    const double rssProcs = residentMiB();

    auto openEditor = [&] (AtmosProcessor& p) -> juce::AudioProcessorEditor* {
        auto* ed = p.createEditorIfNeeded();
        if (fixture.isNotEmpty() || quality.isNotEmpty())
            if (auto* e = dynamic_cast<AtmosEditor*> (ed)) e->applyBenchOptions (fixture, quality);
        ed->addToDesktop (0); // plain window: no window manager is needed (CI runs under Xvfb)
        ed->setVisible (true);
        return ed;
    };
    auto closeEditor = [] (AtmosProcessor& p) {
        if (auto* ed = p.getActiveEditor())
        {
            p.editorBeingDeleted (ed);
            delete ed;
        }
    };

    // Open/close cycles first (leak check)
    double openMsCold = 0, openMsWarm = 0;
    for (int c = 0; c < (instances > 0 ? cycles : 0); ++c)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        openEditor (*procs[0]);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0 - 30;
        (c == 0 ? openMsCold : openMsWarm) += ms;
        closeEditor (*procs[0]);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    if (cycles > 1) openMsWarm /= (cycles - 1);
    const double rssAfterCycles = residentMiB();

    const auto e0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < editors; ++i)
        openEditor (*procs[i]);
    const double openAllMs = juce::Time::getMillisecondCounterHiRes() - e0;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500); // settle: first paints, asset decode
    const double rssOpen = residentMiB();

    AudioRunner audio;
    for (auto* p : procs)
        audio.procs.push_back (p);
    audio.start();
    const double m0 = threadCpuSeconds();
    const double w0 = juce::Time::getMillisecondCounterHiRes();
    juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000));
    const double msgCpu = threadCpuSeconds() - m0;
    const double msgWall = (juce::Time::getMillisecondCounterHiRes() - w0) / 1000.0;
    audio.stop();
    const double rssRun = residentMiB();

    int frames = 0;
    double frameP95 = 0;
    for (auto* p : procs)
        if (auto* e = dynamic_cast<AtmosEditor*> (p->getActiveEditor()))
        {
            frames += e->benchFrames();
            frameP95 = std::max (frameP95, e->benchUpdateP95Ms());
        }

    for (auto* p : procs)
        closeEditor (*p);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    const double rssClosed = residentMiB();

    std::sort (audio.blockMs.begin(), audio.blockMs.end());
    auto pct = [&] (double q) { return audio.blockMs.empty() ? 0.0 : audio.blockMs[(size_t) std::min<double> (audio.blockMs.size() - 1, q * audio.blockMs.size())]; };
    std::printf ("{\"instances\":%d,\"editors\":%d,\"fixture\":\"%s\",\"quality\":\"%s\",\"seconds\":%.1f,"
                 "\"dspLoad\":%.4f,\"dspBlockMsP50\":%.4f,\"dspBlockMsP99\":%.4f,\"dspBlockMsMax\":%.3f,\"overruns\":%d,"
                 "\"guiLoad\":%.4f,\"guiCpuMsPerSecond\":%.2f,\"frames\":%d,\"fps\":%.1f,\"updateP95Ms\":%.3f,"
                 "\"openAllMs\":%.1f,\"openMsCold\":%.1f,\"openMsWarm\":%.1f,"
                 "\"rssStartMiB\":%.1f,\"rssProcsMiB\":%.1f,\"rssAfterCyclesMiB\":%.1f,\"rssOpenMiB\":%.1f,\"rssRunMiB\":%.1f,\"rssClosedMiB\":%.1f,\"cycles\":%d}\n",
                 instances, editors, fixture.toRawUTF8(), quality.toRawUTF8(), seconds, audio.cpu / std::max (1e-9, audio.wall), pct (0.5), pct (0.99),
                 audio.worstBlockMs, audio.xruns, msgCpu / std::max (1e-9, msgWall), msgCpu * 1000.0 / std::max (1e-9, msgWall), frames,
                 editors > 0 ? frames / (double) editors / std::max (1e-9, msgWall) : 0.0, frameP95, openAllMs, openMsCold, openMsWarm, rss0,
                 rssProcs, rssAfterCycles, rssOpen, rssRun, rssClosed, cycles);
    std::fflush (stdout);
    // --hold N: stay alive N seconds after reporting, so heap/vmmap can inspect the process
    if (const double hold = arg (a, "--hold", "0").getDoubleValue(); hold > 0)
        juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (hold * 1000));
    return 0;
}
