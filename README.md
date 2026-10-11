# Hybrid Pan — JUCE plugin

A real-time stereo panner that implements the three panning laws from
`hybridPanMix.m` (Duwal, Arias, Bulla & Chon, *"Two is Better Than One,"*
AES 159th Convention, 2025, plus the Wightman & Kistler duplex-theory
extension). Drop one instance on a track; it sums the input to mono and pans it
to a position in **[-1, +1]** using the technique you select.

- **ICLD** — constant-power (equal-power, ~3 dB) level pan. The standard DAW pan law.
- **ICTD** — equal level on both channels; the contralateral channel is delayed
  by a sub-sample amount (Lagrange-3 fractional delay).
- **HYBD** — hybrid of the two:
  - **Broadband** — ICLD + ICTD across the whole spectrum (matches the AES paper).
  - **Freqsplit** — ICTD below the crossover, ICLD above it (research extension).

## Parameters

| Parameter    | Range            | Notes |
|--------------|------------------|-------|
| **Pan**      | -1 … +1          | -1 hard left, 0 center, +1 hard right. Smoothed. |
| **Mode**     | HYBD / ICLD / ICTD | Which technique is rendered. |
| **HYBD Sub-mode** | Freqsplit / Broadband | Only active in HYBD. |
| **Crossover**| 100 … 2000 Hz    | Freqsplit band split (default 700 Hz). Only active in HYBD Freqsplit. |
| **Max ICTD** | 0 … 2 ms         | Time difference at hard pan (default 0.3 ms, per the paper). |
| **Output**   | -24 … +24 dB     | Manual trim (replaces the offline LUFS matching, see below). |

## How the MATLAB maps onto the plugin

`hybridPanMix.m` is an **offline batch renderer**: it takes N mono tracks, renders
the whole mix three ways at once, LUFS-matches them, and writes WAVs for A/B/C
listening. A plugin is real-time and single-instance, so the port keeps the DSP
that defines each technique and drops the batch scaffolding.

| MATLAB (`hybridPanMix.m`)                              | Plugin |
|--------------------------------------------------------|--------|
| Equal-power gains `theta=(pan+1)*pi/4`, `gL=cos`, `gR=sin` | `HybridPanner::processSample` (identical) |
| Lagrange-3 fractional delay helper                     | `FractionalDelay` (streaming ring buffer, bit-exact with the MATLAB formula) |
| `butter(2, fc/(fs/2))` low/high split                  | `juce::dsp::IIR` Butterworth LP/HP (Q = 1/√2 = same filter) |
| Contralateral-channel delay, magnitude `|pan|*maxICTD` | same, applied per sample |
| Renders ICLD **and** ICTD **and** HYBD together        | renders **one** technique at a time via the Mode parameter |
| N tracks summed into the mix                            | one instance per track; input summed to mono |
| Offline integrated-LUFS matching + peak safety         | **not ported** — replaced by the Output trim (see below) |
| File I/O, resampling, analysis plots (`Arias_HW4.m`, `compareOutputs.m`) | host handles playback; not part of the plugin |

### Real-time adaptations (and why)

- **1-sample latency.** The MATLAB Lagrange interpolator reads one sample *ahead*
  of the nominal integer delay (`x(n-intD+1)`), which is fine offline with a
  padded signal but non-causal in a stream. The plugin gives every path a
  uniform 1-sample delay so all four taps stay in the past, then reports
  `setLatencySamples(1)` for host delay compensation. Relative ITD is preserved
  exactly, and the latency is constant across modes so switching never disturbs
  the host's PDC.
- **No cross-mix LUFS matching.** That step normalizes the three *separate* mixes
  to each other for a fair blind test — it has no meaning when only one technique
  is playing. Use the **Output** trim if you want to level-match while A/B-ing by
  toggling the Mode control. (The three laws are already near-equal-power by
  construction, so the offsets are small.)
- **Mono summing.** Each MATLAB "track" is mono; the plugin sums a stereo input
  to mono before panning, matching that model.

The fractional delay and the panning math were verified numerically against the
MATLAB formulas (constant-power law, contralateral delay direction/magnitude,
and Lagrange interpolation are bit-exact).

## Hybrid Pan Mixer (offline app)

A second target, **Hybrid Pan Mixer**, is the offline workflow of `Arias_HW4.m`
and `compareOutputs.m` built on a straight port of `hybridPanMix.m`.

1. Add audio files (WAV/AIFF/FLAC/MP3/OGG) to the **LEFT**, **CENTER** and
   **RIGHT** positions. Each position has a pan value (default -1 / 0 / +1) and
   can hold several files. Stereo files are summed to mono; files at different
   sample rates are resampled to the highest one.
2. Pick the HYBD mode (Freqsplit / Broadband), crossover and max ICTD.
3. Press **Render mixes**.

It writes these to the output folder (by default, next to the first audio file):

| File | Contents |
|------|----------|
| `output_HYBD.wav`, `output_ICLD.wav`, `output_ICTD.wav` | The three 24-bit stereo mixes, LUFS-matched to their median with a shared -1 dBFS peak safety gain. |
| `output_analysis.png` | `Arias_HW4.m` panels: L/R waveform, L/R spectrum, inter-channel cross-correlation (peak lag = effective ITD), side spectrum. |
| `comparison_analysis.png` | `compareOutputs.m` panels: goniometers, coherence, inter-channel phase, side/mid ratio. |
| `output_report.txt` | Per-track table, loudness and summary statistics (also shown in the app). |

The same plots and report are shown in the app's tabs. It can also be driven
from the command line; with `--exit` it renders and quits:

```bash
"Hybrid Pan Mixer.exe" --left gtr.wav --center vox.wav bass.wav --right keys.wav --out mixes --mode freqsplit --exit
```

## Build

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE is fetched automatically at
configure time (or point CMake at a local checkout — see the comment in
`CMakeLists.txt`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Outputs the plugin as VST3, AU (macOS) and Standalone, plus the Hybrid Pan
Mixer app, under `build/*_artefacts/Release`. The plugin is not copied to the
system plugin folder (`COPY_PLUGIN_AFTER_BUILD` is off); copy the `.vst3`
there yourself.

## Source layout

```
CMakeLists.txt
Source/
  PluginProcessor.{h,cpp}     APVTS parameters, bus layout, block processing
  PluginEditor.{h,cpp}        UI: pan + band-allocation visualiser, knobs, mode selectors
  dsp/
    HybridPanner.h            per-sample panning law (the ported core)
    FractionalDelay.h         Lagrange-3 fractional delay line
  mixer/                      Hybrid Pan Mixer app
    HybridPanMix.{h,cpp}      offline port of hybridPanMix.m (three mixes + LUFS matching)
    MixSession.{h,cpp}        load / resample / render / write WAVs
    MixAnalysis.{h,cpp}       analysis from Arias_HW4.m and compareOutputs.m
    PlotComponent.{h,cpp}     plot drawing and PNG export
    MainComponent.{h,cpp}     UI
    Main.cpp                  application entry point
```
