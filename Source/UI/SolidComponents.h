#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Rotary knob (same interaction as Virus Delay):
      - vertical drag (shift = fine), scroll wheel (shift = fine)
      - double-click resets to the default value
      - small knobs show their label underneath, which swaps to the value while
        hovering or dragging
      - tooltips show real units
    Drawing is delegated to CustomLookAndFeel::drawRotarySlider.
*/
class SolidKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    SolidKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> describeValue;   // real units
    juce::String extraTooltip;                            // appended to the tooltip
    juce::String inactiveHint;                            // shown in the tooltip while disabled
    bool showValueAlways = false;                         // small knobs: value line under the label

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setValueWithGesture (double v);
    juce::String valueText() const;

    Size knobSize;
    juce::String label;
    double defaultValue = 0.0;
    float lastDragY = 0.0f;
    bool dragging = false, hovering = false;
};

//==============================================================================
/** Connects a SolidKnob to a parameter with proper host gesture handling. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, SolidKnob&);
    ~KnobAttachment();

private:
    SolidKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "CEILING   -0.3 dB". */
class LcdDisplay : public juce::Component
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setDimmed (bool shouldDim);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
    bool dimmed = false;
};

//==============================================================================
/**
    Horizontal bar meter with peak hold. Level meters show dBFS (blue) with a ceiling
    marker; gain-reduction meters grow from the left in red.
*/
class BarMeter : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    enum class Kind { Level, GainReduction };

    BarMeter (Kind, const juce::String& label);

    /** level: linear peak for Level meters, dB of reduction for GainReduction meters. */
    void push (float value, float deltaSeconds);
    void setCeilingDb (float db);
    void setDimmed (bool shouldDim);
    void setLabel (const juce::String&);
    void reset();
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void paint (juce::Graphics&) override;

private:
    float toProportion (float db) const noexcept;

    Kind kind;
    juce::String label;
    float currentDb = -100.0f, holdDb = -100.0f, holdTime = 0.0f, ceilingDb = 0.0f;
    bool dimmed = false;
};

//==============================================================================
/**
    Input -> output transfer curve in dB. Shows what Ceiling and Knee do: the curve is
    a straight line (untouched) up to the knee start, bends through the shaded knee and
    flattens out at the ceiling. A dot tracks the current input peak.
*/
class TransferCurve : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    void reset();
    std::function<void()> onDoubleClick;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void setCurve (float ceilingDb, float kneeDb);
    void setCaption (const juce::String&);
    void setInputPeak (float linearPeak, float deltaSeconds);
    void paint (juce::Graphics&) override;

private:
    float ceiling = -0.3f, knee = 3.0f, peakDb = -100.0f;
    juce::String caption;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
