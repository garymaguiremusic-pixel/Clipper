#include "ClipEngine.h"

namespace solidclip
{
    namespace
    {
        constexpr int kMaxChunk = 256;   // internal block size; keeps config switches short
        constexpr int kMaxFactorShift = 4;

        float mix (float a, float b, float amount) noexcept
        {
            return amount <= 0.0f ? a : (amount >= 1.0f ? b : a + amount * (b - a));
        }
    }

    ClipEngine::ClipEngine() = default;
    ClipEngine::~ClipEngine() = default;

    ClipEngine::Config ClipEngine::configFrom (const EngineSettings& s) noexcept
    {
        Config c;
        c.osIndex = std::clamp (s.oversamplingIndex, 0, numOversamplingChoices - 1);
        c.filter = s.filter;
        c.lookahead = s.shape > 0.0f;
        return c;
    }

    juce::dsp::Oversampling<float>& ClipEngine::oversamplerFor (const Config& c) const noexcept
    {
        return *oversamplers[(size_t) (c.osIndex * 2 + (int) c.filter)];
    }

    int ClipEngine::oversamplingPad (const Config& c) const noexcept
    {
        const int f = 1 << c.osIndex;
        const auto latencyOs = (int) std::lround ((double) oversamplerFor (c).getLatencyInSamples() * f);
        return (f - latencyOs % f) % f;
    }

    int ClipEngine::latencyFor (int osIndex, FilterType filter, bool lookahead) const noexcept
    {
        Config c { std::clamp (osIndex, 0, numOversamplingChoices - 1), filter, lookahead };
        const int f = 1 << c.osIndex;
        const auto latencyOs = (int) std::lround ((double) oversamplerFor (c).getLatencyInSamples() * f);
        const int osLatency = (latencyOs + oversamplingPad (c)) / f;
        return osLatency + (lookahead ? lookaheadBase : 0) + guardLatency;
    }

    //==============================================================================
    void ClipEngine::prepare (double newSampleRate, int maxBlockSize, const EngineSettings& settings)
    {
        sampleRate = newSampleRate;
        maxBlock = std::clamp (maxBlockSize, 1, kMaxChunk);
        lookaheadBase = (int) std::lround (sampleRate * maxLookaheadSeconds);

        using OS = juce::dsp::Oversampling<float>;
        for (int f = 0; f < numOversamplingChoices; ++f)
        {
            for (int t = 0; t < 2; ++t)
            {
                const bool iir = t == (int) FilterType::MinPhaseIIR;
                // IIR (full-signal path) also carries the reference through the filters
                const size_t channels = (iir && f > 0) ? 4 : 2;
                auto& o = oversamplers[(size_t) (f * 2 + t)];
                o = std::make_unique<OS> (channels, (size_t) f,
                                          iir ? OS::filterHalfBandPolyphaseIIR : OS::filterHalfBandFIREquiripple,
                                          true, false);
                o->initProcessing ((size_t) maxBlock);
            }
        }

        const int maxStageLookahead = lookaheadBase * (1 << kMaxFactorShift) + (1 << kMaxFactorShift);
        clipper.prepare (maxStageLookahead, maxStageLookahead + 1);
        clipperDelay.prepare (maxStageLookahead);
        referenceDelay.prepare (maxStageLookahead);

        guard.prepare (sampleRate);
        guardLatency = guard.getLatency();
        preGuardRef.prepare (guardLatency);
        preGuardRef.setDelay (guardLatency);

        int maxTotal = 0;
        for (int f = 0; f < numOversamplingChoices; ++f)
            for (int t = 0; t < 2; ++t)
                maxTotal = std::max (maxTotal, latencyFor (f, (FilterType) t, true));
        dryRaw.prepare (maxTotal + maxBlock);
        dryGained.prepare (maxTotal + maxBlock);

        upInput.setSize (4, maxBlock);
        upInput.clear();
        downOutput.setSize (4, maxBlock);
        downOutput.clear();

        controls.resize ((size_t) maxBlock);
        for (auto* v : { &inGain, &outGain, &guardCeiling, &guardAmt, &deltaAmt, &bypassAmt })
            v->assign ((size_t) maxBlock, 0.0f);

        for (auto* v : { &inputGainDb, &outputGainDb, &ceilingDb, &kneeDb })
            v->reset (sampleRate, 0.02);
        for (auto* v : { &stereoAmount, &midAmount, &sideAmount, &lrLinkAmount, &guardAmount, &deltaAmount, &bypassAmount })
            v->reset (sampleRate, 0.01);

        switchStep = (float) (1.0 / (0.005 * sampleRate));
        setTargets (settings, true);

        active = pending = configFrom (settings);
        applyConfig (active);
        switchFade = 1.0f;
        fadingOut = false;
    }

