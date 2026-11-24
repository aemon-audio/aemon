// Aemon Compressor: signal engine ("Clean" character). AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own.
//
// Feed-forward, log-domain design: detector (peak or RMS, optional side-chain high-pass, internal or
// external side-chain) -> soft-knee gain computer -> branching attack/release smoothing of the gain
// reduction (with optional programme-dependent auto release) -> applied to the audio, which can be
// delayed for lookahead. Mix blends with the equally delayed dry signal; Auto level matches the
// output loudness to the input slowly. Neutral settings are bit-exact. Allocation-free on the audio
// path once prepared.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::comp
{

struct Params
{
    double thresholdDb = -18.0;
    double ratio = 2.0;            // >= 20 means infinity
    double kneeDb = 6.0;
    double attackMs = 10.0;
    double releaseMs = 150.0;
    bool autoRelease = false;
    double lookaheadMs = 0.0;      // 0..10
    bool rmsDetector = false;
    double scHighPassHz = 20.0;    // <= 20 means off
    bool externalSidechain = false;
    double mix = 1.0;              // 0..1
    double outputDb = 0.0;
    bool autoLevel = false;
    bool bypass = false;
};

/** Static gain computer: gain change in dB (<= 0) for a detector level in dB. */
inline double gainComputer (double x, double T, double R, double W)
{
    const double slope = R >= 19.95 ? 1.0 : 1.0 - 1.0 / R;
    if (2.0 * (x - T) < -W) return 0.0;
    if (W > 0.0 && std::abs (2.0 * (x - T)) <= W) { const double d = x - T + W * 0.5; return -slope * d * d / (2.0 * W); }
    return -slope * (x - T);
}

class Engine
{
public:
    static constexpr double maxLookaheadMs = 10.0;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        const int maxDelay = (int) std::ceil (maxLookaheadMs * 0.001 * fs) + 2;
        for (auto& d : delay) d.assign ((size_t) maxDelay + (size_t) maxBlock + 1, 0.0f);
        writePos = 0;
        holdSamples = (int) std::round (0.005 * fs);
        peakDecay = std::exp (-1.0 / (0.005 * fs));
        reset();
        updateCoefficients();
        (void) maxBlock;
    }

    void reset()
    {
        grDb = 0.0; rmsState = 0.0; sustain = 0.0; peakEnv = 0.0; holdLeft = 0;
        for (auto& h : hp) h = {};
        inPower = outPower = 1e-12; makeupDb = 0.0; appliedMakeup = 0.0;
        bypassMix = p.bypass ? 1.0 : 0.0;
        outGain = std::pow (10.0, p.outputDb / 20.0);
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
    }

    void setParams (const Params& np)
    {
        const bool hpChanged = np.scHighPassHz != p.scHighPassHz;
        p = np;
        lookahead = (int) std::round (std::clamp (p.lookaheadMs, 0.0, maxLookaheadMs) * 0.001 * fs);
        if (hpChanged) updateCoefficients();
        atk = std::exp (-1.0 / (std::max (p.attackMs, 0.01) * 0.001 * fs));
    }

    int latencySamples() const { return lookahead; }
    double currentGainReductionDb() const { return grDb; }
    double currentMakeupDb() const { return appliedMakeup; }

    /** Processes in place. sideL/sideR may be null (internal side-chain). Returns the deepest gain
        reduction in the block (dB, <= 0). */
    double process (float* L, float* R, int n, const float* sideL = nullptr, const float* sideR = nullptr)
    {
        if (isNeutral()) return 0.0;
        const bool useHp = p.scHighPassHz > 20.5;
        const bool external = p.externalSidechain && sideL != nullptr;
        const double rmsC = 1.0 - std::exp (-1.0 / (0.010 * fs));
        const double T = p.thresholdDb, Rt = p.ratio, W = std::max (0.0, p.kneeDb);
        const double mix = std::clamp (p.mix, 0.0, 1.0);
        const double level = 1.0 - std::exp (-1.0 / (3.0 * fs));          // 3 s loudness averaging for Auto level
        const double gainGlide = 1.0 - std::exp (-1.0 / (0.02 * fs));      // 20 ms for output / makeup / bypass
        const double targetOut = std::pow (10.0, p.outputDb / 20.0);
        const int dsz = (int) delay[0].size();
        double deepest = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            double dL = external ? sideL[i] : inL, dR = external ? (sideR ? sideR[i] : sideL[i]) : inR;
            if (useHp) { dL = hp[0].run (dL, hc); dR = hp[1].run (dR, hc); }

            double det;
            if (p.rmsDetector) { rmsState += rmsC * (std::max (dL * dL, dR * dR) - rmsState); det = std::sqrt (2.0 * rmsState); }
            else
            {   // peak with a 5 ms hold: follows the waveform's peaks, not the dips between them
                const double a = std::max (std::abs (dL), std::abs (dR));
                if (a >= peakEnv) { peakEnv = a; holdLeft = holdSamples; }
                else if (holdLeft > 0) --holdLeft;
                else peakEnv = std::max (a, peakEnv * peakDecay);
                det = peakEnv;
            }
            const double x = 20.0 * std::log10 (det + 1e-12);
            const double target = gainComputer (x, T, Rt, W);

            double relMs = p.releaseMs;
            if (p.autoRelease) relMs = std::clamp (p.releaseMs * (0.35 + 0.65 * std::min (1.0, sustain / 0.6)), 10.0, 2000.0);
            const double rel = std::exp (-1.0 / (relMs * 0.001 * fs));
            grDb = target < grDb ? atk * grDb + (1.0 - atk) * target : rel * grDb + (1.0 - rel) * target;
            sustain = grDb < -1.0 ? std::min (2.0, sustain + 1.0 / fs) : std::max (0.0, sustain - 2.0 / fs);
            deepest = std::min (deepest, grDb);

            // lookahead: the audio is delayed, the detector is not
            delay[0][(size_t) writePos] = inL; delay[1][(size_t) writePos] = inR;
            const int rp = (writePos - lookahead + dsz) % dsz;
            writePos = (writePos + 1) % dsz;
            const double aL = delay[0][(size_t) rp], aR = delay[1][(size_t) rp];

            const double g = std::exp (grDb * 0.11512925464970229);   // 10^(dB/20)
            const double bl = (1.0 - mix) * aL + mix * aL * g, br = (1.0 - mix) * aR + mix * aR * g;

            if (p.autoLevel)
            {
                inPower += level * ((aL * aL + aR * aR) - inPower);
                outPower += level * ((bl * bl + br * br) - outPower);
                if (inPower > 1e-9) makeupDb = std::clamp (10.0 * std::log10 (inPower / std::max (outPower, 1e-12)), 0.0, 24.0);
            }
            const double wantMakeup = p.autoLevel ? makeupDb : 0.0;
            appliedMakeup += gainGlide * (wantMakeup - appliedMakeup);
            outGain += gainGlide * (targetOut - outGain);
            const double k = outGain * std::pow (10.0, appliedMakeup / 20.0);

            double oL = bl * k, oR = br * k;
            bypassMix += gainGlide * ((p.bypass ? 1.0 : 0.0) - bypassMix);
            if (bypassMix > 1e-6) { oL += bypassMix * (aL - oL); oR += bypassMix * (aR - oR); }
            L[i] = (float) oL; R[i] = (float) oR;
        }
        return deepest;
    }

