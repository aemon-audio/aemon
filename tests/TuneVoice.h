// A synthetic singer whose pitch is known at every sample, for the Tune tests. Made here, no recording. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace tunevoice
{
constexpr double pi = 3.14159265358979323846;
struct Rnd { unsigned s = 5; double next() { s = s * 1664525u + 1013904223u; return (double) s / 2147483648.0 - 1.0; } };
struct Res { double a = 0, b = 0; double run (double x, double f, double bw, double fs) { const double r = std::exp (-pi * bw / fs), c = 2 * r * std::cos (2 * pi * f / fs), g = 1 - c + r * r; const double y = g * x + c * a - r * r * b; b = a; a = y; return y; } };

struct Take { std::vector<float> x; std::vector<double> hz; };      // hz 0: not sung

/** A voice: band-limited pulses through four formants. `pitch` in Hz by time (0: silent), `vowel` 0..4, breath 0..1. */
/** breath: how far the noise of the breath is under the tone, dB (a clear voice 25 and more, a breathy one 12, almost whispered 3). */
struct Settings { double formant = 1, breath = 100, level = 0.25, vibrato = 0, rate = 5.4; int vowel = 0; unsigned seed = 5; bool jitter = true; };
inline Take sing (double fs, double seconds, const std::function<double (double)>& pitch, Settings s = {})
{
    static const double F[5][4] { { 760, 1250, 2600, 3500 }, { 480, 1850, 2550, 3500 }, { 310, 2200, 2950, 3600 }, { 480, 880, 2500, 3500 }, { 340, 820, 2300, 3400 } }; static const double G[4] { 1, 0.6, 0.35, 0.2 }, BW[4] { 80, 100, 150, 220 };
    Take t; const int n = (int) (seconds * fs); t.x.resize ((size_t) n); t.hz.resize ((size_t) n); Rnd r; r.s = s.seed; Res f[4], af[4]; std::vector<float> nz ((size_t) n, 0.0f); double ph = 0, env = 0, jit = 0, peak = 1e-9, last = 0; const double over = 1;
    for (int i = 0; i < n; ++i)
    {
        const double time = i / fs; double hz = pitch (time); const bool on = hz > 0;
        if (on) { jit += (r.next() - jit) * 0.002; hz *= std::pow (2.0, (s.vibrato * std::sin (2 * pi * s.rate * time) + (s.jitter ? jit * 60 : 0)) / 1200.0); }
        t.hz[(size_t) i] = on ? hz : 0; ph += (on ? hz : 0) / fs * over; if (ph >= 1) ph -= 1;
        // glottal flow, its derivative is the source
        const double gl = ph < 0.6 ? std::pow (std::sin (pi * std::pow (ph / 0.6, 1.6)), 2) : 0, src = (gl - last) * fs / 48000; last = gl; env += ((on ? 1.0 : 0.0) - env) * (1 - std::exp (-1 / (0.008 * fs)));
        double y = 0, z = 0; const double in = src * 8 * env, wn = s.breath < 90 ? r.next() * std::sqrt (fs / 48000) * env * (0.5 + gl) : 0; for (int k = 0; k < 4; ++k) { const double g = G[k] * (k % 2 ? -1 : 1) * F[s.vowel][k] / 500; y += f[k].run (in, F[s.vowel][k] * s.formant, BW[k], fs) * g; if (s.breath < 90) z += af[k].run (wn, F[s.vowel][k] * s.formant, BW[k] * 2, fs) * g; }
        nz[(size_t) i] = (float) z;
        t.x[(size_t) i] = (float) y;
    }
    if (s.breath < 90) { double ey = 1e-20, ez = 1e-20; for (int i = 0; i < n; ++i) { ey += (double) t.x[(size_t) i] * t.x[(size_t) i]; ez += (double) nz[(size_t) i] * nz[(size_t) i]; } const double g = std::sqrt (ey / ez) * std::pow (10.0, -s.breath / 20); for (int i = 0; i < n; ++i) t.x[(size_t) i] += (float) (g * nz[(size_t) i]); }
    for (float v : t.x) peak = std::max (peak, (double) std::abs (v));
    for (auto& v : t.x) v = (float) (v / peak * s.level * 2);
    return t;
}
/** Notes as MIDI numbers, each `len` seconds with a short gap or a slide between. cents: how far off each is sung. */
struct Note { double midi, from, to, cents = 0; };
inline double hzOf (double midi) { return 440 * std::pow (2.0, (midi - 69) / 12); }
inline std::function<double (double)> melody (std::vector<Note> notes, double slide)
{
    return [notes, slide] (double t) { for (size_t i = 0; i < notes.size(); ++i) { auto& n = notes[i]; if (t >= n.from && t < n.to) { double m = n.midi + n.cents / 100; if (slide > 0 && i > 0 && n.from - notes[i - 1].to < 0.01) { const double u = std::clamp ((t - n.from) / slide, 0.0, 1.0), p = notes[i - 1].midi + notes[i - 1].cents / 100; m = p + (m - p) * (u * u * (3 - 2 * u)); } return hzOf (m); } } return 0.0; };
}
inline double rms (const std::vector<float>& x) { double e = 1e-20; for (float v : x) e += (double) v * v; return std::sqrt (e / (double) std::max<size_t> (1, x.size())); }
inline void addNoise (std::vector<float>& x, double db, unsigned seed = 9) { Rnd r; r.s = seed; const double g = rms (x) * std::pow (10.0, db / 20) * std::sqrt (3.0); for (auto& v : x) v += (float) (g * r.next()); }
/** A small hard room: twelve early echoes and a decaying tail. */
inline void addRoom (std::vector<float>& x, double fs, double db)
{
    Rnd r; r.s = 77; std::vector<float> wet (x.size(), 0.0f); const double want = rms (x) * std::pow (10.0, db / 20); std::vector<std::pair<int, double>> taps; for (int k = 0; k < 900; ++k) { const double t = 0.004 + 0.25 * (r.next() * 0.5 + 0.5); taps.push_back ({ (int) (t * fs), r.next() * std::exp (-t * 14) }); }
    for (auto& [d, g] : taps) for (size_t i = (size_t) d; i < x.size(); ++i) wet[i] += (float) (g * x[i - (size_t) d]);
    const double k = want / rms (wet); for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (k * wet[i]);
}
inline void addBeat (std::vector<float>& x, double fs, double db)
{
    Rnd r; r.s = 31; std::vector<float> b (x.size(), 0.0f); double ph = 0, bass = 0; for (size_t i = 0; i < x.size(); ++i) { const double t = (double) i / fs, q = std::fmod (t, 0.43), h = std::fmod (t + 0.21, 0.43); ph += 2 * pi * (48 + 110 * std::exp (-q * 34)) / fs; bass += 2 * pi * 55 / fs; b[i] = (float) (0.9 * std::sin (ph) * std::exp (-q * 8) + 0.3 * r.next() * std::exp (-h * 25) + 0.4 * std::sin (bass)); }
    const double g = rms (x) * std::pow (10.0, db / 20) / rms (b); for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (g * b[i]);
}
} // namespace tunevoice
