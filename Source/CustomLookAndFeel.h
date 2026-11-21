#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
namespace SolidColours
{
    // black, with orange as the accent, light grey for labels and white for highlights
    const juce::Colour background   { 0xff0e0e0f };
    const juce::Colour panelTop     { 0xff1b1b1d };
    const juce::Colour panelBottom  { 0xff111112 };
    const juce::Colour headerTop    { 0xff2e2e31 };
    const juce::Colour headerBottom { 0xff1e1e20 };
    const juce::Colour headerText   { 0xffd9d9db };
    const juce::Colour labelText    { 0xffb4b4b8 };
    const juce::Colour bevelDark    { 0xff000000 };
    const juce::Colour bevelLight   { 0x22ffffff };
    const juce::Colour insetTop     { 0xff050505 };
    const juce::Colour insetBottom  { 0xff141415 };
    const juce::Colour lcdTop       { 0xff1c1714 };
    const juce::Colour lcdBottom    { 0xff0b0908 };
    const juce::Colour lcdDimTop    { 0xff171718 };
    const juce::Colour lcdDimBottom { 0xff0c0c0d };
    const juce::Colour accent       { 0xffff7a1a };   // orange
    const juce::Colour accentGlow   { 0xffe8600c };
    const juce::Colour accentLight  { 0xffffd2a8 };
    const juce::Colour lightGrey    { 0xffc8c8cc };

    // fixed shades for buttons, bars, popups and text
    const juce::Colour accentTop    { 0xffff8a2a };   // orange gradient (toggled buttons, meter bars)
    const juce::Colour accentBottom { 0xffc4520a };   // ... also the popup highlight
    const juce::Colour buttonTop    { 0xff343437 };   // dark button gradient
    const juce::Colour buttonBottom { 0xff1a1a1c };
    const juce::Colour popup        { 0xff121213 };   // popup menu / tooltip background
    const juce::Colour popupText    { 0xffe8ebee };
    const juce::Colour buttonText   { 0xffdfe3e6 };   // light text on dark buttons
    const juce::Colour dimText      { 0xff6e6e72 };   // text on inactive LCDs
    const juce::Colour arcTrack     { 0xff2a1a10 };   // recessed track under the orange arc
    const juce::Colour knobRimTop   { 0xff5a5a5e };   // small black knob
    const juce::Colour knobRimBottom{ 0xff0a0a0b };
    const juce::Colour knobCapTop   { 0xff2c2c2f };
    const juce::Colour knobCapBottom{ 0xff0d0d0e };
    const juce::Colour windowTop    { 0xff1a1a1c };   // window background gradient
    const juce::Colour windowBottom { 0xff070708 };
    const juce::Colour stripTop     { 0xff222224 };   // top strip
    const juce::Colour stripBottom  { 0xff111112 };
    const juce::Colour barTop       { 0xff121213 };   // bottom bar
    const juce::Colour barBottom    { 0xff050505 };
    const juce::Colour red          { 0xffff3b30 };
}

//==============================================================================
/**
    Draws every control procedurally (gradients, paths, drop shadows), so the
    plugin needs no image assets.
*/
class CustomLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CustomLookAndFeel();

    // Fonts ------------------------------------------------------------------------
    static juce::Font uiFont (float height, bool bold = false);

    // Slider -----------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    // ComboBox / popup ---------------------------------------------------------------
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    // Buttons ------------------------------------------------------------------------
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Tooltip ------------------------------------------------------------------------
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    // Panel helpers (used by the editor and custom components) ------------------------
    static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& title);
    static void drawInsetBox (juce::Graphics&, juce::Rectangle<float> bounds, float corner = 3.0f);
    static void drawLcd (juce::Graphics&, juce::Rectangle<float> bounds, bool dimmed);
    static void drawSmallLabel (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                                juce::Justification = juce::Justification::centred);

private:
    void drawLargeKnob (juce::Graphics&, juce::Rectangle<float> area, float pos, float start, float end);
    void drawSmallKnob (juce::Graphics&, juce::Rectangle<float> area, float pos, float start, float end);
    static void drawTicks (juce::Graphics&, juce::Point<float> centre, float rInner, float rOuter,
                           float start, float end, int count, juce::Colour, float thickness);
};
