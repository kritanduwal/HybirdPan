#include "MixAnalysis.h"
#include "Style.h"

#include <juce_dsp/juce_dsp.h>
#include <complex>

namespace
{
    using Signal = HybridPanMix::Signal;
    using Stereo = HybridPanMix::Stereo;

    constexpr double eps = 2.220446049250313e-16;

    // Symmetric Hann window, as MATLAB's hann(len).
    std::vector<double> hann (int len)
    {
        std::vector<double> w ((size_t) len, 1.0);
        if (len > 1)
            for (int i = 0; i < len; ++i)
                w[(size_t) i] = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * i / (len - 1)));
        return w;
    }

    int orderForSize (int n)
    {
        int order = 0;
        while ((1 << order) < n) ++order;
        return order;
    }

    /** Windowed, zero-padded real FFT. Returns interleaved re/im for bins 0..size/2. */
    class Spectrum
    {
    public:
        explicit Spectrum (int order) : fft (order), size (1 << order), buffer ((size_t) size * 2) {}

        const float* transform (const double* x, int len, const std::vector<double>& window)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (int i = 0; i < len; ++i)
                buffer[(size_t) i] = (float) (x[i] * window[(size_t) i]);

            fft.performRealOnlyForwardTransform (buffer.data(), true);
            return buffer.data();
        }

        std::vector<float> magnitudeDb (const Signal& x, int len, const std::vector<double>& window)
        {
            const float* s = transform (x.data(), len, window);
            std::vector<float> db ((size_t) size / 2);
            for (int k = 0; k < size / 2; ++k)
                db[(size_t) k] = (float) (20.0 * std::log10 (std::hypot ((double) s[2 * k], (double) s[2 * k + 1]) + eps));
            return db;
        }

        juce::dsp::FFT fft;
        const int size;

    private:
        std::vector<float> buffer;
    };

    /** Index of the peak of the causal moving-sum of L^2 + R^2 (MATLAB: filter + max). */
    int loudestWindowEnd (const Stereo& m, int windowLen)
    {
        double sum = 0.0, best = -1.0;
        int bestIndex = 0;

        auto energy = [&] (int i) { return m.left[(size_t) i] * m.left[(size_t) i] + m.right[(size_t) i] * m.right[(size_t) i]; };

        for (int i = 0; i < m.size(); ++i)
        {
            sum += energy (i);
            if (i >= windowLen) sum -= energy (i - windowLen);
            if (sum > best) { best = sum; bestIndex = i; }
        }

        return bestIndex;
    }

    /** MATLAB xcorr(a, b, maxLag, 'normalized'): lags -maxLag..+maxLag. */
    std::vector<double> xcorrNormalised (const double* a, const double* b, int len, int maxLag)
    {
        double ea = 0.0, eb = 0.0;
        for (int n = 0; n < len; ++n) { ea += a[n] * a[n]; eb += b[n] * b[n]; }
        const double norm = std::sqrt (ea * eb);

        std::vector<double> c ((size_t) (2 * maxLag + 1), 0.0);
        if (norm <= 0.0)
            return c;

        for (int m = -maxLag; m <= maxLag; ++m)
        {
            double s = 0.0;
            for (int n = juce::jmax (0, -m); n < juce::jmin (len, len - m); ++n)
                s += a[n + m] * b[n];
            c[(size_t) (m + maxLag)] = s / norm;
        }

        return c;
    }

    struct Peak { double lagSamples = 0.0, value = 0.0; };

    /** Integer peak of an xcorr, refined to sub-sample accuracy with a parabola. */
    Peak refinedPeak (const std::vector<double>& y, int maxLag)
    {
        const int idx = (int) std::distance (y.begin(), std::max_element (y.begin(), y.end()));
        double offset = 0.0, value = y[(size_t) idx];

        if (idx > 0 && idx < (int) y.size() - 1)
        {
            const double y0 = y[(size_t) idx - 1], y1 = y[(size_t) idx], y2 = y[(size_t) idx + 1];
            const double denom = y0 - 2.0 * y1 + y2;
            if (std::abs (denom) >= 1.0e-12)
                offset = 0.5 * (y0 - y2) / denom;
            value = y1 - 0.25 * (y0 - y2) * offset;
        }

        return { (double) (idx - maxLag) + offset, value };
    }

    Signal difference (const Stereo& m)
    {
        Signal s ((size_t) m.size());
        for (size_t i = 0; i < s.size(); ++i) s[i] = m.left[i] - m.right[i];
        return s;
    }

    std::vector<float> toFloat (const std::vector<double>& v)
    {
        return std::vector<float> (v.begin(), v.end());
    }

    /** y-range for dB plots: from the highest value down by at most `span` dB. */
    juce::Range<float> dbRange (std::initializer_list<const std::vector<float>*> ys, float span)
    {
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        for (auto* y : ys)
            for (auto v : *y)
                if (std::isfinite (v)) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }

        if (hi < lo)
            return { -1.0f, 1.0f };

        return { juce::jmax (lo, hi - span) - 3.0f, hi + 5.0f };
    }

    //==============================================================================
    /** Welch estimates (Hann, nfft 4096, 50% overlap) as used by compareOutputs.m. */
    struct Welch
    {
        std::vector<float> freq, coherence, phaseDeg, sideMidDb;
        double meanCoherence = 0.0;

        float phaseAt (double hz) const
        {
            size_t best = 0;
            for (size_t i = 1; i < freq.size(); ++i)
                if (std::abs (freq[i] - hz) < std::abs (freq[best] - hz)) best = i;
            return phaseDeg[best];
        }
    };

    Welch welch (const Stereo& m, double fs)
    {
        const int order = 12, nfft = 1 << order, hop = nfft / 2, bins = nfft / 2 + 1;
        const auto window = hann (nfft);
        double windowPower = 0.0;
        for (auto w : window) windowPower += w * w;

        Spectrum spectrum (order);
        std::vector<double> pLL ((size_t) bins), pRR ((size_t) bins), pSS ((size_t) bins), pMM ((size_t) bins);
        std::vector<std::complex<double>> pLR ((size_t) bins);
        std::vector<std::complex<double>> L ((size_t) bins);

        // Signals shorter than one segment are analysed as a single zero-padded one.
        Signal padL = m.left, padR = m.right;
        if ((int) padL.size() < nfft) { padL.resize ((size_t) nfft, 0.0); padR.resize ((size_t) nfft, 0.0); }

        int segments = 0;
        for (int start = 0; start + nfft <= (int) padL.size(); start += hop, ++segments)
        {
            const float* s = spectrum.transform (padL.data() + start, nfft, window);
            for (int k = 0; k < bins; ++k)
                L[(size_t) k] = { s[2 * k], s[2 * k + 1] };

            s = spectrum.transform (padR.data() + start, nfft, window);
            for (int k = 0; k < bins; ++k)
            {
                const std::complex<double> l = L[(size_t) k], r { s[2 * k], s[2 * k + 1] };
                pLL[(size_t) k] += std::norm (l);
                pRR[(size_t) k] += std::norm (r);
                pLR[(size_t) k] += l * std::conj (r);
                pSS[(size_t) k] += std::norm (l - r);
                pMM[(size_t) k] += std::norm (l + r);
            }
        }

        // Same PSD scaling as pwelch, so the eps floor below behaves like the MATLAB.
        const double psdScale = 1.0 / (fs * windowPower * juce::jmax (1, segments));

        Welch w;
        for (int k = 0; k < bins; ++k)
        {
            const size_t i = (size_t) k;
            const double denom = pLL[i] * pRR[i];
            const double coherence = denom > 0.0 ? std::norm (pLR[i]) / denom : 0.0;

            w.freq.push_back ((float) (k * fs / nfft));
            w.coherence.push_back ((float) coherence);
            w.phaseDeg.push_back ((float) juce::radiansToDegrees (std::arg (pLR[i])));
            w.sideMidDb.push_back ((float) (10.0 * std::log10 ((pSS[i] * psdScale + eps) / (pMM[i] * psdScale + eps))));
            w.meanCoherence += coherence / bins;
        }

        return w;
    }
}

