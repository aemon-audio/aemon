// Aemon core: crossovers for 2 to 5 bands. AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own. Allocation-free.
//
// Linkwitz-Riley filters (24 or 48 dB per octave). Each split leaves the bands in phase with each other; bands
// split off early are passed through the phase shift of the later splits, so all bands add back to a flat level.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace aemon::xover
{

/** State-variable filter (trapezoidal), stable while its frequency moves. */
struct Svf
{
    double g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, s1 = 0, s2 = 0;
    void set (double freq, double q, double fs)
    {
        g = std::tan (3.14159265358979323846 * std::clamp (freq, 5.0, 0.49 * fs) / fs);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void clear() { s1 = s2 = 0; }
    inline void run (double x, double& lo, double& bp, double& hi)
    {
        const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
        lo = v2; bp = v1; hi = x - k * v1 - v2;
    }
    inline double low (double x) { double l, b, h; run (x, l, b, h); return l; }
    inline double high (double x) { double l, b, h; run (x, l, b, h); return h; }
    inline double allpass (double x) { double l, b, h; run (x, l, b, h); return x - 2 * k * b; }
};

/** One split point. slope 24 or 48 dB per octave. */
class Split
{
public:
    void set (double freq, int slopeDb, double fs)
    {
        n = slopeDb >= 48 ? 2 : 1;
        static constexpr double q24[1] { 0.70710678118654752 }, q48[2] { 0.54119610014619698, 1.30656296487637653 };
        for (int i = 0; i < n; ++i)
        {
            const double q = n == 1 ? q24[0] : q48[i];
            for (int r = 0; r < 2; ++r) { lo[i][r].set (freq, q, fs); hi[i][r].set (freq, q, fs); }
            ap[i].set (freq, q, fs);
        }
    }
    void clear() { for (auto& a : lo) for (auto& f : a) f.clear(); for (auto& a : hi) for (auto& f : a) f.clear(); for (auto& f : ap) f.clear(); }
    inline void split (double x, double& low, double& high)
    {
        double l = x, h = x;
        for (int i = 0; i < n; ++i) for (int r = 0; r < 2; ++r) { l = lo[i][r].low (l); h = hi[i][r].high (h); }
        low = l; high = h;
    }
    /** The phase shift of low + high, for bands that do not go through this split. */
    inline double align (double x) { for (int i = 0; i < n; ++i) x = ap[i].allpass (x); return x; }

private:
    int n = 1;
    Svf lo[2][2], hi[2][2], ap[2];
};

/** Splits one channel into up to 5 bands. */
class Bank
{
public:
    static constexpr int maxBands = 5;
    void prepare (double sampleRate) { fs = sampleRate; glide = 1.0 - std::exp (-32.0 / (0.02 * fs)); snap(); clear(); }
    /** freqs: bands - 1 values. They are kept in order and at least a third of an octave apart. */
    void set (int numBands, const double* freqs, int slopeDb)
    {
        bands = std::clamp (numBands, 1, maxBands);
        slope = slopeDb;
        double prev = 0;
        for (int i = 0; i < bands - 1; ++i) { target[(size_t) i] = std::clamp (std::max (freqs[i], prev * 1.26), 20.0, 20000.0); prev = target[(size_t) i]; }
    }
    void snap() { cur = target; apply(); }
    void clear() { for (auto& r : splits) for (auto& s : r) s.clear(); count = 0; }
    int numBands() const { return bands; }

    inline void process (double x, double* out)
    {
        if (count == 0) moveFrequencies();
        count = (count + 1) & 31;
        double rest = x;
        for (int i = 0; i < bands - 1; ++i)
        {
            double low, high;
            splits[(size_t) i][(size_t) i].split (rest, low, high);
            out[i] = low; rest = high;
            for (int b = 0; b < i; ++b) out[b] = splits[(size_t) i][(size_t) b].align (out[b]);
        }
        out[bands - 1] = rest;
    }

private:
    void moveFrequencies()
    {
        bool moved = slope != usedSlope || bands != usedBands;
        for (int i = 0; i < bands - 1; ++i)
        {
            auto& c = cur[(size_t) i]; const double t = target[(size_t) i];
            if (std::abs (c - t) > 1e-3) { c *= std::pow (t / std::max (1.0, c), glide); if (std::abs (c - t) < 0.5e-3 * t) c = t; moved = true; }
        }
        if (moved) apply();
    }
    void apply()
    {
        for (int i = 0; i < bands - 1; ++i) for (int b = 0; b <= i; ++b) splits[(size_t) i][(size_t) b].set (cur[(size_t) i], slope, fs);
        usedSlope = slope; usedBands = bands;
    }
    double fs = 48000, glide = 0.1;
    int bands = 1, slope = 24, usedSlope = 0, usedBands = 0, count = 0;
    std::array<double, maxBands - 1> target { 120, 1000, 4000, 10000 }, cur { 120, 1000, 4000, 10000 };
    // splits[i][i] splits at frequency i; splits[i][b < i] aligns band b
    std::array<std::array<Split, maxBands>, maxBands - 1> splits;
};

} // namespace aemon::xover
