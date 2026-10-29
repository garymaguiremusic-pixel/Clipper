#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr int kTopStripH = 34, kBottomBarY = 352;
    const Identifier uiWidthId ("uiWidth");
}

//==============================================================================
// SolidClipView
//==============================================================================
SolidClipView::SolidClipView (SolidClipAudioProcessor& p)
    : processor (p)
{
    auto& apvts = processor.apvts;

    // Dropdowns ------------------------------------------------------------------
    setupCombo (oversamplingBox, sc::getOversamplingNames(), "Oversampling factor for detection and gain (catches inter-sample peaks)");
    setupCombo (filterBox,       sc::getFilterNames(),       "Min phase IIR = low latency. Linear phase FIR = bit-transparent below the knee, more latency");
    comboAttachments.push_back (std::make_unique<ComboAttachment> (apvts, sc::ParamIDs::oversampling, oversamplingBox));
    comboAttachments.push_back (std::make_unique<ComboAttachment> (apvts, sc::ParamIDs::filterType, filterBox));

    // Knobs ----------------------------------------------------------------------
    const std::pair<SolidKnob*, const char*> knobs[] =
    {
        { &inputKnob, sc::ParamIDs::inputGain }, { &outputKnob, sc::ParamIDs::outputGain },
        { &ceilingKnob, sc::ParamIDs::ceiling }, { &kneeKnob, sc::ParamIDs::knee },
        { &shapeKnob, sc::ParamIDs::shape },
    };
    for (auto& [knob, id] : knobs)
    {
        knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (id), *knob));
        addAndMakeVisible (*knob);
    }

    inputKnob.showValueAlways = outputKnob.showValueAlways = true;
    inputKnob.extraTooltip   = "Drive into the clipper. With Link on, Output moves the opposite way";
    ceilingKnob.extraTooltip = "The level nothing gets above";
    kneeKnob.extraTooltip    = "How gently clipping starts: 0 dB = hard clip at the ceiling.\n"
                               "3 dB = the curve starts bending 3 dB below the ceiling (smoother, fewer harsh harmonics).\n"
                               "Below (ceiling - knee) the audio is untouched - see the transfer curve";
    shapeKnob.extraTooltip   = "0 % = pure clipper. Higher = smoother gain over up to 3 ms with matching lookahead";

    // Toggle buttons ---------------------------------------------------------------
    const std::tuple<TextButton*, const char*, const char*> buttons[] =
    {
        { &bypassButton,   sc::ParamIDs::bypass,   "Bypass (latency compensated)" },
        { &gainLinkButton, sc::ParamIDs::gainLink, "Link Input and Output: turning Input up turns Output down by the same amount" },
        { &deltaButton,    sc::ParamIDs::delta,    "Delta: listen to exactly what is removed (dry - wet, latency aligned)" },
        { &guardButton,    sc::ParamIDs::guard,    "True-peak guard: catches the small overshoots the down-sampling filter leaves" },
        { &lrLinkButton,   sc::ParamIDs::lrLink,   "Stereo mode: on = the same gain on L and R (the stereo image never changes).\n"
                                                   "Off = L and R are clipped independently" },
    };
    for (auto& [button, id, tip] : buttons)
    {
        button->setClickingTogglesState (true);
        button->setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, *button));
        addAndMakeVisible (*button);
    }

    // Mode selector: three radio buttons on the Mode parameter -------------------------
    const char* modeTips[] =
    {
        "Stereo: clip the left and right channels",
        "Mid: clip only the mid (L + R). The sides are not touched",
        "Side: clip only the sides (L - R). The mid is not touched",
    };
    for (int i = 0; i < 3; ++i)
    {
        modeButtons[i].setTooltip (modeTips[i]);
        modeButtons[i].setRadioGroupId (1001);
        modeButtons[i].onClick = [this, i] { setMode (i); };
        addAndMakeVisible (modeButtons[i]);
    }

    for (Component* c : { (Component*) &ceilingLcd, (Component*) &kneeLcd, (Component*) &smoothingLcd, (Component*) &latencyLcd,
                          (Component*) &transferCurve, (Component*) &inMeter, (Component*) &outMeter,
                          (Component*) &grMeter1, (Component*) &grMeter2, (Component*) &guardGr })
        addAndMakeVisible (*c);

    for (BarMeter* meter : { &inMeter, &outMeter, &grMeter1, &grMeter2, &guardGr })
        meter->onDoubleClick = [this] { resetMeters(); };
    transferCurve.onDoubleClick = [this] { resetMeters(); };
    transferCurve.setTooltip ("Ceiling and Knee as an input -> output curve. Double-click to reset the meters");

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    setSize (kWidth, kHeight);
    createBackground();

    lastTimerMs = Time::getMillisecondCounterHiRes();
    timerCallback();
    startTimerHz (30);
}

