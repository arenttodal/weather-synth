#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Storage.h"

using namespace atmos;

namespace
{
int64_t nowUnix() { return juce::Time::currentTimeMillis() / 1000; }
const char* kMacroIds[] = { "tone", "bloom", "space", "motion", "intensity" };
const char* kMacroNames[] = { "Tone", "Bloom", "Space", "Motion", "Intensity" };
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AtmosProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < 5; ++i)
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { kMacroIds[i], 1 }, kMacroNames[i],
                                                            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    return l;
}

AtmosProcessor::AtmosProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MACROS", layout())
{
    pTone = apvts.getRawParameterValue ("tone");
    pBloom = apvts.getRawParameterValue ("bloom");
    pSpace = apvts.getRawParameterValue ("space");
    pMotion = apvts.getRawParameterValue ("motion");
    pIntensity = apvts.getRawParameterValue ("intensity");

    // Sound immediately from the last known sky (or an estimate) so the plugin
    // is never silent while the network answers.
    auto last = Storage::lastSky();
    Day start = last.isValid() ? estimateDay (last.lat, last.lon, last.placeName, nowUnix())
                               : estimateDay (63.43, 10.39, "Trondheim", nowUnix());
    if (last.isValid()) start.country = last.country;
    applyDay (start);
    setStatus (Status::dealing, ATMOS_U8 ("Reading today's sky…"));

    // Give the host a moment to restore a saved project before dealing today
    startTimer (400);
}

AtmosProcessor::~AtmosProcessor() { stopTimer(); }

bool AtmosProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void AtmosProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr, block);
    lastFrame = engine.currentFrame();
    audioClimateVersion = -1;
}

Macros AtmosProcessor::readMacros() const
{
    Macros m;
    m.tone = pTone->load();
    m.bloom = pBloom->load();
    m.space = pSpace->load();
    m.motion = pMotion->load();
    m.intensity = pIntensity->load();
    return m;
}

Params AtmosProcessor::currentParams() const { return resolveLeash (day.climate(), readMacros()); }

void AtmosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    bool changed = false;
    const int v = climateVersion.load();
    if (v != audioClimateVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (climateLock);
        if (tl.isLocked())
        {
            audioClimate = sharedClimate;
            audioClimateVersion = v;
            changed = true;
        }
    }
    const Macros m = readMacros();
    if (changed || ! audioHasTarget || m.tone != audioMacros.tone || m.bloom != audioMacros.bloom || m.space != audioMacros.space
        || m.motion != audioMacros.motion || m.intensity != audioMacros.intensity)
    {
        audioMacros = m;
        engine.setTarget (resolveLeash (audioClimate, m));
        audioHasTarget = true;
    }
    engine.render (buffer, midi);
}

void AtmosProcessor::applyDay (const Day& d)
{
    {
        // Hosts may save the project from another thread at any moment
        const juce::ScopedLock sl (dayLock);
        day = d;
    }
    const Climate c = d.climate();
    const double frame = natureParams (c).frame;
    if (std::abs (frame - lastFrame) > 0.0005)
    {
        lastFrame = frame;
        if (auto old = engine.installFrame (frame)) retiredBanks.push_back (std::move (old));
    }
    {
        const juce::SpinLock::ScopedLockType sl (climateLock);
        sharedClimate = c;
    }
    climateVersion.fetch_add (1);
    sendChangeMessage();
}

void AtmosProcessor::setStatus (Status s, const juce::String& msg)
{
    statusNow = s;
    statusText = msg;
    sendChangeMessage();
}

void AtmosProcessor::dealToday()
{
    previewing = false;
    setStatus (Status::dealing, ATMOS_U8 ("Reading today's sky…"));
    auto onResult = [this] (WeatherClient::Result r) {
        if (previewing) return; // the globe took over meanwhile
        const auto home = Storage::home();
        if (r.ok)
        {
            if (home.set && home.name.isNotEmpty()) r.day.placeName = home.name;
            Storage::setLastSky (r.day);
            applyDay (r.day);
            setStatus (Status::live, ATMOS_U8 ("Live sky · ") + juce::Time::getCurrentTime().toString (false, true, false));
            return;
        }
        // Offline: estimate the sky for the last place we knew
        auto last = Storage::lastSky();
        Day est = home.set ? estimateDay (home.lat, home.lon, home.name, nowUnix())
                  : last.isValid() ? estimateDay (last.lat, last.lon, last.placeName, nowUnix())
                                   : estimateDay (day.lat, day.lon, day.placeName, nowUnix());
        if (! home.set && last.isValid()) est.country = last.country;
        applyDay (est);
        setStatus (Status::estimate, r.error + ". Playing an estimated sky.");
    };
    const auto home = Storage::home();
    if (home.set)
        weather.fetchAt (home.lat, home.lon, onResult);
    else
        weather.fetchHere (onResult);
    dealtOnce = true;
}