    void ClipEngine::reset()
    {
        applyConfig (active);
    }

    void ClipEngine::applyConfig (const Config& c) noexcept
    {
        os = &oversamplerFor (c);
        os->reset();
        factorShift = c.osIndex;
        factor = 1 << factorShift;

        const int pad = oversamplingPad (c);
        stageLookahead = c.lookahead ? lookaheadBase * factor : 0;
        // the pad makes the total latency a whole number of host samples
        clipper.configure (stageLookahead + pad, 0);
        clipperDelay.setDelay (stageLookahead + pad);
        clipperDelay.reset();
        referenceDelay.setDelay (stageLookahead + pad);
        referenceDelay.reset();
        currentWindow = -1;

        totalLatency = latencyFor (c.osIndex, c.filter, c.lookahead);
        dryRaw.reset();
        dryGained.reset();
        preGuardRef.reset();
        guard.reset();
        upInput.clear();
        downOutput.clear();
        latencySamples.store (totalLatency);
    }

    void ClipEngine::setTargets (const EngineSettings& s, bool immediately) noexcept
    {
        auto set = [immediately] (Linear& v, float target)
        {
            if (immediately) v.setCurrentAndTargetValue (target);
            else             v.setTargetValue (target);
        };

        set (inputGainDb, s.inputGainDb);
        set (outputGainDb, s.outputGainDb);
        set (ceilingDb, s.ceilingDb);
        set (kneeDb, s.kneeDb);
        set (stereoAmount, s.mode == Mode::Stereo ? 1.0f : 0.0f);
        set (midAmount,    s.mode == Mode::Mid    ? 1.0f : 0.0f);
        set (sideAmount,   s.mode == Mode::Side   ? 1.0f : 0.0f);
        set (lrLinkAmount, s.lrLink ? 1.0f : 0.0f);
        set (guardAmount, s.guard ? 1.0f : 0.0f);
        set (deltaAmount, s.delta ? 1.0f : 0.0f);
        set (bypassAmount, s.bypass ? 1.0f : 0.0f);
        shapeTarget = std::clamp (s.shape, 0.0f, 1.0f);
    }

    //==============================================================================
    void ClipEngine::process (float* left, float* right, int numSamples, const EngineSettings& settings) noexcept
    {
        pending = configFrom (settings);
        setTargets (settings, false);

        for (int start = 0; start < numSamples; start += maxBlock)
            processChunk (left + start, right + start, std::min (maxBlock, numSamples - start));
    }

    void ClipEngine::processChunk (float* left, float* right, int n) noexcept
    {
        // configuration changes (oversampling, filter, lookahead) fade out, switch, fade in
        fadingOut = pending != active;
        if (fadingOut && switchFade <= 0.0f)
        {
            active = pending;
            applyConfig (active);
            fadingOut = false;
        }

        // 1. control values at the host rate ------------------------------------------------
        SoftClipCurve curve;
        curve.set (ceilingDb.getCurrentValue(), kneeDb.getCurrentValue());
        float inG = dbToGain (inputGainDb.getCurrentValue()), outG = dbToGain (outputGainDb.getCurrentValue());

        for (int i = 0; i < n; ++i)
        {
            auto& cf = controls[(size_t) i];

            if (ceilingDb.isSmoothing() || kneeDb.isSmoothing())
                curve.set (ceilingDb.getNextValue(), kneeDb.getNextValue());
            if (inputGainDb.isSmoothing())  inG  = dbToGain (inputGainDb.getNextValue());
            if (outputGainDb.isSmoothing()) outG = dbToGain (outputGainDb.getNextValue());

            cf.curve = curve;
            cf.modes.stereo = stereoAmount.getNextValue();
            cf.modes.mid = midAmount.getNextValue();
            cf.modes.side = sideAmount.getNextValue();
            cf.modes.lrLink = lrLinkAmount.getNextValue();

            inGain[(size_t) i] = inG;
            outGain[(size_t) i] = outG;
            guardCeiling[(size_t) i] = curve.ceiling;
            guardAmt[(size_t) i] = guardAmount.getNextValue();
            deltaAmt[(size_t) i] = deltaAmount.getNextValue();
            bypassAmt[(size_t) i] = bypassAmount.getNextValue();
        }

        const int window = active.lookahead ? 1 + (int) std::lround (shapeTarget * (float) stageLookahead) : 1;
        if (window != currentWindow)
        {
            clipper.setWindow (window, window);
            currentWindow = window;
        }

        // 2. input gain, dry delays ------------------------------------------------------------
        const bool difference = usesDifferencePath (active);
        auto* inL = upInput.getWritePointer (0);
        auto* inR = upInput.getWritePointer (1);
        float inPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float raw[2] = { left[i], right[i] };
            dryRaw.push (raw);
            const float g = inGain[(size_t) i];
            inL[i] = raw[0] * g;
            inR[i] = raw[1] * g;
            const float gained[2] = { inL[i], inR[i] };
            dryGained.push (gained);
            inPeak = std::max (inPeak, std::max (std::abs (inL[i]), std::abs (inR[i])));
        }

