#pragma once

#include <algorithm>
#include <cmath>
#include <functional>

namespace solidclip
{
    /** Intentional exact float comparison (for the bit-transparent fast paths). */
    inline bool exactlyEquals (float a, float b) noexcept  { return std::equal_to<float>() (a, b); }

    inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept    { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

    /**
        Symmetric soft-clip transfer curve, expressed as a *gain* for a detector level.

        The curve is exactly linear up to the threshold (ceiling - knee). Above that,
        only the overshoot is bent with a tanh, which joins the linear part with
        matching first and second derivatives and approaches the ceiling
        asymptotically:

            target(d) = d                          for d <= T
                      = T + K * tanh ((d - T) / K)  for d >  T      (K = ceiling - T)

        The returned gain is target / d, which is exactly 1.0f below the threshold so
        quiet material is left bit-identical. Because the curve works on a magnitude
        and the gain is applied to the signed signal, it is odd-symmetric: no DC and
        no even harmonics.
    */
    struct SoftClipCurve
    {
        float threshold = 1.0f;   // linear, ceiling - knee
        float kneeWidth = 0.0f;   // linear, ceiling - threshold
        float ceiling   = 1.0f;   // linear

        void set (float ceilingDb, float kneeDb) noexcept
        {
            ceiling   = dbToGain (ceilingDb);
            threshold = kneeDb > 0.0f ? ceiling * dbToGain (-kneeDb) : ceiling;
            kneeWidth = ceiling - threshold;
        }

        float target (float d) const noexcept
        {
            if (d <= threshold)
                return d;
            if (kneeWidth <= 0.0f)
                return ceiling;
            return threshold + kneeWidth * std::tanh ((d - threshold) / kneeWidth);
        }

        float gain (float d) const noexcept
        {
            if (d <= threshold)
                return 1.0f;
            return std::min (1.0f, target (d) / d);
        }
    };
}
