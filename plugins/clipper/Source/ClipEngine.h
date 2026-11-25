// Aemon Clipper: signal engine. AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own. Allocation-free once prepared.
//
// Push, clip on the clean distortion core (only the difference is oversampled, so untouched sound stays
// bit-exact), then a ceiling guard: band-limiting makes a clean clipper overshoot a little between the flat
// tops, and the guard takes those tips down (true peak) with a smooth gain instead of cutting them again.
#pragma once

#include "aemon_Distortion.h"
#include "aemon_TruePeak.h"

namespace aemon::clip
{

struct Params
{
    double pushDb = 0.0;        // 0..18
    double ceilingDb = -1.0;    // -12..0
    double softness = 0.35;     // 0 hard .. 1 soft
    double link = 1.0;          // 0..1
    double mix = 1.0;           // 0..1
    int oversampling = 8;       // 1, 2, 4, 8
    bool delta = false;         // hear only what is removed
    bool bypass = false;
};

class Engine
{
public:
    using Peak = dist::TruePeakDetector<32, 4, 70>;
    static constexpr int guardLook = 8;       // samples the guard looks ahead
    static constexpr double guardHoldMs = 12.0, guardReleaseMs = 45.0;
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        setup();
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            os[c].clear(); shaper[c].reset();
            std::fill (delay[c].begin(), delay[c].end(), 0.0);
            gDelay[c].fill (0.0); gDry[c].fill (0.0);
            need[c].fill (1.0); held[c].fill (1.0); guard[c] = recent[c] = 1.0; hold[c] = 0; peak[c].reset(); clippedLately[c] = 0;
        }
        joint.reset();
        pos = gp = 0; tick = 0;
        push = gainOf (p.pushDb); ceil = gainOf (p.ceilingDb); mix = p.mix; link = p.link;
        curve.knee = p.softness;
        deltaMix = p.delta ? 1.0 : 0.0; bypassMix = p.bypass ? 1.0 : 0.0;
        clipped = 0; deepest = deepestIn = 0.0;
    }
    void setParams (const Params& np)
    {
        const bool factor = np.oversampling != p.oversampling;
        p = np;
        if (factor && fs > 0) { setup(); reset(); }
    }
    bool changesLatency (const Params& np) const { return np.oversampling != p.oversampling; }
    int latencySamples() const { return dist::Oversampler::latencyFor (p.oversampling) + guardLook + Peak::delay(); }
    /** Since the last call: the most taken off a peak (dB, >= 0) and how many samples were clipped. */
    void takeStats (double& offDb, int& samples) { offDb = deepest; samples = clipped; deepest = deepestIn = 0; clipped = 0; }

    void process (float* Lp, float* Rp, int n)
    {
        const double glide = 1.0 - std::exp (-1.0 / (0.02 * fs));
        const double tPush = gainOf (p.pushDb), tCeil = gainOf (p.ceilingDb), guardRel = 1.0 - std::exp (-1.0 / (guardReleaseMs * 0.001 * fs));
        const int holdLen = (int) (guardHoldMs * 0.001 * fs);
        for (int i = 0; i < n; ++i)
        {
            push = move (push, tPush, glide); ceil = move (ceil, tCeil, glide);
            mix = move (mix, p.mix, glide); link = move (link, p.link, glide);
            deltaMix = move (deltaMix, p.delta ? 1.0 : 0.0, glide); bypassMix = move (bypassMix, p.bypass ? 1.0 : 0.0, glide);
            if (--tick <= 0)
            {
                tick = 16;
                const double k = curve.knee + 0.2 * (p.softness - curve.knee), k2 = std::abs (k - p.softness) < 1e-4 ? p.softness : k;
                if (k2 != curve.knee) { curve.knee = k2; for (auto& s : shaper) s.refresh (curve); joint.refresh (curve); }
            }
            const double in[2] { Lp[i], Rp[i] };
            double up[2][8], diff[2];
            const double norm = push / ceil;
            for (int c = 0; c < 2; ++c) os[c].up (in[c] * norm, up[c]);
            const int f = os[0].factor();
            if (link >= 1.0 || link <= 0.0)
                for (int k = 0; k < f; ++k)
                {
                    const double l = up[0][k], r = up[1][k], m = std::max (std::abs (l), std::abs (r));
                    if (link >= 1.0) { const double ratio = m > 1e-9 ? joint (m, curve) / m : (joint (m, curve), 0.0); shaper[0].skip (l); shaper[1].skip (r); up[0][k] = l * ratio; up[1][k] = r * ratio; }
                    else { joint.skip (m); up[0][k] = shaper[0] (l, curve); up[1][k] = shaper[1] (r, curve); }
                }
            else for (int k = 0; k < f; ++k)
            {
                const double l = up[0][k], r = up[1][k], m = std::max (std::abs (l), std::abs (r));
                const double dj = joint (m, curve), dl = shaper[0] (l, curve), dr = shaper[1] (r, curve);
                const double ratio = m > 1e-9 ? dj / m : 0.0;
                up[0][k] = dl + link * (l * ratio - dl);
                up[1][k] = dr + link * (r * ratio - dr);
            }
            for (int c = 0; c < 2; ++c) diff[c] = os[c].down (up[c]);
            for (int c = 0; c < 2; ++c) delay[c][(size_t) pos] = in[c];
            pos = (pos + 1) % (int) delay[0].size();

            double raw[2], wet[2], q[2];
            for (int c = 0; c < 2; ++c)
            {
                raw[c] = delay[c][(size_t) pos];                        // the input, lined up
                wet[c] = raw[c] * push + diff[c] * ceil;                // exact when nothing is clipped and Push is 0
                clippedLately[c] = std::abs (diff[c]) > 0.0 ? 4096 : std::max (0, clippedLately[c] - 1);
                double top = std::abs (wet[c]);
                if (clippedLately[c] > 0) top = peak[c].push (wet[c]); else peak[c].skip (wet[c]);
                q[c] = top > ceil ? ceil / top : 1.0;
                if (std::abs (diff[c]) > 1e-4) ++clipped;
                const double was = std::abs (raw[c] * push) / ceil;     // what the curve takes off this peak
                if (was > deepestIn) { deepestIn = was; deepest = std::max (deepest, 20.0 * std::log10 (was / std::max (1e-6, curve.f (was)))); }
            }
            const double qj = std::min (q[0], q[1]);
            // the guard: minimum over guardLook + 1 samples, averaged over guardLook, short release
            for (int c = 0; c < 2; ++c)
            {
                const double need1 = q[c] + link * (qj - q[c]);
                need[c][(size_t) gp] = need1;
                double h = 1.0; for (int k = 0; k <= guardLook; ++k) h = std::min (h, need[c][(size_t) ((gp - k) & mask)]);
                held[c][(size_t) gp] = h;
                double sum = 0; for (int k = 0; k < guardLook; ++k) sum += held[c][(size_t) ((gp - k) & mask)];
                const double G = std::min (1.0, sum / guardLook);
                // hold for longer than one wave, so the gain stands still on a steady sound instead of following it
                if (G <= guard[c]) { guard[c] = G; hold[c] = holdLen; }
                else if (G < 1.0 - 1e-6 && G <= recent[c] * 1.0005) { hold[c] = std::max (hold[c], holdLen / 2); }
                else if (hold[c] > 0) --hold[c];
                else guard[c] = std::min (G, guard[c] + (1.0 - guard[c]) * guardRel);
                recent[c] = guard[c];
                if (guard[c] > 1.0 - 1e-9) guard[c] = 1.0;
                gDelay[c][(size_t) gp] = wet[c]; gDry[c][(size_t) gp] = raw[c];
                const size_t o = (size_t) ((gp - guardLook - Peak::delay()) & mask);
                const double dry = gDry[c][o], loud = dry * push;
                double w = gDelay[c][o] * guard[c];
                w = std::clamp (w, -ceil, ceil);
                double y = mix >= 1.0 ? w : loud + mix * (w - loud);
                if (deltaMix > 0.0) y += deltaMix * ((loud - w) - y);
                if (bypassMix > 0.0) y = bypassMix >= 1.0 ? dry : y + bypassMix * (dry - y);
                (c ? Rp : Lp)[i] = (float) y;
            }
            gp = (gp + 1) & mask;
        }
    }

