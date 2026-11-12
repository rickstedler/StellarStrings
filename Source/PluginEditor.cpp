#include "PluginEditor.h"

const juce::Colour RetroLookAndFeel::panel     { 0xff2a211c };
const juce::Colour RetroLookAndFeel::panelDark { 0xff17120f };
const juce::Colour RetroLookAndFeel::accent    { 0xffff8a2a };
const juce::Colour RetroLookAndFeel::accent2   { 0xffe8c15a };
const juce::Colour RetroLookAndFeel::text      { 0xfff3e6d3 };
const juce::Colour RetroLookAndFeel::textDim   { 0xffa8957f };

RetroLookAndFeel::RetroLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, panelDark.withAlpha (0.6f));
    setColour (juce::Label::textColourId, text);
    setColour (juce::ComboBox::backgroundColourId, panelDark);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.5f));
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panelDark);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.8f));
    setColour (juce::PopupMenu::highlightedTextColourId, panelDark);
    setColour (juce::ToggleButton::textColourId, text);
}

void RetroLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;

    // Bahn
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (panelDark.withAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    g.setGradientFill (juce::ColourGradient (accent2.withAlpha (alpha), centre.x - radius, centre.y,
                                             accent.withAlpha (alpha), centre.x + radius, centre.y, false));
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knopfkappe
    const float capR = radius - 8.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a3b31), centre.x, centre.y - capR,
                                             juce::Colour (0xff1e1814), centre.x, centre.y + capR, false));
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capR + 3.0f, 3.0f, capR * 0.55f, 1.5f);
    g.setColour (text.withAlpha (alpha));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void RetroLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : panelDark);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? accent2 : (highlighted ? accent.withAlpha (0.7f) : textDim.withAlpha (0.5f)));
    g.drawRoundedRectangle (r, 5.0f, 1.2f);
    g.setColour (on ? panelDark : textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred, 1);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, RetroLookAndFeel::textDim);
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

//==============================================================================
namespace
{
/** Stapelt Kind-Komponenten senkrecht (für Schalter-Spalten). */
class Column : public juce::Component
{
public:
    void add (juce::Component* c) { items.push_back (c); addAndMakeVisible (c); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 14);
        const int n = juce::jmax (1, (int) items.size()), gap = 6;
        const int h = juce::jmin (30, (r.getHeight() - gap * (n - 1)) / n);
        for (auto* c : items) { c->setBounds (r.removeFromTop (h)); r.removeFromTop (gap); }
    }
private:
    std::vector<juce::Component*> items;
};

constexpr int kKnobW = 82;
constexpr int kColW  = 92;
} // namespace

StellarStringsEditor::StellarStringsEditor (StellarStringsProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    auto makeColumn = [this] (std::initializer_list<juce::Component*> comps) -> juce::Component*
    {
        auto col = std::make_unique<Column>();
        for (auto* c : comps) col->add (c);
        auto* raw = col.get();
        addAndMakeVisible (raw);
        columns.push_back (std::move (col));
        return raw;
    };

    // --- Orchester ---------------------------------------------------------
    sections.push_back ({ "ORCHESTRA", {}, {
        knob ("violin", "VIOLIN"), knob ("viola", "VIOLA"), knob ("cello", "CELLO"),
        knob ("bass", "C-BASS"), knob ("horn", "HORN"), knob ("trumpet", "TRUMPET"),
        knob ("bassSplit", "BASS SPLIT") } });

    // --- Stimme ------------------------------------------------------------
    masterKnob = knob ("master", "MASTER");
    sections.push_back ({ "VOICE", {}, {
        knob ("attack", "CRESCENDO"), knob ("release", "SUSTAIN"), knob ("velSens", "VELOCITY"),
        knob ("vintage", "VINTAGE"), knob ("tone", "TONE"), masterKnob } });

    // --- Ensemble ----------------------------------------------------------
    sections.push_back ({ "STEREO ENSEMBLE", {}, {
        makeColumn ({ toggle ("ensOn", "ON") }),
        knob ("ensDepth", "DEPTH"), knob ("ensRate", "RATE"), knob ("ensWidth", "WIDTH"), knob ("ensMix", "MIX") } });

    // --- Ping-Pong ---------------------------------------------------------
    for (int i = 0; i < StellarStringsProcessor::delayDivisionNames().size(); ++i)
        divisionBox.addItem (StellarStringsProcessor::delayDivisionNames()[i], i + 1);
    divisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, "dlyDiv", divisionBox);
    delayTimeKnob = knob ("dlyTime", "TIME");
    sections.push_back ({ "PING-PONG DELAY", {}, {
        makeColumn ({ toggle ("dlyOn", "ON"), toggle ("dlySync", "SYNC"), &divisionBox }),
        delayTimeKnob, knob ("dlyFeedback", "FEEDBACK"), knob ("dlyDamp", "DAMPING"),
        knob ("dlyMod", "WOW"), knob ("dlyMix", "MIX") } });

    // --- Space -------------------------------------------------------------
    sections.push_back ({ "SPACE REVERB", {}, {
        makeColumn ({ toggle ("revOn", "ON") }),
        knob ("revSize", "SIZE"), knob ("revDecay", "DECAY"), knob ("revDamp", "DAMPING"),
        knob ("revModDepth", "MOD DEPTH"), knob ("revModRate", "MOD RATE"), knob ("revPredelay", "PRE-DELAY"),
        knob ("revWidth", "WIDTH"), knob ("revMix", "MIX") } });

    // --- Presets -----------------------------------------------------------
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);
    presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0 && idx != processor.getCurrentProgram())
            processor.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    setSize (1150, 640);
    startTimerHz (10);
}

