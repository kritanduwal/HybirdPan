% compareOutputs.m
% Standalone analysis script for comparing the three output WAVs produced
% by Arias_HW4.m. Reads output_HYBD.wav, output_ICLD.wav, output_ICTD.wav
% from the current working directory and produces a 4-panel comparison.
%
% Panels:
%   (a) Goniometer / vectorscope (L vs R, XY plot) - the classic stereo
%       image view. ICLD-only: tilted line (correlated, unequal amplitude).
%       ICTD-only: tilted ellipse (correlated, time-shifted).
%       HYBD:       wider ellipse (combines both effects).
%
%   (b) Magnitude-squared coherence between L and R vs frequency.
%       ICLD-only: ~1 everywhere (channels are scaled copies of each other).
%       ICTD-only: drops at HF (time shift = large phase shift up high).
%       HYBD freqsplit: distinctive shape - drops in lows (ITD region),
%                       stays high above the crossover.
%
%   (c) Phase difference angle(L/R) vs frequency.
%       ICLD-only: flat at 0 across spectrum.
%       ICTD-only: linear ramp (slope = -2*pi*delay).
%       HYBD freqsplit: ramp only below crossover, flat above.
%
%   (d) Side-to-mid ratio |L-R| / |L+R| vs frequency.
%       Shows WHERE in the spectrum each technique places stereo energy.
%
% Author: Austin Arias  |  Spring 2026

clear; clc; close all;

%% ------------------------------------------------------------------
%  Load the three output mixes
%  ------------------------------------------------------------------
files = {'output_ICLD.wav', 'output_ICTD.wav', 'output_HYBD.wav'};
labels = {'ICLD-only', 'ICTD-only', 'HYBD'};
colors = {[0.5 0.5 0.5], [0.0 0.6 0.0], [0.0 0.0 0.8]};
linewidths = [1.5, 1.5, 2.0];

mixes = cell(1, 3);
fsAll = zeros(1, 3);
for i = 1:3
    if ~isfile(files{i})
        error('Could not find %s in current directory. Run Arias_HW4.m first.', files{i});
    end
    [audio, fs] = audioread(files{i});
    if size(audio, 2) ~= 2
        error('%s is not stereo.', files{i});
    end
    mixes{i} = audio;
    fsAll(i) = fs;
end
if ~all(fsAll == fsAll(1))
    error('Sample rates differ across output files.');
end
fs = fsAll(1);

% Trim all to common length
minLen = min(cellfun(@(m) size(m,1), mixes));
for i = 1:3
    mixes{i} = mixes{i}(1:minLen, :);
end
fprintf('Loaded 3 mixes, %d samples at %d Hz (%.2f sec).\n\n', ...
    minLen, fs, minLen/fs);

%% ------------------------------------------------------------------
%  Compute analysis quantities
%  ------------------------------------------------------------------
% For coherence/phase: use Welch-style averaging via mscohere and cpsd.
% Pick a window that gives reasonable frequency resolution.
nfft = 4096;
winLen = nfft;
overlap = nfft / 2;
win = hann(winLen);

cohData = cell(1, 3);
phaseData = cell(1, 3);
sideMidData = cell(1, 3);

for i = 1:3
    L = mixes{i}(:, 1);
    R = mixes{i}(:, 2);

    % Magnitude-squared coherence
    [cxy, fcoh] = mscohere(L, R, win, overlap, nfft, fs);
    cohData{i} = struct('f', fcoh, 'cxy', cxy);

    % Phase difference via cross-power spectral density
    [pxy, fcps] = cpsd(L, R, win, overlap, nfft, fs);
    phaseData{i} = struct('f', fcps, 'phase', angle(pxy));

    % Side-to-mid ratio (averaged via Welch)
    side = L - R;
    mid  = L + R;
    [Pside, fsm] = pwelch(side, win, overlap, nfft, fs);
    [Pmid,  ~]   = pwelch(mid,  win, overlap, nfft, fs);
    smRatio_dB = 10*log10((Pside + eps) ./ (Pmid + eps));
    sideMidData{i} = struct('f', fsm, 'sm_dB', smRatio_dB);
end

%% ------------------------------------------------------------------
%  Plot
%  ------------------------------------------------------------------
fig = figure('Position', [100, 100, 1300, 900], 'Color', 'w');

% --- (a) Goniometer / vectorscope ---
% Use a representative segment - the loudest 0.5 second window of the HYBD mix.
segLen = round(0.5 * fs);
if minLen < segLen
    segLen = minLen;
