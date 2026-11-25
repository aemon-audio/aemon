// Aemon core: straight-phase bands that add back exactly. Plain C++, no JUCE, no allocation while processing. AGPL-3.0-or-later.
//
// Two real signals go through one complex transform (left + i right, or side + i middle): the filters are real in
// time, so the two come out again as real and imaginary part. A new design is made at most every 21 ms and fades in.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace aemon::lin
{

constexpr int maxBands = 5;
constexpr double pi = 3.14159265358979323846;

/** What the bands are. order 4 is 24 dB per octave, 8 is 48. Below lowCut nothing passes (20 and under: off). */
struct Spec
{
    int bands = 4, order = 4;
    std::array<double, maxBands - 1> split { 150, 1500, 6000, 12000 };
    double lowCut = 20.0;
    bool operator== (const Spec& o) const { return bands == o.bands && order == o.order && split == o.split && lowCut == o.lowCut; }
    bool operator!= (const Spec& o) const { return ! (*this == o); }
    /** Gain of a band at a frequency. All bands add up to one (except under lowCut). */
    double gain (int b, double f) const
    {
        double rest = 1.0, g = 0.0;
        for (int i = 0; i <= b; ++i)
        {
            double l = 1.0;
            if (i < bands - 1) { double q = f / split[(size_t) i]; q *= q; q *= q; if (order >= 8) q *= q; l = 1.0 / (1.0 + q); }
            g = rest * l; rest *= 1.0 - l;
        }
        if (lowCut > 20.0) { double q = f / lowCut; q *= q; q *= q; q *= q; g *= q / (1.0 + q); }
        return g;
    }
};

class Fft
{
public:
    using C = std::complex<double>;
    void init (int size)
    {
        n = size; tw.resize ((size_t) n / 2); rev.resize ((size_t) n);
        for (int k = 0; k < n / 2; ++k) tw[(size_t) k] = std::polar (1.0, -2.0 * pi * k / n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
    }
    void forward (C* x) const { run (x, false); }
    void inverse (C* x) const { run (x, true); const double s = 1.0 / n; for (int i = 0; i < n; ++i) x[i] *= s; }
private:
    void run (C* x, bool inv) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const C w = tw[(size_t) (k * step)], u = x[i + k], q = x[i + k + half];
                    const double wi = inv ? -w.imag() : w.imag();
                    const C v (q.real() * w.real() - q.imag() * wi, q.real() * wi + q.imag() * w.real());
                    x[i + k] = u + v; x[i + k + half] = u - v;
                }
        }
    }
    int n = 0; std::vector<C> tw; std::vector<int> rev;
};

class Bands
{
public:
    using C = std::complex<double>;
    void prepare (double rate)
    {
        fs = rate; const int scale = fs <= 50000 ? 1 : (fs <= 100000 ? 2 : 4);
        L = 4096 * scale; P = 256 * scale; F = 2 * P; parts = L / P;
        big.init (L); small.init (F);
        spec.assign ((size_t) L, {}); hist.assign ((size_t) (parts * F), {}); acc.assign ((size_t) F, {}); buf.assign ((size_t) F, {});
        for (auto& k : kernel) for (auto& b : k) b.assign ((size_t) (parts * F), {});
        in.assign ((size_t) F, {});
        for (auto& o : out) o.assign ((size_t) P, {});
        window.resize ((size_t) L); for (int i = 0; i < L; ++i) window[(size_t) i] = 0.5 - 0.5 * std::cos (2 * pi * i / L);
        fill = head = 0; use = 0; wait = 0; fresh = true;
    }
    int latency() const { return L / 2 + P; }
    void design (const Spec& s) { want = s; }
    /** Takes the pair (x, y) in, gives the bands of the pair from latency() samples ago. */
    inline void push (double x, double y, C* bandsOut)
    {
        for (int b = 0; b < want.bands; ++b) bandsOut[b] = out[(size_t) b][(size_t) fill];
        in[(size_t) (P + fill)] = { x, y };
        if (++fill == P) { fill = 0; block(); }
    }

private:
    void build (int k)
    {
        have = want;
        for (int first = 0; first < have.bands; first += 2)
        {
            const bool two = first + 1 < have.bands;
            for (int i = 0; i <= L / 2; ++i)
            {
                const double f = i * fs / L; const C v (have.gain (first, f), two ? have.gain (first + 1, f) : 0.0);
                spec[(size_t) i] = v; if (i > 0 && i < L / 2) spec[(size_t) (L - i)] = v;
            }
            big.inverse (spec.data());      // two zero-phase kernels, one real, one imaginary
            for (int p = 0; p < parts; ++p)
            {
                std::fill (buf.begin(), buf.end(), C {});
                for (int i = 0; i < P; ++i) { const int t = p * P + i; buf[(size_t) i] = spec[(size_t) ((t + L / 2) & (L - 1))] * window[(size_t) t]; }
                small.forward (buf.data());
                auto* a = kernel[(size_t) k][(size_t) first].data() + p * F; auto* b = two ? kernel[(size_t) k][(size_t) (first + 1)].data() + p * F : nullptr;
                for (int i = 0; i < F; ++i) { const C z = buf[(size_t) i], c = std::conj (buf[(size_t) ((F - i) & (F - 1))]); a[i] = 0.5 * (z + c); if (b) { const C d = 0.5 * (z - c); b[i] = C (d.imag(), -d.real()); } }
            }
        }
    }
    void render (int k, int b, std::vector<C>& to, bool fade)
    {
        std::fill (acc.begin(), acc.end(), C {});
        const auto& ker = kernel[(size_t) k][(size_t) b];
        for (int p = 0; p < parts; ++p)
        {
            const auto* h = hist.data() + ((head + p) % parts) * F; const auto* q = ker.data() + p * F;
            for (int i = 0; i < F; ++i) { const double ar = h[i].real(), ai = h[i].imag(), br = q[i].real(), bi = q[i].imag(); acc[(size_t) i] += C (ar * br - ai * bi, ar * bi + ai * br); }
        }
        small.inverse (acc.data());
        for (int i = 0; i < P; ++i)
            if (! fade) to[(size_t) i] = acc[(size_t) (P + i)];
            else { const double t = 0.5 - 0.5 * std::cos (pi * (i + 0.5) / P); to[(size_t) i] = to[(size_t) i] * (1 - t) + acc[(size_t) (P + i)] * t; }
    }
    void block()
    {
        head = (head + parts - 1) % parts;
        auto* slot = hist.data() + head * F;
        std::copy (in.begin(), in.end(), slot);
        small.forward (slot);
        std::copy (in.begin() + P, in.end(), in.begin());
        if (fresh) { build (use); fresh = false; }
        if (wait > 0) --wait;
        const bool change = wait == 0 && want != have;
        for (int b = 0; b < have.bands; ++b) render (use, b, out[(size_t) b], false);
        if (change)
        {
            for (int b = have.bands; b < want.bands; ++b) std::fill (out[(size_t) b].begin(), out[(size_t) b].end(), C {});
            build (1 - use); use = 1 - use; wait = 4;
            for (int b = 0; b < have.bands; ++b) render (use, b, out[(size_t) b], true);
        }
    }
    double fs = 48000; int L = 4096, P = 256, F = 512, parts = 16, fill = 0, head = 0, use = 0, wait = 0; bool fresh = true;
    Fft big, small;
    std::vector<C> spec, hist, acc, buf, in;
    std::array<std::array<std::vector<C>, maxBands>, 2> kernel;
    std::array<std::vector<C>, maxBands> out;
    std::vector<double> window;
    Spec want, have;
};

} // namespace aemon::lin
