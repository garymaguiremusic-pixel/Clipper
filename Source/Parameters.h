#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/ClipEngine.h"

namespace sc
{
    namespace ParamIDs
    {
        inline constexpr const char* inputGain    = "inputGain";
        inline constexpr const char* ceiling      = "ceiling";
        inline constexpr const char* knee         = "knee";
        inline constexpr const char* shape        = "shape";
        inline constexpr const char* oversampling = "oversampling";
        inline constexpr const char* filterType   = "filterType";
        inline constexpr const char* mode         = "mode";
        inline constexpr const char* lrLink       = "lrLink";
        inline constexpr const char* outputGain   = "outputGain";
        inline constexpr const char* gainLink     = "gainLink";
        inline constexpr const char* delta        = "delta";
        inline constexpr const char* guard        = "guard";
        inline constexpr const char* bypass       = "bypass";
    }

    inline juce::StringArray getOversamplingNames()  { return { "1x", "2x", "4x", "8x", "16x" }; }
    inline juce::StringArray getFilterNames()        { return { "Min Phase IIR", "Linear Phase FIR" }; }
    inline juce::StringArray getModeNames()          { return { "Stereo", "Mid", "Side" }; }

    inline juce::String formatDb (float v, int)      { return juce::String (v, 1) + " dB"; }
    inline juce::String formatPercent (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> params;

        auto addFloat = [&params] (const char* id, const char* name, float min, float max, float def,
                                   float step, String (*format) (float, int), const char* unit)
        {
            NormalisableRange<float> range (min, max, step);
            params.push_back (std::make_unique<AudioParameterFloat> (
                ParameterID { id, 1 }, name, range, def,
                AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (format)));
        };
        auto addBool = [&params] (const char* id, const char* name, bool def)
        {
            params.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
        };
        auto addChoice = [&params] (const char* id, const char* name, const StringArray& items, int def)
        {
            params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
        };

        addFloat (ParamIDs::inputGain,   "Input Gain",   -24.0f, 24.0f,  0.0f, 0.01f, formatDb, "dB");
        addFloat (ParamIDs::ceiling,     "Ceiling",      -12.0f,  0.0f, -0.3f, 0.01f, formatDb, "dB");
        addFloat (ParamIDs::knee,        "Knee",           0.0f, 12.0f,  3.0f, 0.01f, formatDb, "dB");
        addFloat (ParamIDs::shape,       "Shape",          0.0f, 100.0f, 30.0f, 0.1f, formatPercent, "%");
        addChoice (ParamIDs::oversampling, "Oversampling", getOversamplingNames(), 3);
        addChoice (ParamIDs::filterType,   "Filter Type",  getFilterNames(), 0);
        addChoice (ParamIDs::mode,         "Mode",         getModeNames(), 0);
        addBool (ParamIDs::lrLink,       "L/R Link",     true);
        addFloat (ParamIDs::outputGain,  "Output Gain",  -24.0f, 24.0f, 0.0f, 0.01f, formatDb, "dB");
        addBool (ParamIDs::gainLink,     "In/Out Link",  true);
        addBool (ParamIDs::delta,        "Delta",        false);
        addBool (ParamIDs::guard,        "True Peak Guard", true);
        addBool (ParamIDs::bypass,       "Bypass",       false);

        return { params.begin(), params.end() };
    }

    /** Snapshot of the parameters for the engine (lock-free: raw atomic reads). */
    inline solidclip::EngineSettings readSettings (juce::AudioProcessorValueTreeState& apvts)
    {
        auto v = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
        solidclip::EngineSettings s;
        s.inputGainDb       = v (ParamIDs::inputGain);
        s.outputGainDb      = v (ParamIDs::outputGain);
        s.ceilingDb         = v (ParamIDs::ceiling);
        s.kneeDb            = v (ParamIDs::knee);
        s.shape             = v (ParamIDs::shape) * 0.01f;
        s.oversamplingIndex = (int) v (ParamIDs::oversampling);
        s.filter            = (int) v (ParamIDs::filterType) == 1 ? solidclip::FilterType::LinearPhaseFIR : solidclip::FilterType::MinPhaseIIR;
        s.mode              = (solidclip::Mode) juce::jlimit (0, 2, (int) v (ParamIDs::mode));
        s.lrLink            = v (ParamIDs::lrLink) > 0.5f;
        s.delta             = v (ParamIDs::delta) > 0.5f;
        s.guard             = v (ParamIDs::guard) > 0.5f;
        s.bypass            = v (ParamIDs::bypass) > 0.5f;
        return s;
    }
}