SolidClipView::~SolidClipView()
{
    stopTimer();
}

void SolidClipView::setupCombo (ComboBox& box, const StringArray& items, const String& tooltip)
{
    box.addItemList (items, 1);
    box.setTooltip (tooltip);
    box.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (box);
}

float SolidClipView::raw (const char* id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

void SolidClipView::setMode (int index)
{
    auto* param = processor.apvts.getParameter (sc::ParamIDs::mode);
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    param->endChangeGesture();
}

void SolidClipView::resetMeters()
{
    // drop whatever the audio thread collected so far, then clear the holds
    auto& m = processor.getMeters();
    for (auto* a : { &m.inputPeak, &m.outputPeak, &m.leftGrDb, &m.rightGrDb, &m.midGrDb, &m.sideGrDb, &m.guardGrDb })
        a->store (0.0f);
    for (BarMeter* meter : { &inMeter, &outMeter, &grMeter1, &grMeter2, &guardGr })
        meter->reset();
    transferCurve.reset();
}

//==============================================================================
void SolidClipView::resized()
{
    ioPanel    = { 8.0f,   40.0f, 112.0f, 236.0f };
    clipPanel  = { 126.0f, 40.0f, 330.0f, 236.0f };
    curvePanel = { 462.0f, 40.0f, 210.0f, 236.0f };
    osPanel    = { 678.0f, 40.0f, 134.0f, 236.0f };
    meterArea  = { 8.0f,  282.0f, 804.0f, 64.0f };

    bypassButton.setBounds (752, 8, 60, 19);

    // IN / OUT
    inputKnob.setBounds (28, 56, 72, 82);
    gainLinkButton.setBounds (30, 141, 68, 18);
    outputKnob.setBounds (28, 162, 72, 82);
    deltaButton.setBounds (16, 248, 96, 22);

    // CLIPPER
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setBounds (136 + i * 104, 62, 98, 22);
    ceilingKnob.setBounds (142, 94, 90, 90);
    kneeKnob.setBounds (240, 94, 90, 90);
    shapeKnob.setBounds (352, 102, 64, 72);
    ceilingLcd.setBounds (142, 188, 89, 22);
    kneeLcd.setBounds (241, 188, 89, 22);
    lrLinkButton.setBounds (136, 240, 150, 22);
    smoothingLcd.setBounds (296, 240, 150, 22);

    // TRANSFER CURVE
    transferCurve.setBounds (470, 62, 194, 206);

    // OVERSAMPLING
    oversamplingBox.setBounds (686, 80, 118, 21);
    filterBox.setBounds (686, 122, 118, 21);
    guardButton.setBounds (686, 164, 118, 22);
    latencyLcd.setBounds (686, 240, 118, 22);

    // meters
    const int colX[] = { 16, 284, 552 }, rowY[] = { 288, 316 }, colW = 252, rowH = 24;
    inMeter.setBounds  (colX[0], rowY[0], colW, rowH);
    outMeter.setBounds (colX[0], rowY[1], colW, rowH);
    grMeter1.setBounds (colX[1], rowY[0], colW, rowH);
    grMeter2.setBounds (colX[1], rowY[1], colW, rowH);
    guardGr.setBounds  (colX[2], rowY[0], colW, rowH);

    helpButton.setBounds (kWidth - 26, kBottomBarY + 6, 20, 20);
    helpOverlay.setBounds (getLocalBounds());
}

void SolidClipView::setRenderScale (float scale)
{
    if (std::abs (scale - renderScale) > 0.01f || ! background.isValid())
    {
        renderScale = scale;
        createBackground();
        repaint();
    }
}

void SolidClipView::setActive (Component& c, bool active)
{
    if (c.isEnabled() != active)
    {
        c.setEnabled (active);
        c.setAlpha (active ? 1.0f : 0.4f);
    }
}

void SolidClipView::updateActiveStates()
{
    const int mode = jlimit (0, 2, (int) raw (sc::ParamIDs::mode));
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setToggleState (i == mode, dontSendNotification);

    const bool stereo = mode == (int) solidclip::Mode::Stereo;
    setActive (lrLinkButton, stereo);
    lrLinkButton.setButtonText (raw (sc::ParamIDs::lrLink) > 0.5f ? "L/R Link: On" : "L/R Link: Off");

    if (stereo)
    {
        grMeter1.setLabel ("L GR");
        grMeter2.setLabel ("R GR");
        grMeter1.setDimmed (false);
        grMeter2.setDimmed (false);
    }
    else
    {
        grMeter1.setLabel ("Mid GR");
        grMeter2.setLabel ("Side GR");
        grMeter1.setDimmed (mode != (int) solidclip::Mode::Mid);
        grMeter2.setDimmed (mode != (int) solidclip::Mode::Side);
    }
    guardGr.setDimmed (raw (sc::ParamIDs::guard) < 0.5f);
}

//==============================================================================
void SolidClipView::createBackground()
{
    const float s = renderScale;
    const int w = roundToInt (kWidth * s), h = roundToInt (kHeight * s);
    background = Image (Image::RGB, std::max (1, w), std::max (1, h), true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (s));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x5c11);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 2200; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    // soft vignette
    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void SolidClipView::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Stylised lowercase wordmark: chrome "solid", blue "clip", and a swoosh.
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "solid", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "clip", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (SolidColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (SolidColours::blue.withAlpha (0.0f), sx, sy, SolidColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SolidClipView::paint (Graphics& g)
{
    g.drawImage (background, Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kHeight));

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (SolidColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });

    // --- panels ---------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, ioPanel,    "In / Out");
    CustomLookAndFeel::drawPanel (g, clipPanel,  "Clipper");
    CustomLookAndFeel::drawPanel (g, curvePanel, "Transfer Curve");
    CustomLookAndFeel::drawPanel (g, osPanel,    "Oversampling");

    auto divider = [&g] (float y, float x0, float x1)
    {
        g.setColour (SolidColours::bevelDark);
        g.drawHorizontalLine ((int) y, x0, x1);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) y + 1, x0, x1);
    };
    auto label = [&g] (const char* text, float x, float y, float w)
    {
        CustomLookAndFeel::drawSmallLabel (g, text, { x, y, w, 12.0f }, Justification::centredLeft);
    };


    divider (218.0f, 134.0f, 448.0f);
    label ("Stereo channels", 136.0f, 226.0f, 150.0f);
    label ("Smoothing", 296.0f, 226.0f, 150.0f);

    label ("Factor", 686.0f, 66.0f, 118.0f);
    label ("Filter", 686.0f, 108.0f, 118.0f);
    label ("True peak", 686.0f, 150.0f, 118.0f);
    label ("Latency", 686.0f, 226.0f, 118.0f);

    // --- meter strip ----------------------------------------------------------------
    CustomLookAndFeel::drawInsetBox (g, meterArea, 4.0f);

    // --- bottom bar -----------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 7.0f, 52.0f, 18.0f);
    g.setGradientFill (ColourGradient (SolidColours::headerTop, 0.0f, tab.getY(), SolidColours::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (SolidColours::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (SolidColours::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("CLIP", tab, Justification::centred);

    auto inset = bar.reduced (70.0f, 7.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffc8cdd2));
    g.drawText ("SOLID CLIP", inset, Justification::centred);
}

//==============================================================================
void SolidClipView::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = (float) jlimit (0.0, 0.1, (now - lastTimerMs) * 0.001);
    lastTimerMs = now;

    updateActiveStates();

    const int mode = jlimit (0, 2, (int) raw (sc::ParamIDs::mode));
    const float ceiling = raw (sc::ParamIDs::ceiling), knee = raw (sc::ParamIDs::knee);
    ceilingLcd.setContent ("CEILING", String (ceiling, 1) + " dB");
    kneeLcd.setContent ("KNEE", knee < 0.05f ? String ("HARD") : String (knee, 1) + " dB");

    const float shape = raw (sc::ParamIDs::shape) * 0.01f;
    smoothingLcd.setContent ("SMOOTH", shape <= 0.0f ? String ("CLIP") : String (shape * 3.0f, 2) + " ms");

    const int latency = processor.getLatencySamples();
    const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    latencyLcd.setContent (String (latency) + " smp", String (1000.0 * latency / sr, 1) + " ms");

    const bool linked = raw (sc::ParamIDs::gainLink) > 0.5f;
    outputKnob.extraTooltip = linked ? "Linked to Input: it moves the opposite way so the level stays the same"
                                     : "Gain after clipping (can move the output above the ceiling)";

    static const char* captions[] = { "CLIPPING  L + R", "CLIPPING  MID ONLY", "CLIPPING  SIDE ONLY" };
    transferCurve.setCurve (ceiling, knee);
    transferCurve.setCaption (mode == 0 && raw (sc::ParamIDs::lrLink) > 0.5f ? String ("CLIPPING  L + R  (LINKED)") : String (captions[mode]));

    auto& m = processor.getMeters();
    const float inPeak = m.inputPeak.exchange (0.0f);
    transferCurve.setInputPeak (inPeak, dt);
    inMeter.setCeilingDb (ceiling);
    outMeter.setCeilingDb (ceiling);
    inMeter.push (inPeak, dt);
    outMeter.push (m.outputPeak.exchange (0.0f), dt);

    const float l = m.leftGrDb.exchange (0.0f), r = m.rightGrDb.exchange (0.0f);
    const float mid = m.midGrDb.exchange (0.0f), side = m.sideGrDb.exchange (0.0f);
    grMeter1.push (mode == 0 ? l : mid, dt);
    grMeter2.push (mode == 0 ? r : side, dt);
    guardGr.push (m.guardGrDb.exchange (0.0f), dt);
}

//==============================================================================
// SolidClipAudioProcessorEditor
//==============================================================================
SolidClipAudioProcessorEditor::SolidClipAudioProcessorEditor (SolidClipAudioProcessor& p)
    : AudioProcessorEditor (&p), view (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (view);

    constexpr double ratio = (double) SolidClipView::kWidth / (double) SolidClipView::kHeight;
    setResizable (true, true);
    setResizeLimits (SolidClipView::kWidth * 3 / 4, SolidClipView::kHeight * 3 / 4,
                     SolidClipView::kWidth * 5 / 2, SolidClipView::kHeight * 5 / 2);
    getConstrainer()->setFixedAspectRatio (ratio);

    const int savedWidth = (int) p.apvts.state.getProperty (uiWidthId, SolidClipView::kWidth);
    const int width = jlimit (SolidClipView::kWidth * 3 / 4, SolidClipView::kWidth * 5 / 2, savedWidth);
    setSize (width, roundToInt (width / ratio));
}

SolidClipAudioProcessorEditor::~SolidClipAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SolidClipAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) SolidClipView::kWidth;
    view.setTransform (AffineTransform::scale (scale));
    view.setBounds (0, 0, SolidClipView::kWidth, SolidClipView::kHeight);

    const float displayScale = Component::getApproximateScaleFactorForComponent (this);
    view.setRenderScale (scale * std::max (1.0f, displayScale));

    if (auto* proc = dynamic_cast<SolidClipAudioProcessor*> (getAudioProcessor()))
        proc->apvts.state.setProperty (uiWidthId, getWidth(), nullptr);
}
