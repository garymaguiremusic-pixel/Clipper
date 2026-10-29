#pragma once

#include "GainSmoother.h"
#include "Lookahead.h"
#include "SoftClipCurve.h"
#include "ModeClipper.h"
#include <array>
#include <cmath>

namespace solidclip
{
    /**
        4x true-peak detector using the ITU-R BS.1770-4 (Annex 2) 48-tap interpolation
        filter, the reference most true-peak meters follow. For one sample it returns,
        per lane, max(|x|) over the sample itself and the four interpolated points between
        it and the next sample (at +1/8, +3/8, +5/8, +7/8).
    */
    template <int NumLanes>
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

        /** Pushes one value per lane; writes the peak estimates for the values pushed
            `delay` frames ago. */
        void process (const float* in, float* peaks) noexcept
        {
            constexpr int n = tapsPerPhase;
            for (int ch = 0; ch < NumLanes; ++ch)
                history[(size_t) ch][(size_t) pos] = history[(size_t) ch][(size_t) pos + n] = in[ch];
            pos = (pos + 1) % n;

            // history[pos .. pos + n - 1] runs oldest -> newest
            for (int ch = 0; ch < NumLanes; ++ch)
            {
                const float* x = history[(size_t) ch].data() + pos;
                float peak = std::abs (x[n - 1 - delay]);
                for (const auto& c : coeffs)
                {
                    double acc = 0.0;
                    for (int j = 0; j < n; ++j)
                        acc += c[j] * (double) x[n - 1 - j];
                    peak = std::max (peak, (float) std::abs (acc));
                }
                peaks[ch] = peak;
            }
        }

    private:
        std::array<std::array<float, 2 * tapsPerPhase>, (size_t) NumLanes> history {};
        int pos = 0;
    };

    /**
        Final safety stage at the host rate. Oversampled clipping is followed by the
        down-sampling filter, which removes the clipped harmonics above Nyquist and can
        leave small overshoots. This stage looks ahead at the true peaks of the
        down-sampled signal and applies a short, smooth gain dip only where they would
        exceed the ceiling. It works on the same signals as the mode (L/R, Mid or Side), so
        in Mid mode it never touches the sides and vice versa. Where nothing exceeds the
        ceiling, the gain is exactly 1.0.
    */
    class TruePeakGuard
    {
    public:
        void prepare (double sampleRate)
        {
            box = std::max (1, (int) std::lround (sampleRate * 0.0007));
            const int k = Detector::span;
            smootherDelay = k + box - 1;
            smoother.prepare (smootherDelay, box + 2 * k);
            smoother.configure (smootherDelay, k);
            smoother.setWindow (box + 2 * k, box);
            latency = Detector::delay + smootherDelay;
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

        /** In-place on one frame. amount fades the guard in and out. Returns the gains applied. */
        AppliedGains process (float* frame, float ceiling, float amount, const ModeAmounts& modes) noexcept
        {
            float lanes[4], peaks[4];
            lanes[0] = frame[0];
            lanes[1] = frame[1];
            MidSideCodec::encode (frame[0], frame[1], lanes[2], lanes[3]);
            detector.process (lanes, peaks);

            auto required = [ceiling, amount] (float peak)
            {
                const float r = peak > ceiling ? ceiling / peak : 1.0f;
                return amount >= 1.0f ? r : (amount <= 0.0f ? 1.0f : 1.0f + amount * (r - 1.0f));
            };
            const RequiredGains r { required (peaks[0]), required (peaks[1]), required (peaks[2]), required (peaks[3]) };

            delayLine.process (frame);
            return smoother.process (frame, r, modes);
        }

    private:
        using Detector = TruePeakDetector<4>;
        Detector detector;
        ModeClipper smoother;
        Lookahead<2> delayLine;
        int box = 1, smootherDelay = 0, latency = 0;
    };
}
