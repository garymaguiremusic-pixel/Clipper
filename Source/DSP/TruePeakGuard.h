#pragma once

#include "GainSmoother.h"
#include "Lookahead.h"
#include "SoftClipCurve.h"
#include <array>
#include <cmath>

namespace solidclip
{
    /**
        4x true-peak detector using the ITU-R BS.1770-4 (Annex 2) 48-tap interpolation
        filter, the reference most true-peak meters follow. For one sample it returns
        max(|x|) over the sample itself and the four interpolated points between it and
        the next sample (at +1/8, +3/8, +5/8, +7/8), on both channels.
    */
    class TruePeakDetector
    {
    public:
        static constexpr int tapsPerPhase = 12;
        static constexpr int delay = 6;   // the reported sample is 6 frames old
        static constexpr int span = 6;    // samples on either side that influence it

        // h[4j + phase], j = 0..11 multiplies x[newest - j]
        static constexpr double coeffs[4][tapsPerPhase] =
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

        TruePeakDetector()  { reset(); }

        void reset() noexcept
        {
            for (auto& ch : history)
                ch.fill (0.0f);
            pos = 0;
        }

        /** Pushes a frame; returns the peak estimate for the frame pushed `delay` frames ago. */
        float process (float l, float r) noexcept
        {
            constexpr int n = tapsPerPhase;
            history[0][(size_t) pos] = history[0][(size_t) pos + n] = l;
            history[1][(size_t) pos] = history[1][(size_t) pos + n] = r;
            pos = (pos + 1) % n;

            // history[pos .. pos + n - 1] runs oldest -> newest
            float peak = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* x = history[(size_t) ch].data() + pos;
                peak = std::max (peak, std::abs (x[n - 1 - delay]));
                for (const auto& c : coeffs)
                {
                    double acc = 0.0;
                    for (int j = 0; j < n; ++j)
                        acc += c[j] * (double) x[n - 1 - j];
                    peak = std::max (peak, (float) std::abs (acc));
                }
            }
            return peak;
        }

    private:
        std::array<std::array<float, 2 * tapsPerPhase>, 2> history {};
        int pos = 0;
    };

    /**
        Final safety stage at the host rate. Oversampled clipping is followed by the
        down-sampling filter, which removes the clipped harmonics above Nyquist and can
        leave small overshoots. This stage looks ahead at the true peaks of the
        down-sampled signal and applies a short, smooth, linked gain dip only where they
        would exceed the ceiling. Where nothing exceeds it, the gain is exactly 1.0.
    */
    class TruePeakGuard
    {
    public:
        void prepare (double sampleRate)
        {
            box = std::max (1, (int) std::lround (sampleRate * 0.0007));
            const int k = TruePeakDetector::span;
            smootherDelay = k + box - 1;
            smoother.prepare (smootherDelay, box + 2 * k);
            smoother.configure (smootherDelay, k);
            smoother.setWindow (box + 2 * k, box);
            latency = TruePeakDetector::delay + smootherDelay;
            delayLine.prepare (latency);
            delayLine.setDelay (latency);
            reset();
        }

        int getLatency() const noexcept { return latency; }

        void reset() noexcept
        {
            detector.reset();
            smoother.reset();
            delayLine.reset();
        }

        /** In-place on one frame. amount fades the guard in and out. Returns the gain applied. */
        float process (float* frame, float ceiling, float amount) noexcept
        {
            const float peak = detector.process (frame[0], frame[1]);
            float required = peak > ceiling ? ceiling / peak : 1.0f;
            if (amount < 1.0f)
                required = amount <= 0.0f ? 1.0f : 1.0f + amount * (required - 1.0f);

            const float g = smoother.process (required);
            delayLine.process (frame);
            if (! exactlyEquals (g, 1.0f))
            {
                frame[0] *= g;
                frame[1] *= g;
            }
            return g;
        }

    private:
        TruePeakDetector detector;
        GainSmoother smoother;
        Lookahead<2> delayLine;
        int box = 1, smootherDelay = 0, latency = 0;
    };
}
