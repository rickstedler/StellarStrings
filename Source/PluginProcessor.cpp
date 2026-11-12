#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
namespace
{
juce::String fmtPercent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String fmtSeconds (float v, int)
{
    return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                    : juce::String (v, 2) + " s";
}
juce::String fmtMs (float v, int)  { return juce::String (juce::roundToInt (v)) + " ms"; }
juce::String fmtDb (float v, int)  { return juce::String (v, 1) + " dB"; }
juce::String fmtX (float v, int)   { return juce::String (v, 2) + " x"; }
juce::String fmtHz (float v, int)  { return juce::String (v, 2) + " Hz"; }

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* id, const char* name,
                                                      juce::NormalisableRange<float> range, float def,
                                                      juce::String (*fmt) (float, int))
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (fmt));
}

juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

const double kDivisionBeats[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75,
                                  2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };

//==============================================================================
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // nicht genannte Parameter = Default
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Stellar Pad", {} },
        { "Classic Ensemble", {
            { "violin", 0.85f }, { "viola", 0.7f }, { "cello", 0.25f }, { "attack", 0.08f }, { "release", 0.9f },
            { "ensDepth", 0.7f }, { "ensMix", 0.9f }, { "dlyOn", 0.0f },
            { "revSize", 0.35f }, { "revDecay", 2.2f }, { "revMix", 0.22f }, { "revModDepth", 0.25f } } },
        { "Cosmic Cello Drift", {
            { "violin", 0.2f }, { "viola", 0.5f }, { "cello", 0.9f }, { "bass", 0.6f },
            { "attack", 1.2f }, { "release", 3.5f }, { "vintage", 0.6f }, { "tone", 0.55f },
            { "ensDepth", 0.85f }, { "ensRate", 0.7f }, { "dlyDiv", 9.0f }, { "dlyFeedback", 0.55f },
            { "dlyDamp", 0.6f }, { "revSize", 0.9f }, { "revDecay", 14.0f }, { "revMix", 0.5f } } },
        { "Orbit Brass & Strings", {
            { "violin", 0.6f }, { "viola", 0.4f }, { "cello", 0.3f }, { "horn", 0.55f }, { "trumpet", 0.35f },
            { "attack", 0.12f }, { "release", 1.2f }, { "ensDepth", 0.6f },
            { "dlyDiv", 6.0f }, { "dlyFeedback", 0.4f }, { "dlyMix", 0.25f },
            { "revSize", 0.6f }, { "revDecay", 5.0f }, { "revMix", 0.35f } } },
        { "Deep Space", {
            { "violin", 0.75f }, { "viola", 0.75f }, { "cello", 0.5f }, { "attack", 1.8f }, { "release", 6.0f },
            { "vintage", 0.5f }, { "ensDepth", 1.0f }, { "ensWidth", 1.15f },
            { "dlyDiv", 9.0f }, { "dlyFeedback", 0.7f }, { "dlyMix", 0.38f }, { "dlyDamp", 0.55f }, { "dlyMod", 0.45f },
            { "revSize", 1.0f }, { "revDecay", 25.0f }, { "revModDepth", 0.85f }, { "revModRate", 0.25f },
            { "revPredelay", 90.0f }, { "revWidth", 1.25f }, { "revMix", 0.62f }, { "master", -5.0f } } },
        { "Dry Divider", {
            { "ensOn", 0.0f }, { "dlyOn", 0.0f }, { "revOn", 0.0f }, { "attack", 0.01f }, { "release", 0.3f },
            { "master", -6.0f } } },
    };
    return list;
}
} // namespace

