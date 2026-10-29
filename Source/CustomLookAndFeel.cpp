#include "CustomLookAndFeel.h"
#include "UI/SolidComponents.h"

using namespace juce;

//==============================================================================
CustomLookAndFeel::CustomLookAndFeel()
{
    setColour (ComboBox::textColourId,                  Colours::white);
    setColour (ComboBox::backgroundColourId,            SolidColours::insetBottom);
    setColour (ComboBox::outlineColourId,               SolidColours::bevelDark);
    setColour (ComboBox::arrowColourId,                 Colour (0xffd0d4d8));
    setColour (PopupMenu::backgroundColourId,           Colour (0xff1b1e21));
    setColour (PopupMenu::textColourId,                 Colour (0xffe8ebee));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (0xff2a6fb8));
    setColour (PopupMenu::highlightedTextColourId,      Colours::white);
    setColour (PopupMenu::headerTextColourId,           SolidColours::labelText);
    setColour (TextButton::textColourOffId,             Colour (0xffdfe3e6));
    setColour (TextButton::textColourOnId,              Colours::white);
    setColour (TooltipWindow::textColourId,             Colours::white);
    setColour (TooltipWindow::backgroundColourId,       Colour (0xf0163f78));
    setColour (TooltipWindow::outlineColourId,          SolidColours::blue);
}

Font CustomLookAndFeel::uiFont (float height, bool bold)
{
    return Font (FontOptions (height, bold ? Font::bold : Font::plain));
}

//==============================================================================
void CustomLookAndFeel::drawTicks (Graphics& g, Point<float> c, float rInner, float rOuter,
                                   float start, float end, int count, Colour colour, float thickness)
{
    g.setColour (colour);
    for (int i = 0; i < count; ++i)
    {
        const float a = start + (end - start) * (float) i / (float) (count - 1);
        const float s = std::sin (a), co = -std::cos (a);
        const bool major = (i % 5) == 0;
        const float r0 = major ? rInner - 1.5f : rInner;
        g.drawLine (c.x + s * r0, c.y + co * r0, c.x + s * rOuter, c.y + co * rOuter,
                    major ? thickness * 1.4f : thickness);
    }
}

void CustomLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                          float start, float end, Slider& slider)
{
    auto area = Rectangle<int> (x, y, width, height).toFloat();
    auto* knob = dynamic_cast<SolidKnob*> (&slider);

    if (knob != nullptr && knob->getKnobSize() == SolidKnob::Size::Large)
        drawLargeKnob (g, area, pos, start, end);
    else
        drawSmallKnob (g, area, pos, start, end);
}

