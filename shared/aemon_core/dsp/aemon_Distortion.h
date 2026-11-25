// Aemon core: clean distortion. AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own. Allocation-free once prepared.
//
// Distortion creates harmonics above the audio range, which fold back as false tones (aliasing). Two things
// prevent that here: oversampling (2x to 8x, linear phase) whose last filter is fully closed before the base
// Nyquist frequency, and first-order antiderivative anti-aliasing (ADAA) of the curve itself.
//
// Only the difference between the curve and a straight line goes through the oversampling. The original signal
// is delayed and added back, so where the curve is straight the output is bit-exact, and the filters never
// colour the sound.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::dist
{

/** Linear-phase FIR lowpass as a 2x interpolator and decimator (Kaiser window). half = group delay. */
class HalfStage
{
public:
    /** Edges as fractions of the higher sample rate. */
    void design (int half, double passEdge, double stopEdge, double attenuationDb)
    {
        d = half; n = 2 * d + 1;
        h.assign ((size_t) n, 0.0);
        const double pi = 3.14159265358979323846, fc = 0.5 * (passEdge + stopEdge);
        const double beta = attenuationDb > 50 ? 0.1102 * (attenuationDb - 8.7) : 0.5842 * std::pow (attenuationDb - 21, 0.4) + 0.07886 * (attenuationDb - 21);
        double sum = 0;
        for (int i = 0; i < n; ++i)
        {
            const double x = i - d, u = x / (d + 1.0);
            const double s = std::abs (x) < 1e-12 ? 2 * fc : std::sin (2 * pi * fc * x) / (pi * x);
            h[(size_t) i] = s * bessel0 (beta * std::sqrt (std::max (0.0, 1 - u * u))) / bessel0 (beta);
            sum += h[(size_t) i];
        }
        for (auto& v : h) v /= sum;
        even.assign ((size_t) (d + 1), 0.0); odd.assign ((size_t) (d + 1), 0.0);
        for (int i = 0; i < n; ++i) (i % 2 ? odd : even)[(size_t) (i / 2)] = h[(size_t) i];
        clear();
    }
    void clear()
    {
        up.assign ((size_t) (2 * (d + 1)), 0.0); down.assign ((size_t) (2 * n), 0.0);
        upPos = downPos = 0;
    }
    int groupDelay() const { return d; }          // samples at the higher rate

    /** One sample in, two out (at the higher rate, level kept). */
    inline void interpolate (double x, double& y0, double& y1)
    {
        const int m = d + 1;
        upPos = upPos == 0 ? m - 1 : upPos - 1;                 // newest first, written twice: always contiguous
        up[(size_t) upPos] = up[(size_t) (upPos + m)] = x;
        y0 = 2 * dot (even.data(), up.data() + upPos, m);
        y1 = 2 * dot (odd.data(), up.data() + upPos, m - 1);
    }
    /** Two samples in (at the higher rate), one out. */
    inline double decimate (double x0, double x1)
    {
        push (x0);
        const double acc = dot (h.data(), down.data() + downPos, n);
        push (x1);
        return acc;
    }

private:
    inline void push (double x) { downPos = downPos == 0 ? n - 1 : downPos - 1; down[(size_t) downPos] = down[(size_t) (downPos + n)] = x; }
    static inline double dot (const double* a, const double* b, int len)
    {
        double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        int i = 0;
        for (; i + 4 <= len; i += 4) { s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1]; s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3]; }
        for (; i < len; ++i) s0 += a[i] * b[i];
        return (s0 + s1) + (s2 + s3);
    }
    static double bessel0 (double x) { double s = 1, t = 1; for (int i = 1; i < 60; ++i) { t *= (x / (2.0 * i)) * (x / (2.0 * i)); s += t; } return s; }
    int d = 0, n = 1;
    std::vector<double> h, even, odd, up, down;
    int upPos = 0, downPos = 0;
};

/** Runs a function at 1x, 2x, 4x or 8x. Delay is a whole number of base samples, the same for every sample rate. */
class Oversampler
{
public:
    static constexpr int maxStages = 3, firstDelay = 126;
    void prepare (int factor)
    {
        stages = factor >= 8 ? 3 : (factor >= 4 ? 2 : (factor >= 2 ? 1 : 0));
        // edges in units of the base rate: pass 0.448 (21.5 kHz at 48 kHz), fully closed at 0.5 (Nyquist)
        s[0].design (firstDelay, 0.448 / 2, 0.5 / 2, 100.0);
        s[1].design (12, 0.448 / 4, 0.5 - 0.448 / 4, 104.0);
        s[2].design (8, 0.448 / 8, 0.5 - 0.448 / 8, 104.0);
    }
    void clear() { for (auto& x : s) x.clear(); }
    int factor() const { return 1 << stages; }
    int latency() const { return latencyFor (1 << stages); }
    static int latencyFor (int factor) { return factor >= 8 ? firstDelay + 6 + 2 : (factor >= 4 ? firstDelay + 6 : (factor >= 2 ? firstDelay : 0)); }

