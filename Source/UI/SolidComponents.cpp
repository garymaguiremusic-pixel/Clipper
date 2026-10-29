#include "SolidComponents.h"
#include "../CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
// SolidKnob
//==============================================================================
SolidKnob::SolidKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String SolidKnob::valueText() const
{
    return describeValue ? describeValue (getValue()) : String (getValue(), 2);
}

String SolidKnob::getTooltip()
{
    String text = label.toUpperCase() + "  " + valueText();
    if (extraTooltip.isNotEmpty())
        text << "\n" << extraTooltip;
    if (! isEnabled() && inactiveHint.isNotEmpty())
        text << "\n" << inactiveHint;
    return text;
}

void SolidKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        knobArea = bounds;
    }
    else
    {
        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        if ((hovering || dragging) && isEnabled())
        {
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (SolidColours::blue);
            g.drawText (valueText(), labelArea, Justification::centred);
        }
        else
        {
            CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
        }
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

void SolidKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void SolidKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = true;
    lastDragY = e.position.y;
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void SolidKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    // Incremental drag in proportion space so shift mid-drag never jumps.
    const double pixelsForFullRange = e.mods.isShiftDown() ? 1500.0 : 200.0;
    const double delta = (double) (lastDragY - e.position.y) / pixelsForFullRange;
    lastDragY = e.position.y;

    if (std::abs (delta) > 1.0e-9)
    {
        const double prop = jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + delta);
        setValue (proportionOfLengthToValue (prop), sendNotificationSync);
    }
}

void SolidKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void SolidKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void SolidKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    const double step = (e.mods.isShiftDown() ? 1.0 : 4.0) / 127.0;
    const double amount = jlimit (-1.0, 1.0, (double) dir * 8.0) * step;
    const double prop = jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + amount);
    setValueWithGesture (proportionOfLengthToValue (prop));
}

void SolidKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void SolidKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, SolidKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto range = p.getNormalisableRange();
    knob.setNormalisableRange ({ (double) range.start, (double) range.end,
                                 [range] (double, double, double v) { return (double) range.convertFrom0to1 ((float) v); },
                                 [range] (double, double, double v) { return (double) range.convertTo0to1 ((float) v); },
                                 [range] (double, double, double v) { return (double) range.snapToLegalValue ((float) v); } });
    knob.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));
    knob.describeValue = [&p] (double v) { return p.getText (p.convertTo0to1 ((float) v), 32); };

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, dimmed);

    const auto textColour = dimmed ? Colour (0xff8a9097) : Colours::white;
    auto textArea = b.reduced (6.0f, 0.0f);

    const auto font = CustomLookAndFeel::uiFont (11.0f, true);
    g.setFont (font);

    const float valueWidth = GlyphArrangement::getStringWidth (font, valueText) + 4.0f;
    auto nameArea = textArea.withTrimmedRight (valueWidth).toNearestInt();

    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawFittedText (nameText, nameArea.translated (0, 1), Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), Justification::centredRight);
    g.setColour (textColour);
    g.drawFittedText (nameText, nameArea, Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea, Justification::centredRight);
}

//==============================================================================
// BarMeter
//==============================================================================
namespace
{
    constexpr float kLevelFloorDb = -36.0f;
    constexpr float kGrRangeDb = 12.0f;
    constexpr float kHoldSeconds = 1.5f;
    constexpr float kFallDbPerSecond = 24.0f;
}

BarMeter::BarMeter (Kind k, const String& labelText) : kind (k), label (labelText)
{
    setInterceptsMouseClicks (false, false);
}

float BarMeter::toProportion (float db) const noexcept
{
    if (kind == Kind::Level)
        return jlimit (0.0f, 1.0f, (db - kLevelFloorDb) / -kLevelFloorDb);
    return jlimit (0.0f, 1.0f, db / kGrRangeDb);
}

void BarMeter::push (float value, float dt)
{
    const float db = kind == Kind::Level ? Decibels::gainToDecibels (value, -100.0f) : std::max (0.0f, value);
    const float floorDb = kind == Kind::Level ? -100.0f : 0.0f;
    const auto oldCurrent = currentDb, oldHold = holdDb;

    // instant attack, smooth fall
    currentDb = db >= currentDb ? db : std::max (db, currentDb - kFallDbPerSecond * dt);

    if (db >= holdDb)
    {
        holdDb = db;
        holdTime = kHoldSeconds;
    }
    else if ((holdTime -= dt) <= 0.0f)
    {
        holdDb = std::max (floorDb, std::max (currentDb, holdDb - kFallDbPerSecond * dt));
    }

    if (std::abs (oldCurrent - currentDb) > 0.01f || std::abs (oldHold - holdDb) > 0.01f)
        repaint();
}

