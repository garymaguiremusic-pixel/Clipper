#pragma once

#include "Lookahead.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace solidclip
{
    /**
        Turns a per-sample "required gain" r[n] (<= 1) into a smooth gain envelope that
        is guaranteed never to exceed the required gain of the sample it is applied to,
        so the ceiling is never overshot.

        Pipeline (all at the processing rate):
          1. min-hold over the last `holdLength` required gains   (monotonic deque)
          2. two cascaded moving averages whose combined support is `boxLength`
             (gives an S-shaped attack and release instead of a linear ramp)
          3. read the result back so that it lines up with the audio sample m = n - delay
          4. clamp with r[m] itself (exact guarantee, also while the window changes)

        With holdLength >= boxLength + 2 * offset, every hold window that feeds the gain
        of sample m also covers m and its `offset` neighbours, so the gain there is
        <= their required gains. offset = 0 is the ordinary lookahead clipper; the
        true-peak guard uses offset > 0 to protect the interpolator's span.

        The averages run on integer (fixed point) sums of the reduction (1 - gain), so
        there is no floating point drift and, when nothing is being reduced, the output
        is exactly 1.0. Reductions are rounded up, so the average errs towards more gain
        reduction, never less.
    */
    class GainSmoother
    {
    public:
        void prepare (int maxDelay, int maxHold)
        {
            const int span = std::max (maxDelay, maxHold) + 4;
            size = nextPowerOfTwo (span);
            mask = size - 1;
            required.assign ((size_t) size, 1.0f);
            smoothed.assign ((size_t) size, 1.0f);
            prefix1.assign ((size_t) size, 0);
            prefix2.assign ((size_t) size, 0);
            dequeIndex.assign ((size_t) size, 0);
            dequeValue.assign ((size_t) size, 1.0f);
            reset();
        }

        /** delay >= offset + boxLength - 1, holdLength >= boxLength. Call reset() afterwards. */
        void configure (int newDelay, int newOffset) noexcept
        {
            delay  = std::clamp (newDelay, 0, size - 4);
            offset = std::clamp (newOffset, 0, delay);
        }

        /** Can be changed at any time; the deque is rebuilt from the stored history. */
        void setWindow (int newHold, int newBox) noexcept
        {
            newBox  = std::clamp (newBox, 1, delay - offset + 1);
            newHold = std::clamp (newHold, newBox, size - 4);
            if (newHold == hold && newBox == box)
                return;

            hold = newHold;
            box  = newBox;
            box1 = (box + 1) / 2;
            box2 = box - box1 + 1;
            inv1 = 1.0 / ((double) box1 * kScale);
            inv2 = 1.0 / ((double) box2 * kScale);

            // rebuild the min-hold deque for the new window from the stored history
            dqHead = dqTail = 0;
            for (int64_t i = n - hold + 1; i <= n; ++i)
                pushDeque (i, required[(size_t) (i & mask)]);
        }

        int getDelay() const noexcept   { return delay; }

        void reset() noexcept
        {
            std::fill (required.begin(), required.end(), 1.0f);
            std::fill (smoothed.begin(), smoothed.end(), 1.0f);
            std::fill (prefix1.begin(), prefix1.end(), 0);
            std::fill (prefix2.begin(), prefix2.end(), 0);
            n = 0;
            dqHead = dqTail = 0;
            pushDeque (0, 1.0f);
        }

        /** Feeds the required gain of the newest sample, returns the gain for sample n - delay. */
        float process (float r) noexcept
        {
            ++n;
            const auto slot = (size_t) (n & mask);
            required[slot] = r;

            if (delay == 0)
                return r;

            // 1. min-hold
            pushDeque (n, r);
            while (dequeIndex[(size_t) dqHead] <= n - hold)
                dqHead = (dqHead + 1) & mask;
            const float h = dequeValue[(size_t) dqHead];

            // 2. two moving averages on fixed point reduction sums (modular arithmetic)
            const uint64_t q1 = quantiseReduction (1.0 - (double) h);
            prefix1[slot] = prefix1[(size_t) ((n - 1) & mask)] + q1;
            const uint64_t s1 = prefix1[slot] - prefix1[(size_t) ((n - box1) & mask)];
            const double a1 = s1 == 0 ? 1.0 : 1.0 - (double) s1 * inv1;

            const uint64_t q2 = quantiseReduction (1.0 - a1);
            prefix2[slot] = prefix2[(size_t) ((n - 1) & mask)] + q2;
            const uint64_t s2 = prefix2[slot] - prefix2[(size_t) ((n - box2) & mask)];
            const float a2 = s2 == 0 ? 1.0f : (float) (1.0 - (double) s2 * inv2);
            smoothed[slot] = a2;

            // 3. align: the average computed at time t belongs to sample t - offset - box + 1
            const int readBack = delay - offset - box + 1;
            const float g = smoothed[(size_t) ((n - readBack) & mask)];

            // 4. exact clamp against the sample's own requirement
            return std::min (g, required[(size_t) ((n - delay) & mask)]);
        }

    private:
        static constexpr double kScale = 1099511627776.0; // 2^40

        static uint64_t quantiseReduction (double x) noexcept
        {
            return x <= 0.0 ? 0u : (uint64_t) std::ceil (x * kScale);
        }

        void pushDeque (int64_t index, float value) noexcept
        {
            while (dqTail != dqHead && dequeValue[(size_t) ((dqTail - 1) & mask)] >= value)
                dqTail = (dqTail - 1) & mask;
            dequeIndex[(size_t) dqTail] = index;
            dequeValue[(size_t) dqTail] = value;
            dqTail = (dqTail + 1) & mask;
        }

        std::vector<float> required, smoothed;
        std::vector<uint64_t> prefix1, prefix2;
        std::vector<int64_t> dequeIndex;
        std::vector<float> dequeValue;
        int size = 8, mask = 7, dqHead = 0, dqTail = 0;
        int delay = 0, offset = 0, hold = 1, box = 1, box1 = 1, box2 = 1;
        double inv1 = 1.0 / kScale, inv2 = 1.0 / kScale;
        int64_t n = 0;
    };
}