StellarStringsEditor::~StellarStringsEditor()
{
    setLookAndFeel (nullptr);
}

Knob* StellarStringsEditor::knob (const char* id, const char* label)
{
    knobs.push_back (std::make_unique<Knob> (processor.apvts, id, label));
    addAndMakeVisible (*knobs.back());
    return knobs.back().get();
}

juce::ToggleButton* StellarStringsEditor::toggle (const char* id, const char* label)
{
    toggles.push_back (std::make_unique<juce::ToggleButton> (label));
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, id, *toggles.back()));
    return toggles.back().get();
}

void StellarStringsEditor::timerCallback()
{
    const bool sync = processor.apvts.getRawParameterValue ("dlySync")->load() > 0.5f;
    delayTimeKnob->setEnabled (! sync);
    divisionBox.setEnabled (sync);

    if (presetBox.getSelectedId() != processor.getCurrentProgram() + 1)
        presetBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
}

void StellarStringsEditor::paint (juce::Graphics& g)
{
    using L = RetroLookAndFeel;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f1915), 0.0f, 0.0f,
                                             juce::Colour (0xff0e0b09), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // Kopfzeile mit Retro-Streifen
    auto header = getLocalBounds().removeFromTop (64).toFloat();
    const juce::Colour stripes[] = { juce::Colour (0xffe8c15a), juce::Colour (0xffff8a2a),
                                     juce::Colour (0xffd9482b), juce::Colour (0xff8e2f3c) };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (stripes[i]);
        g.fillRect (juce::Rectangle<float> (header.getRight() - 470.0f + (float) i * 14.0f, 0.0f, 9.0f, header.getHeight())
                        .withTrimmedTop (10.0f).withTrimmedBottom (10.0f));
    }

    g.setColour (L::text);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("STELLAR STRINGS", 22, 8, 420, 34, juce::Justification::centredLeft);
    g.setColour (L::accent);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("RETRO STRING ENSEMBLE  /  STEREO CHORUS  /  PING-PONG  /  SPACE", 24, 40, 520, 16,
                juce::Justification::centredLeft);

    g.setColour (L::textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("PRESET", presetBox.getX() - 70, presetBox.getY(), 62, presetBox.getHeight(),
                juce::Justification::centredRight);

    for (const auto& s : sections)
    {
        const auto r = s.bounds.toFloat();
        g.setColour (L::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (L::accent.withAlpha (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (L::accent);
        g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 13.0f, 4.0f, 14.0f, 2.0f);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.setColour (L::text);
        g.drawText (s.title, (int) r.getX() + 24, (int) r.getY() + 8, 300, 24, juce::Justification::centredLeft);
    }
}

void StellarStringsEditor::resized()
{
    presetBox.setBounds (getWidth() - 270, 18, 250, 28);

    const int margin = 16, gap = 12, rowH = 176;
    auto widthOf = [] (const Section& s)
    {
        int w = 20;
        for (auto* c : s.items) w += (dynamic_cast<Knob*> (c) != nullptr) ? kKnobW : kColW;
        return w;
    };

    int y = 72;
    // Zeile 1: Orchester + Stimme
    sections[0].bounds = { margin, y, widthOf (sections[0]), rowH };
    sections[1].bounds = { sections[0].bounds.getRight() + gap, y,
                           getWidth() - margin - sections[0].bounds.getRight() - gap, rowH };
    y += rowH + gap;
    // Zeile 2: Ensemble + Delay
    sections[2].bounds = { margin, y, widthOf (sections[2]), rowH };
    sections[3].bounds = { sections[2].bounds.getRight() + gap, y,
                           getWidth() - margin - sections[2].bounds.getRight() - gap, rowH };
    y += rowH + gap;
    // Zeile 3: Space
    sections[4].bounds = { margin, y, getWidth() - 2 * margin, rowH };

    for (auto& s : sections)
    {
        auto area = s.bounds.reduced (10, 0).withTrimmedTop (36).withTrimmedBottom (10);
        const int contentW = widthOf (s) - 20;
        area.removeFromLeft (juce::jmax (0, (area.getWidth() - contentW) / 2)); // zentrieren
        for (auto* c : s.items)
        {
            const int w = (dynamic_cast<Knob*> (c) != nullptr) ? kKnobW : kColW;
            c->setBounds (area.removeFromLeft (w).reduced (3, 0));
        }
    }
}
