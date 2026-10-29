#pragma once

#include "GainSmoother.h"
#include "SoftClipCurve.h"
#include <cmath>

namespace solidclip
{
    /**
        Stereo stage: one gain computed from max(|L|, |R|) and applied to both channels,
        so the L/R ratio of every sample is untouched (no added side-channel content).
    */
    class LinkedClipper
    {
    public:
        void prepare (int maxLookahead)
        {
            delayLine.prepare (maxLookahead);
            smoother.prepare (maxLookahead, maxLookahead + 1);
        }

        /** lookahead in samples; the smoothing window is set separately with setWindow(). */
        void configure (int lookahead) noexcept
        {
            delayLine.setDelay (lookahead);
            smoother.configure (lookahead, 0);
            reset();
        }

        void setWindow (int window) noexcept     { smoother.setWindow (window, window); }
        int getLatency() const noexcept          { return delayLine.getDelay(); }

        void reset() noexcept
        {
            delayLine.reset();
            smoother.reset();
        }

        /** Processes one frame in place. amount 0..1 crossfades between bypassed and clipped. */
        float process (float* frame, const SoftClipCurve& curve, float amount) noexcept
        {
            const float detector = std::max (std::abs (frame[0]), std::abs (frame[1]));
            const float smoothedGain = smoother.process (curve.gain (detector));
            delayLine.process (frame);

            const float g = amount >= 1.0f ? smoothedGain
                          : (amount <= 0.0f ? 1.0f : 1.0f + amount * (smoothedGain - 1.0f));
            if (! exactlyEquals (g, 1.0f))
            {
                frame[0] *= g;
                frame[1] *= g;
            }
            return g;
        }

    private:
        Lookahead<2> delayLine;
        GainSmoother smoother;
    };
}
