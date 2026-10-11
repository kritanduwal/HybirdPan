#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

//==============================================================================
/**
    Third-order Lagrange fractional delay line (real-time, sample-by-sample).

    This is the streaming equivalent of the offline `lagrangeFractionalDelay`
    helper in hybridPanMix.m. The MATLAB version reads x at the fractional
    position (n - D) using four taps

        x(n-intD+1), x(n-intD), x(n-intD-1), x(n-intD-2)

    weighted by Lagrange-3 coefficients h0..h3 that depend only on the
    fractional part d of D. Note the *top* tap sits one sample ahead of the
    nominal integer delay (n-intD+1). Offline this is fine because MATLAB
    left-pads the signal; in a causal real-time context that tap would require
    a one-sample look-ahead. We therefore only ever request delays >= 1 sample
    (the reference/undelayed branch reads at exactly 1.0, i.e. a plain z^-1),
    which keeps every tap in the past. The resulting constant one-sample
    latency is reported to the host via setLatencySamples(1).

    Reference: Laakso et al., "Splitting the Unit Delay", IEEE SP Mag, 1996.
*/
class FractionalDelay
{
public:
    FractionalDelay() = default;

    /** Allocate the ring buffer. maxDelaySamples is the largest delay that will
        ever be requested (including the +1 reference offset). */
    void prepare (int maxDelaySamples)
    {
        int needed = maxDelaySamples + 4;           // +4 for the four Lagrange taps
        int size = 1;
        while (size < needed) size <<= 1;            // round up to power of two
        if (size < 8) size = 8;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    /** Push one new input sample (this becomes x(n), the newest sample). */
    inline void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Read the signal delayed by `delay` samples (delay >= 1 for causality).
        `delay` may be fractional. Must be called AFTER push() for the current
        sample so that the newest sample is x(n). */
    inline float read (float delay) const noexcept
    {
        // Clamp so we never read past the buffer.
        const float maxD = (float) (mask - 3);
        if (delay > maxD) delay = maxD;
        if (delay < 1.0f) delay = 1.0f;             // enforce causality

        const int   intD = (int) std::floor (delay);
        const float d    = delay - (float) intD;     // fractional part in [0,1)

        // Lagrange-3 coefficients (identical to hybridPanMix.m).
        const float h0 = -d * (d - 1.0f) * (d - 2.0f) / 6.0f;   // weight on x(n-intD+1)
        const float h1 =  (d + 1.0f) * (d - 1.0f) * (d - 2.0f) / 2.0f; // x(n-intD)
        const float h2 = -(d + 1.0f) * d * (d - 2.0f) / 2.0f;   // x(n-intD-1)
        const float h3 =  (d + 1.0f) * d * (d - 1.0f) / 6.0f;   // x(n-intD-2)

        // Newest written sample x(n) sits at (writePos - 1).
        const int p0 = (writePos - 1) & mask;

        const float tTop = buffer[(size_t) ((p0 - (intD - 1)) & mask)]; // x(n-intD+1)
        const float t1   = buffer[(size_t) ((p0 -  intD)      & mask)]; // x(n-intD)
        const float t2   = buffer[(size_t) ((p0 - (intD + 1)) & mask)]; // x(n-intD-1)
        const float t3   = buffer[(size_t) ((p0 - (intD + 2)) & mask)]; // x(n-intD-2)

        return h0 * tTop + h1 * t1 + h2 * t2 + h3 * t3;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};
