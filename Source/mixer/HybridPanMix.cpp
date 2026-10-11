#include "HybridPanMix.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double pi   = 3.141592653589793;
    constexpr double eps  = 2.220446049250313e-16;

    using Signal = HybridPanMix::Signal;
    using Stereo = HybridPanMix::Stereo;
    using Biquad = HybridPanMix::Biquad;

    void addScaled (Signal& dst, const Signal& src, double gain)
    {
        for (size_t i = 0; i < dst.size(); ++i)
            dst[i] += gain * src[i];
    }

    void scale (Stereo& m, double gain)
    {
        for (auto& v : m.left)  v *= gain;
        for (auto& v : m.right) v *= gain;
    }

    double peak (const Stereo& m)
    {
        double p = 0.0;
        for (auto v : m.left)  p = std::max (p, std::abs (v));
        for (auto v : m.right) p = std::max (p, std::abs (v));
        return p;
    }

    // K-weighting stage 1: high shelf at ~1681 Hz, +4 dB (BS.1770).
    Biquad kWeightingStage1 (double fs)
    {
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan (pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        return { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }

    // K-weighting stage 2: RLB high-pass at ~38 Hz (BS.1770).
    Biquad kWeightingStage2 (double fs)
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan (pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;

        return { 1.0 / a0, -2.0 / a0, 1.0 / a0,
                 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
}

//==============================================================================
Signal HybridPanMix::Biquad::process (const Signal& x) const
{
    Signal y (x.size());
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    for (size_t n = 0; n < x.size(); ++n)
    {
        const double out = b0 * x[n] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[n];
        y2 = y1; y1 = out;
        y[n] = out;
    }

    return y;
}

HybridPanMix::Biquad HybridPanMix::lowpass (double fc, double fs, double q)
{
    const double K  = std::tan (pi * fc / fs);
    const double a0 = 1.0 + K / q + K * K;
    return { K * K / a0, 2.0 * K * K / a0, K * K / a0,
             2.0 * (K * K - 1.0) / a0, (1.0 - K / q + K * K) / a0 };
}

HybridPanMix::Biquad HybridPanMix::highpass (double fc, double fs, double q)
{
    const double K  = std::tan (pi * fc / fs);
    const double a0 = 1.0 + K / q + K * K;
    return { 1.0 / a0, -2.0 / a0, 1.0 / a0,
             2.0 * (K * K - 1.0) / a0, (1.0 - K / q + K * K) / a0 };
}

//==============================================================================
Signal HybridPanMix::fractionalDelay (const Signal& x, double D)
{
    const int N    = (int) x.size();
    const int intD = (int) std::floor (D);
    const double d = D - intD;

    // Lagrange-3 coefficients (identical to hybridPanMix.m).
    const double h0 = -d * (d - 1.0) * (d - 2.0) / 6.0;          // x(n - intD + 1)
    const double h1 =  (d + 1.0) * (d - 1.0) * (d - 2.0) / 2.0;  // x(n - intD)
    const double h2 = -(d + 1.0) * d * (d - 2.0) / 2.0;          // x(n - intD - 1)
    const double h3 =  (d + 1.0) * d * (d - 1.0) / 6.0;          // x(n - intD - 2)

    // Equivalent to MATLAB's xPad = [zeros(intD + 2, 1); x; 0].
    auto at = [&] (int i) { return (i >= 0 && i < N) ? x[(size_t) i] : 0.0; };

    Signal y ((size_t) N);
    for (int n = 0; n < N; ++n)
        y[(size_t) n] = h0 * at (n - intD + 1) + h1 * at (n - intD)
                      + h2 * at (n - intD - 1) + h3 * at (n - intD - 2);

    return y;
}

//==============================================================================
double HybridPanMix::integratedLUFS (const Stereo& x, double fs)
{
    const auto s1 = kWeightingStage1 (fs);
    const auto s2 = kWeightingStage2 (fs);
    const Signal kL = s2.process (s1.process (x.left));
    const Signal kR = s2.process (s1.process (x.right));

    // Mean-square in 400 ms blocks with 75% overlap.
    const int N        = (int) kL.size();
    const int blockLen = (int) std::lround (0.4 * fs);
    const int hopLen   = (int) std::lround (0.1 * fs);
    const int nBlocks  = (N >= blockLen) ? (N - blockLen) / hopLen + 1 : 0;

    if (nBlocks <= 0)
        return -70.0;   // silence / too short

    // Running sum of the channel-summed squares, so each block is O(1).
    std::vector<double> cumulative ((size_t) N + 1, 0.0);
    for (int i = 0; i < N; ++i)
        cumulative[(size_t) i + 1] = cumulative[(size_t) i]
                                   + kL[(size_t) i] * kL[(size_t) i] + kR[(size_t) i] * kR[(size_t) i];

    std::vector<double> blockMS ((size_t) nBlocks);
    for (int b = 0; b < nBlocks; ++b)
    {
        const size_t start = (size_t) (b * hopLen);
        blockMS[(size_t) b] = (cumulative[start + (size_t) blockLen] - cumulative[start]) / blockLen;
    }

    auto toLUFS = [] (double ms) { return -0.691 + 10.0 * std::log10 (ms + eps); };

    auto gatedMean = [&] (double gate, bool& any)
    {
        double sum = 0.0;
        int count = 0;
        for (auto ms : blockMS)
            if (toLUFS (ms) > -70.0 && toLUFS (ms) > gate) { sum += ms; ++count; }

        any = count > 0;
        return any ? sum / count : 0.0;
    };

    // Absolute gate at -70 LUFS, then relative gate 10 LU below the ungated mean.
    bool any = false;
    const double absMean = gatedMean (-70.0, any);
    if (! any)
        return -70.0;

    const double ungated = toLUFS (absMean);
    const double relMean = gatedMean (ungated - 10.0, any);
    return any ? toLUFS (relMean) : ungated;
}

//==============================================================================
HybridPanMix::Result HybridPanMix::render (std::vector<Signal> tracks, std::vector<double> pans,
                                           double fs, const Options& options)
{
    const size_t N = tracks.size();
    pans.resize (N, 0.0);

    auto gainsDb = options.gainsDb;
    gainsDb.resize (N, 0.0);

    // Length harmonisation: zero-pad to the longest track.
    size_t Lmax = 0;
    for (auto& t : tracks) Lmax = std::max (Lmax, t.size());
    for (auto& t : tracks) t.resize (Lmax, 0.0);

    const double maxICTDsamples = options.maxICTDms * 1.0e-3 * fs;
    const double invSqrt2 = 1.0 / std::sqrt (2.0);

    Result r;
    for (auto* m : { &r.hybd, &r.icld, &r.ictd })
    {
        m->left.assign  (Lmax, 0.0);
        m->right.assign (Lmax, 0.0);
    }

    // 2nd-order Butterworth low/high split, as in the MATLAB.
    const auto lp = lowpass  (options.crossoverHz, fs);
    const auto hp = highpass (options.crossoverHz, fs);

    for (size_t k = 0; k < N; ++k)
    {
        Signal x = tracks[k];
        const double gain = std::pow (10.0, gainsDb[k] / 20.0);
        for (auto& v : x) v *= gain;

        const double pan = std::clamp (pans[k], -1.0, 1.0);

        // ICLD via constant-power pan law: pan [-1,+1] -> theta [0, pi/2].
        const double theta = (pan + 1.0) * pi / 4.0;
        const double gL = std::cos (theta);
        const double gR = std::sin (theta);

        // ICTD: the contralateral channel is delayed, linearly in |pan|.
        const double delaySamples = std::abs (pan) * maxICTDsamples;
        const bool leftDelayed = pan > 0.0;   // source on the right -> delay L

        // ICLD-only: gain only, no delay.
        addScaled (r.icld.left,  x, gL);
        addScaled (r.icld.right, x, gR);

        // ICTD-only: equal level on both channels, delay on the contralateral one.
        const Signal xDelayed = delaySamples > 0.0 ? fractionalDelay (x, delaySamples) : x;
        addScaled (r.ictd.left,  leftDelayed ? xDelayed : x, invSqrt2);
        addScaled (r.ictd.right, leftDelayed ? x : xDelayed, invSqrt2);

        if (options.mode == Mode::Broadband)
        {
            // ICLD + ICTD applied broadband (matches Duwal et al. 2025).
            addScaled (r.hybd.left,  leftDelayed ? xDelayed : x, gL);
            addScaled (r.hybd.right, leftDelayed ? x : xDelayed, gR);
        }
        else
        {
            // Freqsplit: ICTD below the crossover, ICLD above it.
            const Signal xLow  = lp.process (x);
            const Signal xHigh = hp.process (x);
            const Signal xLowDelayed = delaySamples > 0.0 ? fractionalDelay (xLow, delaySamples) : xLow;

            addScaled (r.hybd.left,  leftDelayed ? xLowDelayed : xLow, invSqrt2);
            addScaled (r.hybd.right, leftDelayed ? xLow : xLowDelayed, invSqrt2);
            addScaled (r.hybd.left,  xHigh, gL);
            addScaled (r.hybd.right, xHigh, gR);
        }

        // Diagnostics for the per-track table.
        const double sign = pan > 0.0 ? 1.0 : (pan < 0.0 ? -1.0 : 0.0);

        TrackInfo t;
        t.pan         = pan;
        t.gainDb      = gainsDb[k];
        t.icldDb      = pan == 0.0 ? 0.0
                                   : sign * 20.0 * std::log10 (std::max (gL, gR) / std::max (std::min (gL, gR), eps));
        t.ictdSamples = delaySamples * sign;
        t.ictdMs      = delaySamples / fs * 1.0e3 * sign;
        t.leftChannel = pan > 0.0 ? "delayed" : (pan < 0.0 ? "undelayed" : "none");
        r.info.perTrack.push_back (t);
    }

    // LUFS-match the three mixes to their median loudness.
    const double lufsH = integratedLUFS (r.hybd, fs);
    const double lufsL = integratedLUFS (r.icld, fs);
    const double lufsT = integratedLUFS (r.ictd, fs);

    double sorted[] = { lufsH, lufsL, lufsT };
    std::sort (std::begin (sorted), std::end (sorted));
    double target = sorted[1];

    scale (r.hybd, std::pow (10.0, (target - lufsH) / 20.0));
    scale (r.icld, std::pow (10.0, (target - lufsL) / 20.0));
    scale (r.ictd, std::pow (10.0, (target - lufsT) / 20.0));

    // Peak safety: if any mix exceeds -1 dBFS, scale all three equally.
    const double peakAll    = std::max ({ peak (r.hybd), peak (r.icld), peak (r.ictd) });
    const double targetPeak = std::pow (10.0, -1.0 / 20.0);
    if (peakAll > targetPeak)
    {
        const double safety = targetPeak / peakAll;
        scale (r.hybd, safety);
        scale (r.icld, safety);
        scale (r.ictd, safety);
        target += 20.0 * std::log10 (safety);
    }

    r.info.mode        = options.mode;
    r.info.crossoverHz = options.crossoverHz;
    r.info.lufsHYBD    = integratedLUFS (r.hybd, fs);
    r.info.lufsICLD    = integratedLUFS (r.icld, fs);
    r.info.lufsICTD    = integratedLUFS (r.ictd, fs);
    r.info.lufsTarget  = target;
    return r;
}
