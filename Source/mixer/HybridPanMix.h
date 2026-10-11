#pragma once

#include <vector>

//==============================================================================
/**
    Offline port of hybridPanMix.m.

    Takes N mono tracks plus a pan position per track and renders three stereo
    mixes of the same material - HYBD, ICLD-only and ICTD-only - then
    LUFS-matches them (to the median of the three) and applies a shared -1 dBFS
    peak safety gain, exactly as the MATLAB does.

    Unlike the real-time HybridPanner used by the plugin, this works on whole
    signals in double precision and keeps the MATLAB's non-causal Lagrange
    interpolator (zero-padded, no added latency).
*/
struct HybridPanMix
{
    using Signal = std::vector<double>;

    struct Stereo
    {
        Signal left, right;
        int size() const noexcept { return (int) left.size(); }
    };

    enum class Mode { Freqsplit, Broadband };

    struct Options
    {
        Mode   mode        = Mode::Freqsplit;
        double crossoverHz = 700.0;
        double maxICTDms   = 0.3;
        std::vector<double> gainsDb;      // one per track; empty = all 0 dB
    };

    struct TrackInfo
    {
        double pan = 0.0, gainDb = 0.0, icldDb = 0.0, ictdSamples = 0.0, ictdMs = 0.0;
        const char* leftChannel = "none"; // "delayed", "undelayed" or "none"
    };

    struct Info
    {
        Mode   mode = Mode::Freqsplit;
        double crossoverHz = 700.0;
        std::vector<TrackInfo> perTrack;
        double lufsHYBD = -70.0, lufsICLD = -70.0, lufsICTD = -70.0, lufsTarget = -70.0;
    };

    struct Result
    {
        Stereo hybd, icld, ictd;
        Info   info;
    };

    /** All tracks must already share the sample rate fs. Shorter tracks are
        zero-padded to the longest. */
    static Result render (std::vector<Signal> tracks, std::vector<double> pans,
                          double fs, const Options& options);

    //==============================================================================
    // Building blocks (also used by the analysis code).

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        Signal process (const Signal& x) const;
    };

    /** 2nd-order sections; q = 1/sqrt(2) gives MATLAB's butter(2, fc/(fs/2)). */
    static Biquad lowpass  (double fc, double fs, double q = 0.7071067811865476);
    static Biquad highpass (double fc, double fs, double q = 0.7071067811865476);

    /** Lagrange-3 fractional delay of D >= 0 samples; output length == input length. */
    static Signal fractionalDelay (const Signal& x, double D);

    /** Integrated loudness (simplified ITU-R BS.1770, as in the MATLAB). */
    static double integratedLUFS (const Stereo& x, double fs);
};