void CustomLookAndFeel::drawLargeKnob (Graphics& g, Rectangle<float> area, float pos, float start, float end)
{
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float R = size * 0.5f - 1.0f;
    const float angle = start + pos * (end - start);

    // Fine tick marks around the outside
    drawTicks (g, c, R - 4.5f, R, start, end, 41, Colour (0xff9aa1a8).withAlpha (0.75f), 0.8f);

    // Blue arc ring: dark recessed track, glow and bright value arc
    const float ringR = R - 9.5f;
    {
        Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, start, end, true);
        g.setColour (Colour (0xff0a0c0e));
        g.strokePath (track, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xff1a2633));
        g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        if (pos > 0.001f)
        {
            Path value;
            value.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, start, angle, true);
            g.setColour (SolidColours::blueGlow.withAlpha (0.18f));
            g.strokePath (value, PathStrokeType (12.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (SolidColours::blueGlow.withAlpha (0.35f));
            g.strokePath (value, PathStrokeType (7.5f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (SolidColours::blue);
            g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Colour (0xffc9e6ff).withAlpha (0.8f));
            g.strokePath (value, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    // Chrome dial
    const float dialR = ringR - 7.0f;
    const auto dialBounds = Rectangle<float> (dialR * 2.0f, dialR * 2.0f).withCentre (c);

    Path dial;
    dial.addEllipse (dialBounds);
    DropShadow (Colours::black.withAlpha (0.75f), 10, { 0, 4 }).drawForPath (g, dial);

    // outer chamfer
    g.setGradientFill (ColourGradient (Colour (0xfff5f7f9), dialBounds.getX(), dialBounds.getY(),
                                       Colour (0xff6c737a), dialBounds.getRight(), dialBounds.getBottom(), false));
    g.fillPath (dial);

    // brushed face
    const auto face = dialBounds.reduced (dialR * 0.09f);
    ColourGradient faceGrad (Colour (0xffe9ecef), face.getX(), face.getY(),
                             Colour (0xff9ba3ab), face.getRight(), face.getBottom(), false);
    faceGrad.addColour (0.45, Colour (0xffd2d7dc));
    g.setGradientFill (faceGrad);
    g.fillEllipse (face);

    // concentric brushing rings
    for (float r = face.getWidth() * 0.5f - 1.0f; r > 2.0f; r -= 1.6f)
    {
        const float alpha = 0.035f + 0.03f * std::sin (r * 2.3f);
        g.setColour ((((int) r) % 2 == 0 ? Colours::white : Colours::black).withAlpha (alpha));
        g.drawEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 0.7f);
    }

    // specular highlight (soft cone at top-left)
    {
        ColourGradient spec (Colours::white.withAlpha (0.55f), face.getX() + face.getWidth() * 0.3f, face.getY() + face.getHeight() * 0.22f,
                             Colours::white.withAlpha (0.0f), face.getCentreX(), face.getCentreY() + face.getHeight() * 0.1f, true);
        g.setGradientFill (spec);
        g.fillEllipse (face);
    }

    g.setColour (Colour (0xff3b4046));
    g.drawEllipse (dialBounds, 1.0f);
    g.setColour (Colours::white.withAlpha (0.6f));
    g.drawEllipse (face, 0.8f);

    // pointer line
    const float s = std::sin (angle), co = -std::cos (angle);
    const float p0 = dialR * 0.22f, p1 = dialR * 0.86f;
    g.setColour (Colours::white.withAlpha (0.5f));
    g.drawLine (c.x + s * p0 + 0.8f, c.y + co * p0 + 0.8f, c.x + s * p1 + 0.8f, c.y + co * p1 + 0.8f, 3.2f);
    g.setColour (Colour (0xff1c2025));
    g.drawLine (c.x + s * p0, c.y + co * p0, c.x + s * p1, c.y + co * p1, 3.0f);
}

void CustomLookAndFeel::drawSmallKnob (Graphics& g, Rectangle<float> area, float pos, float start, float end)
{
    const float size = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float R = size * 0.5f - 1.0f;
    const float angle = start + pos * (end - start);

    drawTicks (g, c, R - 3.5f, R, start, end, 21, Colour (0xff8d949b).withAlpha (0.8f), 0.8f);

    const float bodyR = R - 6.0f;
    const auto body = Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c);

    Path bodyPath;
    bodyPath.addEllipse (body);
    DropShadow (Colours::black.withAlpha (0.8f), 6, { 0, 3 }).drawForPath (g, bodyPath);

    // rim
    g.setGradientFill (ColourGradient (Colour (0xff6a6f75), body.getX(), body.getY(),
                                       Colour (0xff141618), body.getX(), body.getBottom(), false));
    g.fillEllipse (body);

    // rubberised cap
    const auto cap = body.reduced (bodyR * 0.14f);
    ColourGradient capGrad (Colour (0xff4a4f55), cap.getCentreX(), cap.getY(),
                            Colour (0xff1f2225), cap.getCentreX(), cap.getBottom(), false);
    g.setGradientFill (capGrad);
    g.fillEllipse (cap);

    // soft sheen
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.12f), cap.getCentreX(), cap.getY(),
                                       Colours::white.withAlpha (0.0f), cap.getCentreX(), cap.getCentreY(), false));
    g.fillEllipse (cap.reduced (1.0f));

    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (body, 0.8f);

    // indicator line
    const float s = std::sin (angle), co = -std::cos (angle);
    const float p0 = bodyR * 0.35f, p1 = bodyR * 0.92f;
    g.setColour (Colour (0xffe6e9ec));
    g.drawLine (c.x + s * p0, c.y + co * p0, c.x + s * p1, c.y + co * p1, 1.8f);
}

//==============================================================================
void CustomLookAndFeel::drawInsetBox (Graphics& g, Rectangle<float> b, float corner)
{
    g.setColour (SolidColours::bevelLight.withAlpha (0.12f));
    g.drawRoundedRectangle (b.translated (0.0f, 1.0f), corner, 1.0f);

    g.setGradientFill (ColourGradient (SolidColours::insetTop, 0.0f, b.getY(), SolidColours::insetBottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);

    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + corner, b.getRight() - corner);
    g.setColour (SolidColours::bevelDark);
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
}

void CustomLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool,
                                      int, int, int, int, ComboBox& box)
{
    const auto b = Rectangle<int> (width, height).toFloat();
    drawInsetBox (g, b);

    if (box.hasKeyboardFocus (false) || box.isPopupActive())
    {
        g.setColour (SolidColours::blue.withAlpha (0.45f));
        g.drawRoundedRectangle (b.reduced (1.0f), 3.0f, 1.0f);
    }

    // small down arrow
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    Path arrow;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (arrow);
}

