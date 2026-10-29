#pragma once

namespace solidclip
{
    /** M = (L + R) / 2, S = (L - R) / 2  and back: L = M + S, R = M - S. */
    struct MidSideCodec
    {
        static void encode (float l, float r, float& m, float& s) noexcept
        {
            m = (l + r) * 0.5f;
            s = (l - r) * 0.5f;
        }

        static void decode (float m, float s, float& l, float& r) noexcept
        {
            l = m + s;
            r = m - s;
        }
    };
}
