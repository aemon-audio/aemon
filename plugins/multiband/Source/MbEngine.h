// Aemon Multiband: 2 to 5 bands, each compressing down and up. Plain C++, no JUCE, no allocation while processing. AGPL-3.0-or-later.
//
// input -> bands (Linkwitz-Riley, or straight phase) -> per band: detector, gain computer, smoothing -> sum
// Mix and Amount act inside each band, so nothing combs. With every band at rest the output is the input, bit for bit.
#pragma once

#include "aemon_Crossover.h"
#include "aemon_LinearBands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::mb
{

constexpr int maxBands = 5;

struct Band
{
    double threshold = 0, ratio = 1;        // down: above the threshold. ratio >= 20 is a limiter
    double upThreshold = -60, upRatio = 1;  // up: below the threshold
    double attackMs = 20, releaseMs = 150, gainDb = 0;
    bool on = true, solo = false;
};

struct Params
{
    int bands = 4;
    std::array<double, maxBands - 1> split { 120, 800, 5000, 10000 };
    int slope = 24;                 // 24 or 48 dB per octave
    bool straight = false;
    std::array<Band, maxBands> band;
    double kneeDb = 6, lookaheadMs = 0, amount = 1, mix = 1, outputDb = 0, upLimitDb = 12;
    bool autoLevel = false, external = false, bypass = false;

    bool atRest() const
    {
        if (outputDb != 0 || autoLevel) return false;
        for (int b = 0; b < bands; ++b) { const auto& q = band[(size_t) b]; if (q.solo) return false; if (! q.on) continue; if (q.gainDb != 0) return false; if (amount > 0 && mix > 0 && (q.ratio > 1.0 || q.upRatio > 1.0)) return false; }
        return true;
    }
};

/** Change in dB above the threshold (0 or less), soft knee W. */
inline double downGain (double x, double T, double R, double W)
{
    const double slope = R >= 19.95 ? 1.0 : 1.0 - 1.0 / R, d = x - T;
    if (slope <= 0 || 2 * d < -W) return 0;
    if (W > 0 && std::abs (2 * d) <= W) { const double q = d + W / 2; return -slope * q * q / (2 * W); }
    return -slope * d;
}
/** Change in dB below the up threshold (0 or more), at most limit, fading out for near silence so that noise is not raised. */
inline double upGain (double x, double T, double R, double W, double limit)
{
    if (R <= 1.0) return 0;
    const double slope = 1.0 - 1.0 / R, d = T - x;
    double g = 2 * d < -W ? 0 : (W > 0 && std::abs (2 * d) <= W ? slope * (d + W / 2) * (d + W / 2) / (2 * W) : slope * d);
    g = std::min (g, limit);
    const double f = std::clamp ((x + 80.0) / 20.0, 0.0, 1.0);
    return g * f * f * (3 - 2 * f);
}
inline double gainFor (double x, const Band& b, double knee, double upLimit) { return downGain (x, b.threshold, b.ratio, knee) + upGain (x, b.upThreshold, b.upRatio, knee, upLimit); }

/** K weighting (BS.1770) for the level match, any rate. */
class KWeight
{
public:
    void prepare (double fs)
    {
        auto design = [fs] (std::array<double, 5>& c, double f0, double q, double gainDb, bool shelf)
        {
            const double K = std::tan (3.14159265358979323846 * f0 / fs);
            if (shelf) { const double Vh = std::pow (10.0, gainDb / 20.0), Vb = std::pow (Vh, 0.4996667741545416), a0 = 1 + K / q + K * K; c = { (Vh + Vb * K / q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / q + K * K) / a0, 2 * (K * K - 1) / a0, (1 - K / q + K * K) / a0 }; }
            else { const double a0 = 1 + K / q + K * K; c = { 1, -2, 1, 2 * (K * K - 1) / a0, (1 - K / q + K * K) / a0 }; }
        };
        design (s, 1681.974450955533, 0.7071752369554196, 3.999843853973347, true); design (h, 38.13547087602444, 0.5003270373238773, 0, false);
        z = {};
    }
    inline double run (double x) { const double y = s[0] * x + z[0]; z[0] = s[1] * x - s[3] * y + z[1]; z[1] = s[2] * x - s[4] * y; const double o = h[0] * y + z[2]; z[2] = h[1] * y - h[3] * o + z[3]; z[3] = h[2] * y - h[4] * o; return o; }
private:
    std::array<double, 5> s {}, h {}; std::array<double, 4> z {};
};

class Engine
{
public:
    struct Frame { std::array<float, maxBands> level {}, gain {}; };
    /** How long the detector holds, from the lowest frequency of a band. */
    static double holdMs (double low) { return std::clamp (600.0 / low, 2.0, 14.0); }

    void setParams (const Params& p) { want = p; }
    void prepare (double rate)
    {
        fs = rate;
        for (auto& b : bank) b.prepare (fs);
        for (auto& b : lin) b.prepare (fs);
        for (auto& b : scBank) b.prepare (fs);
        const int maxLook = (int) std::ceil (0.010 * fs) + 2;
        ringLen = 1; while (ringLen < maxLook) ringLen <<= 1;
        for (auto& c : ring) for (auto& b : c) b.assign ((size_t) ringLen, 0.0f);
        delayLen = 1; while (delayLen < lin[0].latency() + maxLook + 8) delayLen <<= 1;
        for (auto& d : dry) d.assign ((size_t) delayLen, 0.0f);
        for (auto& k : kIn) k.prepare (fs);
        for (auto& k : kOut) k.prepare (fs);
        fast = 1 - std::exp (-1 / (0.010 * fs)); loudCoef = 1 - std::exp (-1 / (3.0 * fs));
       
        restart();
    }
    /** Structure: what needs a restart under a fade. */
    bool changesLatency (const Params& p) const { return p.bands != cur.bands || p.slope != cur.slope || p.straight != cur.straight || lookOf (p) != look; }
    void applyLatency() { if (changesLatency (want)) restart(); }
    int latencySamples() const { return (cur.straight ? lin[0].latency() : 0) + look; }
    double matchDb() const { return nowMatchDb; }
    Frame takeFrame() noexcept { auto f = frame; frame.level.fill (-120); frame.gain.fill (0); return f; }

    void process (float* L, float* R, int n, const float* scL = nullptr, const float* scR = nullptr) noexcept
    {
        const auto keep = cur;
        cur = want; cur.bands = keep.bands; cur.slope = keep.slope; cur.straight = keep.straight;
        design();
        const int B = cur.bands, rmask = ringLen - 1, dmask = delayLen - 1, lat = latencySamples();
        const bool ext = cur.external && scL != nullptr;
        bool anySolo = false; for (int b = 0; b < B; ++b) anySolo |= cur.band[(size_t) b].solo;
        std::array<double, maxBands> atk {}, rel {}, gainGoal {}, hear {};
        for (int b = 0; b < B; ++b)
        {
            const auto& q = cur.band[(size_t) b];
            atk[(size_t) b] = 1 - std::exp (-1 / (std::max (0.05, q.attackMs) * 0.001 * fs)); rel[(size_t) b] = 1 - std::exp (-1 / (std::max (1.0, q.releaseMs) * 0.001 * fs));
            gainGoal[(size_t) b] = q.on ? q.gainDb : 0.0; hear[(size_t) b] = ! anySolo || q.solo ? 1.0 : 0.0;
        }
        const double restGoal = cur.atRest() ? 1.0 : 0.0, bypGoal = cur.bypass ? 1.0 : 0.0, stepN = 1 / (0.010 * fs), outGoal = std::pow (10.0, cur.outputDb / 20);
        std::array<lin::Bands::C, lin::maxBands> pair; double bl[maxBands] {}, br[maxBands] {}, sl[maxBands] {}, sr[maxBands] {};

        for (int i = 0; i < n; ++i)
        {
            dry[0][(size_t) (write & dmask)] = L[i]; dry[1][(size_t) (write & dmask)] = R[i];
            const float xl = dry[0][(size_t) ((write - lat) & dmask)], xr = dry[1][(size_t) ((write - lat) & dmask)];
            if (cur.straight) { lin[0].push (L[i], R[i], pair.data()); for (int b = 0; b < B; ++b) { bl[b] = pair[(size_t) b].real(); br[b] = pair[(size_t) b].imag(); } }
            else { bank[0].process (L[i], bl); bank[1].process (R[i], br); }
            if (ext)
            {
                if (cur.straight) { lin[1].push (scL[i], scR ? scR[i] : scL[i], pair.data()); for (int b = 0; b < B; ++b) { sl[b] = pair[(size_t) b].real(); sr[b] = pair[(size_t) b].imag(); } }
                else { scBank[0].process (scL[i], sl); scBank[1].process (scR ? scR[i] : scL[i], sr); }
            }

            double ol = 0, orr = 0, plainL = 0, plainR = 0;
            for (int b = 0; b < B; ++b)
            {
                auto& s = st[(size_t) b]; const auto& q = cur.band[(size_t) b];
                const double a = ext ? std::max (std::abs (sl[b]), std::abs (sr[b])) : std::max (std::abs (bl[b]), std::abs (br[b]));
                if (a >= s.peak) { s.peak = a; s.hold = hold[(size_t) b]; } else if (s.hold > 0) --s.hold; else s.peak = std::max (a, s.peak * decay[(size_t) b]);
                const double x = 20 * std::log10 (s.peak + 1e-7);
                const double target = q.on ? gainFor (x, q, cur.kneeDb, cur.upLimitDb) : 0.0;
                s.g += (target - s.g) * (target < s.g ? atk[(size_t) b] : rel[(size_t) b]);
                if (std::abs (s.g) < 1e-6 && target == 0.0) s.g = 0;
                s.trim += fast * (gainGoal[(size_t) b] - s.trim); if (std::abs (s.trim - gainGoal[(size_t) b]) < 1e-6) s.trim = gainGoal[(size_t) b];
                s.hear += fast * (hear[(size_t) b] - s.hear); if (std::abs (s.hear - hear[(size_t) b]) < 1e-7) s.hear = hear[(size_t) b];
                amount += amountStep * (cur.amount - amount); mix += amountStep * (cur.mix - mix);
                if (std::abs (amount - cur.amount) < 1e-7) amount = cur.amount;
                if (std::abs (mix - cur.mix) < 1e-7) mix = cur.mix;
                frame.level[(size_t) b] = std::max (frame.level[(size_t) b], (float) x);
                const double shown = s.g * amount; if (std::abs (shown) > std::abs (frame.gain[(size_t) b])) frame.gain[(size_t) b] = (float) shown;

                double al = bl[b], ar = br[b];
                if (look > 0) { auto& r0 = ring[0][(size_t) b]; auto& r1 = ring[1][(size_t) b]; r0[(size_t) (write & rmask)] = (float) al; r1[(size_t) (write & rmask)] = (float) ar; al = r0[(size_t) ((write - look) & rmask)]; ar = r1[(size_t) ((write - look) & rmask)]; }
                plainL += al; plainR += ar;
                const double g = (1.0 + mix * (std::exp (s.g * amount * 0.11512925464970229) - 1.0)) * std::exp (s.trim * 0.11512925464970229) * s.hear;
                ol += al * g; orr += ar * g;
            }
            ++write;

            if (cur.autoLevel && ! anySolo)
            {
                const double a = kIn[0].run (plainL), b = kIn[1].run (plainR), c = kOut[0].run (ol), d = kOut[1].run (orr);
                powIn += loudCoef * (a * a + b * b - powIn); powOut += loudCoef * (c * c + d * d - powOut);
                if (powIn > 1e-8 && powOut > 1e-9) matchGoal = std::clamp (10 * std::log10 (powIn / powOut), -18.0, 18.0);
            }
            nowMatchDb += fast * ((cur.autoLevel && ! anySolo ? matchGoal : 0.0) - nowMatchDb); if (! cur.autoLevel && std::abs (nowMatchDb) < 1e-6) nowMatchDb = 0;
            out += fast * (outGoal - out); if (std::abs (out - outGoal) < 1e-8) out = outGoal;
            const double k = out * std::exp (nowMatchDb * 0.11512925464970229);
            ol *= k; orr *= k;

            // at rest (and nothing still letting go): the input itself
            bool quiet = restGoal > 0; if (quiet) for (int b = 0; b < B; ++b) { const auto& s = st[(size_t) b]; quiet &= (s.g == 0 || amount * mix == 0) && s.trim == 0 && s.hear == 1; } quiet = quiet && nowMatchDb == 0;
            const double goal = bypGoal > 0 || quiet ? 1.0 : 0.0;
            clean = goal > clean ? std::min (goal, clean + stepN) : std::max (goal, clean - stepN);
            if (clean >= 1) { L[i] = xl; R[i] = xr; }
            else { L[i] = (float) (ol + clean * (xl - ol)); R[i] = (float) (orr + clean * (xr - orr)); }
        }
    }

private:
    struct State { double peak = 0, g = 0, trim = 0, hear = 1; int hold = 0; };
    int lookOf (const Params& p) const { return (int) std::lround (std::clamp (p.lookaheadMs, 0.0, 10.0) * 0.001 * fs); }
    void restart()
    {
        cur = want; look = lookOf (cur);
        for (auto& b : bank) { b.set (cur.bands, cur.split.data(), cur.slope); b.snap(); b.clear(); }
        for (auto& b : scBank) { b.set (cur.bands, cur.split.data(), cur.slope); b.snap(); b.clear(); }
        for (auto& b : lin) b.prepare (fs);
        for (auto& c : ring) for (auto& b : c) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& d : dry) std::fill (d.begin(), d.end(), 0.0f);
        st = {}; for (size_t b = 0; b < maxBands; ++b) { st[b].trim = cur.band[b].on ? cur.band[b].gainDb : 0.0; }
        powIn = powOut = 1e-9; matchGoal = nowMatchDb = 0; out = std::pow (10.0, cur.outputDb / 20);
        amount = cur.amount; mix = cur.mix; clean = cur.atRest() || cur.bypass ? 1.0 : 0.0; write = 0;
        amountStep = 1 - std::exp (-1 / (0.02 * fs * maxBands));
        design(); takeFrame();
    }
    void design()
    {
        for (auto& b : bank) b.set (cur.bands, cur.split.data(), cur.slope);
        for (auto& b : scBank) b.set (cur.bands, cur.split.data(), cur.slope);
        lin::Spec s; s.bands = cur.bands; s.order = cur.slope >= 48 ? 8 : 4; double prev = 0; for (int i = 0; i < cur.bands - 1; ++i) { s.split[(size_t) i] = std::clamp (std::max (cur.split[(size_t) i], prev * 1.26), 20.0, 20000.0); prev = s.split[(size_t) i]; }
        for (auto& b : lin) b.design (s);
        // peak detector: holds long enough to see the next half wave of the lowest sound in the band
        for (int b = 0; b < cur.bands; ++b) { const double f = b == 0 ? 35.0 : cur.split[(size_t) (b - 1)], ms = holdMs (f); hold[(size_t) b] = (int) (ms * 0.001 * fs); decay[(size_t) b] = std::exp (-1 / (ms * 0.001 * fs)); }
    }
    Params want, cur;
    std::array<xover::Bank, 2> bank, scBank; std::array<lin::Bands, 2> lin;
    std::array<std::array<std::vector<float>, maxBands>, 2> ring; std::array<std::vector<float>, 2> dry;
    std::array<State, maxBands> st; std::array<int, maxBands> hold {}; std::array<double, maxBands> decay {};
    std::array<KWeight, 2> kIn, kOut; Frame frame;
    double fs = 48000, fast = 0.01, loudCoef = 0, powIn = 0, powOut = 0, matchGoal = 0, nowMatchDb = 0, out = 1, amount = 1, mix = 1, amountStep = 0, clean = 1;
    int look = 0, ringLen = 512, delayLen = 4096; unsigned write = 0;
};

} // namespace aemon::mb
