#pragma once
#include "Bible.h"
#include "Core.h"
#include "Day.h"
#include "WeatherClient.h"
#include <juce_audio_processors/juce_audio_processors.h>

class AtmosProcessor : public juce::AudioProcessor,
                       public juce::ChangeBroadcaster,
                       private juce::Timer
{
public:
    AtmosProcessor();
    ~AtmosProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Atmospheric"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Today"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Day control (message thread) ----
    enum class Status { dealing, live, estimate, restored, preview, previewLoading };
    void dealToday();
    void previewAt (double lat, double lon);
    void endPreview();
    bool isPreviewing() const { return previewing; }
    bool keepDay (const juce::String& name);
    void loadDay (const atmos::Day&);
    bool dayIsCurrent() const; // false when the loaded Day is from an earlier date
    // Copies taken under the lock: the host may restore a project from another thread
    atmos::Day currentDay() const;
    atmos::Climate currentClimate() const { return currentDay().climate(); }
    atmos::CoreSound currentSound() const;
    atmos::Patch currentPatch() const; // what the engine is playing (core sound + weather + macros)
    Status status() const { return statusNow; }
    juce::String statusMessage() const { return statusText; }

    // The bible of core sounds this plugin deals from
    const std::vector<atmos::CoreSound>& getBible() const { return bible; }
    juce::String bibleSource() const { return bibleFrom; }

    juce::AudioProcessorValueTreeState apvts;
    atmos::WeatherClient weather;

    // Tests and offline tools
    atmos::Core& getCore() { return core; }
    void applyDayForTest (const atmos::Day& d) { applyDay (d); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void timerCallback() override;
    // Plays a Day. A Day that has been heard before brings its own core sound; a new one draws from the bible.
    void applyDay (const atmos::Day&);
    atmos::Macros readMacros() const;
    void setStatus (Status, const juce::String&);
    void handleMidi (const juce::MidiMessage&);

    atmos::Core core;
    std::vector<atmos::CoreSound> bible;
    juce::String bibleFrom;

    // 'day' and 'sound' are written on the message thread; dayLock guards them for state saves
    atmos::Day day, homeDay;
    atmos::CoreSound sound;
    juce::CriticalSection dayLock;
    bool previewing = false, dealtOnce = false;
    std::atomic<bool> stateRestored { false };
    std::atomic<int> dayGeneration { 0 }; // bumped whenever something else sets the Day, so a late weather reply can't overwrite it
    Status statusNow = Status::dealing;
    juce::String statusText;

    // Hand-off to the audio thread: plain data only, so resolving a patch never allocates there
    struct AudioSide
    {
        atmos::Patch home, lo, hi;
        atmos::Climate climate;
    };
    juce::SpinLock audioLock;
    AudioSide shared, audio;
    std::atomic<int> sharedVersion { 0 };
    int audioVersion = -1;
    atmos::Macros audioMacros;
    juce::AudioBuffer<float> scratch;

    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pBloom = nullptr;
    std::atomic<float>* pSpace = nullptr;
    std::atomic<float>* pMotion = nullptr;
    std::atomic<float>* pIntensity = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AtmosProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosProcessor)
};
