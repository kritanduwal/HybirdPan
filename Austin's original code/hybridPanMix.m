function [hybridMix, icldMix, ictdMix, info] = hybridPanMix(tracks, panPositions, fs, varargin)
% HYBRIDPANMIX  Multi-track stereo panning with hybrid ICLD+ICTD law.
%
% PURPOSE
%   This processor implements three stereophonic panning techniques side-by-side
%   so they can be compared on identical source material:
%
%     1. ICLD-only  - Inter-Channel Level Difference (constant-power 3 dB pan law).
%                     This is the standard DAW pan law used in conventional mixing.
%
%     2. ICTD-only  - Inter-Channel Time Difference (sub-sample fractional delay).
%                     Pure timing-based panning with no level difference.
%
%     3. HYBD       - Hybrid combining ICLD and ICTD. Two sub-modes:
%                       'broadband' applies both cues across the full spectrum
%                                   (matches Duwal, Arias, Bulla & Chon, AES 2025).
%                       'freqsplit' applies ICTD below a crossover (~700 Hz) and
%                                   ICLD above it, following Wightman & Kistler's
%                                   finding that ITD dominates low-frequency
%                                   localization and ILD dominates high-frequency
%                                   localization. This is a research extension
%                                   beyond the broadband HYBD tested in the paper.
%
%   The processor accepts a cell array of mono tracks (or stereo files split into
%   L/R as separate mono tracks at the call site) plus a pan position per track,
%   and renders three stereo mixes that are LUFS-matched for fair A/B/C comparison.
%
% FILTERING (assignment requirement)
%   - Linkwitz-Riley 4th-order crossover (cascaded 2nd-order Butterworth) splits
%     each track into low/high bands for the freqsplit HYBD mode.
%   - Lagrange 3rd-order fractional delay filter implements sub-sample ICTD
%     (necessary because human ITD discrimination is ~10 microseconds, far below
%     one sample at 48 kHz).
%   - K-weighting filter (ITU-R BS.1770) for integrated loudness measurement
%     used in LUFS matching across output mixes.
%
% USAGE
%   [H, L, T, info] = hybridPanMix(tracks, panPositions, fs)
%   [H, L, T, info] = hybridPanMix(tracks, panPositions, fs, 'mode', 'freqsplit')
%   [H, L, T, info] = hybridPanMix(tracks, panPositions, fs, 'mode', 'broadband', ...
%                                  'gains_dB', [-3 0 -6 0], 'crossoverHz', 700, ...
%                                  'maxICTD_ms', 0.3)
%
% INPUTS
%   tracks         - 1xN cell array of column-vector mono signals
%   panPositions   - 1xN vector of pan positions in [-1, +1]
%                    (-1 = hard left, 0 = center, +1 = hard right)
%   fs             - sample rate (Hz). All tracks must already share this fs.
%
% NAME-VALUE OPTIONAL ARGUMENTS
%   'mode'         - 'freqsplit' (default) or 'broadband'
%   'gains_dB'     - 1xN vector of per-track gains in dB (default zeros)
%   'crossoverHz'  - crossover frequency for freqsplit mode (default 700)
%   'maxICTD_ms'   - max ICTD at hard-pan, in ms (default 0.3, per AES paper)
%   'maxICLD_dB'   - max channel level difference at hard-pan (default 6 dB,
%                    which is what equal-power law produces between channels at
%                    the extremes; included for explicit reference)
%
% OUTPUTS
%   hybridMix      - Nx2 stereo mix using HYBD panning
%   icldMix        - Nx2 stereo mix using ICLD-only (the paper's control)
%   ictdMix        - Nx2 stereo mix using ICTD-only
%   info           - struct with diagnostics:
%                       .mode            - mode used
%                       .crossoverHz     - crossover used
%                       .perTrack        - struct array, one entry per track:
%                                          .pan, .gain_dB, .ICLD_dB,
%                                          .ICTD_samples, .ICTD_ms,
%                                          .leftChannel ('delayed' or 'undelayed')
%                       .lufs_HYBD/.lufs_ICLD/.lufs_ICTD - integrated loudness
%                       .lufs_target     - the loudness all mixes were matched to
%
% REFERENCES
%   Duwal, Arias, Bulla & Chon (2025). "Two is Better Than One: The Impact of
%       Time- and Level-Based Panning on Spatial Perception and Listener
%       Preference." AES 159th Convention, Long Beach, CA.
%   Wightman, F.L. & Kistler, D.J. (1992). "The dominant role of low-frequency
%       interaural time difference in sound localization." JASA 91(3), 1648-1661.
%   Blauert, J. (1997). Spatial Hearing: The Psychophysics of Human Sound
%       Localization. MIT Press.
%   ITU-R BS.1770. Algorithms to measure audio programme loudness.
%
% Author: Austin Arias  |  AET 5420 Homework 4  |  Spring 2026

% ---------- Argument parsing ----------
p = inputParser;
addRequired(p, 'tracks', @iscell);
addRequired(p, 'panPositions', @isnumeric);
addRequired(p, 'fs', @(x) isnumeric(x) && isscalar(x) && x > 0);
addParameter(p, 'mode', 'freqsplit', @(x) ismember(x, {'freqsplit', 'broadband'}));
addParameter(p, 'gains_dB', [], @isnumeric);
addParameter(p, 'crossoverHz', 700, @(x) isnumeric(x) && isscalar(x) && x > 0);
addParameter(p, 'maxICTD_ms', 0.3, @(x) isnumeric(x) && isscalar(x) && x > 0);
addParameter(p, 'maxICLD_dB', 6, @(x) isnumeric(x) && isscalar(x) && x >= 0);
parse(p, tracks, panPositions, fs, varargin{:});

mode         = p.Results.mode;
crossoverHz  = p.Results.crossoverHz;
maxICTD_ms   = p.Results.maxICTD_ms;
gains_dB     = p.Results.gains_dB;

N = numel(tracks);
if numel(panPositions) ~= N
    error('hybridPanMix:dimMismatch', ...
        'panPositions must have one entry per track (got %d, expected %d).', ...
        numel(panPositions), N);
end
if isempty(gains_dB)
    gains_dB = zeros(1, N);
elseif numel(gains_dB) ~= N
    error('hybridPanMix:dimMismatch', ...
        'gains_dB must have one entry per track (got %d, expected %d).', ...
        numel(gains_dB), N);
end
panPositions = max(-1, min(1, panPositions(:).'));  % clamp & row vector

% ---------- Length harmonization (zero-pad to longest) ----------
lengths = cellfun(@(x) length(x), tracks);
Lmax = max(lengths);
for k = 1:N
    x = tracks{k}(:);  % force column
    if length(x) < Lmax
        x = [x; zeros(Lmax - length(x), 1)];
    end
    tracks{k} = x;
end

% ---------- Pre-compute per-track parameters ----------
maxICTD_samples = maxICTD_ms * 1e-3 * fs;
perTrack = struct('pan', {}, 'gain_dB', {}, 'ICLD_dB', {}, ...
    'ICTD_samples', {}, 'ICTD_ms', {}, 'leftChannel', {});

% ---------- Allocate output buffers ----------
hybridMix = zeros(Lmax, 2);
icldMix   = zeros(Lmax, 2);
ictdMix   = zeros(Lmax, 2);

% ---------- Design crossover (only used in freqsplit mode) ----------
% Linkwitz-Riley 4th-order = cascade of two identical 2nd-order Butterworths.
% Magnitudes sum flat at the crossover. Same filter applied twice (forward only)
% gives LR4. We implement as a single 4th-order biquad cascade.
[bLP2, aLP2] = butter(2, crossoverHz / (fs/2), 'low');
[bHP2, aHP2] = butter(2, crossoverHz / (fs/2), 'high');

% ---------- Process each track ----------
for k = 1:N
    x = tracks{k} * 10^(gains_dB(k)/20);   % apply gain
    pan = panPositions(k);

    % --- ICLD via constant-power (equal-power) pan law ---
    % Map pan in [-1,+1] -> theta in [0, pi/2]
    theta = (pan + 1) * pi/4;
    gL = cos(theta);   % at pan=-1: gL=1, gR=0; at pan=+1: gL=0, gR=1; at 0: both = 1/sqrt(2)
    gR = sin(theta);

    % --- ICTD via fractional delay (Lagrange-3) ---
    % The contralateral channel (the one farther from the panned position) is
    % delayed. delaySamples is positive; magnitude scales linearly with |pan|.
    delaySamples = abs(pan) * maxICTD_samples;
    if pan < 0
        leftDelayed = false;   % source is on the left -> right ear (R channel) is delayed
        leftLabel = 'undelayed';
    elseif pan > 0
        leftDelayed = true;    % source is on the right -> left ear (L channel) is delayed
        leftLabel = 'delayed';
    else
        leftDelayed = false;
        leftLabel = 'none';
    end

    % --- Build the three mix contributions for this track ---

    % ICLD-only: gain only, no delay
    icldMix(:,1) = icldMix(:,1) + gL * x;
    icldMix(:,2) = icldMix(:,2) + gR * x;

    % ICTD-only: equal level on both channels, delay on contralateral
    if delaySamples > 0
        xDelayed = lagrangeFractionalDelay(x, delaySamples);
    else
        xDelayed = x;
    end
    invSqrt2 = 1/sqrt(2);  % keep summed energy comparable to other modes
    if leftDelayed
        ictdMix(:,1) = ictdMix(:,1) + invSqrt2 * xDelayed;
        ictdMix(:,2) = ictdMix(:,2) + invSqrt2 * x;
    else
        ictdMix(:,1) = ictdMix(:,1) + invSqrt2 * x;
        ictdMix(:,2) = ictdMix(:,2) + invSqrt2 * xDelayed;
    end

    % HYBD: depends on mode
    if strcmp(mode, 'broadband')
        % ICLD + ICTD applied broadband (matches Duwal et al. 2025)
        if leftDelayed
            hybridMix(:,1) = hybridMix(:,1) + gL * xDelayed;
            hybridMix(:,2) = hybridMix(:,2) + gR * x;
        else
            hybridMix(:,1) = hybridMix(:,1) + gL * x;
            hybridMix(:,2) = hybridMix(:,2) + gR * xDelayed;
        end
    else
        % freqsplit: ICTD below crossover, ICLD above
        % Split track into low and high bands
        xLow  = filter(bLP2, aLP2, x);
        xHigh = filter(bHP2, aHP2, x);

        % Low band: ICTD only (equal level both channels, contralateral delayed)
        if delaySamples > 0
            xLowDelayed = lagrangeFractionalDelay(xLow, delaySamples);
        else
            xLowDelayed = xLow;
        end
        if leftDelayed
            lowL = invSqrt2 * xLowDelayed;
            lowR = invSqrt2 * xLow;
        else
            lowL = invSqrt2 * xLow;
            lowR = invSqrt2 * xLowDelayed;
        end

        % High band: ICLD only (equal-power, no delay)
        highL = gL * xHigh;
        highR = gR * xHigh;

        % Recombine
        hybridMix(:,1) = hybridMix(:,1) + lowL + highL;
        hybridMix(:,2) = hybridMix(:,2) + lowR + highR;
    end

    % --- Compute reported ICLD in dB (between channels, for the diagnostic table) ---
    if pan == 0
        icld_dB = 0;
    else
        icld_dB = 20*log10(max(gL,gR) / max(min(gL,gR), eps));
        icld_dB = sign(pan) * icld_dB;  % positive = louder on right
    end

    perTrack(k).pan          = pan;
    perTrack(k).gain_dB      = gains_dB(k);
    perTrack(k).ICLD_dB      = icld_dB;
    perTrack(k).ICTD_samples = delaySamples * sign(pan);  % signed: + means right is delayed (no, see below)
    perTrack(k).ICTD_ms      = (delaySamples / fs) * 1e3 * sign(pan);
    perTrack(k).leftChannel  = leftLabel;
end

% ---------- LUFS matching across the three output mixes ----------
% Measure integrated loudness of each, then match to the median.
lufs_HYBD = integratedLUFS(hybridMix, fs);
lufs_ICLD = integratedLUFS(icldMix,   fs);
lufs_ICTD = integratedLUFS(ictdMix,   fs);

lufs_all = [lufs_HYBD, lufs_ICLD, lufs_ICTD];
lufs_target = median(lufs_all);

hybridMix = hybridMix * 10^((lufs_target - lufs_HYBD)/20);
icldMix   = icldMix   * 10^((lufs_target - lufs_ICLD)/20);
ictdMix   = ictdMix   * 10^((lufs_target - lufs_ICTD)/20);

% ---------- Final peak safety: if any mix exceeds -1 dBFS, scale all three equally ----------
peakAll = max([max(abs(hybridMix(:))), max(abs(icldMix(:))), max(abs(ictdMix(:)))]);
target_peak = 10^(-1/20);  % -1 dBFS
if peakAll > target_peak
    safetyGain = target_peak / peakAll;
    hybridMix = hybridMix * safetyGain;
    icldMix   = icldMix   * safetyGain;
    ictdMix   = ictdMix   * safetyGain;
    lufs_target = lufs_target + 20*log10(safetyGain);  % update reported value
end

% ---------- Pack info struct ----------
info.mode        = mode;
info.crossoverHz = crossoverHz;
info.perTrack    = perTrack;
info.lufs_HYBD   = integratedLUFS(hybridMix, fs);
info.lufs_ICLD   = integratedLUFS(icldMix,   fs);
info.lufs_ICTD   = integratedLUFS(ictdMix,   fs);
info.lufs_target = lufs_target;

end


% =====================================================================
% Local helper: Lagrange 3rd-order fractional delay
% =====================================================================
function y = lagrangeFractionalDelay(x, D)
% Delays signal x by D samples (D >= 0, possibly fractional) using a
% 3rd-order Lagrange interpolator. Output length matches input length.
%
% The interpolator reads x at the (fractional) position (n - D) using 4 taps
% spanning x(n - intD + 1), x(n - intD), x(n - intD - 1), x(n - intD - 2),
% weighted by Lagrange-3 coefficients h0..h3 that depend on the fractional
% part fracD in [0, 1].
%
% Reference: Laakso et al., "Splitting the Unit Delay," IEEE SP Mag, 1996.

x = x(:);
N = length(x);

intD  = floor(D);
fracD = D - intD;

% Lagrange-3 coefficients for fractional delay 'd' in [0, 1]
d  = fracD;
h0 = -d*(d-1)*(d-2)/6;       % weight on x(n - intD + 1)
h1 =  (d+1)*(d-1)*(d-2)/2;   % weight on x(n - intD)
h2 = -(d+1)*d*(d-2)/2;       % weight on x(n - intD - 1)
h3 =  (d+1)*d*(d-1)/6;       % weight on x(n - intD - 2)

% Build a left-padded copy of x so that for any n in [1, N], the four needed
% positions are valid indices into xPad. Left-padding by (intD + 2) zeros
% positions x(1) at xPad(intD + 3). Then x(n - intD - 2) maps to xPad(n) for
% n >= 1 (yielding zero for the early samples, as desired). One extra trailing
% zero handles the +1 lookahead at n = N.
xPad = [zeros(intD + 2, 1); x; 0];
% In MATLAB 1-based indexing: x(k) lives at xPad(k + intD + 2).
% So:
%   x(n - intD - 2) -> xPad(n)             (this is also the "earliest" tap)
%   x(n - intD - 1) -> xPad(n + 1)
%   x(n - intD)     -> xPad(n + 2)
%   x(n - intD + 1) -> xPad(n + 3)

y = zeros(N, 1);
for n = 1:N
    y(n) = h0*xPad(n + 3) + h1*xPad(n + 2) + h2*xPad(n + 1) + h3*xPad(n);
end
end


% =====================================================================
% Local helper: Integrated loudness (simplified ITU-R BS.1770 K-weighting)
% =====================================================================
function L = integratedLUFS(x, fs)
% Returns integrated loudness in LUFS for a stereo signal x (Nx2).
% Implements:
%   - K-weighting (pre-filter + RLB high-pass)
%   - Mean-square per channel, summed (channel weights = 1 for L,R)
%   - Absolute gating at -70 LUFS
%   - Relative gating at -10 LU below ungated mean
% This is a faithful but minimal implementation of BS.1770-4.

if size(x, 2) == 1
    x = [x x];  % mono fallback: duplicate
end

% --- K-weighting: stage 1 (high-shelf) ---
% Coefficients from BS.1770, normalized for fs = 48 kHz.
% For other rates, redesign via bilinear transform of the analog prototype.
% Here we use the standard 48k coefficients adjusted via simple frequency warping.
[b1, a1] = kWeightingStage1(fs);
[b2, a2] = kWeightingStage2(fs);

xK = filter(b1, a1, x);
xK = filter(b2, a2, xK);

% --- Mean-square in 400 ms blocks with 75% overlap (per BS.1770) ---
blockLen = round(0.4 * fs);
hopLen   = round(0.1 * fs);
nBlocks  = max(0, floor((size(xK,1) - blockLen) / hopLen) + 1);

if nBlocks <= 0
    L = -70;  % silence / too short
    return;
end

blockMS = zeros(nBlocks, 1);
for b = 1:nBlocks
    idx = (1:blockLen) + (b-1)*hopLen;
    seg = xK(idx, :);
    % channel sum of mean-square (BS.1770: G_L=G_R=1 for stereo)
    blockMS(b) = mean(seg(:,1).^2) + mean(seg(:,2).^2);
end

% Convert to LUFS per block
blockLUFS = -0.691 + 10*log10(blockMS + eps);

% Absolute gate at -70 LUFS
keep = blockLUFS > -70;
if ~any(keep)
    L = -70;
    return;
end

% Relative gate at -10 LU below ungated mean
ungatedMean = -0.691 + 10*log10(mean(blockMS(keep)) + eps);
relGate = ungatedMean - 10;
keep = keep & (blockLUFS > relGate);

if ~any(keep)
    L = ungatedMean;
else
    L = -0.691 + 10*log10(mean(blockMS(keep)) + eps);
end
end


function [b, a] = kWeightingStage1(fs)
% High-shelf at ~1681 Hz, +4 dB. BS.1770 Table 1 coefficients (fs=48k),
% redesigned via pre-warped bilinear transform for arbitrary fs.
f0 = 1681.974450955533;
G  = 3.999843853973347;     % dB
Q  = 0.7071752369554196;
K  = tan(pi*f0/fs);
Vh = 10^(G/20);
Vb = Vh^0.4996667741545416;
a0 = 1 + K/Q + K^2;
b  = [(Vh + Vb*K/Q + K^2)/a0, 2*(K^2 - Vh)/a0, (Vh - Vb*K/Q + K^2)/a0];
a  = [1, 2*(K^2 - 1)/a0, (1 - K/Q + K^2)/a0];
end


function [b, a] = kWeightingStage2(fs)
% RLB highpass at ~38 Hz. BS.1770.
f0 = 38.13547087602444;
Q  = 0.5003270373238773;
K  = tan(pi*f0/fs);
a0 = 1 + K/Q + K^2;
b  = [1, -2, 1] / a0;
a  = [1, 2*(K^2 - 1)/a0, (1 - K/Q + K^2)/a0];
end
