#include "PluginProcessor.h"
#include "BibleFile.h"
#include "PluginEditor.h"
#include "Storage.h"

using namespace atmos;

namespace
{
int64_t nowUnix() { return juce::Time::currentTimeMillis() / 1000; }
const char* kMacroIds[] = { "tone", "bloom", "space", "motion", "intensity" };
const char* kMacroNames[] = { "Tone", "Bloom", "Space", "Motion", "Intensity" };

CoreSound fallbackSound()
{
    CoreSound s;
    s.name = "Plain";
    for (int i = 0; i < kNumParams; ++i)
        s.home[i] = s.lo[i] = s.hi[i] = paramInfo (i).def;
    return s;
}
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

    bible = loadBible (bibleFrom);
    if (bible.empty()) bible.push_back (fallbackSound());

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
    core.prepare (sr, juce::jmax (block, 32));
    scratch.setSize (2, juce::jmax (block, 512));
    audioVersion = -1;
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

Day AtmosProcessor::currentDay() const
{
    const juce::ScopedLock sl (dayLock);
    return day;
}

CoreSound AtmosProcessor::currentSound() const
{
    const juce::ScopedLock sl (dayLock);
    return sound;
}

Patch AtmosProcessor::currentPatch() const
{
    const juce::ScopedLock sl (dayLock);
    return resolvePatch (sound, day.climate(), day.precip, readMacros());
}

void AtmosProcessor::handleMidi (const juce::MidiMessage& msg)
{
    if (msg.isNoteOn())
        core.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
    else if (msg.isNoteOff())
        core.noteOff (msg.getNoteNumber());
    else if (msg.isSustainPedalOn())
        core.setSustain (true);
    else if (msg.isSustainPedalOff())
        core.setSustain (false);
    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        core.allNotesOff();
    else if (msg.isPitchWheel())
        core.setPitchBend ((msg.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
}

void AtmosProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    bool changed = false;
    const int v = sharedVersion.load();
    if (v != audioVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (audioLock);
        if (tl.isLocked())
        {
            audio = shared;
            audioVersion = v;
            changed = true;
        }
    }
    const Macros m = readMacros();
    if (changed || m.tone != audioMacros.tone || m.bloom != audioMacros.bloom || m.space != audioMacros.space
        || m.motion != audioMacros.motion || m.intensity != audioMacros.intensity)
    {
        audioMacros = m;
        core.setPatch (resolvePatch (audio.home, audio.lo, audio.hi, audio.climate, audio.climate.precip, m));
    }

    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();
    auto render = [&] (int start, int len) {
        while (len > 0)
        {
            const int chunk = juce::jmin (len, scratch.getNumSamples());
            float* L = scratch.getWritePointer (0);
            float* R = scratch.getWritePointer (1);
            core.process (L, R, chunk);
            if (chans >= 2)
            {
                buffer.copyFrom (0, start, L, chunk);
                buffer.copyFrom (1, start, R, chunk);
                for (int c = 2; c < chans; ++c)
                    buffer.clear (c, start, chunk);
            }
            else if (chans == 1)
            {
                float* out = buffer.getWritePointer (0, start);
                for (int i = 0; i < chunk; ++i)
                    out[i] = 0.5f * (L[i] + R[i]);
            }
            start += chunk;
            len -= chunk;
        }
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos)
        {
            render (pos, at - pos);
            pos = at;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n) render (pos, n - pos);
}

void AtmosProcessor::applyDay (const Day& in)
{
    Day d = in;
    CoreSound s;
    bool ok = false;
    // Sounds made for an older engine don't carry over; such a Day draws a fresh one
    if (d.sound.isObject() && d.mappingVersion >= kMappingVersion) s = soundFromVar (d.sound, ok);
    if (! ok)
    {
        // A new Day: today's lottery draws a core sound from the bible, and the Day keeps it
        const int i = chooseCoreSound (bible, d.climate(), d.precip);
        s = bible[(size_t) juce::jmax (0, i)];
        d.sound = soundToVar (s);
        d.mappingVersion = kMappingVersion;
    }
    {
        // Hosts may save the project from another thread at any moment
        const juce::ScopedLock sl (dayLock);
        day = d;
        sound = s;
    }
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        shared.home = s.home;
        shared.lo = s.lo;
        shared.hi = s.hi;
        shared.climate = d.climate();
    }
    sharedVersion.fetch_add (1);
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
    const int gen = ++dayGeneration;
    setStatus (Status::dealing, ATMOS_U8 ("Reading today's sky…"));
    auto onResult = [this, gen] (WeatherClient::Result r) {
        if (previewing || gen != dayGeneration) return; // the globe, a kept Day or a project took over meanwhile
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
                                   : estimateDay (currentDay().lat, currentDay().lon, currentDay().placeName, nowUnix());
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
    if (! previewing) homeDay = currentDay();
    previewing = true;
    const int gen = ++dayGeneration;
    setStatus (Status::previewLoading, "Listening to " + juce::String (lat, 2) + ", " + juce::String (lon, 2) + ATMOS_U8 ("…"));
    weather.fetchAt (lat, lon, [this, lat, lon, gen] (WeatherClient::Result r) {
        if (! previewing || gen != dayGeneration) return;
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
    ++dayGeneration;
    applyDay (homeDay);
    setStatus (homeDay.source == "live" ? Status::live : Status::restored, homeDay.source == "live" ? "Back to your sky" : "Back to your Day");
}

bool AtmosProcessor::keepDay (const juce::String& name)
{
    Day d = currentDay(); // carries its core sound
    d.id = juce::Uuid().toDashedString();
    d.name = name.trim();
    return Storage::saveDay (d);
}

void AtmosProcessor::loadDay (const Day& d)
{
    previewing = false;
    ++dayGeneration;
    applyDay (d);
    setStatus (Status::restored, ATMOS_U8 ("Kept Day · ") + (d.name.isNotEmpty() ? d.name : d.autoTitle()));
}

bool AtmosProcessor::dayIsCurrent() const
{
    const Day d = currentDay();
    return d.localDate() == juce::String (localDate (nowUnix(), d.lon, d.utcOffset, d.utcOffsetKnown));
}

void AtmosProcessor::timerCallback()
{
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
    root.setAttribute ("version", 2);
    // Save the Day the user is hearing (a globe preview included) with its core sound,
    // so the project reopens exactly as it sounded, whatever the bible says later
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
            // Hosts may restore from any thread and save again straight away, so the Day itself
            // is applied now (it is lock-protected); only the status line waits for the message thread.
            stateRestored = true;
            ++dayGeneration;
            applyDay (d);
            juce::WeakReference<AtmosProcessor> weak (this);
            const auto title = d.autoTitle();
            auto status = [weak, title] {
                if (auto* self = weak.get())
                {
                    self->previewing = false;
                    self->setStatus (Status::restored, ATMOS_U8 ("Project Day · ") + title);
                }
            };
            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                status();
            else
                juce::MessageManager::callAsync (status);
        }
    }
}

juce::AudioProcessorEditor* AtmosProcessor::createEditor() { return new AtmosEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AtmosProcessor(); }
