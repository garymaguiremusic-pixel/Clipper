#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/SolidComponents.h"

//==============================================================================
/**
    The whole interface, laid out at a fixed design size (kWidth x kHeight). The
    editor scales it with a transform, so every control resizes together and all the
    procedurally drawn graphics stay sharp at any size.
*/
class SolidClipView : public juce::Component,
                      private juce::Timer
{
public:
    static constexpr int kWidth = 820, kHeight = 384;

    explicit SolidClipView (SolidClipAudioProcessor&);
    ~SolidClipView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setRenderScale (float scale);

private:
    void timerCallback() override;
    void setupCombo (juce::ComboBox&, const juce::StringArray& items, const juce::String& tooltip);
    void updateActiveStates();
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    static void setActive (juce::Component&, bool active);
    float raw (const char* id) const;

    SolidClipAudioProcessor& processor;
    juce::Image background;
    float renderScale = 1.0f;

    juce::ComboBox modeBox, oversamplingBox, filterBox;

    SolidKnob inputKnob       { SolidKnob::Size::Small, "Input" };
    SolidKnob outputKnob      { SolidKnob::Size::Small, "Output" };
    SolidKnob ceilingKnob     { SolidKnob::Size::Large, "Ceiling" };
    SolidKnob kneeKnob        { SolidKnob::Size::Large, "Knee" };
    SolidKnob shapeKnob       { SolidKnob::Size::Small, "Shape" };
    SolidKnob midCeilingKnob  { SolidKnob::Size::Small, "Ceiling" };
    SolidKnob midKneeKnob     { SolidKnob::Size::Small, "Knee" };
    SolidKnob sideCeilingKnob { SolidKnob::Size::Small, "Ceiling" };
    SolidKnob sideKneeKnob    { SolidKnob::Size::Small, "Knee" };
    SolidKnob linkKnob        { SolidKnob::Size::Small, "M/S Link" };

    juce::TextButton bypassButton { "Bypass" }, deltaButton { "Delta" }, guardButton { "TP Guard" }, linkButton { "Link" };
    juce::TextButton stereoStageButton { "Stereo Stage" }, msStageButton { "M/S Stage" };
    juce::TextButton midButton { "Mid" }, sideButton { "Side" };

    LcdDisplay ceilingLcd, kneeLcd, smoothingLcd, latencyLcd;

    BarMeter inMeter     { BarMeter::Kind::Level, "In" };
    BarMeter outMeter    { BarMeter::Kind::Level, "Out" };
    BarMeter stereoGr    { BarMeter::Kind::GainReduction, "St GR" };
    BarMeter guardGr     { BarMeter::Kind::GainReduction, "TP GR" };
    BarMeter midGr       { BarMeter::Kind::GainReduction, "Mid GR" };
    BarMeter sideGr      { BarMeter::Kind::GainReduction, "Side GR" };

    HelpButton helpButton;
    HelpOverlay helpOverlay;

    // Attachments (declared after the controls so they are destroyed first)
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Rectangle<float> ioPanel, stereoPanel, msPanel, osPanel, meterArea;
    double lastTimerMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolidClipView)
};

//==============================================================================
class SolidClipAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SolidClipAudioProcessorEditor (SolidClipAudioProcessor&);
    ~SolidClipAudioProcessorEditor() override;

    void resized() override;

private:
    CustomLookAndFeel lnf;
    SolidClipView view;
    juce::TooltipWindow tooltipWindow { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SolidClipAudioProcessorEditor)
};
