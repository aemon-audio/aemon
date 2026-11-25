// Aemon Imager: width without touching the mono sum. Plain C++, no JUCE, no allocation while processing. AGPL-3.0-or-later.
//
// Middle M = (L + R) / 2 is what mono keeps, side S = (L - R) / 2 is what mono loses. Only the side is ever touched:
//   X = S - balance * M + make * D(M)        (D: a diffuse copy of the middle without bass)
//   S' = sum over four bands of width * safe gain * X_band,      L = M + S',  R = M - S'
// so L + R is the input's L + R in every setting (to the last bit a float holds).
// Straight: the bands are zero-phase FIR filters, the middle is only delayed. Zero: gentle filters without delay. Both add back exactly.
#pragma once

#include "aemon_LinearBands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace aemon::img
{

constexpr int bands = 4;
constexpr double pi = 3.14159265358979323846, monoOff = 20.0;

struct Params
{
    double width = 1.0;                                 // 0..2
    std::array<double, bands> band { 1, 1, 1, 1 };      // 0..2 each
    std::array<double, 3> split { 150, 1500, 6000 };
    double monoHz = monoOff;                            // the side below this goes; 20 and under: off
    double make = 0.0;                                  // 0..1
    double balance = 0.0;                               // -0.5..0.5: how much of the middle hides in the side
    bool safe = false; double limitDb = 3.0;            // a band may lose this much in mono
    bool zeroDelay = false;
    bool hearMono = false, hearSide = false, bypass = false;

    bool neutral() const { bool b = width == 1.0 && monoHz <= monoOff && make <= 0.0 && balance == 0.0 && ! safe; for (double w : band) b = b && w == 1.0; return b; }
};

/** Band gain at a frequency (Straight). They add up to one, except below the mono frequency. */
inline lin::Spec spec (const std::array<double, 3>& split, double monoHz) { lin::Spec s; s.split = { split[0], split[1], split[2], 12000 }; s.lowCut = monoHz; return s; }
inline double bandGain (int b, double f, const std::array<double, 3>& split, double monoHz) { return spec (split, monoHz).gain (b, f); }

struct Hp2
{
    void set (double f, double fs) { const double g = std::tan (pi * std::min (f, fs * 0.45) / fs); k = std::sqrt (2.0); a1 = 1 / (1 + g * (g + k)); gg = g; }
    inline double run (double x) { const double v1 = a1 * (a + gg * (x - b)), v2 = b + gg * v1; a = 2 * v1 - a; b = 2 * v2 - b; return x - k * v1 - v2; }
    void reset() { a = b = 0; }
    double k = 1.414, a1 = 1, gg = 0, a = 0, b = 0;
};

/** Gentle bands without delay: each takes a first-order low-pass off what is left, so they add back exactly. */
class ZeroBands
{
public:
    void prepare (double rate) { fs = rate; for (auto& c : ch) { c.z = {}; c.hp.reset(); } tick = 0; first = true; coef = 1.0 - std::exp (-1.0 / (0.015 * fs)); slow = 1.0 - std::exp (-32.0 / (0.02 * fs)); }
    void design (const std::array<double, 3>& split, double monoHz)
    {
        for (size_t i = 0; i < 3; ++i) goal[i] = 1.0 - std::exp (-2 * pi * split[i] / fs);
        mono = monoHz > monoOff; goalHz = std::max (monoHz, monoOff);
        if (first) { a = goal; hz = goalHz; blend = mono ? 1.0 : 0.0; for (auto& c : ch) c.hp.set (hz, fs); first = false; }
    }
    /** Once per sample, before the two pushes: everything glides. */
    inline void glide()
    {
        for (size_t i = 0; i < 3; ++i) a[i] += coef * (goal[i] - a[i]);
        blend += coef * ((mono ? 1.0 : 0.0) - blend);
        if (std::abs (blend - (mono ? 1.0 : 0.0)) < 1e-7) blend = mono ? 1.0 : 0.0;
        if (++tick >= 32) { tick = 0; if (hz != goalHz) { hz *= std::exp (std::log (goalHz / hz) * slow); if (std::abs (hz - goalHz) < 0.01) hz = goalHz; for (auto& c : ch) c.hp.set (hz, fs); } }
    }
    inline void push (int c, double x, std::array<double, bands>& o)
    {
        auto& s = ch[(size_t) c];
        const double h = s.hp.run (x);
        double r = blend <= 0.0 ? x : x + blend * (h - x);
        for (size_t i = 0; i < 3; ++i) { s.z[i] += a[i] * (r - s.z[i]); o[i] = s.z[i]; r -= s.z[i]; }
        o[3] = r;
    }
private:
    struct Ch { std::array<double, 3> z {}; Hp2 hp; };
    std::array<Ch, 2> ch; std::array<double, 3> a {}, goal {}; double fs = 48000, coef = 0.01, slow = 0.1, blend = 0, hz = monoOff, goalHz = monoOff; int tick = 0; bool mono = false, first = true;
};

/** A diffuse copy of the middle that has little to do with it any more: short all-pass echoes, no bass. */
class Diffuser
{
public:
    void prepare (double fs)
    {
        static constexpr double ms[] = { 0.71, 1.13, 1.79, 2.51, 3.67, 4.93 };
        for (size_t i = 0; i < 6; ++i) { buf[i].assign ((size_t) std::max (1, (int) std::lround (ms[i] * 0.001 * fs)), 0.0); pos[i] = 0; }
        hp.set (180, fs); hp.reset();
    }
    inline double run (double m)
    {
        double x = hp.run (m);
        for (size_t i = 0; i < 6; ++i)
        {
            const double g = i % 2 ? -0.55 : 0.55, z = buf[i][(size_t) pos[i]], v = x - g * z;
            buf[i][(size_t) pos[i]] = v; x = z + g * v;
            if (++pos[i] == (int) buf[i].size()) pos[i] = 0;
        }
        return x;
    }
private:
    std::array<std::vector<double>, 6> buf; std::array<int, 6> pos {}; Hp2 hp;
};

class Engine
{
public:
    void setParams (const Params& p) { want = p; }
    void prepare (double rate)
    {
        fs = rate;
        straight.prepare (fs); zeroB.prepare (fs); diffuse.prepare (fs);
        zero = want.zeroDelay;
        delayLen = 1; while (delayLen < straight.latency() + 8) delayLen <<= 1;
        dl.assign ((size_t) delayLen, 0.0f); dr = dl; dx.assign ((size_t) delayLen, 0.0);
        write = 0; coef = 1.0 - std::exp (-1.0 / (0.015 * fs));
        hop = std::max (1, (int) std::lround (0.01 * fs)); powerCoef = 1.0 - std::exp (-0.01 / 0.1);
        hopPos = 0; pm.fill (0); px.fill (0); accM.fill (0); accX.fill (0); gFrom.fill (1); gTo.fill (1);
        for (int b = 0; b < bands; ++b) w[(size_t) b] = want.width * want.band[(size_t) b];
        lean = 1.0 - std::exp (-1.0 / (0.06 * fs)); dm = mm = 0;
        bal = want.balance; make = want.make; neutral = want.neutral() ? 1.0 : 0.0;
        monitor = {}; monitor[(size_t) index (want)] = 1.0;
        heldDb.fill (0);
        design();
    }
    bool changesLatency (const Params& p) const { return p.zeroDelay != zero; }
    void applyLatency() { if (zero == want.zeroDelay) return; zero = want.zeroDelay; straight.prepare (fs); zeroB.prepare (fs); std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f); std::fill (dx.begin(), dx.end(), 0.0); design(); }
    int latencySamples() const { return zero ? 0 : straight.latency(); }
    /** How far the safe limit is holding a band down, dB. */
    double holdingDb (int b) const { return heldDb[(size_t) b]; }

    void process (float* L, float* R, int n) noexcept
    {
        design();
        const int mask = delayLen - 1, lat = latencySamples();
        const double stepN = 1.0 / (0.01 * fs), stepM = 1.0 / (0.008 * fs);
        const double goalN = want.neutral() ? 1.0 : 0.0;
        const int target = index (want);
        std::array<std::complex<double>, bands> pair; std::array<double, bands> bx {}, bm {};
        for (int i = 0; i < n; ++i)
        {
            const double m = 0.5 * ((double) L[i] + R[i]), s = 0.5 * ((double) L[i] - R[i]);
            bal += coef * (want.balance - bal); make += coef * (want.make - make);
            if (std::abs (make - want.make) < 1e-7) make = want.make;
            if (std::abs (bal - want.balance) < 1e-7) bal = want.balance;
            double d = diffuse.run (m);
            // the part of it that still follows the middle would pull the image to one side: take it out
            dm += lean * (d * m - dm); mm += lean * (m * m - mm); d -= dm / (mm + 1e-9) * m;
            const double x = s - bal * m + make * d;
            dl[(size_t) (write & mask)] = L[i]; dr[(size_t) (write & mask)] = R[i];
            const float xl = dl[(size_t) ((write - lat) & mask)], xr = dr[(size_t) ((write - lat) & mask)];
            ++write;
            if (zero) { zeroB.glide(); zeroB.push (0, x, bx); zeroB.push (1, m, bm); }
            else { straight.push (x, m, pair.data()); for (size_t b = 0; b < bands; ++b) { bx[b] = pair[b].real(); bm[b] = pair[b].imag(); } }

            const double t = (double) hopPos / hop; double side = 0;
            for (size_t b = 0; b < bands; ++b)
            {
                w[b] += coef * (want.width * want.band[b] - w[b]);
                const double v = w[b] * bx[b];
                accM[b] += bm[b] * bm[b]; accX[b] += v * v;
                side += v * (gFrom[b] + (gTo[b] - gFrom[b]) * t);
            }
            if (++hopPos >= hop) safeStep();

            const double sum = (double) xl + xr;
            double ol = 0.5 * sum + side, orr = 0.5 * sum - side;
            neutral = goalN > neutral ? std::min (goalN, neutral + stepN) : std::max (goalN, neutral - stepN);
            if (neutral >= 1.0) { ol = xl; orr = xr; }
            else if (neutral > 0.0) { const double q = side + (0.5 * ((double) xl - xr) - side) * neutral; ol = 0.5 * sum + q; orr = 0.5 * sum - q; }
            const float fl = (float) ol;
            const float fr = neutral >= 1.0 ? xr : (float) (sum - fl);      // so that L + R is the input's, whatever L was rounded to

            if (monitor[(size_t) target] >= 1.0) emit (target, L[i], R[i], xl, xr, fl, fr);
            else
            {
                double rest = 0; for (int q = 0; q < 4; ++q) if (q != target) { monitor[(size_t) q] = std::max (0.0, monitor[(size_t) q] - stepM); rest += monitor[(size_t) q]; }
                monitor[(size_t) target] = rest <= 0 ? 1.0 : std::max (0.0, 1.0 - rest);
                double a = 0, b = 0; for (int q = 0; q < 4; ++q) if (monitor[(size_t) q] > 0) { float u, v; emit (q, u, v, xl, xr, fl, fr); a += monitor[(size_t) q] * u; b += monitor[(size_t) q] * v; }
                L[i] = (float) a; R[i] = (float) b;
            }
        }
    }

private:
    static int index (const Params& p) { return p.bypass ? 1 : (p.hearMono ? 2 : (p.hearSide ? 3 : 0)); }
    static inline void emit (int kind, float& a, float& b, float xl, float xr, float l, float r)
    {
        switch (kind) { case 1: a = xl; b = xr; break; case 2: a = b = 0.5f * (l + r); break; case 3: a = b = 0.5f * (l - r); break; default: a = l; b = r; }
    }
    void design() { if (zero) zeroB.design (want.split, want.monoHz); else straight.design (spec (want.split, want.monoHz)); }
    void safeStep() noexcept
    {
        hopPos = 0;
        const double cap = std::pow (10.0, want.limitDb / 10.0) - 1.0;
        for (size_t b = 0; b < bands; ++b)
        {
            pm[b] += (accM[b] - pm[b]) * powerCoef; px[b] += (accX[b] - px[b]) * powerCoef; accM[b] = accX[b] = 0;
            gFrom[b] = gTo[b];
            gTo[b] = want.safe ? std::min (1.0, std::sqrt ((cap * pm[b] + 1e-14) / (px[b] + 1e-14))) : std::min (1.0, gTo[b] + 0.1);
            heldDb[b] = -20.0 * std::log10 (std::max (1e-3, gTo[b]));
        }
    }
    Params want;
    lin::Bands straight; ZeroBands zeroB; Diffuser diffuse;
    std::vector<float> dl, dr; std::vector<double> dx;
    std::array<double, bands> w {}, pm {}, px {}, accM {}, accX {}, gFrom {}, gTo {}, heldDb {};
    std::array<double, 4> monitor { 1, 0, 0, 0 };
    double fs = 48000, coef = 0.01, powerCoef = 0.1, bal = 0, make = 0, neutral = 1, lean = 0.0001, dm = 0, mm = 0;
    int hop = 480, hopPos = 0, delayLen = 4096; unsigned write = 0; bool zero = false;
};

} // namespace aemon::img