void BarMeter::setCeilingDb (float db)
{
    if (std::abs (db - ceilingDb) > 0.001f)
    {
        ceilingDb = db;
        repaint();
    }
}

void BarMeter::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void BarMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto labelArea = b.removeFromLeft (50.0f);
    auto valueArea = b.removeFromRight (54.0f);
    auto track = b.reduced (2.0f, 3.0f);

    CustomLookAndFeel::drawSmallLabel (g, label, labelArea, Justification::centredLeft);
    CustomLookAndFeel::drawInsetBox (g, track, 2.0f);

    auto inner = track.reduced (2.0f);
    const float alpha = dimmed ? 0.3f : 1.0f;

    // scale ticks
    g.setColour (Colours::white.withAlpha (0.07f));
    const int ticks = kind == Kind::Level ? 6 : 6;
    for (int i = 1; i < ticks; ++i)
        g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / (float) ticks), inner.getY(), inner.getBottom());

    const float p = toProportion (currentDb);
    if (p > 0.0f)
    {
        auto bar = inner.withWidth (inner.getWidth() * p);
        const auto top = kind == Kind::Level ? SolidColours::lcdTop : SolidColours::red;
        const auto bottom = kind == Kind::Level ? SolidColours::lcdBottom : SolidColours::red.darker (0.6f);
        g.setGradientFill (ColourGradient (top.withMultipliedAlpha (alpha), 0.0f, bar.getY(),
                                           bottom.withMultipliedAlpha (alpha), 0.0f, bar.getBottom(), false));
        g.fillRect (bar);
        g.setColour (Colours::white.withAlpha (0.18f * alpha));
        g.drawHorizontalLine ((int) bar.getY(), bar.getX(), bar.getRight());
    }

    // peak hold tick
    const float hp = toProportion (holdDb);
    if (hp > 0.0f)
    {
        g.setColour ((kind == Kind::Level ? Colour (0xffc9e6ff) : Colour (0xffffb0aa)).withAlpha (alpha));
        g.fillRect (inner.getX() + inner.getWidth() * hp - 1.0f, inner.getY(), 2.0f, inner.getHeight());
    }

    // ceiling marker on level meters
    if (kind == Kind::Level)
    {
        const float cp = toProportion (ceilingDb);
        g.setColour (Colours::white.withAlpha (0.55f * alpha));
        g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * cp), track.getY(), track.getBottom());
    }

    // numeric peak hold
    String text;
    if (kind == Kind::Level)
        text = holdDb <= -99.0f ? String ("-inf") : String (holdDb, 1);
    else
        text = (holdDb > 0.005f ? "-" : "") + String (holdDb, 2);

    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.setColour ((holdDb > ceilingDb + 0.01f && kind == Kind::Level ? SolidColours::red : Colour (0xffdfe3e6)).withMultipliedAlpha (dimmed ? 0.4f : 1.0f));
    g.drawText (text + " dB", valueArea, Justification::centredRight);
}

//==============================================================================
// Help
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (SolidColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (16.0f, 16.0f);
    CustomLookAndFeel::drawPanel (g, box, "Solid Clip - Quick Help");

    const char* lines[] =
    {
        "LINKED CLIPPING:  one gain for both channels, so L/R (or M/S) ratios never change - no added stereo distortion.",
        "CEILING / KNEE:  output ceiling, and how far below it the soft knee starts. Below the knee nothing is touched.",
        "SHAPE:  0 % = pure clipper. Higher = gain smoothed over up to 3 ms with matching lookahead (clipper/limiter hybrid).",
        "MODE:  Stereo (L/R linked), Mid/Side, or Stereo + M/S in series. Stage buttons switch the serial stages.",
        "M/S LINK:  100 % = one shared gain for Mid and Side. Lower values clip Side on its own - audible side distortion.",
        "OVERSAMPLING:  detection + gain at up to 16x to catch inter-sample peaks. Linear phase is bit-transparent below the knee.",
        "TP GUARD:  catches the small true-peak overshoots left by the down-sampling filter, so the ceiling holds solid.",
        "DELTA:  listen to exactly what is removed (dry - wet, latency aligned).",
        "Knobs: drag, shift-drag = fine, scroll wheel, double-click = default. Hover for values. Drag the corner to resize.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (12.0f, 0.0f).withTrimmedTop (28.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? SolidColours::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (22.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
