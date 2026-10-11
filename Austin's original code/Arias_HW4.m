% Arias_HW4.m
%
% AET 5420 - Audio Signal Processing - Homework 4
%
% Test script for hybridPanMix.m
%
% This script:
%   1. Prompts the user to select audio files from the system file dialog.
%   2. Auto-detects stereo files and splits them into independent L/R mono tracks.
%   3. Resamples all tracks to a common sample rate (the maximum found).
%   4. Prompts the user to assign a pan position to each resulting track.
%   5. Renders three stereo mixes using hybridPanMix:
%        - HYBD  (hybrid ICLD + ICTD, freqsplit by default)
%        - ICLD  (level-only, the paper's control condition)
%        - ICTD  (time-only)
%   6. Writes the three mixes to disk as 24-bit WAV files for offline playback.
%   7. Generates a multi-panel analysis plot demonstrating the processor's
%      behavior on the rendered mixes.
%
% Author: Austin Arias  |  Spring 2026

clear; clc; close all;

%% ------------------------------------------------------------------
%  1. File selection
%  ------------------------------------------------------------------
fprintf('=== Hybrid Pan Mix - Test Script ===\n\n');
fprintf('Select one or more audio files (WAV/AIFF/FLAC/MP3).\n');
fprintf('Stereo files will be auto-split into L and R mono tracks.\n\n');

[files, filePath] = uigetfile( ...
    {'*.wav;*.aif;*.aiff;*.flac;*.mp3', 'Audio files'; '*.*', 'All files'}, ...
    'Select audio files for the mix', ...
    'MultiSelect', 'on');

if isequal(files, 0)
    error('No files selected. Aborting.');
end
if ischar(files)
    files = {files};   % single-file selection returns a char, normalize to cell
end

%% ------------------------------------------------------------------
%  2. Load files, auto-split stereo, resample to common fs
%  ------------------------------------------------------------------
rawTracks = {};       % raw audio
rawFs     = [];       % per-file sample rate
rawLabels = {};       % human-readable label per resulting track

fprintf('Loading files...\n');
for i = 1:numel(files)
    fname = files{i};
    fullPath = fullfile(filePath, fname);
    [audio, fsFile] = audioread(fullPath);
    [~, baseName, ~] = fileparts(fname);

    if size(audio, 2) == 1
        % Mono - one track
        rawTracks{end+1} = audio;
        rawFs(end+1)     = fsFile;
        rawLabels{end+1} = baseName;
        fprintf('  [%2d] %s  (mono, fs=%d)\n', numel(rawTracks), baseName, fsFile);
    elseif size(audio, 2) == 2
        % Stereo - split into two independent mono tracks
        rawTracks{end+1} = audio(:,1);
        rawFs(end+1)     = fsFile;
        rawLabels{end+1} = sprintf('%s_L', baseName);
        fprintf('  [%2d] %s  (stereo->L, fs=%d)\n', numel(rawTracks), baseName, fsFile);

        rawTracks{end+1} = audio(:,2);
        rawFs(end+1)     = fsFile;
        rawLabels{end+1} = sprintf('%s_R', baseName);
        fprintf('  [%2d] %s  (stereo->R, fs=%d)\n', numel(rawTracks), baseName, fsFile);
    else
        warning('File %s has %d channels; skipping (only mono/stereo supported).', ...
            fname, size(audio,2));
    end
end

if isempty(rawTracks)
    error('No usable audio loaded.');
end

% Resample all tracks to the highest fs encountered
fsMix = max(rawFs);
tracks = cell(size(rawTracks));
for k = 1:numel(rawTracks)
    if rawFs(k) ~= fsMix
        tracks{k} = resample(rawTracks{k}, fsMix, rawFs(k));
        fprintf('  Resampled track %d (%s) from %d to %d Hz\n', ...
            k, rawLabels{k}, rawFs(k), fsMix);
    else
        tracks{k} = rawTracks{k};
    end
end
N = numel(tracks);
fprintf('\nLoaded %d track(s) at fs = %d Hz.\n\n', N, fsMix);

%% ------------------------------------------------------------------
%  3. Prompt for pan positions
%  ------------------------------------------------------------------
fprintf('--- Pan assignment ---\n');
fprintf('Enter a pan position in [-1, +1] for each track:\n');
fprintf('  -1 = hard left, 0 = center, +1 = hard right.\n\n');

panPositions = zeros(1, N);
for k = 1:N
    prompt = sprintf('  Pan for track [%d] %s : ', k, rawLabels{k});
    val = input(prompt);
    if isempty(val) || ~isnumeric(val) || ~isscalar(val)
        warning('Invalid input; defaulting to 0 (center).');
        val = 0;
    end
    panPositions(k) = max(-1, min(1, val));
end
fprintf('\n');

% Optional: prompt for HYBD mode
fprintf('--- HYBD mode ---\n');
fprintf('  [1] freqsplit (research extension: ICTD < %d Hz, ICLD > %d Hz)\n', 700, 700);
fprintf('  [2] broadband (matches AES 2025 paper exactly)\n');
modeChoice = input('Select mode [1 or 2, default 1]: ');
if isempty(modeChoice) || ~isnumeric(modeChoice) || modeChoice == 1
    modeStr = 'freqsplit';
else
    modeStr = 'broadband';
end
fprintf('\n');

%% ------------------------------------------------------------------
%  4. Run the processor
%  ------------------------------------------------------------------
fprintf('Processing...\n');
[hybridMix, icldMix, ictdMix, info] = hybridPanMix( ...
    tracks, panPositions, fsMix, ...
    'mode', modeStr, ...
    'crossoverHz', 700, ...
    'maxICTD_ms', 0.3);

% Print diagnostic table
fprintf('\n--- Per-track parameters ---\n');
fprintf('%-3s %-30s %7s %8s %10s %10s\n', ...
    '#', 'Track', 'Pan', 'ICLD_dB', 'ICTD_smp', 'ICTD_ms');
fprintf('%s\n', repmat('-', 1, 75));
for k = 1:N
    pt = info.perTrack(k);
    fprintf('%-3d %-30s %+7.2f %+8.2f %+10.3f %+10.4f\n', ...
        k, rawLabels{k}(1:min(end,30)), pt.pan, pt.ICLD_dB, ...
        pt.ICTD_samples, pt.ICTD_ms);
end
fprintf('\n--- Output mix loudness ---\n');
fprintf('  HYBD: %.2f LUFS\n', info.lufs_HYBD);
fprintf('  ICLD: %.2f LUFS\n', info.lufs_ICLD);
fprintf('  ICTD: %.2f LUFS\n', info.lufs_ICTD);
fprintf('  (target: %.2f LUFS)\n\n', info.lufs_target);

%% ------------------------------------------------------------------
%  5. Write WAV outputs
%  ------------------------------------------------------------------
outDir = pwd;
audiowrite(fullfile(outDir, 'output_HYBD.wav'), hybridMix, fsMix, 'BitsPerSample', 24);
audiowrite(fullfile(outDir, 'output_ICLD.wav'), icldMix,   fsMix, 'BitsPerSample', 24);
audiowrite(fullfile(outDir, 'output_ICTD.wav'), ictdMix,   fsMix, 'BitsPerSample', 24);
fprintf('Wrote output_HYBD.wav, output_ICLD.wav, output_ICTD.wav to:\n  %s\n\n', outDir);

%% ------------------------------------------------------------------
%  6. Visual analysis
%  ------------------------------------------------------------------
% Four panels chosen to make the processor's behavior unambiguous:
%   (a) Waveform L vs R, zoomed on a transient - shows ICTD as visible offset.
%   (b) Magnitude spectrum of L vs R - shows ICLD as level offset; in
%       freqsplit mode, also shows the crossover region.
%   (c) Inter-channel cross-correlation vs lag (in ms) - the peak location
%       directly reveals the effective ITD applied to the mix. This is the
%       key plot: it proves the processor is doing what it claims.
%   (d) Side signal (L - R) magnitude spectrum - shows where stereo energy
%       lives in the frequency domain.

fig = figure('Position', [100, 100, 1200, 800], 'Color', 'w');

% --- (a) Waveform L vs R ---
% Auto-zoom to a region with energy (the loudest 50 ms in the HYBD mix)
energy = sum(hybridMix.^2, 2);
windowLen = round(0.05 * fsMix);
energySm = filter(ones(windowLen,1)/windowLen, 1, energy);
[~, peakIdx] = max(energySm);
zoomStart = max(1, peakIdx - round(0.005 * fsMix));
zoomEnd   = min(size(hybridMix,1), zoomStart + round(0.020 * fsMix));  % 20 ms window
tZoom = (zoomStart:zoomEnd) / fsMix * 1000;  % ms

subplot(2,2,1);
plot(tZoom, hybridMix(zoomStart:zoomEnd, 1), 'b-', 'LineWidth', 1.2); hold on;
plot(tZoom, hybridMix(zoomStart:zoomEnd, 2), 'r-', 'LineWidth', 1.2);
xlabel('Time (ms)'); ylabel('Amplitude');
title('(a) HYBD mix: L vs R waveform (20 ms window around peak)');
legend('Left', 'Right', 'Location', 'best');
grid on;

% --- (b) Magnitude spectrum L vs R ---
nfft = 2^nextpow2(min(size(hybridMix,1), fsMix * 2));   % up to 2 sec
winLen = min(nfft, size(hybridMix,1));
win = hann(winLen);
xL = hybridMix(1:winLen, 1) .* win;
xR = hybridMix(1:winLen, 2) .* win;
XL = abs(fft(xL, nfft));
XR = abs(fft(xR, nfft));
freqs = (0:nfft-1) * fsMix / nfft;
half = 1:floor(nfft/2);

subplot(2,2,2);
semilogx(freqs(half), 20*log10(XL(half) + eps), 'b-', 'LineWidth', 1.2); hold on;
semilogx(freqs(half), 20*log10(XR(half) + eps), 'r-', 'LineWidth', 1.2);
if strcmp(info.mode, 'freqsplit')
    xline(info.crossoverHz, 'k--', 'LineWidth', 1.5, ...
          'Label', sprintf('%d Hz xover', info.crossoverHz), 'LabelOrientation', 'horizontal');
end
xlabel('Frequency (Hz)'); ylabel('Magnitude (dB)');
title(sprintf('(b) HYBD mix: L vs R magnitude spectrum  [mode: %s]', info.mode));
legend('Left', 'Right', 'Location', 'best');
xlim([20 fsMix/2]); grid on;

% --- (c) Inter-channel cross-correlation vs lag ---
% This is the key plot: the peak lag directly shows the effective ITD.
% Compute on the full mix (or a centered 4-second segment for speed).
maxAnaLen = min(size(hybridMix,1), fsMix * 4);
anaStart = max(1, floor((size(hybridMix,1) - maxAnaLen)/2) + 1);
segL = hybridMix(anaStart:anaStart+maxAnaLen-1, 1);
segR = hybridMix(anaStart:anaStart+maxAnaLen-1, 2);

maxLagSamples = round(0.002 * fsMix);   % +/- 2 ms search range
[xcHYB, lagsHYB] = xcorr(segL, segR, maxLagSamples, 'normalized');

% Same for ICLD-only and ICTD-only mixes for comparison
segL_I = icldMix(anaStart:anaStart+maxAnaLen-1, 1);
segR_I = icldMix(anaStart:anaStart+maxAnaLen-1, 2);
[xcICLD, ~] = xcorr(segL_I, segR_I, maxLagSamples, 'normalized');

segL_T = ictdMix(anaStart:anaStart+maxAnaLen-1, 1);
segR_T = ictdMix(anaStart:anaStart+maxAnaLen-1, 2);
[xcICTD, ~] = xcorr(segL_T, segR_T, maxLagSamples, 'normalized');

% In freqsplit mode, the broadband HYBD xcorr is dominated by HF energy where
% there is NO timing difference. To reveal the ITD living in the low band, we
% also compute a low-band-filtered xcorr of the HYBD mix.
[bLPana, aLPana] = butter(4, info.crossoverHz / (fsMix/2), 'low');
segL_LB = filter(bLPana, aLPana, segL);
segR_LB = filter(bLPana, aLPana, segR);
[xcHYB_LB, ~] = xcorr(segL_LB, segR_LB, maxLagSamples, 'normalized');

lagsMs = lagsHYB / fsMix * 1000;

subplot(2,2,3);
plot(lagsMs, xcICLD, 'Color', [0.5 0.5 0.5], 'LineWidth', 1.5); hold on;
plot(lagsMs, xcICTD, 'Color', [0.0 0.6 0.0], 'LineWidth', 1.5);
plot(lagsMs, xcHYB,  'Color', [0.0 0.0 0.8], 'LineWidth', 2.0);
% In freqsplit mode, also show the low-band-filtered HYBD xcorr where the ITD lives
if strcmp(info.mode, 'freqsplit')
    plot(lagsMs, xcHYB_LB, '--', 'Color', [0.6 0.0 0.6], 'LineWidth', 1.5);
end
% Parabolic interpolation around each integer peak to get sub-sample lag
[~, pkI] = max(xcICLD); [~, pkT] = max(xcICTD); [~, pkH] = max(xcHYB);
[refI, valI] = parabolicRefine(xcICLD, pkI);
[refT, valT] = parabolicRefine(xcICTD, pkT);
[refH, valH] = parabolicRefine(xcHYB,  pkH);
% Convert refined index offset (relative to integer peak) into ms
refLagI_ms = (lagsHYB(pkI) + refI) / fsMix * 1000;
refLagT_ms = (lagsHYB(pkT) + refT) / fsMix * 1000;
refLagH_ms = (lagsHYB(pkH) + refH) / fsMix * 1000;
plot(refLagI_ms, valI, 'o', 'Color', [0.5 0.5 0.5], 'MarkerSize', 9, 'LineWidth', 2);
plot(refLagT_ms, valT, 'o', 'Color', [0.0 0.6 0.0], 'MarkerSize', 9, 'LineWidth', 2);
plot(refLagH_ms, valH, 'o', 'Color', [0.0 0.0 0.8], 'MarkerSize', 9, 'LineWidth', 2);
% Annotate the refined peak lags directly on the plot
text(refLagI_ms, valI*1.02, sprintf(' ICLD: %+.3f ms', refLagI_ms), ...
    'Color', [0.4 0.4 0.4], 'FontSize', 8);
text(refLagT_ms, valT*1.02, sprintf(' ICTD: %+.3f ms', refLagT_ms), ...
    'Color', [0.0 0.5 0.0], 'FontSize', 8);
text(refLagH_ms, valH*1.02, sprintf(' HYBD: %+.3f ms', refLagH_ms), ...
    'Color', [0.0 0.0 0.6], 'FontSize', 8);
if strcmp(info.mode, 'freqsplit')
    [~, pkH_LB] = max(xcHYB_LB);
    [refH_LB, valH_LB] = parabolicRefine(xcHYB_LB, pkH_LB);
    refLagH_LB_ms = (lagsHYB(pkH_LB) + refH_LB) / fsMix * 1000;
    plot(refLagH_LB_ms, valH_LB, 's', 'Color', [0.6 0.0 0.6], 'MarkerSize', 9, 'LineWidth', 2);
    text(refLagH_LB_ms, valH_LB*1.02, sprintf(' HYBD lows: %+.3f ms', refLagH_LB_ms), ...
        'Color', [0.4 0.0 0.4], 'FontSize', 8);
end
xline(0, 'k:');
xlabel('L-R lag (ms)  [positive = L delayed relative to R]'); ylabel('Normalized cross-correlation');
title('(c) Inter-channel cross-correlation - peak reveals effective ITD');
if strcmp(info.mode, 'freqsplit')
    legend('ICLD-only', 'ICTD-only', 'HYBD (broadband xcorr)', ...
           sprintf('HYBD (low-band <%d Hz)', info.crossoverHz), 'Location', 'best');
else
    legend('ICLD-only', 'ICTD-only', 'HYBD', 'Location', 'best');
end
grid on;

% --- (d) Side signal magnitude (L - R) for all three mixes ---
sideHYB  = hybridMix(:,1) - hybridMix(:,2);
sideICLD = icldMix(:,1)   - icldMix(:,2);
sideICTD = ictdMix(:,1)   - ictdMix(:,2);

winLen2 = min(nfft, length(sideHYB));
win2 = hann(winLen2);
SH = abs(fft(sideHYB(1:winLen2)  .* win2, nfft));
SI = abs(fft(sideICLD(1:winLen2) .* win2, nfft));
ST = abs(fft(sideICTD(1:winLen2) .* win2, nfft));

subplot(2,2,4);
semilogx(freqs(half), 20*log10(SI(half) + eps), 'Color', [0.5 0.5 0.5], 'LineWidth', 1.2); hold on;
semilogx(freqs(half), 20*log10(ST(half) + eps), 'Color', [0.0 0.6 0.0], 'LineWidth', 1.2);
semilogx(freqs(half), 20*log10(SH(half) + eps), 'Color', [0.0 0.0 0.8], 'LineWidth', 1.5);
if strcmp(info.mode, 'freqsplit')
    xline(info.crossoverHz, 'k--', 'LineWidth', 1.5);
end
xlabel('Frequency (Hz)'); ylabel('Side (L-R) magnitude (dB)');
title('(d) Stereo side energy vs frequency');
legend('ICLD-only', 'ICTD-only', 'HYBD', 'Location', 'best');
xlim([20 fsMix/2]); grid on;

sgtitle(sprintf('hybridPanMix analysis  -  mode: %s  -  %d track(s)', info.mode, N), ...
    'FontWeight', 'bold');

% Save plot
plotPath = fullfile(outDir, 'output_analysis.png');
exportgraphics(fig, plotPath, 'Resolution', 150);
fprintf('Wrote analysis plot to:\n  %s\n\n', plotPath);

fprintf('Done. Play the three WAV files for A/B/C comparison.\n');


% =====================================================================
% Local helper: parabolic peak interpolation for sub-sample lag refinement
% =====================================================================
function [offset, peakVal] = parabolicRefine(y, idx)
% Given a discrete sequence y and an integer peak index 'idx', estimate the
% sub-sample offset of the true peak via parabolic interpolation through
% three points (idx-1, idx, idx+1).
%
% Returns:
%   offset  - sub-sample offset (in [-0.5, +0.5]); add to idx for refined peak
%   peakVal - the interpolated peak amplitude

if idx <= 1 || idx >= length(y)
    offset = 0;
    peakVal = y(idx);
    return;
end
y0 = y(idx-1); y1 = y(idx); y2 = y(idx+1);
denom = (y0 - 2*y1 + y2);
if abs(denom) < 1e-12
    offset = 0;
else
    offset = 0.5 * (y0 - y2) / denom;
end
peakVal = y1 - 0.25 * (y0 - y2) * offset;
end
