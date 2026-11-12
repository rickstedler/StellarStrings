#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/StringSynth.h"
#include "dsp/StereoEnsemble.h"
#include "dsp/PingPongDelay.h"
#include "dsp/SpaceReverb.h"

class StellarStringsProcessor : public juce::AudioProcessor
{
public:
    StellarStringsProcessor();
    ~StellarStringsProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Stellar Strings"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& delayDivisionNames();

private:
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void handleMidi (const juce::MidiMessage& m);
    void updateDspParams();

    stellar::StringSynth synth;
    stellar::StereoEnsemble ensemble;
    stellar::PingPongDelay delay;
    stellar::SpaceReverb reverb;
    stellar::Smoother masterGain;

    std::vector<float> mono;
    double currentBpm = 120.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StellarStringsProcessor)
};