Font CustomLookAndFeel::getComboBoxFont (ComboBox&)        { return uiFont (12.5f, true); }
Font CustomLookAndFeel::getPopupMenuFont()                  { return uiFont (13.0f); }

void CustomLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (Justification::centredLeft);
}

void CustomLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (SolidColours::blue.withAlpha (0.35f));
    g.drawRect (0, 0, width, height, 1);
}

//==============================================================================
void CustomLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour&,
                                              bool isHighlighted, bool isDown)
{
    auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    Colour top = on ? Colour (0xff2f86e0) : Colour (0xff6b7076);
    Colour bottom = on ? Colour (0xff15508f) : Colour (0xff3c4045);
    if (isHighlighted) { top = top.brighter (0.12f); bottom = bottom.brighter (0.12f); }
    if (isDown)        std::swap (top, bottom);

    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 3.0f);
    g.setGradientFill (ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine ((int) b.getY() + 1, b.getX() + 3.0f, b.getRight() - 3.0f);
    g.setColour (SolidColours::bevelDark);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
}

Font CustomLookAndFeel::getTextButtonFont (TextButton&, int)  { return uiFont (11.5f, true); }

void CustomLookAndFeel::drawButtonText (Graphics& g, TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? TextButton::textColourOnId : TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), Justification::centred);
}

//==============================================================================
void CustomLookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
{
    const auto b = Rectangle<int> (width, height).toFloat();
    g.setColour (findColour (TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (findColour (TooltipWindow::outlineColourId).withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (findColour (TooltipWindow::textColourId));
    g.setFont (uiFont (12.5f));
    g.drawFittedText (text, b.reduced (6.0f, 2.0f).toNearestInt(), Justification::centredLeft, 3);
}

//==============================================================================
void CustomLookAndFeel::drawPanel (Graphics& g, Rectangle<float> b, const String& title)
{
    constexpr float corner = 5.0f, headerH = 18.0f;

    // recess: light bevel below/right, dark above/left
    g.setColour (SolidColours::bevelLight);
    g.drawRoundedRectangle (b.translated (0.5f, 1.0f), corner, 1.0f);

    g.setGradientFill (ColourGradient (SolidColours::panelTop, 0.0f, b.getY(), SolidColours::panelBottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);

    // header bar
    Path header;
    const auto h = b.withHeight (headerH);
    header.addRoundedRectangle (h.getX(), h.getY(), h.getWidth(), h.getHeight(), corner, corner, true, true, false, false);
    g.setGradientFill (ColourGradient (SolidColours::headerTop, 0.0f, h.getY(), SolidColours::headerBottom, 0.0f, h.getBottom(), false));
    g.fillPath (header);
    g.setColour (Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine ((int) h.getY() + 1, h.getX() + corner, h.getRight() - corner);
    g.setColour (SolidColours::bevelDark);
    g.drawHorizontalLine ((int) h.getBottom(), h.getX(), h.getRight());
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) h.getBottom() + 1, h.getX() + 1.0f, h.getRight() - 1.0f);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.setFont (uiFont (10.5f, true));
    g.drawText (title.toUpperCase(), h.translated (0.0f, 1.0f), Justification::centred);
    g.setColour (SolidColours::headerText);
    g.drawText (title.toUpperCase(), h, Justification::centred);

    g.setColour (SolidColours::bevelDark);
    g.drawRoundedRectangle (b, corner, 1.0f);
}

void CustomLookAndFeel::drawLcd (Graphics& g, Rectangle<float> b, bool dimmed)
{
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (b.expanded (1.0f), 3.0f);

    const auto top = dimmed ? SolidColours::lcdDimTop : SolidColours::lcdTop;
    const auto bottom = dimmed ? SolidColours::lcdDimBottom : SolidColours::lcdBottom;
    g.setGradientFill (ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 2.5f);

    // glass highlight + faint scanlines
    g.setColour (Colours::white.withAlpha (0.10f));
    g.fillRoundedRectangle (b.withHeight (b.getHeight() * 0.45f).reduced (1.0f, 1.0f), 2.0f);
    g.setColour (Colours::black.withAlpha (0.06f));
    for (float yy = b.getY() + 2.0f; yy < b.getBottom() - 1.0f; yy += 2.0f)
        g.drawHorizontalLine ((int) yy, b.getX() + 1.0f, b.getRight() - 1.0f);

    g.setColour (Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (b, 2.5f, 1.0f);
}

void CustomLookAndFeel::drawSmallLabel (Graphics& g, const String& text, Rectangle<float> area, Justification j)
{
    g.setFont (uiFont (10.0f, true));
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawText (text.toUpperCase(), area.translated (0.0f, 1.0f), j);
    g.setColour (SolidColours::labelText);
    g.drawText (text.toUpperCase(), area, j);
}
