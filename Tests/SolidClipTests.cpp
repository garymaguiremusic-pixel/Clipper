// SolidClip test runner: builds the real processor, feeds test signals and checks the
// guarantees the plugin makes. Exit code 0 = all tests passed.

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"   // the tests compare samples bit-exactly on purpose
#endif

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/GainSmoother.h"
#include "DSP/LinkedClipper.h"
#include "DSP/MidSideClipper.h"

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace
{
    namespace ids = sc::ParamIDs;
    using Signal = std::vector<float>;

    int failures = 0, checks = 0;

    void check (bool ok, const std::string& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("  FAIL: %s\n", what.c_str());
        }
    }

    std::string fmt (const char* f, ...)
    {
        char buf[512];
        va_list args;
        va_start (args, f);
        std::vsnprintf (buf, sizeof (buf), f, args);
        va_end (args);
        return buf;
    }

    //==============================================================================
    // Test rig around the real processor
    struct Rig
    {
        SolidClipAudioProcessor proc;
        double sampleRate = 44100.0;
        int blockSize = 512;

        // the guarantees are about the clipper itself, so the In/Out link is off by default
        Rig() { set (ids::gainLink, 0.0f); }

        void set (const char* id, float value)
        {
            auto* p = proc.apvts.getParameter (id);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        void prepare()
        {
            proc.setPlayConfigDetails (2, 2, sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);
        }

        int latency() const { return proc.getLatencySamples(); }

        /** Processes in irregular block sizes (variable host buffers). */
        void run (Signal& l, Signal& r)
        {
            static const int sizes[] = { 512, 37, 256, 1, 129, 300, 64, 511 };
            juce::MidiBuffer midi;
            size_t pos = 0;
            int k = 0;
            while (pos < l.size())
            {
                const int n = (int) std::min<size_t> ((size_t) std::min (sizes[k++ % 8], blockSize), l.size() - pos);
                float* chans[] = { l.data() + pos, r.data() + pos };
                juce::AudioBuffer<float> buffer (chans, 2, n);
                proc.processBlock (buffer, midi);
                pos += (size_t) n;
            }
        }
    };

    //==============================================================================
    // Signals
    Signal sweep (double fs, double seconds, float amp, double f0 = 20.0, double f1 = 20000.0)
    {
        Signal s ((size_t) (fs * seconds));
        const double k = std::log (f1 / f0) / seconds;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const double t = (double) i / fs;
            s[i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (std::exp (k * t) - 1.0) / k);
        }
        return s;
    }

    Signal noise (double fs, double seconds, float amp, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        Signal s ((size_t) (fs * seconds));
        for (auto& x : s) x = amp * dist (rng);
        return s;
    }

    Signal drums (double fs, double seconds, float amp, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
        Signal s ((size_t) (fs * seconds), 0.0f);
        const size_t period = (size_t) (fs * 0.25);
        for (size_t start = 0; start < s.size(); start += period)
        {
            const bool snare = ((start / period) % 2) == 1;
            for (size_t i = 0; i < (size_t) (fs * 0.2) && start + i < s.size(); ++i)
            {
                const double t = (double) i / fs;
                const double kick = std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * t + 120.0 * (1.0 - std::exp (-t * 30.0)) / 30.0)) * std::exp (-t * 18.0);
                const double click = dist (rng) * std::exp (-t * (snare ? 25.0 : 400.0));
                s[start + i] += amp * (float) (snare ? 0.5 * kick + 0.8 * click : 0.9 * kick + 0.3 * click);
            }
        }
        float peak = 0.0f;
        for (auto x : s) peak = std::max (peak, std::abs (x));
        for (auto& x : s) x *= amp / peak;
        return s;
    }

    float peakOf (const Signal& s, size_t from = 0)
    {
        float p = 0.0f;
        for (size_t i = from; i < s.size(); ++i) p = std::max (p, std::abs (s[i]));
        return p;
    }

    /** 4x true-peak meter per ITU-R BS.1770-4 (48-tap polyphase interpolator) plus sample peak. */
    float truePeakOf (const Signal& s, size_t from = 0)
    {
        static const double h[4][12] =
        {
            {  0.0017089843750,  0.0109863281250, -0.0196533203125,  0.0332031250000, -0.0594482421875,  0.1373291015625,
               0.9721679687500, -0.1022949218750,  0.0476074218750, -0.0266113281250,  0.0148925781250, -0.0083007812500 },
            { -0.0291748046875,  0.0292968750000, -0.0517578125000,  0.0891113281250, -0.1665039062500,  0.4650878906250,
               0.7797851562500, -0.2003173828125,  0.1015625000000, -0.0582275390625,  0.0330810546875, -0.0189208984375 },
            { -0.0189208984375,  0.0330810546875, -0.0582275390625,  0.1015625000000, -0.2003173828125,  0.7797851562500,
               0.4650878906250, -0.1665039062500,  0.0891113281250, -0.0517578125000,  0.0292968750000, -0.0291748046875 },
            { -0.0083007812500,  0.0148925781250, -0.0266113281250,  0.0476074218750, -0.1022949218750,  0.9721679687500,
               0.1373291015625, -0.0594482421875,  0.0332031250000, -0.0196533203125,  0.0109863281250,  0.0017089843750 },
        };

        float peak = peakOf (s, from);
        for (size_t n = std::max (from, (size_t) 11); n < s.size(); ++n)
            for (auto& phase : h)
            {
                double acc = 0;
                for (int j = 0; j < 12; ++j)
                    acc += phase[j] * s[n - (size_t) j];
                peak = std::max (peak, (float) std::abs (acc));
            }
        return peak;
    }

    const char* osName (int i)      { static const char* n[] = { "1x", "2x", "4x", "8x", "16x" }; return n[i]; }
    const char* filterName (int f)  { return f == 0 ? "IIR" : "FIR"; }
    const char* modeName (int m)    { static const char* n[] = { "Stereo", "Mid/Side", "Stereo+M/S" }; return n[m]; }

    //==============================================================================
    void testGainSmootherNeverOvershoots()
    {
        std::printf ("GainSmoother: smoothed gain never exceeds the required gain\n");
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> dist (0.0f, 1.0f);

        for (int delay : { 0, 5, 64, 300 })
        {
            solidclip::GainSmoother sm;
            sm.prepare (delay, delay + 1);
            sm.configure (delay, 0);
            sm.reset();

            std::vector<float> req;
            int window = 1, violations = 0;
            for (int n = 0; n < 40000; ++n)
            {
                if (n % 3000 == 0)
                {
                    window = 1 + (int) (dist (rng) * (float) delay);
                    sm.setWindow (window, window);
                }
                const float r = dist (rng) < 0.05f ? 0.2f + 0.8f * dist (rng) : 1.0f;
                req.push_back (r);
                const float g = sm.process (r);
                const int m = n - delay;
                if (m >= 0 && g > req[(size_t) m] + 1.0e-7f) ++violations;
                if (m >= 0 && g > 1.0f) ++violations;
            }
            check (violations == 0, fmt ("delay %d: %d samples above the required gain", delay, violations));
        }

        // exactly 1.0 when nothing is reduced
        solidclip::GainSmoother sm;
        sm.prepare (100, 101);
        sm.configure (100, 0);
        sm.reset();
        sm.setWindow (101, 101);
        bool exact = true;
        for (int n = 0; n < 2000; ++n)
        {
            const float g = sm.process (n == 500 ? 0.5f : 1.0f);
            if (n > 1000 && g != 1.0f) exact = false;
        }
        check (exact, "gain returns to exactly 1.0 after a reduction");
    }

    void testStageRatios()
    {
        std::printf ("Stages (at the processing rate): linked gain keeps L/R and S/M ratios\n");
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> dist (-2.0f, 2.0f);

        solidclip::SoftClipCurve curve;
        curve.set (-1.0f, 3.0f);

        solidclip::LinkedClipper st;
        st.prepare (64);
        st.configure (32);
        st.setWindow (17);

        solidclip::MidSideClipper ms;
        ms.prepare (64);
        ms.configure (32);
        ms.setWindow (17);
        solidclip::SoftClipCurve sideCurve;
        sideCurve.set (-6.0f, 2.0f);
        solidclip::MidSideClipper::Settings set { &curve, &sideCurve, 1.0f, 1.0f, 1.0f };

        std::vector<std::array<float, 2>> in;
        double worstLR = 0, worstSM = 0;
        int clipped = 0;
        for (int n = 0; n < 20000; ++n)
        {
            std::array<float, 2> x { dist (rng), dist (rng) };
            in.push_back (x);
            float a[2] = { x[0], x[1] }, b[2] = { x[0], x[1] };
            if (st.process (a, curve, 1.0f) < 1.0f) ++clipped;
            ms.process (b, set);
            if (n >= 32)
            {
                const auto& d = in[(size_t) n - 32];
                if (std::abs (d[1]) > 1e-2f)
                    worstLR = std::max (worstLR, std::abs ((double) a[0] / a[1] - (double) d[0] / d[1]) / std::abs ((double) d[0] / d[1]));
                const double m0 = (d[0] + d[1]) * 0.5, s0 = (d[0] - d[1]) * 0.5;
                const double m1 = (b[0] + b[1]) * 0.5, s1 = (b[0] - b[1]) * 0.5;
                if (std::abs (m0) > 1e-2 && std::abs (s0) > 1e-2)
                    worstSM = std::max (worstSM, std::abs (s1 / m1 - s0 / m0) / std::abs (s0 / m0));
                if (std::max (std::abs (a[0]), std::abs (a[1])) > curve.ceiling * 1.000001f)
                    check (false, "stereo stage output above its ceiling");
            }
        }
        check (clipped > 1000, "stereo stage actually clipped the test signal");
        check (worstLR < 1e-5, fmt ("stereo stage L/R ratio error %.3g", worstLR));
        check (worstSM < 1e-4, fmt ("M/S stage (link 100%%) S/M ratio error %.3g", worstSM));
    }

    //==============================================================================
    void testTransparency()
    {
        std::printf ("Transparency: below (ceiling - knee) the output nulls bit-exactly against the dry signal\n");
        const double fs = 44100.0;
        Signal srcL = noise (fs, 1.0, 0.12f, 1), srcR = sweep (fs, 1.0, 0.15f);
        for (size_t i = 0; i < srcL.size(); ++i) srcL[i] += 0.1f * srcR[i];

        struct Case { int os, filter; };
        const Case cases[] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 } };

        for (int mode = 0; mode < 3; ++mode)
            for (float shape : { 0.0f, 50.0f })
                for (auto c : cases)
                {
                    Rig rig;
                    rig.set (ids::mode, (float) mode);
                    rig.set (ids::oversampling, (float) c.os);
                    rig.set (ids::filterType, (float) c.filter);
                    rig.set (ids::shape, shape);
                    rig.prepare();
                    const int lat = rig.latency();

                    Signal l = srcL, r = srcR;
                    rig.run (l, r);

                    size_t mismatches = 0;
                    for (size_t i = 0; i + (size_t) lat < l.size(); ++i)
                        if (l[i + (size_t) lat] != srcL[i] || r[i + (size_t) lat] != srcR[i])
                            ++mismatches;
                    check (mismatches == 0, fmt ("%s %s %s shape %.0f: %zu samples differ from the dry signal",
                                                 modeName (mode), osName (c.os), filterName (c.filter), shape, mismatches));
                }

        std::printf ("  info: Min phase IIR at 2x-16x shifts phase by design, so it is not part of the bit-exact null test\n");
    }

    //==============================================================================
    void testCeiling()
    {
        std::printf ("Ceiling: sample peak and 4x true peak never exceed the ceiling (+12 dB hot input)\n");
        const double fs = 44100.0;
        const float ceilingDb = -0.3f;
        const float ceiling = solidclip::dbToGain (ceilingDb);

        struct Sig { const char* name; Signal l, r; };
        std::vector<Sig> sigs;
        sigs.push_back ({ "sweep", sweep (fs, 2.0, ceiling), sweep (fs, 2.0, ceiling * 0.7f, 30.0, 18000.0) });
        sigs.push_back ({ "noise", noise (fs, 1.0, ceiling, 11), noise (fs, 1.0, ceiling, 12) });
        sigs.push_back ({ "drums", drums (fs, 2.0, ceiling, 21), drums (fs, 2.0, ceiling * 0.8f, 22) });

        float worstTp = -100.0f;
        for (int mode : { 0, 1, 2 })
            for (int os = 0; os < 5; ++os)
                for (int filter = 0; filter < 2; ++filter)
                    for (float shape : { 0.0f, 50.0f })
                    {
                        if (os == 0 && filter == 1) continue;
                        if (mode != 0 && shape > 0.0f) continue;

                        for (auto& s : sigs)
                        {
                            Rig rig;
                            rig.set (ids::mode, (float) mode);
                            rig.set (ids::oversampling, (float) os);
                            rig.set (ids::filterType, (float) filter);
                            rig.set (ids::shape, shape);
                            rig.set (ids::ceiling, ceilingDb);
                            rig.set (ids::inputGain, 12.0f);
                            rig.prepare();

                            Signal l = s.l, r = s.r;
                            rig.run (l, r);

                            const float sp = std::max (peakOf (l), peakOf (r));
                            const float tp = std::max (truePeakOf (l), truePeakOf (r));
                            const float tpOver = solidclip::gainToDb (tp) - ceilingDb;
                            worstTp = std::max (worstTp, tpOver);

                            check (sp <= ceiling * 1.00001f,
                                   fmt ("%s %s %s shape %.0f %s: sample peak %.4f dB over",
                                        modeName (mode), osName (os), filterName (filter), shape, s.name, solidclip::gainToDb (sp) - ceilingDb));
                            check (tpOver <= 0.01f,
                                   fmt ("%s %s %s shape %.0f %s: true peak %.4f dB over",
                                        modeName (mode), osName (os), filterName (filter), shape, s.name, tpOver));
                        }
                    }
        std::printf ("  info: worst true peak relative to the ceiling: %+.3f dB\n", worstTp);
    }

    //==============================================================================
    void testStereoLinking()
    {
        std::printf ("Stereo linking: L = R gives zero side; L/R ratio is never changed\n");
        const double fs = 44100.0;
        Signal mono = drums (fs, 1.0, 1.0f, 5);
        Signal sw = sweep (fs, 1.0, 1.0f);
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.6f * mono[i] + 0.5f * sw[i];

        for (int os = 0; os < 5; ++os)
            for (int filter = 0; filter < 2; ++filter)
            {
                Rig rig;
                rig.set (ids::oversampling, (float) os);
                rig.set (ids::filterType, (float) filter);
                rig.set (ids::inputGain, 12.0f);
                rig.prepare();
                Signal l = mono, r = mono;
                rig.run (l, r);
                float side = 0.0f;
                for (size_t i = 0; i < l.size(); ++i) side = std::max (side, std::abs (l[i] - r[i]));
                check (side == 0.0f, fmt ("%s %s: side output %.3g for L = R", osName (os), filterName (filter), side));
            }

        // ratio, per sample, with the host-rate path (1x: gain is applied per sample)
        Signal inL = drums (fs, 1.0, 1.0f, 8), inR = noise (fs, 1.0, 0.8f, 9);
        for (size_t i = 0; i < inL.size(); ++i) inR[i] = 0.5f * inR[i] + 0.4f * inL[i];

        for (float shape : { 0.0f, 50.0f })
        {
            Rig rig;
            rig.set (ids::oversampling, 0.0f);
            rig.set (ids::inputGain, 12.0f);
            rig.set (ids::shape, shape);
            rig.prepare();
            const auto lat = (size_t) rig.latency();
            Signal l = inL, r = inR;
            rig.run (l, r);

            double worst = 0.0;
            int reduced = 0;
            for (size_t i = 0; i + lat < l.size(); ++i)
            {
                if (std::abs (inR[i]) < 1e-3f || std::abs (inL[i]) < 1e-3f) continue;
                const double ratioIn = (double) inL[i] / inR[i];
                const double ratioOut = (double) l[i + lat] / r[i + lat];
                worst = std::max (worst, std::abs (ratioOut - ratioIn) / std::abs (ratioIn));
                if (std::abs (l[i + lat]) < std::abs (inL[i]) * 3.9f) ++reduced;
            }
            check (reduced > 1000, "the signal was actually clipped");
            check (worst < 1e-5, fmt ("1x shape %.0f: L/R ratio error %.3g", shape, worst));
        }
    }

    void testMidSideLink()
    {
        std::printf ("Mid/Side, Link 100%%: the S/M ratio is preserved\n");
        const double fs = 44100.0;
        Signal m = drums (fs, 1.0, 0.9f, 31), s = noise (fs, 1.0, 0.4f, 32);
        Signal inL (m.size()), inR (m.size());
        for (size_t i = 0; i < m.size(); ++i) { s[i] = 0.5f * s[i] + 0.3f * m[i]; inL[i] = m[i] + s[i]; inR[i] = m[i] - s[i]; }

        for (float shape : { 0.0f, 50.0f })
        {
            Rig rig;
            rig.set (ids::mode, 1.0f);
            rig.set (ids::oversampling, 0.0f);
            rig.set (ids::inputGain, 9.0f);
            rig.set (ids::msLink, 100.0f);
            rig.set (ids::shape, shape);
            rig.prepare();
            const auto lat = (size_t) rig.latency();
            Signal l = inL, r = inR;
            rig.run (l, r);

            double worst = 0.0;
            for (size_t i = 0; i + lat < l.size(); ++i)
            {
                const double m0 = ((double) inL[i] + inR[i]) * 0.5, s0 = ((double) inL[i] - inR[i]) * 0.5;
                const double m1 = ((double) l[i + lat] + r[i + lat]) * 0.5, s1 = ((double) l[i + lat] - r[i + lat]) * 0.5;
                if (std::abs (m0) < 1e-2 || std::abs (s0) < 1e-2) continue;
                worst = std::max (worst, std::abs (s1 / m1 - s0 / m0) / std::abs (s0 / m0));
            }
            check (worst < 1e-4, fmt ("shape %.0f: S/M ratio error %.3g", shape, worst));
        }
    }

    //==============================================================================
    void testLatency()
    {
        std::printf ("Latency: reported latency matches the measured latency (44.1 and 96 kHz)\n");

        for (double fs : { 44100.0, 96000.0 })
        for (int os = 0; os < 5; ++os)
            for (int filter = 0; filter < 2; ++filter)
                for (float shape : { 0.0f, 40.0f })
                {
                    Rig rig;
                    rig.sampleRate = fs;
                    rig.set (ids::oversampling, (float) os);
                    rig.set (ids::filterType, (float) filter);
                    rig.set (ids::shape, shape);
                    rig.prepare();
                    const int reported = rig.latency();
                    const bool exactPath = os == 0 || filter == 1;

                    int measured = -1;
                    if (exactPath)
                    {
                        // impulse: must come out unchanged at exactly the reported latency
                        Signal l (8192, 0.0f), r (8192, 0.0f);
                        l[1000] = 0.25f;
                        r[1000] = -0.125f;
                        rig.run (l, r);
                        size_t arg = 0;
                        for (size_t i = 0; i < l.size(); ++i) if (std::abs (l[i]) > std::abs (l[arg])) arg = i;
                        measured = (int) arg - 1000;
                        check (l[arg] == 0.25f && r[arg] == -0.125f, fmt ("%s %s shape %.0f: impulse altered", osName (os), filterName (filter), shape));
                    }
                    else
                    {
                        // IIR is not a pure delay: measure the delay at low frequencies by
                        // cross-correlating band-limited noise.
                        Signal src = noise (fs, 1.0, 0.2f, 77), lp (src.size());
                        double y1 = 0, y2 = 0;
                        for (size_t i = 0; i < src.size(); ++i) { y1 += 0.05 * (src[i] - y1); y2 += 0.05 * (y1 - y2); lp[i] = (float) y2 * 4.0f; }
                        Signal l = lp, r = lp;
                        rig.run (l, r);
                        double best = -1e30;
                        for (int lag = 0; lag < 1000; ++lag)
                        {
                            double acc = 0;
                            for (size_t i = 4000; i + (size_t) lag < lp.size(); ++i) acc += (double) lp[i] * l[i + (size_t) lag];
                            if (acc > best) { best = acc; measured = lag; }
                        }
                    }

                    check (exactPath ? measured == reported : std::abs (measured - reported) <= 1,
                           fmt ("%.0f Hz %s %s shape %.0f: reported %d, measured %d", fs, osName (os), filterName (filter), shape, reported, measured));
                }
    }

    //==============================================================================
    void testGainLink()
    {
        std::printf ("In/Out Link: the output undoes the input gain (no level change)\n");
        const double fs = 44100.0;
        Signal srcL = noise (fs, 0.5, 0.05f, 41), srcR = sweep (fs, 0.5, 0.05f);

        for (float drive : { 6.0f, 12.0f, -6.0f })
        {
            Rig rig;
            rig.set (ids::gainLink, 1.0f);
            rig.set (ids::inputGain, drive);
            rig.set (ids::oversampling, 0.0f);
            rig.prepare();
            const auto lat = (size_t) rig.latency();
            Signal l = srcL, r = srcR;
            rig.run (l, r);
            double worst = 0.0;
            for (size_t i = 0; i + lat < l.size(); ++i)
                worst = std::max (worst, (double) std::max (std::abs (l[i + lat] - srcL[i]), std::abs (r[i + lat] - srcR[i])));
            check (worst < 1e-6, fmt ("drive %+.0f dB below the knee: level differs by %.3g", drive, worst));
        }

        // driven into the clipper: the output peak follows ceiling - drive
        Rig rig;
        rig.set (ids::gainLink, 1.0f);
        rig.set (ids::inputGain, 12.0f);
        rig.set (ids::oversampling, 0.0f);
        rig.prepare();
        Signal l = drums (fs, 1.0, 1.0f, 5), r = l;
        rig.run (l, r);
        const float expected = solidclip::dbToGain (-0.3f - 12.0f);
        check (peakOf (l) <= expected * 1.0001f, fmt ("linked output peak %.2f dB, expected <= %.2f dB",
                                                     solidclip::gainToDb (peakOf (l)), -12.3));
    }

    void testModeSwitchIsSmooth()
    {
        std::printf ("Mode switching: no timing shift and no clicks\n");
        const double fs = 44100.0;
        Rig rig;
        rig.set (ids::inputGain, 6.0f);
        rig.set (ids::sideCeiling, -12.0f);
        rig.prepare();
        const int lat = rig.latency();

        Signal l = sweep (fs, 1.5, 0.9f, 100.0, 400.0), r = sweep (fs, 1.5, 0.5f, 150.0, 300.0);
        juce::MidiBuffer midi;
        float maxStep = 0.0f;
        size_t pos = 0;
        int block = 0;
        while (pos < l.size())
        {
            if (block % 40 == 10) rig.set (ids::mode, (float) ((block / 40) % 3));
            if (block % 40 == 30) rig.set (ids::mode, (float) ((block / 40 + 1) % 3));
            const int n = (int) std::min<size_t> (256, l.size() - pos);
            float* chans[] = { l.data() + pos, r.data() + pos };
            juce::AudioBuffer<float> buffer (chans, 2, n);
            rig.proc.processBlock (buffer, midi);
            check (rig.latency() == lat, "latency changed with the mode");
            pos += (size_t) n;
            ++block;
        }
        for (size_t i = (size_t) lat + 1; i < l.size(); ++i)
            maxStep = std::max (maxStep, std::max (std::abs (l[i] - l[i - 1]), std::abs (r[i] - r[i - 1])));

        // a 400 Hz sine at full scale moves at most 2*pi*400/44100 = 0.057 per sample
        check (maxStep < 0.08f, fmt ("largest sample step while switching modes: %.4f", maxStep));
    }

    //==============================================================================
    void reportPerformance()
    {
        std::printf ("Performance (info): real-time factor, 10 s of stereo audio at 44.1 kHz\n");
        const double fs = 44100.0;
        Signal srcL = drums (fs, 10.0, 1.0f, 1), srcR = noise (fs, 10.0, 0.5f, 2);
        for (int mode : { 0, 2 })
            for (int os : { 2, 4 })
                for (int filter : { 0, 1 })
                {
                    Rig rig;
                    rig.set (ids::mode, (float) mode);
                    rig.set (ids::oversampling, (float) os);
                    rig.set (ids::filterType, (float) filter);
                    rig.set (ids::inputGain, 6.0f);
                    rig.prepare();
                    Signal l = srcL, r = srcR;
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    rig.run (l, r);
                    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;
                    std::printf ("  %-11s %-3s %s: %6.1fx real time (latency %d samples)\n",
                                 modeName (mode), osName (os), filterName (filter), 10.0 / seconds, rig.latency());
                }
    }

    /** Renders the editor at a given width to a PNG (used to check the GUI and resizing). */
    int snapshot (const juce::File& file, int width, int mode)
    {
        SolidClipAudioProcessor proc;
        proc.setPlayConfigDetails (2, 2, 44100.0, 512);
        proc.prepareToPlay (44100.0, 512);

        // run some hot audio first so the meters show something
        Signal l = drums (44100.0, 0.5, 1.0f, 3), r = noise (44100.0, 0.5, 0.6f, 4);
        auto* in = proc.apvts.getParameter (ids::inputGain);
        in->setValueNotifyingHost (in->convertTo0to1 (6.0f));
        auto* modeParam = proc.apvts.getParameter (ids::mode);
        modeParam->setValueNotifyingHost (modeParam->convertTo0to1 ((float) mode));
        proc.prepareToPlay (44100.0, 512);
        juce::MidiBuffer midi;
        float* chans[] = { l.data(), r.data() };
        juce::AudioBuffer<float> buffer (chans, 2, 512);
        proc.processBlock (buffer, midi);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (width, juce::roundToInt (width * (double) SolidClipView::kHeight / SolidClipView::kWidth));

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, out) ? 0 : 1;
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), argc >= 4 ? std::atoi (argv[3]) : SolidClipView::kWidth,
                         argc >= 5 ? std::atoi (argv[4]) : 0);

    testGainSmootherNeverOvershoots();
    testStageRatios();
    testTransparency();
    testCeiling();
    testStereoLinking();
    testMidSideLink();
    testLatency();
    testGainLink();
    testModeSwitchIsSmooth();
    reportPerformance();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