private:
    static double gainOf (double db) { return std::pow (10.0, db / 20.0); }
    static double move (double v, double t, double k) { v += k * (t - v); return std::abs (v - t) < 1e-7 ? t : v; }
    void setup()
    {
        for (int c = 0; c < 2; ++c) { os[c].prepare (p.oversampling); delay[c].reserve (160); delay[c].assign ((size_t) os[c].latency() + 1, 0.0); }   // room for every factor: no allocation when it changes
        pos = 0;
    }
    Params p;
    double fs = 0;
    dist::ClipCurve curve;
    dist::Oversampler os[2];
    Peak peak[2];
    int clippedLately[2] { 0, 0 };
    dist::DeltaShaper<dist::ClipCurve> shaper[2], joint;
    std::vector<double> delay[2];
    static constexpr int size = 64, mask = size - 1;
    static_assert (guardLook + Peak::delay() + 1 < size);
    std::array<double, size> gDelay[2], gDry[2], need[2], held[2];
    double guard[2] { 1, 1 }, recent[2] { 1, 1 };
    int hold[2] { 0, 0 };
    double push = 1, ceil = 1, mix = 1, link = 1, deltaMix = 0, bypassMix = 0, deepest = 0, deepestIn = 0;
    int pos = 0, gp = 0, tick = 0, clipped = 0;
};

} // namespace aemon::clip