const juce::StringArray& StellarStringsProcessor::delayDivisionNames()
{
    static const juce::StringArray names { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                           "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
    return names;
}

//==============================================================================
APVTS::ParameterLayout StellarStringsProcessor::createLayout()
{
    APVTS::ParameterLayout layout;
    const juce::NormalisableRange<float> unit (0.0f, 1.0f);

    // Orchester-Register
    layout.add (makeFloat ("violin",  "Violin",     unit, 0.8f, fmtPercent));
    layout.add (makeFloat ("viola",   "Viola",      unit, 0.6f, fmtPercent));
    layout.add (makeFloat ("cello",   "Cello",      unit, 0.4f, fmtPercent));
    layout.add (makeFloat ("bass",    "Contrabass", unit, 0.0f, fmtPercent));
    layout.add (makeFloat ("horn",    "Horn",       unit, 0.0f, fmtPercent));
    layout.add (makeFloat ("trumpet", "Trumpet",    unit, 0.0f, fmtPercent));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "bassSplit", 1 }, "Bass Split", 24, 72, 48,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
            { return juce::MidiMessage::getMidiNoteName (v, true, true, 3); })));

    // Stimme
    layout.add (makeFloat ("attack",   "Crescendo", skewed (0.001f, 4.0f, 0.4f),  0.35f, fmtSeconds));
    layout.add (makeFloat ("release",  "Sustain",   skewed (0.01f, 8.0f, 1.2f),   2.0f,  fmtSeconds));
    layout.add (makeFloat ("velSens",  "Velocity",  unit, 0.0f, fmtPercent));
    layout.add (makeFloat ("vintage",  "Vintage",   unit, 0.35f, fmtPercent));
    layout.add (makeFloat ("tone",     "Tone",      unit, 0.72f, fmtPercent));

    // Ensemble
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ensOn", 1 }, "Ensemble On", true));
    layout.add (makeFloat ("ensDepth", "Ensemble Depth", unit, 0.75f, fmtPercent));
    layout.add (makeFloat ("ensRate",  "Ensemble Rate",  skewed (0.25f, 2.0f, 1.0f), 1.0f, fmtX));
    layout.add (makeFloat ("ensWidth", "Ensemble Width", juce::NormalisableRange<float> (0.0f, 1.5f), 1.0f, fmtPercent));
    layout.add (makeFloat ("ensMix",   "Ensemble Mix",   unit, 0.85f, fmtPercent));

    // Ping-Pong-Delay
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "dlyOn", 1 }, "Delay On", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "dlySync", 1 }, "Delay Sync", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "dlyDiv", 1 }, "Delay Division",
                                                              delayDivisionNames(), 6));
    layout.add (makeFloat ("dlyTime",     "Delay Time",     skewed (20.0f, 2000.0f, 400.0f), 420.0f, fmtMs));
    layout.add (makeFloat ("dlyFeedback", "Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.95f), 0.5f, fmtPercent));
    layout.add (makeFloat ("dlyDamp",     "Delay Damping",  unit, 0.45f, fmtPercent));
    layout.add (makeFloat ("dlyMod",      "Delay Wow",      unit, 0.25f, fmtPercent));
    layout.add (makeFloat ("dlyMix",      "Delay Mix",      unit, 0.3f, fmtPercent));

    // Space-Hall
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "revOn", 1 }, "Reverb On", true));
    layout.add (makeFloat ("revSize",     "Reverb Size",      unit, 0.8f, fmtPercent));
    layout.add (makeFloat ("revDecay",    "Reverb Decay",     skewed (0.5f, 30.0f, 6.0f), 9.0f, fmtSeconds));
    layout.add (makeFloat ("revDamp",     "Reverb Damping",   unit, 0.4f, fmtPercent));
    layout.add (makeFloat ("revModDepth", "Reverb Mod Depth", unit, 0.6f, fmtPercent));
    layout.add (makeFloat ("revModRate",  "Reverb Mod Rate",  skewed (0.05f, 3.0f, 0.5f), 0.4f, fmtHz));
    layout.add (makeFloat ("revPredelay", "Reverb Pre-Delay", juce::NormalisableRange<float> (0.0f, 250.0f), 45.0f, fmtMs));
    layout.add (makeFloat ("revWidth",    "Reverb Width",     juce::NormalisableRange<float> (0.0f, 1.5f), 1.2f, fmtPercent));
    layout.add (makeFloat ("revMix",      "Reverb Mix",       unit, 0.45f, fmtPercent));

    layout.add (makeFloat ("master", "Master", juce::NormalisableRange<float> (-36.0f, 6.0f), -3.0f, fmtDb));
    return layout;
}

//==============================================================================
StellarStringsProcessor::StellarStringsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "StellarStrings", createLayout())
{
}