    /** One base sample to factor() samples at the higher rate. b holds 8. */
    inline void up (double x, double* b)
    {
        b[0] = x;
        int n = 1;
        for (int k = 0; k < stages; ++k)
        {
            double t[8];
            for (int i = 0; i < n; ++i) s[(size_t) k].interpolate (b[i], t[2 * i], t[2 * i + 1]);
            n *= 2;
            for (int i = 0; i < n; ++i) b[i] = t[i];
        }
    }
    /** Back to one base sample. */
    inline double down (double* b)
    {
        int n = 1 << stages;
        for (int k = stages - 1; k >= 0; --k)
        {
            n /= 2;
            for (int i = 0; i < n; ++i) b[i] = s[(size_t) k].decimate (b[2 * i], b[2 * i + 1]);
        }
        return b[0];
    }
    template <typename Fn>
    inline double process (double x, Fn&& fn)
    {
        double b[8];
        up (x, b);
        for (int i = 0; i < (1 << stages); ++i) b[i] = fn (b[i]);
        return down (b);
    }

private:
    int stages = 0;
    std::array<HalfStage, maxStages> s;
};

/** Clip curve from soft to hard. Straight up to the knee, then a smooth bend (quadratic) that reaches the ceiling
    exactly, then flat. knee 0 = hard, 1 = bends from half the ceiling. Normalised to a ceiling of 1. */
struct ClipCurve
{
    double knee = 0.0;
    double start() const { return 1.0 - 0.5 * std::clamp (knee, 0.0, 1.0); }          // where the bend begins
    double full() const { return 2.0 - start(); }                                      // where the curve has become flat
    inline double f (double x) const
    {
        const double a = std::abs (x), s = start();
        double y;
        if (a <= s) return x;
        if (a >= full()) y = 1.0;
        else { const double t = a - s; y = a - t * t / (4.0 * (1.0 - s)); }
        return x < 0 ? -y : y;
    }
    /** Antiderivative of f(x) - x (even function, 0 at 0). */
    inline double D (double x) const
    {
        const double a = std::abs (x), s = start(), w = 1.0 - s, e = full();
        if (a <= s) return 0.0;
        if (a < e) { const double t = a - s; return -t * t * t / (12.0 * w); }
        const double atE = w > 0 ? -std::pow (2.0 * w, 3) / (12.0 * w) : 0.0;         // value where the bend ends
        return atE + (a - e) - 0.5 * (a * a - e * e);
    }
};

/** ADAA of curve(x) - x. Returns exactly 0 while the curve is straight. */
template <typename Curve>
class DeltaShaper
{
public:
    void reset() { x1 = 0.0; D1 = 0.0; stale = false; }
    /** Call when the curve has changed: the previous point must lie on the new curve. */
    void refresh (const Curve& c) { D1 = c.D (x1); }
    /** Keeps up with the signal without computing (while another shaper does the work). */
    inline void skip (double x) { x1 = x; stale = true; }
    inline double operator() (double x, const Curve& c)
    {
        if (stale) { refresh (c); stale = false; }
        const double D0 = c.D (x), dx = x - x1;
        const double y = std::abs (dx) > 1e-5 ? (D0 - D1) / dx : c.f (0.5 * (x + x1)) - 0.5 * (x + x1);
        x1 = x; D1 = D0;
        return y;
    }
private:
    double x1 = 0.0, D1 = 0.0;
    bool stale = false;
};

/** One channel: y = x (delayed) + oversampled (curve(x) - x). */
template <typename Curve>
class DeltaChannel
{
public:
    void prepare (int factor)
    {
        os.prepare (factor);
        delay.assign ((size_t) os.latency() + 1, 0.0);
        clear();
    }
    void clear() { os.clear(); sh.reset(); std::fill (delay.begin(), delay.end(), 0.0); pos = 0; }
    int latency() const { return os.latency(); }
    /** x is normalised to the curve. Returns the processed sample; dry is the delayed input. */
    inline double process (double x, const Curve& c, double& dry)
    {
        const double diff = os.process (x, [this, &c] (double v) { return sh (v, c); });
        delay[(size_t) pos] = x;
        pos = (pos + 1) % (int) delay.size();
        dry = delay[(size_t) pos];
        return dry + diff;
    }
private:
    Oversampler os;
    DeltaShaper<Curve> sh;
    std::vector<double> delay;
    int pos = 0;
};

} // namespace aemon::dist
