#pragma once
#include "ClimateMapper.h"
#include "Day.h"
#include "Engine.h"
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
    const atmos::Day& currentDay() const { return day; }
    atmos::Climate currentClimate() const { return day.climate(); }
    atmos::Params currentParams() const;
    Status status() const { return statusNow; }
    juce::String statusMessage() const { return statusText; }

    juce::AudioProcessorValueTreeState apvts;
    atmos::WeatherClient weather;

    // Tests and offline tools
    atmos::Engine& getEngine() { return engine; }
    void applyDayForTest (const atmos::Day& d) { applyDay (d); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void timerCallback() override;
    void applyDay (const atmos::Day&);
    atmos::Macros readMacros() const;
    void setStatus (Status, const juce::String&);

    atmos::Engine engine;
    atmos::Day day, homeDay;
    bool previewing = false, stateRestored = false, dealtOnce = false;
    Status statusNow = Status::dealing;
    juce::String statusText;
    int timerTicks = 0;

    // Climate hand-off to the audio thread
    juce::SpinLock climateLock;
    atmos::Climate sharedClimate;
    std::atomic<int> climateVersion { 0 };
    int audioClimateVersion = -1;
    atmos::Climate audioClimate;
    atmos::Macros audioMacros;
    bool audioHasTarget = false;

    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pBloom = nullptr;
    std::atomic<float>* pSpace = nullptr;
    std::atomic<float>* pMotion = nullptr;
    std::atomic<float>* pIntensity = nullptr;

    std::vector<std::shared_ptr<const atmos::WavetableBank>> retiredBanks;
    double lastFrame = -1;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AtmosProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosProcessor)
};
