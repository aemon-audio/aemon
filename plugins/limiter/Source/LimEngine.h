// Aemon Limiter: signal engine. AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own.
//
// Lookahead brickwall limiter. For each sample the gain needed to keep the (true) peak under the
// ceiling is computed; a sliding minimum over the lookahead window followed by a moving average of
// the same length gives a smooth gain that is guaranteed to be down before the peak arrives (the
// audio is delayed to line up). A release stage controls the recovery. True-peak detection uses a
// windowed-sinc polyphase interpolator (2x to 8x). Gain is applied at the base rate, so the limiter
// itself adds no aliasing.
// A moving gain changes the shape between the samples, so the output can peak higher than the detector
// predicted from the input. A second, much smaller stage therefore checks the limited signal itself (8x)
// and takes off the last tenths of a dB. Both detectors share one fixed delay.
// Optional TPDF dither at the very end. Allocation-free once prepared.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aemon::lim
{

enum class Style { clean = 0, punchy, dense, safe };

struct Params
{
    double pushDb = 0.0;          // input gain into the limiter, 0..24
    double ceilingDb = -1.0;      // -6..0
    double releaseMs = 80.0;      // 5..1000
    bool autoRelease = true;
    double lookaheadMs = 1.5;     // 0.1..10
    Style style = Style::clean;
    double link = 1.0;            // 0..1 stereo link
    bool truePeak = true;
    int oversampling = 4;         // 1, 2, 4, 8 (detection only)
    int ditherBits = 0;           // 0 = off, 16 or 24
    bool delta = false;           // output only what is removed
    bool bypass = false;
    double bypassGainDb = 0.0;    // level applied to the bypassed signal (equal-loudness comparison)
};

/** Windowed-sinc polyphase interpolator used to estimate inter-sample peaks (Kaiser window). */
template <int Taps, int Beta10>   // Beta10: window sharpness x 10
class SincPeakDetector
{
public:
    static constexpr int maxFactor = 8;
    static constexpr int delay() { return Taps / 2 - 1; }

    void prepare (int factor)
    {
        k = std::clamp (factor, 1, maxFactor);
        const double pi = 3.14159265358979323846, beta = Beta10 / 10.0;
        for (int ph = 0; ph < k; ++ph)
        {
            double sum = 0.0;
            for (int t = 0; t < Taps; ++t)
            {
                // tap t of phase ph samples the sinc at x = (t - Taps/2 + 1) - ph/k (fractional delay)
                const double x = (double) (t - Taps / 2 + 1) - (double) ph / k;
                const double s = std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double u = x / (Taps / 2.0);
                const double win = std::abs (u) < 1.0 ? bessel0 (beta * std::sqrt (1.0 - u * u)) / bessel0 (beta) : 0.0;
                sum += coef[(size_t) (ph * Taps + t)] = s * win;
            }
            for (int t = 0; t < Taps; ++t) coef[(size_t) (ph * Taps + t)] /= sum;   // exact for steady signals
        }
        reset();
    }
    void reset()
    {
        for (auto& h : hist) h.fill (0.0);
        pos[0] = pos[1] = 0;
    }
    /** Push one sample per channel; returns the largest |value| between the sample delay() ago and the
        next one, for a channel. */
    inline double push (int ch, double x)
    {
        auto& h = hist[(size_t) ch];
        h[(size_t) pos[ch]] = x;
        double peak = 0.0;
        for (int ph = 0; ph < k; ++ph)
        {
            double acc = 0.0;
            int idx = pos[ch];
            const double* c = coef.data() + ph * Taps;
            for (int t = 0; t < Taps; ++t) { acc += c[t] * h[(size_t) idx]; idx = idx == 0 ? Taps - 1 : idx - 1; }
            peak = std::max (peak, std::abs (acc));
        }
        pos[ch] = (pos[ch] + 1) % Taps;
        return peak;
    }

protected:
    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int i = 1; i < 40; ++i) { term *= (x / (2.0 * i)) * (x / (2.0 * i)); sum += term; }
        return sum;
    }
    int k = 1;
    std::array<double, (size_t) (maxFactor * Taps)> coef {};
    std::array<std::array<double, (size_t) Taps>, 2> hist {};
    int pos[2] { 0, 0 };
};

/** Output check: the accurate reconstruction, and the 4x filter of ITU-R BS.1770 exactly as loudness
    meters and streaming services apply it, lined up with each other. The higher reading counts. */
