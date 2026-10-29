#pragma once

#include <juce_dsp/juce_dsp.h>
#include "ModeClipper.h"
#include "TruePeakGuard.h"
#include "Lookahead.h"
#include <array>
#include <atomic>
#include <memory>

namespace solidclip
{
    enum class Mode { Stereo = 0, Mid, Side };
    enum class FilterType { MinPhaseIIR = 0, LinearPhaseFIR };

    struct EngineSettings
    {
        float inputGainDb = 0.0f, outputGainDb = 0.0f;
        float ceilingDb = -0.3f, kneeDb = 3.0f;
        float shape = 0.3f;                     // 0..1
        int oversamplingIndex = 2;              // 0..4 -> 1x..16x
        FilterType filter = FilterType::MinPhaseIIR;
        Mode mode = Mode::Stereo;
        bool lrLink = true;                     // Stereo: same gain on L and R
        bool delta = false, guard = true, bypass = false;
    };

    /** Peak values for the editor, written by the audio thread, collected by the UI. */
    struct MeterState
    {
        std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };
        std::atomic<float> leftGrDb { 0.0f }, rightGrDb { 0.0f }, midGrDb { 0.0f }, sideGrDb { 0.0f }, guardGrDb { 0.0f };

        static void raise (std::atomic<float>& a, float v) noexcept
        {
            float current = a.load (std::memory_order_relaxed);
            while (v > current && ! a.compare_exchange_weak (current, v, std::memory_order_relaxed)) {}
        }
    };

    /**
        The complete SolidClip signal path. Everything is allocated in prepare(); process()
        is real-time safe.

          input gain -> [up-sample] -> clipper (Stereo / Mid / Side) -> [down-sample]
                     -> true-peak guard -> delta / output gain -> bypass mix

        The clipper computes the gains of every mode all the time, so every mode has the same
        latency and switching modes is a short crossfade of the mode amounts.

        Linear-phase (and 1x) processing uses a "difference" path: the dry signal is only
        delayed, and what goes through the oversampler is the *removed* part
        (clipped - unclipped). When nothing is clipped that is exactly zero, so the output
        is bit-identical to the latency-aligned input. Min-phase IIR filters shift the
        phase of the whole band, so there the full signal is oversampled instead.
    */
    class ClipEngine
    {
    public:
        static constexpr int numOversamplingChoices = 5;
        static constexpr double maxLookaheadSeconds = 0.003;

        ClipEngine();
        ~ClipEngine();

        /** Allocates everything and applies the settings immediately (no fade). */
        void prepare (double sampleRate, int maxBlockSize, const EngineSettings&);
        void reset();

        /** Processes a stereo block of any length in place. */
        void process (float* left, float* right, int numSamples, const EngineSettings&) noexcept;

        int getLatencySamples() const noexcept  { return latencySamples.load(); }
        MeterState& getMeters() noexcept        { return meters; }

        /** Latency the engine would report for a configuration (used for tests and prepare). */
        int latencyFor (int oversamplingIndex, FilterType, bool lookahead) const noexcept;

    private:
        struct Config
        {
            int osIndex = 2;
            FilterType filter = FilterType::MinPhaseIIR;
            bool lookahead = true;
            bool operator== (const Config& o) const noexcept { return osIndex == o.osIndex && filter == o.filter && lookahead == o.lookahead; }
            bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
        };

        static Config configFrom (const EngineSettings&) noexcept;
        static bool usesDifferencePath (const Config& c) noexcept { return c.osIndex == 0 || c.filter == FilterType::LinearPhaseFIR; }
        juce::dsp::Oversampling<float>& oversamplerFor (const Config&) const noexcept;
        void applyConfig (const Config&) noexcept;
        void setTargets (const EngineSettings&, bool immediately) noexcept;
        void processChunk (float* left, float* right, int numSamples) noexcept;
        int oversamplingPad (const Config&) const noexcept;

        // configuration ------------------------------------------------------------------
        double sampleRate = 44100.0;
        int maxBlock = 512, lookaheadBase = 0;
        Config active, pending;
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, numOversamplingChoices * 2> oversamplers;
        juce::dsp::Oversampling<float>* os = nullptr;
        int factor = 1, factorShift = 0, stageLookahead = 0, totalLatency = 0, guardLatency = 0;
        std::atomic<int> latencySamples { 0 };

        // DSP blocks ---------------------------------------------------------------------
        ModeClipper clipper;
        Lookahead<2> clipperDelay;      // audio delay matching the clipper's lookahead
        Lookahead<2> referenceDelay;    // oversampled input aligned with the stage output
        Lookahead<2> dryRaw;            // un-gained input, for bypass
        Lookahead<2> dryGained;         // gained input, for the difference path
        Lookahead<2> preGuardRef;       // reference signal aligned with the guard output
        TruePeakGuard guard;
        int currentWindow = -1;

        // scratch (allocated in prepare) -------------------------------------------------
        juce::AudioBuffer<float> upInput, downOutput;

        // smoothed controls ----------------------------------------------------------------
        using Linear = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
        Linear inputGainDb, outputGainDb, ceilingDb, kneeDb;
        Linear stereoAmount, midAmount, sideAmount, lrLinkAmount, guardAmount, deltaAmount, bypassAmount;
        float shapeTarget = 0.0f;

        // per-chunk control arrays at the host rate
        struct ControlFrame
        {
            SoftClipCurve curve;
            ModeAmounts modes;
        };
        std::vector<ControlFrame> controls;
        std::vector<float> inGain, outGain, guardCeiling, guardAmt, deltaAmt, bypassAmt;

        // configuration change fade --------------------------------------------------------
        float switchFade = 1.0f, switchStep = 0.01f;
        bool fadingOut = false;

        MeterState meters;
    };
}
