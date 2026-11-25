// Aemon core: true-peak detector. AGPL-3.0-or-later. Header-only, free of JUCE.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace aemon::dist
{

/** Largest level between the samples, by windowed-sinc interpolation (Kaiser) and a parabola through the points, so
    that the reading of a steady sound does not depend on where its samples fall. Answers for the interval between the
    sample delay() ago and the next one. */
template <int Taps, int Factor, int Beta10>
class TruePeakDetector
{
public:
    static constexpr int delay() { return Taps / 2; }
    TruePeakDetector()
    {
        const double pi = 3.14159265358979323846, beta = Beta10 / 10.0;
        for (int ph = 0; ph < Factor; ++ph)
        {
            double sum = 0;
            for (int t = 0; t < Taps; ++t)
            {
                const double x = (double) (t - Taps / 2 + 1) - (double) ph / Factor, u = x / (Taps / 2.0);
                const double s = std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                coef[(size_t) (ph * Taps + t)] = s * (std::abs (u) < 1 ? i0 (beta * std::sqrt (1 - u * u)) / i0 (beta) : 0.0);
                sum += coef[(size_t) (ph * Taps + t)];
            }
            for (int t = 0; t < Taps; ++t) coef[(size_t) (ph * Taps + t)] /= sum;
        }
        reset();
    }
    void reset() { hist.fill (0.0); pos = 0; a = b = 0; }
    /** Remembers a sample without measuring. */
    inline void skip (double x)
    {
        pos = pos == 0 ? Taps - 1 : pos - 1;
        hist[(size_t) pos] = hist[(size_t) (pos + Taps)] = x;
        a = b = 0;
    }
    inline double push (double x)
    {
        pos = pos == 0 ? Taps - 1 : pos - 1;
        hist[(size_t) pos] = hist[(size_t) (pos + Taps)] = x;
        double peak = 0, v[Factor];
        for (int ph = 0; ph < Factor; ++ph)
        {
            const double* c = coef.data() + ph * Taps; const double* h = hist.data() + pos;
            double s0 = 0, s1 = 0; int t = 0;
            for (; t + 2 <= Taps; t += 2) { s0 += c[t] * h[t]; s1 += c[t + 1] * h[t + 1]; }
            // phases run backwards in time within one push: 0 is the newest sample of the interval
            v[ph] = s0 + s1;
        }
        for (int ph = Factor - 1; ph >= 0; --ph)
        {
            const double c = v[ph], m = std::abs (b);
            peak = std::max ({ peak, std::abs (c), m });
            if (m >= std::abs (a) && m >= std::abs (c))
            {
                const double curve = a - 2 * b + c;
                if (std::abs (curve) > 1e-12) peak = std::max (peak, std::abs (b - (c - a) * (c - a) / (8 * curve)));
            }
            a = b; b = c;
        }
        return peak;
    }
private:
    static double i0 (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; }
    std::array<double, (size_t) (Taps * Factor)> coef {};
    std::array<double, (size_t) (2 * Taps)> hist {};
    int pos = 0;
    double a = 0, b = 0;
};

} // namespace aemon::dist
