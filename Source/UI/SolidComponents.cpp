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
        if (showValueAlways)
        {
            auto valueArea = bounds.removeFromBottom (13.0f);
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (SolidColours::blue.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
            g.drawText (valueText(), valueArea, Justification::centred);
        }

        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        if ((hovering || dragging) && isEnabled() && ! showValueAlways)
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
    setTooltip ("Double-click to reset all meters");
}

void BarMeter::reset()
{
    currentDb = holdDb = kind == Kind::Level ? -100.0f : 0.0f;
    holdTime = 0.0f;
    repaint();
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

void BarMeter::setLabel (const String& newLabel)
{
    if (newLabel != label)
    {
        label = newLabel;
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
// TransferCurve
//==============================================================================
namespace
{
    constexpr float kCurveMinDb = -24.0f, kCurveMaxDb = 6.0f;
}

void TransferCurve::reset()
{
    peakDb = -100.0f;
    repaint();
}

void TransferCurve::setCurve (float ceilingDb, float kneeDb)
{
    if (std::abs (ceilingDb - ceiling) > 0.001f || std::abs (kneeDb - knee) > 0.001f)
    {
        ceiling = ceilingDb;
        knee = kneeDb;
        repaint();
    }
}

void TransferCurve::setCaption (const String& text)
{
    if (text != caption)
    {
        caption = text;
        repaint();
    }
}

void TransferCurve::setInputPeak (float linearPeak, float dt)
{
    const float db = Decibels::gainToDecibels (linearPeak, -100.0f);
    const float next = db >= peakDb ? db : std::max (db, peakDb - 30.0f * dt);
    if (std::abs (next - peakDb) > 0.05f)
    {
        peakDb = next;
        repaint();
    }
}

void TransferCurve::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b, 3.0f);
    auto plot = b.reduced (8.0f).withTrimmedBottom (12.0f).withTrimmedLeft (16.0f);

    auto xFor = [&] (float db) { return plot.getX() + plot.getWidth() * (db - kCurveMinDb) / (kCurveMaxDb - kCurveMinDb); };
    auto yFor = [&] (float db) { return plot.getBottom() - plot.getHeight() * (db - kCurveMinDb) / (kCurveMaxDb - kCurveMinDb); };

    // grid every 6 dB
    g.setFont (CustomLookAndFeel::uiFont (8.5f, true));
    for (float db = kCurveMinDb; db <= kCurveMaxDb + 0.01f; db += 6.0f)
    {
        g.setColour (Colours::white.withAlpha (std::abs (db) < 0.01f ? 0.16f : 0.06f));
        g.drawVerticalLine ((int) xFor (db), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
        g.setColour (SolidColours::labelText.withAlpha (0.6f));
        g.drawText (String ((int) db), Rectangle<float> (xFor (db) - 12.0f, plot.getBottom() + 1.0f, 24.0f, 10.0f), Justification::centred);
        g.drawText (String ((int) db), Rectangle<float> (b.getX() + 2.0f, yFor (db) - 5.0f, 20.0f, 10.0f), Justification::centredLeft);
    }

    const float c = ceiling, t = ceiling - knee;

    // knee region
    if (knee > 0.0f)
    {
        g.setColour (SolidColours::blue.withAlpha (0.10f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (xFor (t), plot.getY(), xFor (c), plot.getBottom()));
        g.setColour (SolidColours::blue.withAlpha (0.45f));
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ xFor (t), plot.getY(), xFor (t), plot.getBottom() }, dashes, 2, 1.0f);
        g.drawText ("KNEE", Rectangle<float> (xFor (t) + 2.0f, plot.getBottom() - 12.0f, 40.0f, 10.0f), Justification::centredLeft);
    }

    // unity (no change) reference
    g.setColour (Colours::white.withAlpha (0.18f));
    const float dashes[] = { 2.0f, 3.0f };
    g.drawDashedLine ({ xFor (kCurveMinDb), yFor (kCurveMinDb), xFor (kCurveMaxDb), yFor (kCurveMaxDb) }, dashes, 2, 1.0f);

    // ceiling
    g.setColour (SolidColours::red.withAlpha (0.55f));
    g.drawHorizontalLine ((int) yFor (c), plot.getX(), plot.getRight());
    g.drawText ("CEILING", Rectangle<float> (plot.getX() + 2.0f, yFor (c) + 2.0f, 60.0f, 10.0f), Justification::centredLeft);

    // the curve itself (same maths as the DSP)
    const float cLin = Decibels::decibelsToGain (c), tLin = Decibels::decibelsToGain (t), k = cLin - tLin;
    auto outDb = [&] (float inDb)
    {
        const float d = Decibels::decibelsToGain (inDb);
        const float y = d <= tLin ? d : (k <= 0.0f ? cLin : tLin + k * std::tanh ((d - tLin) / k));
        return Decibels::gainToDecibels (y, -100.0f);
    };

    Path curve;
    for (int i = 0; i <= 120; ++i)
    {
        const float inDb = kCurveMinDb + (kCurveMaxDb - kCurveMinDb) * (float) i / 120.0f;
        const Point<float> p { xFor (inDb), yFor (outDb (inDb)) };
        if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
    }
    g.setColour (SolidColours::blueGlow.withAlpha (0.3f));
    g.strokePath (curve, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (SolidColours::blue);
    g.strokePath (curve, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // live input peak
    if (peakDb > kCurveMinDb)
    {
        const float inDb = std::min (peakDb, kCurveMaxDb);
        const Point<float> p { xFor (inDb), yFor (outDb (inDb)) };
        g.setColour (Colours::white.withAlpha (0.35f));
        g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (p));
        g.setColour (Colours::white);
        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (p));
    }

    g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
    g.setColour (SolidColours::labelText);
    g.drawText (caption, plot.reduced (4.0f, 2.0f).removeFromTop (12.0f), Justification::centredLeft);
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
        "MODE:  STEREO clips L and R. MID clips only the mid (sides untouched). SIDE clips only the sides (mid untouched).",
        "L/R LINK (Stereo):  on = the same gain on L and R, so the stereo image never changes. Off = each side clipped on its own.",
        "CEILING:  the level nothing gets above.  KNEE:  how gently clipping starts - 0 dB = hard clip, more = softer.",
        "SHAPE:  0 % = pure clipper. Higher = gain smoothed over up to 3 ms with matching lookahead (clipper/limiter hybrid).",
        "IN/OUT LINK:  turning Input up turns Output down by the same amount, so you hear more clipping, not more level.",
        "OVERSAMPLING:  detection + gain at up to 16x to catch inter-sample peaks. Linear phase is bit-transparent below the knee.",
        "TP GUARD:  catches the small true-peak overshoots left by the down-sampling filter, so the ceiling holds solid.",
        "DELTA:  listen to exactly what is removed (dry - wet, latency aligned).",
        "Knobs: drag, shift-drag = fine, scroll wheel, double-click = default. Double-click a meter to reset them all.",
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
