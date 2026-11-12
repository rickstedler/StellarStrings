#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    static const juce::Colour panel, panelDark, accent, accent2, text, textDim;
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class StellarStringsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StellarStringsEditor (StellarStringsProcessor&);
    ~StellarStringsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
        std::vector<juce::Component*> items;
    };

    void timerCallback() override;
    Knob* knob (const char* id, const char* label);
    juce::ToggleButton* toggle (const char* id, const char* label);

    StellarStringsProcessor& processor;
    RetroLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;

    juce::ComboBox divisionBox, presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;
    std::vector<std::unique_ptr<juce::Component>> columns; // nach den Kindern deklariert → zuerst zerstört

    std::vector<Section> sections;
    Knob* masterKnob = nullptr;
    Knob* delayTimeKnob = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StellarStringsEditor)
};
