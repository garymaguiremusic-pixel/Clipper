#pragma once

#include "GainSmoother.h"
#include "MidSideCodec.h"
#include "SoftClipCurve.h"
#include <algorithm>

namespace solidclip
{
    /** How much of each mode is engaged (crossfaded over ~10 ms when the mode changes). */
    struct ModeAmounts
    {
        float stereo = 1.0f;   // clip L and R
        float mid = 0.0f;      // clip only M = (L + R) / 2, S untouched
        float side = 0.0f;     // clip only S = (L - R) / 2, M untouched
        float lrLink = 1.0f;   // stereo: 1 = same gain on L and R, 0 = independent
    };

    /** Required gains (<= 1) for the four signals the modes can clip. */
    struct RequiredGains { float left = 1.0f, right = 1.0f, mid = 1.0f, side = 1.0f; };

    /** The gains actually applied (for the meters). */
    struct AppliedGains { float left = 1.0f, right = 1.0f, mid = 1.0f, side = 1.0f; };

    /**
        Turns per-sample required gains into smooth, never-overshooting gains (one
        GainSmoother per lane) and applies them to an L/R frame according to the mode:

          Stereo : L *= gL, R *= gR   (linked: both use min(rL, rR), so the L/R ratio is exact)
          Mid    : M *= gM, S untouched   ->  L += (gM - 1) M,  R += (gM - 1) M
          Side   : S *= gS, M untouched   ->  L += (gS - 1) S,  R -= (gS - 1) S

        Every lane always runs, so switching modes is a plain crossfade of the amounts with
        no state to rebuild. With a gain of exactly 1 the frame is not touched at all.
    */
    class ModeClipper
    {
    public:
        void prepare (int maxDelay, int maxHold)
        {
            for (auto& l : lanes)
                l.prepare (maxDelay, maxHold);
        }

        void configure (int delay, int offset) noexcept
        {
            for (auto& l : lanes)
                l.configure (delay, offset);
            reset();
        }

        void setWindow (int hold, int box) noexcept
        {
            for (auto& l : lanes)
                l.setWindow (hold, box);
        }

        void reset() noexcept
        {
            for (auto& l : lanes)
                l.reset();
        }

        /** Feeds the newest required gains; applies the resulting gains to `frame`, which the
            caller has already delayed so it lines up with the smoothers' output. */
        AppliedGains process (float* frame, const RequiredGains& r, const ModeAmounts& a) noexcept
        {
            const float gL  = lanes[0].process (r.left);
            const float gR  = lanes[1].process (r.right);
            const float gLR = lanes[2].process (std::min (r.left, r.right));
            const float gM  = lanes[3].process (r.mid);
            const float gS  = lanes[4].process (r.side);

            AppliedGains g;
            g.left  = fade (blend (gL, gLR, a.lrLink), a.stereo);
            g.right = fade (blend (gR, gLR, a.lrLink), a.stereo);
            g.mid   = fade (gM, a.mid);
            g.side  = fade (gS, a.side);

            const bool stereoOnly = a.mid <= 0.0f && a.side <= 0.0f;
            if (stereoOnly)
            {
                if (! exactlyEquals (g.left, 1.0f))  frame[0] *= g.left;
                if (! exactlyEquals (g.right, 1.0f)) frame[1] *= g.right;
                return g;
            }

            float m, s;
            MidSideCodec::encode (frame[0], frame[1], m, s);
            float dL = 0.0f, dR = 0.0f;
            if (! exactlyEquals (g.left, 1.0f))  dL += (g.left - 1.0f) * frame[0];
            if (! exactlyEquals (g.right, 1.0f)) dR += (g.right - 1.0f) * frame[1];
            if (! exactlyEquals (g.mid, 1.0f))
            {
                const float d = (g.mid - 1.0f) * m;
                dL += d;
                dR += d;
            }
            if (! exactlyEquals (g.side, 1.0f))
            {
                const float d = (g.side - 1.0f) * s;
                dL += d;
                dR -= d;
            }
            frame[0] += dL;
            frame[1] += dR;
            return g;
        }

        /** Required gains for one frame from a clip curve (|L|, |R|, |M|, |S| detectors). */
        static RequiredGains fromCurve (const float* frame, const SoftClipCurve& curve) noexcept
        {
            float m, s;
            MidSideCodec::encode (frame[0], frame[1], m, s);
            return { curve.gain (std::abs (frame[0])), curve.gain (std::abs (frame[1])),
                     curve.gain (std::abs (m)), curve.gain (std::abs (s)) };
        }

    private:
        static float fade (float gain, float amount) noexcept
        {
            return amount >= 1.0f ? gain : (amount <= 0.0f ? 1.0f : 1.0f + amount * (gain - 1.0f));
        }

        static float blend (float own, float linked, float link) noexcept
        {
            return link >= 1.0f ? linked : (link <= 0.0f ? own : own + link * (linked - own));
        }

        GainSmoother lanes[5];   // L, R, L/R linked, Mid, Side
    };
}
