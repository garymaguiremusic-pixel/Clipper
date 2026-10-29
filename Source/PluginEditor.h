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
    void setMode (int index);
    void resetMeters();

    SolidClipAudioProcessor& processor;
    juce::Image background;
    float renderScale = 1.0f;

    juce::ComboBox oversamplingBox, filterBox;

    SolidKnob inputKnob   { SolidKnob::Size::Small, "Input" };
    SolidKnob outputKnob  { SolidKnob::Size::Small, "Output" };
    SolidKnob ceilingKnob { SolidKnob::Size::Large, "Ceiling" };
    SolidKnob kneeKnob    { SolidKnob::Size::Large, "Knee" };
    SolidKnob shapeKnob   { SolidKnob::Size::Small, "Shape" };

    juce::TextButton bypassButton { "Bypass" }, deltaButton { "Delta" }, guardButton { "TP Guard" };
    juce::TextButton gainLinkButton { "Link" }, lrLinkButton { "L/R Link" };
    juce::TextButton modeButtons[3] { juce::TextButton { "Stereo" }, juce::TextButton { "Mid" }, juce::TextButton { "Side" } };

    LcdDisplay ceilingLcd, kneeLcd, smoothingLcd, latencyLcd;
    TransferCurve transferCurve;

    BarMeter inMeter  { BarMeter::Kind::Level, "In" };
    BarMeter outMeter { BarMeter::Kind::Level, "Out" };
    BarMeter grMeter1 { BarMeter::Kind::GainReduction, "L GR" };
    BarMeter grMeter2 { BarMeter::Kind::GainReduction, "R GR" };
    BarMeter guardGr  { BarMeter::Kind::GainReduction, "TP GR" };

    HelpButton helpButton;
    HelpOverlay helpOverlay;

    // Attachments (declared after the controls so they are destroyed first)
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Rectangle<float> ioPanel, clipPanel, curvePanel, osPanel, meterArea;
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