void AtmosProcessor::previewAt (double lat, double lon)
{
    if (! previewing) homeDay = day;
    previewing = true;
    setStatus (Status::previewLoading, "Listening to " + juce::String (lat, 2) + ", " + juce::String (lon, 2) + ATMOS_U8 ("…"));
    weather.fetchAt (lat, lon, [this, lat, lon] (WeatherClient::Result r) {
        if (! previewing) return;
        Day d = r.ok ? r.day : estimateDay (lat, lon, {}, nowUnix());
        d.source = r.ok ? "globe" : "globe estimate";
        applyDay (d);
        setStatus (Status::preview, r.ok ? ATMOS_U8 ("Globe preview · live weather") : ATMOS_U8 ("Globe preview · ") + r.error + ", estimated");
    });
}

void AtmosProcessor::endPreview()
{
    if (! previewing) return;
    previewing = false;
    applyDay (homeDay);
    setStatus (homeDay.source == "live" ? Status::live : Status::restored, homeDay.source == "live" ? "Back to your sky" : "Back to your Day");
}

bool AtmosProcessor::keepDay (const juce::String& name)
{
    Day d = day;
    d.id = juce::Uuid().toDashedString();
    d.name = name.trim();
    return Storage::saveDay (d);
}

void AtmosProcessor::loadDay (const Day& d)
{
    previewing = false;
    applyDay (d);
    setStatus (Status::restored, ATMOS_U8 ("Kept Day · ") + (d.name.isNotEmpty() ? d.name : d.autoTitle()));
}

bool AtmosProcessor::dayIsCurrent() const
{
    return day.localDate() == juce::String (localDate (nowUnix(), day.lon, day.utcOffset, day.utcOffsetKnown));
}

void AtmosProcessor::timerCallback()
{
    // Free waveform tables the audio thread has handed back
    retiredBanks.clear();

    if (! dealtOnce && ! stateRestored)
    {
        dealToday();
        stopTimer();
        startTimer (60 * 1000);
        return;
    }
    if (! dealtOnce)
    {
        dealtOnce = true;
        stopTimer();
        startTimer (60 * 1000);
    }
    sendChangeMessage(); // lets the editor notice when midnight has passed
}

void AtmosProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement root ("Atmospheric");
    root.setAttribute ("version", 1);
    // Save the Day the user is hearing (a globe preview included), so the project reopens as it sounded
    Day snapshot;
    {
        const juce::ScopedLock sl (dayLock);
        snapshot = day;
    }
    auto* dayXml = root.createNewChildElement ("Day");
    dayXml->addTextElement (juce::JSON::toString (snapshot.toVar(), true));
    if (auto macros = apvts.copyState().createXml()) root.addChildElement (macros.release());
    copyXmlToBinary (root, dest);
}

void AtmosProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("Atmospheric")) return;
    if (auto* m = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*m));
    if (auto* dx = xml->getChildByName ("Day"))
    {
        const Day d = Day::fromVar (juce::JSON::parse (dx->getAllSubText()));
        if (d.isValid())
        {
            stateRestored = true;
            juce::WeakReference<AtmosProcessor> weak (this);
            auto apply = [weak, d] {
                if (auto* self = weak.get())
                {
                    self->previewing = false;
                    self->applyDay (d);
                    self->setStatus (Status::restored, ATMOS_U8 ("Project Day · ") + d.autoTitle());
                }
            };
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                apply();
            else
                juce::MessageManager::callAsync (apply);
        }
    }
}

juce::AudioProcessorEditor* AtmosProcessor::createEditor() { return new AtmosEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AtmosProcessor(); }