template <int Taps, int Beta10>
class OutputPeakDetector : public SincPeakDetector<Taps, Beta10>
{
public:
    static_assert (Taps >= 12);
    inline double push (int ch, double x)
    {
        const double peak = SincPeakDetector<Taps, Beta10>::push (ch, x);
        const auto& h = this->hist[(size_t) ch];
        const int newest = (this->pos[ch] - 1 - (Taps / 2 - 6) + 2 * Taps) % Taps;   // centre the 12 taps on the same instant
        double m[4] {};
        int idx = newest;
        for (int t = 0; t < 12; ++t)
        {
            for (int ph = 0; ph < 4; ++ph) m[ph] += bs1770[(size_t) ph][(size_t) t] * h[(size_t) idx];
            idx = idx == 0 ? Taps - 1 : idx - 1;
        }
        return std::max ({ peak, std::abs (m[0]), std::abs (m[1]), std::abs (m[2]), std::abs (m[3]) });
    }

private:
    static constexpr double bs1770[4][12] = {
        { 0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000, -0.0594482421875, 0.1373291015625, 0.9721679687500, -0.1022949218750, 0.0476074218750, -0.0266113281250, 0.0148925781250, -0.0083007812500 },
        { -0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250, -0.1665039062500, 0.4650878906250, 0.7797851562500, -0.2003173828125, 0.1015625000000, -0.0582275390625, 0.0330810546875, -0.0189208984375 },
        { -0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000, -0.2003173828125, 0.7797851562500, 0.4650878906250, -0.1665039062500, 0.0891113281250, -0.0517578125000, 0.0292968750000, -0.0291748046875 },
        { -0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750, -0.1022949218750, 0.9721679687500, 0.1373291015625, -0.0594482421875, 0.0332031250000, -0.0196533203125, 0.0109863281250, 0.0017089843750 } };
};

/** The two true-peak detectors (input, and limited signal) and the delay they add together. */
struct PeakInterpolator
{
    using Input = SincPeakDetector<12, 40>;    // finds the peaks to limit
    using Output = OutputPeakDetector<28, 30>; // checks the limited signal
    static constexpr int fade = 5;             // samples over which the output correction fades in
    static constexpr int outputDelay() { return Output::delay() + fade; }
    static constexpr int delay() { return Input::delay() + outputDelay(); }   // 23 samples
};

