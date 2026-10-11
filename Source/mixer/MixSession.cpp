#include "MixSession.h"
#include "MixAnalysis.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    using Signal = HybridPanMix::Signal;

    /** Band-limited resampling (stands in for MATLAB's resample()). */
    Signal resample (const Signal& in, double sourceFs, double targetFs)
    {
        const double ratio = sourceFs / targetFs;
        juce::WindowedSincInterpolator interpolator;

        // The interpolator delays its output; render that much extra and drop it.
        const int latency = juce::roundToInt (interpolator.getBaseLatency() / ratio);
        const int outLen  = (int) std::ceil ((double) in.size() * targetFs / sourceFs);
        const int total   = outLen + latency;

        std::vector<float> source ((size_t) std::ceil (total * ratio) + 512, 0.0f), rendered ((size_t) total);
        std::copy (in.begin(), in.end(), source.begin());
        interpolator.process (ratio, source.data(), rendered.data(), total);

        return Signal (rendered.begin() + latency, rendered.end());
    }

    bool writeWav (const juce::File& file, const HybridPanMix::Stereo& mix, double fs)
    {
        file.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk())
            return false;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), fs, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;

        stream.release();   // the writer owns it now

        juce::AudioBuffer<float> buffer (2, mix.size());
        for (int i = 0; i < mix.size(); ++i)
        {
            buffer.setSample (0, i, (float) mix.left[(size_t) i]);
            buffer.setSample (1, i, (float) mix.right[(size_t) i]);
        }

        return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
}

//==============================================================================
RenderOutcome runRender (const RenderRequest& request)
{
    RenderOutcome outcome;
    juce::StringArray log;
    log.add ("=== Hybrid Pan Mix ===");
    log.add ({});
    log.add ("Loading files...");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::vector<Signal> tracks;
    std::vector<double> rates, pans;
    juce::StringArray labels;

    for (auto& input : request.inputs)
    {
        const auto name = input.file.getFileName();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input.file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            log.add ("  Could not read " + name + "; skipping.");
            continue;
        }

        if (reader->numChannels > 2)
        {
            log.add ("  " + name + " has " + juce::String (reader->numChannels)
                     + " channels; skipping (only mono/stereo supported).");
            continue;
        }

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

        // Each position takes mono sources, so stereo files are summed to mono
        // (same 0.5 * (L + R) the plugin uses).
        const bool stereo = buffer.getNumChannels() == 2;
        Signal x ((size_t) buffer.getNumSamples());
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            x[(size_t) i] = stereo ? 0.5 * ((double) buffer.getSample (0, i) + (double) buffer.getSample (1, i))
                                   : (double) buffer.getSample (0, i);

        tracks.push_back (std::move (x));
        rates.push_back (reader->sampleRate);
        pans.push_back (input.pan);
        labels.add (input.file.getFileNameWithoutExtension());

        log.add ("  [" + juce::String ((int) tracks.size()).paddedLeft (' ', 2) + "] " + labels[labels.size() - 1]
                 + "  (" + input.position + ", " + (stereo ? "stereo->mono" : "mono")
                 + ", fs=" + juce::String (juce::roundToInt (reader->sampleRate)) + ")");
    }

    if (tracks.empty())
    {
        log.add ({});
        log.add ("No usable audio loaded.");
        outcome.report = log.joinIntoString ("\n");
        return outcome;
    }

    // Resample everything to the highest sample rate encountered.
    const double fs = *std::max_element (rates.begin(), rates.end());
    for (size_t k = 0; k < tracks.size(); ++k)
        if (rates[k] != fs)
        {
            tracks[k] = resample (tracks[k], rates[k], fs);
            log.add ("  Resampled track " + juce::String ((int) k + 1) + " (" + labels[(int) k] + ") from "
                     + juce::String (juce::roundToInt (rates[k])) + " to " + juce::String (juce::roundToInt (fs)) + " Hz");
        }

    const int numTracks = (int) tracks.size();
    log.add ({});
    log.add (juce::String::formatted ("Loaded %d track(s) at fs = %d Hz.", numTracks, juce::roundToInt (fs)));

    const auto mixes = HybridPanMix::render (std::move (tracks), pans, fs, request.options);
    const auto& info = mixes.info;

    log.add ({});
    log.add ("HYBD mode: " + juce::String (info.mode == HybridPanMix::Mode::Freqsplit
                                               ? "freqsplit (ICTD below " + juce::String (juce::roundToInt (info.crossoverHz))
                                                     + " Hz, ICLD above)"
                                               : "broadband (ICLD + ICTD across the full band)"));
    log.add ({});
    log.add ("--- Per-track parameters ---");
    log.add (juce::String ("#").paddedRight (' ', 3) + " " + juce::String ("Track").paddedRight (' ', 30)
             + " " + juce::String ("Pan").paddedLeft (' ', 7) + " " + juce::String ("ICLD_dB").paddedLeft (' ', 8)
             + " " + juce::String ("ICTD_smp").paddedLeft (' ', 10) + " " + juce::String ("ICTD_ms").paddedLeft (' ', 10));
    log.add (juce::String::repeatedString ("-", 75));

    for (int k = 0; k < numTracks; ++k)
    {
        const auto& t = info.perTrack[(size_t) k];
        log.add (juce::String (k + 1).paddedRight (' ', 3) + " " + labels[k].substring (0, 30).paddedRight (' ', 30)
                 + juce::String::formatted (" %+7.2f %+8.2f %+10.3f %+10.4f", t.pan, t.icldDb, t.ictdSamples, t.ictdMs));
    }

    log.add ({});
    log.add ("--- Output mix loudness ---");
    log.add (juce::String::formatted ("  HYBD: %.2f LUFS", info.lufsHYBD));
    log.add (juce::String::formatted ("  ICLD: %.2f LUFS", info.lufsICLD));
    log.add (juce::String::formatted ("  ICTD: %.2f LUFS", info.lufsICTD));
    log.add (juce::String::formatted ("  (target: %.2f LUFS)", info.lufsTarget));
    log.add ({});

    // Write the three mixes as 24-bit WAVs.
    const auto dir = request.outputDirectory;
    dir.createDirectory();

    const bool written = writeWav (dir.getChildFile ("output_HYBD.wav"), mixes.hybd, fs)
                      && writeWav (dir.getChildFile ("output_ICLD.wav"), mixes.icld, fs)
                      && writeWav (dir.getChildFile ("output_ICTD.wav"), mixes.ictd, fs);

    if (written)
    {
        log.add ("Wrote output_HYBD.wav, output_ICLD.wav, output_ICTD.wav to:");
        log.add ("  " + dir.getFullPathName());
    }
    else
    {
        log.add ("ERROR: could not write the output WAV files to:");
        log.add ("  " + dir.getFullPathName());
    }

    auto analysis = MixAnalysis::run (mixes, fs, numTracks);
    log.add ({});
    log.add (analysis.summary);

    outcome.ok         = written;
    outcome.report     = log.joinIntoString ("\n");
    outcome.analysis   = std::move (analysis.analysis);
    outcome.comparison = std::move (analysis.comparison);
    return outcome;
}
