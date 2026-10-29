#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>

namespace solidclip
{
    inline int nextPowerOfTwo (int n) noexcept
    {
        int p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    /**
        Fixed integer delay for a frame of NumChannels samples (interleaved ring buffer).
        All memory is allocated in prepare(); setDelay() / process() never allocate.
        Besides the main delay, read() can fetch any other tap up to the capacity.
    */
    template <int NumChannels>
    class Lookahead
    {
    public:
        void prepare (int maxDelaySamples)
        {
            size = nextPowerOfTwo (std::max (1, maxDelaySamples + 1));
            mask = size - 1;
            buffer.assign ((size_t) size * NumChannels, 0.0f);
            writePos = 0;
        }

        void setDelay (int d) noexcept      { delay = std::clamp (d, 0, mask); }
        int getDelay() const noexcept       { return delay; }
        int getCapacity() const noexcept    { return mask; }

        void reset() noexcept
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        /** Writes the frame, advances, and leaves the frame delayed by `delay` in place. */
        void process (float* frame) noexcept
        {
            push (frame);
            if (delay > 0)
                read (delay, frame);
        }

        void push (const float* frame) noexcept
        {
            writePos = (writePos + 1) & mask;
            auto* dst = buffer.data() + (size_t) writePos * NumChannels;
            for (int c = 0; c < NumChannels; ++c)
                dst[c] = frame[c];
        }

        /** Reads the frame written `tap` pushes ago (0 = the newest). */
        void read (int tap, float* frame) const noexcept
        {
            const auto* src = buffer.data() + (size_t) ((writePos - tap) & mask) * NumChannels;
            for (int c = 0; c < NumChannels; ++c)
                frame[c] = src[c];
        }

    private:
        std::vector<float> buffer;
        int size = 1, mask = 0, writePos = 0, delay = 0;
    };
}
