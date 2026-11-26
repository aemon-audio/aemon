// Synthetic drums for the Transient and Low End tests and pictures. Made here, no recording. AGPL-3.0-or-later.
#pragma once
#include <cmath>
#include <string>
#include <vector>

namespace drumkit
{
constexpr double pi = 3.14159265358979;
struct Noise { unsigned s = 5; double next() { s = s * 1664525u + 1013904223u; return (double) s / 2147483648.0 - 1.0; } };
struct Svf { double a = 0, b = 0; double band (double x, double f, double q, double fs) { const double g = std::tan (pi * f / fs), k = 1 / q, a1 = 1 / (1 + g * (g + k)), v1 = a1 * (a + g * (x - b)), v2 = b + g * v1; a = 2 * v1 - a; b = 2 * v2 - b; return v1 * k; } };
/** One hit, `seconds` long. kick, snare, hat, tom, clap. */
inline std::vector<float> hit (const std::string& kind, double fs, double seconds, int seed = 1)
{
    const int n = (int) (seconds * fs); std::vector<float> x ((size_t) n); Noise r; r.s = (unsigned) seed * 77u + 5u; Svf f, g; double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, on = std::min (1.0, t / 0.0008), w = r.next() * std::sqrt (fs / 48000); double y = 0;
        if (kind == "kick") { ph += 2 * pi * (50 + 150 * std::exp (-t * 40)) / fs; y = 0.8 * std::sin (ph) * std::exp (-t * 7) + 0.25 * f.band (w, 3000, 0.7, fs) * std::exp (-t * 180); }
        else if (kind == "snare") { ph += 2 * pi * 190 / fs; y = 0.4 * std::sin (ph) * std::exp (-t * 18) + 0.45 * f.band (w, 3500, 0.5, fs) * std::exp (-t * 14) + 0.2 * w * std::exp (-t * 90); }
        else if (kind == "hat") y = 0.7 * f.band (w, 9000, 0.8, fs) * std::exp (-t * 30);
        else if (kind == "tom") { ph += 2 * pi * (110 + 40 * std::exp (-t * 20)) / fs; y = 0.7 * std::sin (ph) * std::exp (-t * 6) + 0.15 * f.band (w, 1800, 0.8, fs) * std::exp (-t * 80); }
        else { double e = 0; for (double d : { 0.0, 0.011, 0.023 }) if (t >= d) e = std::max (e, std::exp (-(t - d) * 220)); e = std::max (e, t > 0.023 ? 0.6 * std::exp (-(t - 0.023) * 16) : 0.0); y = 1.2 * g.band (w, 1500, 1.2, fs) * e; }
        x[(size_t) i] = (float) (y * on);
    }
    return x;
}
/** Hits every `beat` seconds. */
inline std::vector<float> loop (const std::string& kind, double fs, int hits, double beat) { std::vector<float> x; for (int k = 0; k < hits; ++k) { const auto h = hit (kind, fs, beat, k + 1); x.insert (x.end(), h.begin(), h.end()); } return x; }
} // namespace drumkit