class Engine
{
public:
    static constexpr double maxLookaheadMs = 10.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxLa = (int) std::ceil (maxLookaheadMs * 0.001 * fs) + 2;
        ring = maxLa + PeakInterpolator::Input::delay() + 4;
        for (int c = 0; c < 2; ++c)
        {
            audio[c].assign ((size_t) ring, 0.0);
            need[c].assign ((size_t) ring, 1.0);
            held[c].assign ((size_t) ring, 1.0);
            dq[c].assign ((size_t) ring, 0);
        }
        prepareDetectors();
        updateWindow();
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (audio[c].begin(), audio[c].end(), 0.0);
            std::fill (need[c].begin(), need[c].end(), 1.0);
            std::fill (held[c].begin(), held[c].end(), 1.0);
            dqHead[c] = dqTail[c] = 0;
            boxSum[c] = (double) L;
            gain[c] = 1.0;
            sustain[c] = 0.0;
            outAudio[c].fill (0.0);
            outGain[c].fill (1.0);
            outNeed[c].fill (1.0);
            outHeld[c].fill (1.0);
            trim[c] = 1.0;
        }
        detector.reset(); outDetector.reset();
        w = 0; n = 0; ow = 0;
        bypassMix = p.bypass ? 1.0 : 0.0;
        deltaMix = p.delta ? 1.0 : 0.0;
        bypassGain = std::pow (10.0, p.bypassGainDb / 20.0);
        pushGain = std::pow (10.0, p.pushDb / 20.0);
        rng = 0x9E3779B97F4A7C15ull;
        deepestDb = 0.0;
    }

    void setParams (const Params& np)
    {
        const bool detectors = np.truePeak != p.truePeak || np.oversampling != p.oversampling;
        const bool window = np.lookaheadMs != p.lookaheadMs;
        p = np;
        if (fs <= 0) return;
        if (detectors) prepareDetectors();          // keeps the audio running
        if (window)
        {
            const int before = L;
            updateWindow();
            if (L != before) reset();               // a new latency: the caller fades around this (changesLatency)
        }
    }

    /** True if these parameters would change the latency, which restarts the delay lines. */
    bool changesLatency (const Params& np) const { return windowFor (np.lookaheadMs) != L; }

    /** Total delay of the audio: lookahead window plus the true-peak detectors. Constant for a lookahead. */
    int latencySamples() const { return L + PeakInterpolator::delay(); }
    double lastBlockDeepestDb() const { return deepestDb; }

    void process (float* Lp, float* Rp, int count)
    {
        const double ceil = std::pow (10.0, p.ceilingDb / 20.0);
        const double targetPush = std::pow (10.0, p.pushDb / 20.0);
        const double targetBypassGain = std::pow (10.0, p.bypassGainDb / 20.0);
        const double glide = 1.0 - std::exp (-1.0 / (0.02 * fs));
        double styleScale = 1.0;
        switch (p.style) { case Style::clean: styleScale = 1.0; break; case Style::punchy: styleScale = 0.5; break; case Style::dense: styleScale = 2.0; break; case Style::safe: styleScale = 3.0; break; }
        const double baseRel = std::clamp (p.releaseMs * styleScale, 1.0, 3000.0);
        const double lsb = p.ditherBits == 16 ? 1.0 / 32768.0 : (p.ditherBits == 24 ? 1.0 / 8388608.0 : 0.0);
        const int D = PeakInterpolator::Input::delay();
        const bool checkOutput = p.truePeak && p.oversampling > 1;
        const double target = checkOutput ? ceil * std::pow (10.0, -truePeakMarginDb / 20.0) : ceil;
        const double trimRc = std::exp (-1.0 / (trimReleaseMs * 0.001 * fs));
        deepestDb = 0.0;

        for (int i = 0; i < count; ++i)
        {
            pushGain += glide * (targetPush - pushGain);
            const double in[2] { Lp[i] * pushGain, Rp[i] * pushGain };

            // required gain per channel (true or sample peak), aligned to the audio delayed by D
            double r[2];
            for (int c = 0; c < 2; ++c)
            {
                const double pk = detector.push (c, in[c]);   // with 1x this is just the sample, delayed like the audio
                r[c] = pk > target ? target / pk : 1.0;
            }
            // stereo link (in dB: blend each side's requirement with the joint one)
            const double joint = std::min (r[0], r[1]);
            for (int c = 0; c < 2; ++c) r[c] = std::exp ((1.0 - p.link) * std::log (r[c]) + p.link * std::log (joint));

            double g[2];
            for (int c = 0; c < 2; ++c)
            {
                // sliding minimum over the window (monotonic deque of indices)
                need[c][(size_t) w] = r[c];
                while (dqTail[c] != dqHead[c] && need[c][(size_t) dq[c][(size_t) ((dqTail[c] - 1 + ring) % ring)]] >= r[c]) dqTail[c] = (dqTail[c] - 1 + ring) % ring;
                dq[c][(size_t) dqTail[c]] = w; dqTail[c] = (dqTail[c] + 1) % ring;
                while (((n - idxAge (c)) > L)) dqHead[c] = (dqHead[c] + 1) % ring;   // hold over L+1 samples
                const double h = need[c][(size_t) dq[c][(size_t) dqHead[c]]];
                // moving average of the held minimum over the same length
                boxSum[c] += h - held[c][(size_t) ((w - L + ring) % ring)];
                held[c][(size_t) w] = h;
                const double G = std::min (1.0, boxSum[c] / L);
                // release: drop instantly to G, recover at the release rate
                double relMs = baseRel;
                if (p.autoRelease) relMs = baseRel * (0.3 + 0.7 * std::min (1.0, sustain[c] / 0.4));
                const double rc = std::exp (-1.0 / (relMs * 0.001 * fs));
                gain[c] = G < gain[c] ? G : std::min (G, 1.0 - (1.0 - gain[c]) * rc);
                sustain[c] = gain[c] < 0.89 ? std::min (2.0, sustain[c] + 1.0 / fs) : std::max (0.0, sustain[c] - 2.0 / fs);
                g[c] = gain[c];
            }

            // audio delayed by window (L) plus interpolator (D): an inter-sample peak between two samples
            // is then covered for both of them
            for (int c = 0; c < 2; ++c) audio[c][(size_t) w] = in[c];
            const int rd = (w - L - D + 2 * ring) % ring;
            double a[2] { audio[0][(size_t) rd], audio[1][(size_t) rd] };
            w = (w + 1) % ring; ++n;

            // second stage: check the limited signal and trim what is still over (tenths of a dB)
            double q[2] { 1.0, 1.0 };
            if (checkOutput)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const double pk = outDetector.push (c, a[c] * g[c]);
                    q[c] = pk > target ? target / pk : 1.0;
                }
                const double qJoint = std::min (q[0], q[1]);
                if (qJoint < 1.0)
                    for (int c = 0; c < 2; ++c) q[c] = std::exp ((1.0 - p.link) * std::log (q[c]) + p.link * std::log (qJoint));
            }
            for (int c = 0; c < 2; ++c)
            {
                outAudio[c][(size_t) ow] = a[c];
                outGain[c][(size_t) ow] = g[c];
                {
                    // also runs (with nothing to trim) while true peak is off, so a trim always lets go smoothly;
                    // same shape as the main stage: minimum held over fade + 1 samples, then averaged over fade
                    outNeed[c][(size_t) ow] = q[c];
                    double h = 1.0, sum = 0.0;
                    for (int k = 0; k <= PeakInterpolator::fade; ++k) h = std::min (h, outNeed[c][(size_t) ((ow - k) & outMask)]);
                    outHeld[c][(size_t) ow] = h;
                    for (int k = 0; k < PeakInterpolator::fade; ++k) sum += outHeld[c][(size_t) ((ow - k) & outMask)];
                    const double T = std::min (1.0, sum / PeakInterpolator::fade);
                    trim[c] = T < trim[c] ? T : std::min (T, 1.0 - (1.0 - trim[c]) * trimRc);
                }
                const size_t ord = (size_t) ((ow - PeakInterpolator::outputDelay()) & outMask);
                a[c] = outAudio[c][ord];
                g[c] = outGain[c][ord] * trim[c];
            }
            ow = (ow + 1) & outMask;

            double y[2];
            deltaMix += glide * ((p.delta ? 1.0 : 0.0) - deltaMix);
            if (std::abs (deltaMix - (p.delta ? 1.0 : 0.0)) < 1e-6) deltaMix = p.delta ? 1.0 : 0.0;
            for (int c = 0; c < 2; ++c)
            {
                y[c] = std::clamp (a[c] * g[c], -ceil, ceil);           // final safety: never above the ceiling
                if (deltaMix > 0.0) y[c] += deltaMix * (a[c] * (1.0 - g[c]) - y[c]);
            }
            deepestDb = std::min (deepestDb, 20.0 * std::log10 (std::max (1e-9, std::min (g[0], g[1]))));

            // bypass: crossfade to the equally delayed input, optionally level-matched
            bypassMix += glide * ((p.bypass ? 1.0 : 0.0) - bypassMix);
            bypassGain += glide * (targetBypassGain - bypassGain);
            if (bypassMix > 1e-6)
                for (int c = 0; c < 2; ++c) y[c] += bypassMix * (a[c] / pushGain * bypassGain - y[c]);

            if (lsb > 0.0 && ! p.bypass)
                for (int c = 0; c < 2; ++c) y[c] = std::round ((y[c] + (uniform() - uniform()) * lsb) / lsb) * lsb;   // TPDF dither, then quantise
            Lp[i] = (float) y[0]; Rp[i] = (float) y[1];
        }
    }