        // 3. oversampled stages ------------------------------------------------------------------
        const size_t osChannels = difference ? 2 : 4;
        juce::dsp::AudioBlock<const float> inBlock (upInput.getArrayOfReadPointers(), osChannels, (size_t) n);
        auto osBlock = os->processSamplesUp (inBlock);
        float* o0 = osBlock.getChannelPointer (0);
        float* o1 = osBlock.getChannelPointer (1);
        float* o2 = difference ? nullptr : osBlock.getChannelPointer (2);
        float* o3 = difference ? nullptr : osBlock.getChannelPointer (3);

        AppliedGains minGain;
        auto trackMin = [] (AppliedGains& m, const AppliedGains& g)
        {
            m.left = std::min (m.left, g.left);
            m.right = std::min (m.right, g.right);
            m.mid = std::min (m.mid, g.mid);
            m.side = std::min (m.side, g.side);
        };
        const int osSamples = n << factorShift;

        for (int j = 0; j < osSamples; ++j)
        {
            const auto& cf = controls[(size_t) (j >> factorShift)];
            float frame[2] = { o0[j], o1[j] };
            float ref[2]   = { o0[j], o1[j] };
            referenceDelay.process (ref);

            const auto required = ModeClipper::fromCurve (frame, cf.curve);
            clipperDelay.process (frame);
            trackMin (minGain, clipper.process (frame, required, cf.modes));

            if (difference)
            {
                o0[j] = frame[0] - ref[0];
                o1[j] = frame[1] - ref[1];
            }
            else
            {
                o0[j] = frame[0];
                o1[j] = frame[1];
                o2[j] = ref[0];
                o3[j] = ref[1];
            }
        }

        juce::dsp::AudioBlock<float> outBlock (downOutput.getArrayOfWritePointers(), osChannels, (size_t) n);
        os->processSamplesDown (outBlock);

        // 4. host-rate output: guard, delta, output gain, bypass ---------------------------------
        const auto* d0 = downOutput.getReadPointer (0);
        const auto* d1 = downOutput.getReadPointer (1);
        const auto* d2 = downOutput.getReadPointer (2);
        const auto* d3 = downOutput.getReadPointer (3);
        const int preGuardTap = totalLatency - guardLatency;
        AppliedGains minGuard;
        float outPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const int age = n - 1 - i;   // pushes since sample i went into the dry delays
            float pre[2], wet[2], raw[2];

            if (difference)
            {
                dryGained.read (preGuardTap + age, pre);
                wet[0] = pre[0] + d0[i];
                wet[1] = pre[1] + d1[i];
            }
            else
            {
                pre[0] = d2[i];
                pre[1] = d3[i];
                wet[0] = d0[i];
                wet[1] = d1[i];
            }

            preGuardRef.process (pre);
            trackMin (minGuard, guard.process (wet, guardCeiling[(size_t) i], guardAmt[(size_t) i], controls[(size_t) i].modes));

            const float dA = deltaAmt[(size_t) i];
            float outL = dA > 0.0f ? mix (wet[0], pre[0] - wet[0], dA) : wet[0];
            float outR = dA > 0.0f ? mix (wet[1], pre[1] - wet[1], dA) : wet[1];

            const float og = outGain[(size_t) i];
            outL *= og;
            outR *= og;

            const float bA = bypassAmt[(size_t) i];
            if (bA > 0.0f)
            {
                dryRaw.read (totalLatency + age, raw);
                outL = mix (outL, raw[0], bA);
                outR = mix (outR, raw[1], bA);
            }

            switchFade = fadingOut ? std::max (0.0f, switchFade - switchStep) : std::min (1.0f, switchFade + switchStep);
            if (switchFade < 1.0f)
            {
                outL *= switchFade;
                outR *= switchFade;
            }

            left[i] = outL;
            right[i] = outR;
            outPeak = std::max (outPeak, std::max (std::abs (outL), std::abs (outR)));
        }

        MeterState::raise (meters.inputPeak, inPeak);
        MeterState::raise (meters.outputPeak, outPeak);
        MeterState::raise (meters.leftGrDb, -gainToDb (minGain.left));
        MeterState::raise (meters.rightGrDb, -gainToDb (minGain.right));
        MeterState::raise (meters.midGrDb, -gainToDb (minGain.mid));
        MeterState::raise (meters.sideGrDb, -gainToDb (minGain.side));
        MeterState::raise (meters.guardGrDb, -gainToDb (std::min ({ minGuard.left, minGuard.right, minGuard.mid, minGuard.side })));
    }
}
