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
        return osLatency + 2 * (lookahead ? lookaheadBase : 0) + guardLatency;
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
        stereoStage.prepare (maxStageLookahead);
        msStage.prepare (maxStageLookahead);
        referenceDelay.prepare (2 * maxStageLookahead);

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

        for (auto* v : { &inputGainDb, &outputGainDb, &ceilingDb, &kneeDb, &midCeilingDb, &midKneeDb, &sideCeilingDb, &sideKneeDb, &link })
            v->reset (sampleRate, 0.02);
        for (auto* v : { &stereoAmount, &msAmount, &midAmount, &sideAmount, &guardAmount, &deltaAmount, &bypassAmount })
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
        stereoStage.configure (stageLookahead);
        msStage.configure (stageLookahead + pad);
        referenceDelay.setDelay (2 * stageLookahead + pad);
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
        set (midCeilingDb, s.midCeilingDb);
        set (midKneeDb, s.midKneeDb);
        set (sideCeilingDb, s.sideCeilingDb);
        set (sideKneeDb, s.sideKneeDb);
        set (link, std::clamp (s.link, 0.0f, 1.0f));

        const float stereoOn = s.mode == Mode::Stereo ? 1.0f : (s.mode == Mode::MidSide ? 0.0f : (s.stereoStageOn ? 1.0f : 0.0f));
        const float msOn     = s.mode == Mode::Stereo ? 0.0f : (s.mode == Mode::MidSide ? 1.0f : (s.msStageOn ? 1.0f : 0.0f));
        set (stereoAmount, stereoOn);
        set (msAmount, msOn);
        set (midAmount, s.midOn ? 1.0f : 0.0f);
        set (sideAmount, s.sideOn ? 1.0f : 0.0f);
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
        SoftClipCurve stereoCurve, midCurve, sideCurve;
        stereoCurve.set (ceilingDb.getCurrentValue(), kneeDb.getCurrentValue());
        midCurve.set (midCeilingDb.getCurrentValue(), midKneeDb.getCurrentValue());
        sideCurve.set (sideCeilingDb.getCurrentValue(), sideKneeDb.getCurrentValue());
        float inG = dbToGain (inputGainDb.getCurrentValue()), outG = dbToGain (outputGainDb.getCurrentValue());

        for (int i = 0; i < n; ++i)
        {
            auto& cf = controls[(size_t) i];

            if (ceilingDb.isSmoothing() || kneeDb.isSmoothing())
                stereoCurve.set (ceilingDb.getNextValue(), kneeDb.getNextValue());
            if (midCeilingDb.isSmoothing() || midKneeDb.isSmoothing())
                midCurve.set (midCeilingDb.getNextValue(), midKneeDb.getNextValue());
            if (sideCeilingDb.isSmoothing() || sideKneeDb.isSmoothing())
                sideCurve.set (sideCeilingDb.getNextValue(), sideKneeDb.getNextValue());
            if (inputGainDb.isSmoothing())  inG  = dbToGain (inputGainDb.getNextValue());
            if (outputGainDb.isSmoothing()) outG = dbToGain (outputGainDb.getNextValue());

            const float ms = msAmount.getNextValue();
            cf.stereo = stereoCurve;
            cf.mid = midCurve;
            cf.side = sideCurve;
            cf.link = link.getNextValue();
            cf.stereoAmt = stereoAmount.getNextValue();
            cf.midAmt = ms * midAmount.getNextValue();
            cf.sideAmt = ms * sideAmount.getNextValue();

            inGain[(size_t) i] = inG;
            outGain[(size_t) i] = outG;
            guardCeiling[(size_t) i] = stereoCurve.ceiling;
            guardAmt[(size_t) i] = guardAmount.getNextValue();
            deltaAmt[(size_t) i] = deltaAmount.getNextValue();
            bypassAmt[(size_t) i] = bypassAmount.getNextValue();
        }

        const int window = active.lookahead ? 1 + (int) std::lround (shapeTarget * (float) stageLookahead) : 1;
        if (window != currentWindow)
        {
            stereoStage.setWindow (window);
            msStage.setWindow (window);
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

        float minStereo = 1.0f, minMid = 1.0f, minSide = 1.0f;
        const int osSamples = n << factorShift;
        MidSideClipper::Settings msSettings;

        for (int j = 0; j < osSamples; ++j)
        {
            const auto& cf = controls[(size_t) (j >> factorShift)];
            float frame[2] = { o0[j], o1[j] };
            float ref[2]   = { o0[j], o1[j] };
            referenceDelay.process (ref);

            minStereo = std::min (minStereo, stereoStage.process (frame, cf.stereo, cf.stereoAmt));

            msSettings.mid = &cf.mid;
            msSettings.side = &cf.side;
            msSettings.midAmount = cf.midAmt;
            msSettings.sideAmount = cf.sideAmt;
            msSettings.link = cf.link;
            const auto g = msStage.process (frame, msSettings);
            minMid = std::min (minMid, g.mid);
            minSide = std::min (minSide, g.side);

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
        float minGuard = 1.0f, outPeak = 0.0f;

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
            minGuard = std::min (minGuard, guard.process (wet, guardCeiling[(size_t) i], guardAmt[(size_t) i]));

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
        MeterState::raise (meters.stereoGrDb, -gainToDb (minStereo));
        MeterState::raise (meters.midGrDb, -gainToDb (minMid));
        MeterState::raise (meters.sideGrDb, -gainToDb (minSide));
        MeterState::raise (meters.guardGrDb, -gainToDb (minGuard));
    }
}
