#pragma once

#include "GainSmoother.h"
#include "MidSideCodec.h"
#include "SoftClipCurve.h"
#include <cmath>

namespace solidclip
{
    /**
        Mid/Side stage. Each of Mid and Side has its own curve and enable amount.

          shared gain : min of both required gains (= max(|M|, |S|) when the curves match)
          own gains   : each channel on its own
          Link 0..1   : blends own -> shared. At 1 both channels get the same gain, so the
                        S/M ratio (and with it L/R) is preserved on every sample.

        A disabled channel (amount 0) passes untouched with gain exactly 1 and its required
        gain is faded out of the shared detector, so it no longer pulls the other one down.
    */
    class MidSideClipper
    {
    public:
        struct Settings
        {
            const SoftClipCurve* mid  = nullptr;
            const SoftClipCurve* side = nullptr;
            float midAmount = 1.0f, sideAmount = 1.0f;  // stage enable x channel enable
            float link = 1.0f;
        };

        struct Gains { float mid = 1.0f, side = 1.0f; };

        void prepare (int maxLookahead)
        {
            delayLine.prepare (maxLookahead);
            for (auto* s : { &shared, &midOwn, &sideOwn })
                s->prepare (maxLookahead, maxLookahead + 1);
        }

        void configure (int lookahead) noexcept
        {
            delayLine.setDelay (lookahead);
            for (auto* s : { &shared, &midOwn, &sideOwn })
                s->configure (lookahead, 0);
            reset();
        }

        void setWindow (int window) noexcept
        {
            for (auto* s : { &shared, &midOwn, &sideOwn })
                s->setWindow (window, window);
        }

        int getLatency() const noexcept     { return delayLine.getDelay(); }

        void reset() noexcept
        {
            delayLine.reset();
            for (auto* s : { &shared, &midOwn, &sideOwn })
                s->reset();
        }

        Gains process (float* frame, const Settings& st) noexcept
        {
            float m, s;
            MidSideCodec::encode (frame[0], frame[1], m, s);

            const float rMid  = st.mid->gain (std::abs (m));
            const float rSide = st.side->gain (std::abs (s));
            const float rShared = std::min (fade (rMid, st.midAmount), fade (rSide, st.sideAmount));

            const float gShared = shared.process (rShared);
            const float gMidOwn = midOwn.process (rMid);
            const float gSideOwn = sideOwn.process (rSide);

            delayLine.process (frame);

            Gains g;
            g.mid  = fade (blend (gMidOwn, gShared, st.link), st.midAmount);
            g.side = fade (blend (gSideOwn, gShared, st.link), st.sideAmount);

            if (exactlyEquals (g.mid, 1.0f) && exactlyEquals (g.side, 1.0f))
                return g;

            if (exactlyEquals (g.mid, g.side))
            {
                // fully linked: scale L and R directly, the ratio stays exact
                frame[0] *= g.mid;
                frame[1] *= g.mid;
                return g;
            }

            MidSideCodec::encode (frame[0], frame[1], m, s);
            MidSideCodec::decode (m * g.mid, s * g.side, frame[0], frame[1]);
            return g;
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

        Lookahead<2> delayLine;
        GainSmoother shared, midOwn, sideOwn;
    };
}
