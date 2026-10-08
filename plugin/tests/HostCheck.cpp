// AtmosHostCheck <plugin path>: loads the built plugin the way a DAW does and
// exercises it (instantiate, several sample rates and block sizes, MIDI,
// automation, state save/restore into a fresh instance, editor open/close).
// CI also runs pluginval; this is the quick check that works offline.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <thread>

static int failures = 0;
#define EXPECT(cond, msg)                         \
    do                                            \
    {                                             \
        if (! (cond))                             \
        {                                         \
            ++failures;                           \
            std::printf ("  FAIL: %s\n", msg);    \
        }                                         \
        else                                      \
            std::printf ("  ok   %s\n", msg);     \
    } while (0)

static juce::Array<juce::AudioProcessorParameter*> macros (juce::AudioPluginInstance& p)
{
    juce::Array<juce::AudioProcessorParameter*> out;
    for (auto* prm : p.getParameters())
        if (juce::StringArray { "Tone", "Bloom", "Space", "Motion", "Intensity" }.contains (prm->getName (64))) out.add (prm);
    return out;
}

static float run (juce::AudioPluginInstance& p, double sr, int block, int blocks, juce::Random& rng)
{
    p.setPlayConfigDetails (0, 2, sr, block);
    p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    float peak = 0;
    bool finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b % 40 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48 + rng.nextInt (30), (juce::uint8) 100), rng.nextInt (block));
        if (b % 40 == 25) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        if (b % 7 == 0)
            for (auto* prm : macros (p))
                prm->setValue (rng.nextFloat());
        buf.clear();
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (c, i);
                finite &= std::isfinite (x);
                peak = std::max (peak, std::abs (x));
            }
    }
    p.releaseResources();
    return finite ? peak : -1.0f;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    // Print where it died if anything crashes (symbols need debug info next to the binaries)
    juce::SystemStats::setApplicationCrashHandler ([] (void*) {
        std::printf ("\n*** CRASH ***\n%s\n", juce::SystemStats::getStackBacktrace().toRawUTF8());
        std::fflush (stdout);
        std::_Exit (3);
    });
    if (argc < 2)
    {
        std::printf ("usage: AtmosHostCheck <path to .vst3 or .component>\n");
        return 2;
    }
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    const juce::String path = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName();

    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (path)) f->findAllTypesForFile (found, path);
    EXPECT (found.size() == 1, "plugin found in bundle");
    if (found.isEmpty()) return 1;
    const auto& desc = *found[0];
    std::printf ("  %s by %s, %s, instrument=%d\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(), desc.pluginFormatName.toRawUTF8(),
                 (int) desc.isInstrument);
    EXPECT (desc.isInstrument, "reports itself as an instrument");

    juce::String err;
    auto inst = fm.createPluginInstance (desc, 48000, 512, err);
    EXPECT (inst != nullptr, ("instantiates" + (err.isNotEmpty() ? " (" + err + ")" : juce::String())).toRawUTF8());
    if (! inst) return 1;
    std::printf ("  %d host-visible parameters\n", inst->getParameters().size());
    EXPECT (macros (*inst).size() == 5, "exposes the 5 macros (Tone, Bloom, Space, Motion, Intensity)");
    EXPECT (inst->acceptsMidi(), "accepts MIDI");

    juce::Random rng (7);
    bool allOk = true;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 32, 441, 512, 2048 })
        {
            const float pk = run (*inst, sr, block, (int) (sr * 3 / block), rng);
            if (pk < 0 || pk > 1.0f) allOk = false;
        }
    EXPECT (allOk, "audio stays finite and within full scale at 44.1/48/96 kHz, blocks 32..2048");

    // State round trip into a second instance
    for (auto* prm : macros (*inst))
        prm->setValue (0.25f);
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    EXPECT (state.getSize() > 100, "saves state");
    auto second = fm.createPluginInstance (desc, 48000, 512, err);
    second->setStateInformation (state.getData(), (int) state.getSize());
    bool same = true;
    for (auto* prm : macros (*second))
        same &= std::abs (prm->getValue() - 0.25f) < 1e-3f;
    EXPECT (same, "a fresh instance restores the saved macros");
    juce::MemoryBlock state2;
    second->getStateInformation (state2);
    EXPECT (state2 == state, "restored instance saves identical state (same Day)");

    // Editor open/close a few times
    if (inst->hasEditor())
    {
        bool ok = true;
        for (int i = 0; i < 3; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
            ok &= ed != nullptr && ed->getWidth() > 300;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            ed.reset();
        }
        EXPECT (ok, "editor opens and closes cleanly");
    }
    // pluginval's "Editor Automation": editor open, random values into any parameter
    // (MIDI CC emulation ones included), re-prepare per rate, noise in the buffer
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        auto params = inst->getParameters();
        bool finite = true;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (int bs : { 64, 128, 256, 512, 1024 })
            {
                inst->releaseResources();
                inst->setPlayConfigDetails (0, 2, sr, bs);
                inst->prepareToPlay (sr, bs);
                juce::AudioBuffer<float> ab (2, bs);
                juce::MidiBuffer mb;
                mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                for (int i = 0; i < (int) (sr / bs); ++i)
                {
                    for (int k = 0; k < 10; ++k)
                        params[rng.nextInt (params.size())]->setValue (rng.nextFloat());
                    for (int c = 0; c < 2; ++c)
                        for (int j = 0; j < bs; ++j)
                            ab.setSample (c, j, rng.nextFloat() * 2 - 1);
                    inst->processBlock (ab, mb);
                    mb.clear();
                    for (int c = 0; c < 2; ++c)
                        for (int j = 0; j < bs; ++j)
                            finite &= std::isfinite (ab.getSample (c, j));
                }
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            }
        ed.reset();
        EXPECT (finite, "editor automation: random values into all parameters while the editor is open");
    }
    // The same with pluginval's threading: audio + parameter changes on a background
    // thread while the message thread paints an on-screen editor
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
       #if ! JUCE_LINUX // bare Xvfb in CI has no window manager to accept a window
        ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        ed->setVisible (true);
       #endif
        std::atomic<bool> done { false }, finite { true };
        auto params = inst->getParameters();
        std::thread worker ([&] {
            juce::Random wr (99);
            inst->releaseResources();
            inst->prepareToPlay (48000, 256);
            juce::AudioBuffer<float> ab (2, 256);
            juce::MidiBuffer mb;
            for (int i = 0; i < 1000; ++i)
            {
                for (auto* prm : params)
                    prm->setValue (wr.nextFloat());
                if (i % 50 == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 48 + wr.nextInt (24), (juce::uint8) 100), 0);
                inst->processBlock (ab, mb);
                mb.clear();
                for (int c = 0; c < 2; ++c)
                    for (int j = 0; j < 256; ++j)
                        if (! std::isfinite (ab.getSample (c, j))) finite = false;
            }
            done = true;
        });
        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        worker.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        ed.reset();
        EXPECT (finite.load(), "background-thread automation with the editor on screen");
    }
    // pluginval's "Editor Automation" verbatim: no audio, every parameter set from a
    // background thread every 10 ms, 1000 times, editor on screen
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
       #if ! JUCE_LINUX
        ed->addToDesktop (juce::ComponentPeer::windowHasTitleBar);
        ed->setVisible (true);
       #endif
        std::atomic<bool> done { false };
        auto params = inst->getParameters();
        std::thread worker ([&] {
            juce::Random wr (5);
            for (int n = 0; n < 1000; ++n)
            {
                for (auto* prm : params)
                    prm->setValue (wr.nextFloat());
                juce::Thread::sleep (10);
            }
            done = true;
        });
        while (! done)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        worker.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        ed.reset();
        EXPECT (true, "pluginval-style editor automation (no audio, all parameters, 10 s)");
    }
    second.reset();
    inst.reset();
    std::printf ("%s\n", failures == 0 ? "HOST CHECK PASSED" : "HOST CHECK FAILED");
    return failures == 0 ? 0 : 1;
}