private:
    void prepareDetectors()
    {
        detector.prepare (p.truePeak ? p.oversampling : 1);
        outDetector.prepare (PeakInterpolator::Output::maxFactor);   // always 8x, whatever the input detector uses
    }
    int windowFor (double ms) const { return std::max (1, (int) std::round (std::clamp (ms, 0.05, maxLookaheadMs) * 0.001 * fs)); }
    void updateWindow() { L = windowFor (p.lookaheadMs); }
    int idxAge (int c) const
    {
        // absolute sample number of the index at the head of the deque
        const int idx = dq[c][(size_t) dqHead[c]];
        const int back = (w - idx + ring) % ring;
        return (int) (n - back);
    }
    inline double uniform()
    {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return (double) (rng >> 11) * (1.0 / 9007199254740992.0);
    }

    Params p;
    double fs = 48000.0;
    static constexpr double truePeakMarginDb = 0.03;   // covers what the correction itself can move
    static constexpr double trimReleaseMs = 80.0;
    static constexpr int outSize = 32, outMask = outSize - 1;   // holds outputDelay() samples
    static_assert (PeakInterpolator::outputDelay() < outSize);

    int L = 72, ring = 1024, w = 0, ow = 0;
    long long n = 0;
    PeakInterpolator::Input detector;
    PeakInterpolator::Output outDetector;
    std::array<std::vector<double>, 2> audio, need, held;
    std::array<std::vector<int>, 2> dq;
    int dqHead[2] { 0, 0 }, dqTail[2] { 0, 0 };
    std::array<std::array<double, (size_t) outSize>, 2> outAudio {}, outGain {}, outNeed {}, outHeld {};
    double trim[2] { 1, 1 };
    double boxSum[2] { 0, 0 }, gain[2] { 1, 1 }, sustain[2] { 0, 0 };
    double pushGain = 1.0, deltaMix = 0.0, bypassMix = 0.0, bypassGain = 1.0, deepestDb = 0.0;
    uint64_t rng = 1;
};

} // namespace aemon::lim