private:
    struct Biquad
    {
        double z1 = 0, z2 = 0, z3 = 0, z4 = 0;
        inline double run (double x, const std::array<double, 5>& c)
        {
            const double y = c[0] * x + c[1] * z1 + c[2] * z2 - c[3] * z3 - c[4] * z4;
            z2 = z1; z1 = x; z4 = z3; z3 = y;
            return y;
        }
    };

    /** Bit-exact pass-through when the compressor cannot act: ratio 1, no output change, nothing settling. */
    bool isNeutral() const
    {
        return p.ratio <= 1.0 && p.outputDb == 0.0 && ! p.autoLevel && lookahead == 0
            && grDb == 0.0 && appliedMakeup == 0.0 && outGain == 1.0 && (bypassMix == 0.0 || bypassMix == 1.0);
    }

    void updateCoefficients()
    {
        const double f = std::clamp (p.scHighPassHz, 20.0, fs * 0.45);
        const double w = 2 * 3.14159265358979323846 * f / fs, al = std::sin (w) / (2 * 0.7071), c = std::cos (w), a0 = 1 + al;
        hc = { (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - al) / a0 };
    }

    Params p;
    double fs = 48000.0, atk = 0.9;
    int lookahead = 0, writePos = 0;
    double grDb = 0.0, rmsState = 0.0, sustain = 0.0, peakEnv = 0.0, peakDecay = 0.99;
    int holdSamples = 240, holdLeft = 0;
    double inPower = 1e-12, outPower = 1e-12, makeupDb = 0.0, appliedMakeup = 0.0, outGain = 1.0, bypassMix = 0.0;
    std::array<Biquad, 2> hp {};
    std::array<double, 5> hc {};
    std::array<std::vector<float>, 2> delay;
};

} // namespace aemon::comp