bool StellarStringsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void StellarStringsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mono.assign ((size_t) std::max (1, samplesPerBlock), 0.0f);
    synth.prepare (sampleRate, std::max (1, samplesPerBlock));
    ensemble.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    masterGain.reset (sampleRate, 0.05, juce::Decibels::decibelsToGain (p ("master")));
    updateDspParams();
}

void StellarStringsProcessor::updateDspParams()
{
    stellar::StringSynth::Params sp;
    sp.violin = p ("violin"); sp.viola = p ("viola"); sp.cello = p ("cello");
    sp.bass = p ("bass"); sp.horn = p ("horn"); sp.trumpet = p ("trumpet");
    sp.bassSplit = (int) p ("bassSplit");
    sp.attack = p ("attack"); sp.release = p ("release");
    sp.velocitySens = p ("velSens"); sp.vintage = p ("vintage"); sp.tone = p ("tone");
    synth.setParams (sp);

    stellar::StereoEnsemble::Params ep;
    ep.depth = p ("ensDepth"); ep.rate = p ("ensRate"); ep.width = p ("ensWidth");
    ep.mix = p ("ensOn") > 0.5f ? p ("ensMix") : 0.0f;
    ensemble.setParams (ep);

    stellar::PingPongDelay::Params dp;
    if (p ("dlySync") > 0.5f)
    {
        const int div = juce::jlimit (0, (int) std::size (kDivisionBeats) - 1, (int) p ("dlyDiv"));
        dp.timeSeconds = (float) (kDivisionBeats[div] * 60.0 / currentBpm);
    }
    else
        dp.timeSeconds = p ("dlyTime") * 0.001f;
    dp.timeSeconds = std::min (dp.timeSeconds, (float) stellar::PingPongDelay::kMaxSeconds);
    dp.feedback = p ("dlyFeedback"); dp.damping = p ("dlyDamp"); dp.modulation = p ("dlyMod");
    dp.mix = p ("dlyOn") > 0.5f ? p ("dlyMix") : 0.0f;
    delay.setParams (dp);

    stellar::SpaceReverb::Params rp;
    rp.size = p ("revSize"); rp.decaySeconds = p ("revDecay"); rp.damping = p ("revDamp");
    rp.modDepth = p ("revModDepth"); rp.modRate = p ("revModRate"); rp.preDelayMs = p ("revPredelay");
    rp.width = p ("revWidth");
    rp.mix = p ("revOn") > 0.5f ? p ("revMix") : 0.0f;
    reverb.setParams (rp);

    masterGain.setTarget (juce::Decibels::decibelsToGain (p ("master")));
}

void StellarStringsProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                 synth.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())           synth.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())    synth.setSustainPedal (true);
    else if (m.isSustainPedalOff())   synth.setSustainPedal (false);
    else if (m.isPitchWheel())        synth.setPitchBend (m.getPitchWheelValue());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) synth.allNotesOff();
}

void StellarStringsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples == 0 || buffer.getNumChannels() < 2) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 1.0) currentBpm = *bpm;

    if ((int) mono.size() < numSamples)   // Sicherheitsnetz, falls der Host größere Blöcke schickt
    {
        mono.assign ((size_t) numSamples, 0.0f);
    }

    updateDspParams();

    // Synth sample-genau zwischen den MIDI-Events rendern
    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (evPos > pos) { synth.render (mono.data() + pos, evPos - pos); pos = evPos; }
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples) synth.render (mono.data() + pos, numSamples - pos);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    ensemble.process (mono.data(), L, R, numSamples);
    delay.process (L, R, numSamples);
    reverb.process (L, R, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = masterGain.next();
        L[i] *= g; R[i] *= g;
    }
}

//==============================================================================
int StellarStringsProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String StellarStringsProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : "";
}

void StellarStringsProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms())) return;
    currentProgram = index;

    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : presets()[(size_t) index].values)
        if (auto* ranged = apvts.getParameter (id))
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
}

void StellarStringsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void StellarStringsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* StellarStringsProcessor::createEditor()
{
    return new StellarStringsEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StellarStringsProcessor();
}