//==============================================================================
MixAnalysis::Output MixAnalysis::run (const HybridPanMix::Result& mixes, double fs, int numTracks)
{
    using namespace MixerStyle;
    using Style = PlotSeries::Style;

    Output out;

    const auto& H = mixes.hybd;
    const int N = H.size();
    if (N < 4)
        return out;

    const bool freqsplit = mixes.info.mode == HybridPanMix::Mode::Freqsplit;
    const juce::String modeName = freqsplit ? "freqsplit" : "broadband";
    const float nyquist = (float) (fs * 0.5);
    const auto xoverLabel = juce::String (juce::roundToInt (mixes.info.crossoverHz)) + " Hz xover";

    //==============================================================================
    // Arias_HW4.m
    out.analysis.title   = "hybridPanMix analysis  -  mode: " + modeName + "  -  " + juce::String (numTracks) + " track(s)";
    out.analysis.columns = 2;
    out.analysis.rows    = 2;

    // (a) Waveform L vs R: 20 ms window starting 5 ms before the loudest 50 ms.
    {
        const int peakIndex = loudestWindowEnd (H, (int) std::lround (0.05 * fs));
        const int zoomStart = juce::jmax (0, peakIndex - (int) std::lround (0.005 * fs));
        const int zoomEnd   = juce::jmin (N - 1, zoomStart + (int) std::lround (0.020 * fs));

        PlotSeries l { "Left", left, {}, {}, Style::line, 1.4f }, r { "Right", right, {}, {}, Style::line, 1.4f };
        for (int i = zoomStart; i <= zoomEnd; ++i)
        {
            const float t = (float) ((i + 1) / fs * 1000.0);
            l.x.push_back (t); l.y.push_back ((float) H.left[(size_t) i]);
            r.x.push_back (t); r.y.push_back ((float) H.right[(size_t) i]);
        }

        PlotData p;
        p.title  = "(a) HYBD mix: L vs R waveform (20 ms window around peak)";
        p.xLabel = "Time (ms)";
        p.yLabel = "Amplitude";
        p.series = { std::move (l), std::move (r) };
        out.analysis.cells.push_back ({ std::move (p), 0, 0, 1 });
    }

    // Shared FFT set-up for panels (b) and (d): up to 2 s, Hann-windowed.
    const int order  = orderForSize (juce::jmin (N, (int) (fs * 2.0)));
    const int nfft   = 1 << order;
    const int winLen = juce::jmin (nfft, N);
    const auto window = hann (winLen);
    Spectrum spectrum (order);

    std::vector<float> freqs ((size_t) nfft / 2);
    for (int k = 0; k < nfft / 2; ++k)
        freqs[(size_t) k] = (float) (k * fs / nfft);

    // (b) Magnitude spectrum L vs R.
    {
        PlotSeries l { "Left",  left,  freqs, spectrum.magnitudeDb (H.left,  winLen, window), Style::line, 1.2f };
        PlotSeries r { "Right", right, freqs, spectrum.magnitudeDb (H.right, winLen, window), Style::line, 1.2f };

        PlotData p;
        p.title  = "(b) HYBD mix: L vs R magnitude spectrum  [mode: " + modeName + "]";
        p.xLabel = "Frequency (Hz)";
        p.yLabel = "Magnitude (dB)";
        p.logX   = true;
        p.xRange = { 20.0f, nyquist };
        p.yRange = dbRange ({ &l.y, &r.y }, 110.0f);
        if (freqsplit)
            p.vLines.push_back ({ (float) mixes.info.crossoverHz, xoverLabel });
        p.series = { std::move (l), std::move (r) };
        out.analysis.cells.push_back ({ std::move (p), 1, 0, 1 });
    }

    // (c) Inter-channel cross-correlation: the peak lag is the effective ITD.
    {
        const int anaLen   = juce::jmin (N, (int) (fs * 4.0));   // centred 4 s segment
        const int anaStart = (N - anaLen) / 2;
        const int maxLag   = juce::jmin (anaLen - 1, (int) std::lround (0.002 * fs));   // +/- 2 ms

        std::vector<float> lagsMs;
        for (int m = -maxLag; m <= maxLag; ++m)
            lagsMs.push_back ((float) (m / fs * 1000.0));

        PlotData p;
        p.title  = "(c) Inter-channel cross-correlation - peak reveals effective ITD";
        p.xLabel = "L-R lag (ms)  [positive = L delayed relative to R]";
        p.yLabel = "Normalized cross-correlation";
        p.vLines.push_back ({ 0.0f, {} });

        auto add = [&] (const double* l, const double* r, const juce::String& name, const juce::String& shortName,
                        juce::Colour colour, Style style, float thickness, bool squareMarker)
        {
            const auto xc = xcorrNormalised (l, r, anaLen, maxLag);
            const auto peak = refinedPeak (xc, maxLag);
            const float lagMs = (float) (peak.lagSamples / fs * 1000.0);

            p.series.push_back ({ name, colour, lagsMs, toFloat (xc), style, thickness });
            p.markers.push_back ({ lagMs, (float) peak.value, colour,
                                   shortName + ": " + juce::String::formatted ("%+.3f ms", (double) lagMs), squareMarker });
        };

        add (mixes.icld.left.data() + anaStart, mixes.icld.right.data() + anaStart,
             "ICLD-only", "ICLD", icld, Style::line, 1.5f, false);
        add (mixes.ictd.left.data() + anaStart, mixes.ictd.right.data() + anaStart,
             "ICTD-only", "ICTD", ictd, Style::line, 1.5f, false);
        add (H.left.data() + anaStart, H.right.data() + anaStart,
             freqsplit ? "HYBD (broadband xcorr)" : "HYBD", "HYBD", hybd, Style::line, 2.0f, false);

        // In freqsplit mode the broadband xcorr is dominated by the highs, which
        // carry no timing difference - so also show the low band on its own
        // (4th-order Butterworth low-pass at the crossover).
        if (freqsplit)
        {
            const auto lp1 = HybridPanMix::lowpass (mixes.info.crossoverHz, fs, 0.5411961001461970);
            const auto lp2 = HybridPanMix::lowpass (mixes.info.crossoverHz, fs, 1.3065629648763766);
            const Signal segL (H.left.begin()  + anaStart, H.left.begin()  + anaStart + anaLen);
            const Signal segR (H.right.begin() + anaStart, H.right.begin() + anaStart + anaLen);
            const Signal lowL = lp2.process (lp1.process (segL));
            const Signal lowR = lp2.process (lp1.process (segR));

            add (lowL.data(), lowR.data(),
                 "HYBD (low-band <" + juce::String (juce::roundToInt (mixes.info.crossoverHz)) + " Hz)",
                 "HYBD lows", hybdLow, Style::dashed, 1.5f, true);
        }

        out.analysis.cells.push_back ({ std::move (p), 0, 1, 1 });
    }

    // (d) Side signal (L - R) magnitude for all three mixes.
    {
        PlotSeries i { "ICLD-only", icld, freqs, spectrum.magnitudeDb (difference (mixes.icld), winLen, window), Style::line, 1.2f };
        PlotSeries t { "ICTD-only", ictd, freqs, spectrum.magnitudeDb (difference (mixes.ictd), winLen, window), Style::line, 1.2f };
        PlotSeries h { "HYBD",      hybd, freqs, spectrum.magnitudeDb (difference (H),          winLen, window), Style::line, 1.5f };

        PlotData p;
        p.title  = "(d) Stereo side energy vs frequency";
        p.xLabel = "Frequency (Hz)";
        p.yLabel = "Side (L-R) magnitude (dB)";
        p.logX   = true;
        p.xRange = { 20.0f, nyquist };
        p.yRange = dbRange ({ &i.y, &t.y, &h.y }, 110.0f);
        if (freqsplit)
            p.vLines.push_back ({ (float) mixes.info.crossoverHz, {} });
        p.series = { std::move (i), std::move (t), std::move (h) };
        out.analysis.cells.push_back ({ std::move (p), 1, 1, 1 });
    }

    //==============================================================================
    // compareOutputs.m
    struct Entry { const Stereo* mix; juce::String label; juce::Colour colour; float thickness; };
    const Entry entries[] = { { &mixes.icld, "ICLD-only", icld, 1.5f },
                              { &mixes.ictd, "ICTD-only", ictd, 1.5f },
                              { &mixes.hybd, "HYBD",      hybd, 2.0f } };

    out.comparison.title   = "Comparison of ICLD, ICTD, and HYBD output mixes";
    out.comparison.columns = 4;
    out.comparison.rows    = 2;

    // (a) Goniometers over the loudest 0.5 s of the HYBD mix, rotated so mono points up.
    {
        const int segLen   = juce::jmin (N, (int) std::lround (0.5 * fs));
        const int peakIdx  = loudestWindowEnd (H, (int) std::lround (0.05 * fs));
        const int segStart = juce::jmax (0, peakIdx - segLen / 2);
        const int segEnd   = juce::jmin (N - 1, segStart + segLen - 1);
        const double invSqrt2 = 1.0 / std::sqrt (2.0);

        for (int e = 0; e < 3; ++e)
        {
            const auto& m = *entries[e].mix;
            PlotSeries s { {}, entries[e].colour, {}, {}, Style::dots, 1.0f };
            double axLim = 0.0;

            for (int i = segStart; i <= segEnd; ++i)
            {
                const double l = m.left[(size_t) i], r = m.right[(size_t) i];
                s.x.push_back ((float) ((l - r) * invSqrt2));
                s.y.push_back ((float) ((l + r) * invSqrt2));
                axLim = juce::jmax (axLim, std::abs (l), std::abs (r));
            }

            axLim = axLim * 1.1 < eps ? 1.0 : axLim * 1.1;

            PlotData p;
            p.title  = "(a" + juce::String (e + 1) + ") Goniometer: " + entries[e].label;
            p.xLabel = "Side (L-R)";
            p.yLabel = "Mid (L+R)";
            p.square = true;
            p.xRange = p.yRange = { (float) -axLim, (float) axLim };
            p.series = { std::move (s) };
            out.comparison.cells.push_back ({ std::move (p), e, 0, 1 });
        }
    }

    PlotData coherence, phase, sideMid;
    coherence.title  = "(b) Magnitude-squared coherence (L vs R)";
    coherence.xLabel = "Frequency (Hz)";
    coherence.yLabel = "Coherence";
    coherence.yRange = { 0.0f, 1.05f };

    phase.title  = "(c) Inter-channel phase difference  -  ICTD shows as a phase ramp";
    phase.xLabel = "Frequency (Hz)";
    phase.yLabel = "Phase angle(L/R) (degrees)";

    sideMid.title  = "(d) Stereo energy distribution: where each technique places width";
    sideMid.xLabel = "Frequency (Hz)";
    sideMid.yLabel = "Side / Mid ratio (dB)";

    for (auto* p : { &coherence, &phase, &sideMid })
    {
        p->logX   = true;
        p->xRange = { 20.0f, nyquist };
    }

    juce::StringArray lines;
    lines.add ("--- Summary statistics ---");
    lines.add (juce::String().paddedRight (' ', 12) + " " + juce::String ("mean coh.").paddedLeft (' ', 12)
               + " " + juce::String ("phase@200Hz").paddedLeft (' ', 15) + " " + juce::String ("phase@5kHz").paddedLeft (' ', 15));
    lines.add (juce::String::repeatedString ("-", 58));

    for (auto& e : entries)
    {
        const auto w = welch (*e.mix, fs);
        coherence.series.push_back ({ e.label, e.colour, w.freq, w.coherence, Style::line, e.thickness });
        phase    .series.push_back ({ e.label, e.colour, w.freq, w.phaseDeg,  Style::line, e.thickness });
        sideMid  .series.push_back ({ e.label, e.colour, w.freq, w.sideMidDb, Style::line, e.thickness });

        lines.add (e.label.paddedRight (' ', 12)
                   + juce::String::formatted (" %12.3f %+11.2f deg %+11.2f deg", w.meanCoherence,
                                              (double) w.phaseAt (200.0), (double) w.phaseAt (5000.0)));
    }

    sideMid.yRange = dbRange ({ &sideMid.series[0].y, &sideMid.series[1].y, &sideMid.series[2].y }, 90.0f);

    lines.add ({});
    lines.add ("Notes:");
    lines.add ("  ICLD-only should show coherence ~1 and phase ~0 at all freqs.");
    lines.add ("  ICTD-only should show phase ramping linearly with frequency.");
    lines.add ("  HYBD freqsplit should show ICTD-like phase below the crossover,");
    lines.add ("  ICLD-like phase (near 0) above.");
    out.summary = lines.joinIntoString ("\n");

    out.comparison.cells.push_back ({ std::move (coherence), 3, 0, 1 });
    out.comparison.cells.push_back ({ std::move (phase),     0, 1, 2 });
    out.comparison.cells.push_back ({ std::move (sideMid),   2, 1, 2 });

    return out;
}
