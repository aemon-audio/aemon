// A synthetic singer for the De-esser tests and pictures, with the place of every sound known. AGPL-3.0-or-later.
// Pulse train through moving formants, shaped noise for S, SH, Z and T. Made here, no recording.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace voicekit
{
struct Sound { char kind; int from, to; };       // 's' S, 'S' sharp S, 'x' SH, 'z' Z, 't' T, 'v' steady vowel
struct Voice { std::vector<float> x; std::vector<Sound> sounds; };
struct Kind { double pitch = 1.0, sFreq = 1.0, sGain = 1.0, formant = 1.0, level = 0.6; int seed = 5; };

struct Bp { double a = 0, b = 0; double run (double x, double f, double q, double fs) { const double g = std::tan (3.14159265358979 * std::min (f, fs * 0.45) / fs), k = 1 / q, a1 = 1 / (1 + g * (g + k)), v1 = a1 * (a + g * (x - b)), v2 = b + g * v1; a = 2 * v1 - a; b = 2 * v2 - b; return v1 * k; } };

inline Voice sing (double fs, Kind kind = {})
{
    constexpr double pi = 3.14159265358979;
    struct C { double f, q, g, d; bool voiced, hum; };
    static const std::map<char, std::array<double, 4>> vowels { { 'a', { 760, 1200, 2600, 3500 } }, { 'e', { 460, 1900, 2550, 3500 } }, { 'i', { 310, 2200, 2950, 3600 } }, { 'o', { 470, 850, 2500, 3500 } }, { 'u', { 340, 900, 2300, 3400 } } };
    static const std::map<char, C> cons { { 's', { 7200, 1.1, 0.18, 0.12, false, false } }, { 'S', { 8200, 1.3, 0.46, 0.15, false, false } }, { 'x', { 3900, 1.2, 0.28, 0.13, false, false } }, { 'z', { 6800, 1.1, 0.15, 0.10, true, false } },
                                           { 't', { 5200, 0.8, 0.22, 0.03, false, false } }, { 'm', { 0, 1, 0, 0.06, false, true } }, { 'l', { 0, 1, 0, 0.05, false, true } } };
    struct N { char c, v; int note; double beats; };
    static const N phrase[] { { 'l', 'a', 57, 1 }, { 's', 'e', 60, 1 }, { 'S', 'i', 62, 1.5 }, { 'l', 'o', 60, 0.5 }, { 'x', 'o', 57, 1 }, { 't', 'a', 55, 1 }, { 's', 'u', 57, 1 }, { 'z', 'e', 60, 1 }, { 'S', 'a', 64, 1.5 }, { 'm', 'i', 62, 0.5 }, { 'l', 'a', 60, 1 }, { 's', 'o', 57, 1 } };
    const double G[] { 1, 0.55, 0.3, 0.2 }, BW[] { 90, 110, 160, 220 }, beat = 60.0 / 112.0;
    double beats = 0; for (auto& p : phrase) beats += p.beats;
    const int total = (int) std::lround (beats * beat * fs);
    Voice out; out.x.assign ((size_t) total, 0.0f);
    unsigned seed = (unsigned) kind.seed;
    auto rnd = [&seed] { seed = seed * 1664525u + 1013904223u; return (double) seed / 2147483648.0 - 1.0; };
    Bp fm[4], n1, n2, air; double cur[4] { 760, 1200, 2600, 3500 }, ph = 0, hz = 220, last = 0, env = 0; int at = 0;
    for (auto& p : phrase)
    {
        const int n = std::min ((int) std::lround (p.beats * beat * fs), total - at), cd = (int) std::lround (cons.at (p.c).d * fs);
        const C c = cons.at (p.c); const auto& F = vowels.at (p.v); const double tgt = 440 * std::pow (2.0, (p.note - 69) / 12.0) * kind.pitch;
        if (c.f > 0) out.sounds.push_back ({ p.c, at, at + cd });
        out.sounds.push_back ({ 'v', at + cd + (int) (0.06 * fs), at + n - (int) (0.09 * fs) });
        for (int i = 0; i < n; ++i)
        {
            const double t = i / fs, fadeOut = std::min (1.0, (n - i) / (0.07 * fs)); const bool inC = i < cd;
            hz += (tgt - hz) * (1 - std::exp (-1 / (0.045 * fs)));
            ph += hz * (1 + 0.009 * std::sin (2 * pi * 5.3 * t) * std::min (1.0, t / 0.4)) / fs; if (ph >= 1) ph -= 1;
            const double gl = ph < 0.62 ? std::pow (std::sin (pi * std::pow (ph / 0.62, 1.5)), 2) : 0, src = (gl - last) * fs / 48000; last = gl;
            env += ((inC ? (c.hum || c.voiced ? 0.35 : 0.0) : 1.0) * fadeOut - env) * (1 - std::exp (-1 / (0.012 * fs)));
            double y = 0;
            for (int k = 0; k < 4; ++k) { cur[k] += (F[(size_t) k] * kind.formant - cur[k]) * (1 - std::exp (-1 / (0.03 * fs))); y += fm[k].run (src * 9, cur[k], cur[k] / BW[k], fs) * G[k] * (c.hum && inC && k > 0 ? 0.3 : 1); }
            const double nz = rnd() * std::sqrt (fs / 48000);
            y = y * env + air.run (nz, 4200, 0.5, fs) * 0.006 * env;
            if (c.f > 0) { const double f = std::min (c.f * kind.sFreq, 13000.0), e = std::min (1.0, i / (0.018 * fs)) * (inC ? 1.0 : std::exp (-(i - cd) / (0.012 * fs))); if (e > 1e-5) y += n2.run (n1.run (nz, f, c.q, fs) + nz * 0.05, f, 0.6, fs) * c.g * kind.sGain * e; }
            out.x[(size_t) (at + i)] = (float) y;
        }
        at += n;
    }
    float pk = 1e-9f; for (float v : out.x) pk = std::max (pk, std::abs (v));
    for (int i = 0; i < total; ++i) out.x[(size_t) i] *= (float) (kind.level / pk) * std::min ({ 1.0f, (float) i / 200.0f, (float) (total - i) / 200.0f });
    return out;
}

/** One take after the other, each at its own level (dB). */
inline Voice takes (double fs, Kind kind, std::vector<double> levels)
{
    Voice out; const auto v = sing (fs, kind);
    for (double d : levels)
    {
        const int at = (int) out.x.size(); const float g = (float) std::pow (10.0, d / 20.0);
        for (float s : v.x) out.x.push_back (s * g);
        for (auto s : v.sounds) out.sounds.push_back ({ s.kind, s.from + at, s.to + at });
    }
    return out;
}
} // namespace voicekit