end
energy = sum(mixes{3}.^2, 2);
energySm = filter(ones(round(0.05*fs),1), 1, energy);
[~, peakIdx] = max(energySm);
segStart = max(1, peakIdx - round(segLen/2));
segEnd   = min(minLen, segStart + segLen - 1);

for i = 1:3
    subplot(2, 4, i);
    L = mixes{i}(segStart:segEnd, 1);
    R = mixes{i}(segStart:segEnd, 2);
    % Rotate 45 degrees so the "mono" line points up (audio convention)
    M = (L + R) / sqrt(2);
    S = (L - R) / sqrt(2);
    plot(S, M, '.', 'Color', colors{i}, 'MarkerSize', 1);
    axLim = max(abs([L; R])) * 1.1;
    if axLim < eps, axLim = 1; end
    xlim([-axLim, axLim]); ylim([-axLim, axLim]);
    axis square;
    xlabel('Side (L-R)'); ylabel('Mid (L+R)');
    title(sprintf('(a%d) Goniometer: %s', i, labels{i}));
    grid on;
    % Reference lines: vertical = pure mono, horizontal = pure side
    hold on;
    plot([0 0], [-axLim axLim], 'k:', 'LineWidth', 0.5);
    plot([-axLim axLim], [0 0], 'k:', 'LineWidth', 0.5);
end

% --- (b) Magnitude-squared coherence ---
subplot(2, 4, 4);
for i = 1:3
    semilogx(cohData{i}.f, cohData{i}.cxy, ...
        'Color', colors{i}, 'LineWidth', linewidths(i)); hold on;
end
xlabel('Frequency (Hz)'); ylabel('|\gamma|^2');
title('(b) Magnitude-squared coherence (L vs R)');
xlim([20, fs/2]); ylim([0, 1.05]);
legend(labels, 'Location', 'best'); grid on;

% --- (c) Phase difference vs frequency ---
subplot(2, 4, [5 6]);
for i = 1:3
    semilogx(phaseData{i}.f, rad2deg(phaseData{i}.phase), ...
        'Color', colors{i}, 'LineWidth', linewidths(i)); hold on;
end
xlabel('Frequency (Hz)'); ylabel('Phase angle(L/R) (degrees)');
title('(c) Inter-channel phase difference  -  ICTD shows as a phase ramp');
xlim([20, fs/2]);
legend(labels, 'Location', 'best'); grid on;
yline(0, 'k:');

% --- (d) Side-to-mid ratio ---
subplot(2, 4, [7 8]);
for i = 1:3
    semilogx(sideMidData{i}.f, sideMidData{i}.sm_dB, ...
        'Color', colors{i}, 'LineWidth', linewidths(i)); hold on;
end
xlabel('Frequency (Hz)'); ylabel('Side / Mid ratio (dB)');
title('(d) Stereo energy distribution: where each technique places width');
xlim([20, fs/2]);
legend(labels, 'Location', 'best'); grid on;

sgtitle('Comparison of ICLD, ICTD, and HYBD output mixes', 'FontWeight', 'bold');

% Save
plotPath = fullfile(pwd, 'comparison_analysis.png');
exportgraphics(fig, plotPath, 'Resolution', 150);
fprintf('Wrote comparison plot to:\n  %s\n', plotPath);

%% ------------------------------------------------------------------
%  Print summary stats
%  ------------------------------------------------------------------
fprintf('\n--- Summary statistics ---\n');
fprintf('%-12s %12s %12s %12s\n', '', 'mean coh.', 'phase@200Hz', 'phase@5kHz');
fprintf('%s\n', repmat('-', 1, 52));
for i = 1:3
    meanCoh = mean(cohData{i}.cxy);
    [~, idx200] = min(abs(phaseData{i}.f - 200));
    [~, idx5k]  = min(abs(phaseData{i}.f - 5000));
    p200 = rad2deg(phaseData{i}.phase(idx200));
    p5k  = rad2deg(phaseData{i}.phase(idx5k));
    fprintf('%-12s %12.3f %+11.2f° %+11.2f°\n', labels{i}, meanCoh, p200, p5k);
end
fprintf('\nNotes:\n');
fprintf('  ICLD-only should show coherence ~1 and phase ~0 at all freqs.\n');
fprintf('  ICTD-only should show phase ramping linearly with frequency.\n');
fprintf('  HYBD freqsplit should show ICTD-like phase below ~700 Hz,\n');
fprintf('  ICLD-like phase (near 0) above.\n');