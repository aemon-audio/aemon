// Aemon Tune: the pitch change, measured against the same voice really sung at the other pitch. Plain C++, no JUCE. AGPL-3.0-or-later.
#include "TuneShift.h"
#include "TuneVoice.h"
#include "PerceptKit.h"
#include <chrono>
#include <cstdio>
#include <string>

using namespace tunevoice;
using percept::Vec;
namespace
{
int failures = 0;
void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
std::string num (double v, int d = 1) { char b[40]; std::snprintf (b, sizeof b, "%.*f", d, v); return b; }
/** The pitch that was sung, plus so many cents, plus a throat. Uses the Listener as the plugin will. */
Vec shift (const Vec& x, double fs, double cents, double throat, int& late, double lowest = 65)
{
    aemon::tune::Listener l; l.patience = 1; l.lowest = lowest; l.prepare (fs); aemon::tune::Shifter s; s.prepare (fs, lowest, 1); late = s.latency(); aemon::tune::Heard h; Vec y; y.reserve (x.size());
    for (size_t i = 0; i < x.size() + (size_t) late; ++i)
    {
        const float v = i < x.size() ? x[i] : 0.0f; float o = 0;
        if (l.push (v, h)) s.wish (std::max<int64_t> (0, (int64_t) i - h.age), { h.voiced, h.voiced ? fs / h.hz : 0, cents, throat });
        s.run (&v, &o); if (i >= (size_t) late) y.push_back (o);
    }
    return y;
}
/** Pitch error in cents against the truth moved by `cents`: median offset and wander (95 % around it). */
void pitchOf (const Vec& y, const Take& t, double fs, double cents, double& off, double& wander, double& share)
{
    aemon::tune::Listener l; l.patience = 0; l.prepare (fs); aemon::tune::Heard h; std::vector<double> e; int sung = 0;
    for (size_t i = 0; i < y.size(); ++i) if (l.push (y[i], h)) { const int at = (int) i - h.age; if (at < (int) (0.06 * fs) || t.hz[(size_t) at] <= 0 || t.hz[(size_t) (at - (int) (0.06 * fs))] <= 0 || i + (size_t) (0.04 * fs) >= t.hz.size() || t.hz[i + (size_t) (0.04 * fs)] <= 0) continue; ++sung; if (h.voiced) e.push_back (1200 * std::log2 (h.hz / t.hz[(size_t) at]) - cents); }
    share = sung ? (double) e.size() / sung : 0; if (e.empty()) { off = wander = 999; return; }
    auto q = e; std::sort (q.begin(), q.end()); off = q[q.size() / 2]; for (auto& v : q) v = std::abs (v - off); std::sort (q.begin(), q.end()); wander = q[(size_t) (0.95 * (double) q.size())];
}
/** A steady note of known pitch: the level of its partials up to 6 kHz (level apart, weak ones left out) against another sound of the same pitch, and
    how far the rest (noise between the partials, 100 Hz to 6 kHz) is under the note. */
std::vector<double> partials (const Vec& x, double fs, double f0) { std::vector<double> v; const int n = (int) x.size(); for (int k = 1; k * f0 < 6000; ++k) { double re = 0, im = 0, ws = 0; for (int i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * pi * i / n), a = 2 * pi * k * f0 * i / fs; re += w * x[(size_t) i] * std::cos (a); im += w * x[(size_t) i] * std::sin (a); ws += w; } v.push_back (20 * std::log10 (2 * std::sqrt (re * re + im * im) / ws + 1e-9)); } return v; }
void toneDiff (const Vec& a, const Vec& b, double fs, double fa, double fb, double& worst, double& mean)
{
    const auto p = partials (a, fs, fa), q = partials (b, fs, fb); double top = -200; for (double v : p) top = std::max (top, v); double m = 0; int n = 0; for (size_t i = 0; i < std::min (p.size(), q.size()); ++i) if (p[i] > top - 35) { m += q[i] - p[i]; ++n; }
    m /= n; worst = mean = 0; for (size_t i = 0; i < std::min (p.size(), q.size()); ++i) if (p[i] > top - 35) { const double d = std::abs (q[i] - p[i] - m); worst = std::max (worst, d); mean += d / n; }
}
/** The partials the same voice would have at another pitch, guessed from the ones it has (straight lines in dB between them): nobody can know more
    about what the mouth does between two partials. */
std::vector<double> guessed (const std::vector<double>& p, double from, double to) { std::vector<double> v; for (int k = 1; k * to < 6000; ++k) { const double x = k * to / from - 1; const int i = std::clamp ((int) std::floor (x), 0, (int) p.size() - 2); const double f = std::clamp (x - i, 0.0, 1.0); v.push_back (p[(size_t) i] + f * (p[(size_t) i + 1] - p[(size_t) i])); } return v; }
double bestGuess (const Vec& a, const Vec& b, double fs, double fa, double fb) { const auto g = guessed (partials (a, fs, fa), fa, fb), p = partials (b, fs, fb); double top = -200, m = 0, mean = 0; int n = 0; for (double v : p) top = std::max (top, v); for (size_t i = 0; i < std::min (p.size(), g.size()); ++i) if (p[i] > top - 35) { m += g[i] - p[i]; ++n; } m /= n; for (size_t i = 0; i < std::min (p.size(), g.size()); ++i) if (p[i] > top - 35) mean += std::abs (g[i] - p[i] - m) / n; return mean; }
double betweenAt (const Vec& x, double fs, double f0);
/** What is left of a steady note when all its partials are taken out, over tenths of a second. The pitch is fitted within 0.3 cents: that is the
    business of the pitch check, here it would be counted as noise. */
double between (const Vec& x, double fs, double f0) { double best = 1e9; for (int d = -6; d <= 6; ++d) best = std::min (best, betweenAt (x, fs, f0 * std::pow (2.0, d * 0.05 / 1200))); return best; }
double betweenAt (const Vec& x, double fs, double f0)
{
    const int n = (int) (std::round (0.1 * f0) / f0 * fs); double e = 1e-20, r = 1e-20;
    for (int at = 0; at + n <= (int) x.size(); at += n)
    {
        std::vector<double> y (x.begin() + at, x.begin() + at + n); for (double v : y) e += v * v;
        for (int pass = 0; pass < 2; ++pass) for (int k = 1; k * f0 < fs * 0.45; ++k) { double re = 0, im = 0; const double w = 2 * pi * k * f0 / fs; const double cw = std::cos (w), sw = std::sin (w); double c = 1, s = 0; for (int i = 0; i < n; ++i) { re += y[(size_t) i] * c; im += y[(size_t) i] * s; const double c2 = c * cw - s * sw; s = s * cw + c * sw; c = c2; } re *= 2.0 / n; im *= 2.0 / n; c = 1; s = 0; for (int i = 0; i < n; ++i) { y[(size_t) i] -= re * c + im * s; const double c2 = c * cw - s * sw; s = s * cw + c * sw; c = c2; } }
        for (double v : y) r += v * v;
    }
    return percept::db10 (r / e);
}
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); const double fs = 48000; int late = 0;
    // 1. nothing wanted: what goes in comes out, S and breath included
    {
        Rnd r; auto t = sing (fs, 2.0, melody ({ { 57, 0.1, 0.8 }, { 62, 1.0, 1.9 } }, 0.05)); for (size_t i = (size_t) (0.82 * fs); i < (size_t) (0.98 * fs); ++i) t.x[i] += (float) (0.1 * r.next());
        const auto y = shift (t.x, fs, 0, 1, late); double e = 1e-20, s = 1e-20; for (size_t i = 0; i < t.x.size(); ++i) { e += std::pow (y[i] - t.x[i], 2); s += (double) t.x[i] * t.x[i]; }
        check (percept::db10 (e / s) < -50, "No change wanted: the same sound", "difference " + num (percept::db10 (e / s)) + " dB, delay " + num (1000.0 * late / fs) + " ms");
    }
    // 2. every voice, every distance: the right pitch, no warble, no click, the voice kept
    for (double midi : { 45.0, 52.0, 57.0, 64.0, 69.0, 76.0 })
        for (double cents : { -400.0, -200.0, -60.0, 60.0, 200.0, 400.0 })
        {
            Settings q; q.vowel = 1; q.formant = midi < 50 ? 0.9 : (midi > 62 ? 1.15 : 1.0); q.jitter = false; const double hz = hzOf (midi), to = hz * std::pow (2.0, cents / 1200); const auto f = [] (double h) { return [h] (double t) { return t > 0.1 && t < 1.7 ? h : 0.0; }; };
            const auto a = sing (fs, 1.9, f (hz), q), b = sing (fs, 1.9, f (to), q); const auto y = shift (a.x, fs, cents, 1, late); const auto cut = [&] (const Vec& v) { return Vec (v.begin() + (int) (0.3 * fs), v.begin() + (int) (1.3 * fs)); };
            double off, wander, share, worst, mean; pitchOf (y, a, fs, cents, off, wander, share); toneDiff (cut (b.x), cut (y), fs, to, to, worst, mean); const double best = bestGuess (cut (a.x), cut (b.x), fs, hz, to), rest = between (cut (y), fs, to), was = between (cut (a.x), fs, hz), click = percept::clickDb (cut (y), fs), base = percept::clickDb (cut (a.x), fs);
            check (std::abs (off) < 2 && wander < 3 && share > 0.95 && mean < std::max (1.5, best + 0.75) && rest < -30 && click < base + 4, "Note " + num (midi, 0) + " moved by " + num (cents, 0) + " cents", "off " + num (off) + ", wander " + num (wander) + " cents, partials against the voice really sung there: mean " + num (mean) + " dB (best guess " + num (best) + "), most " + num (worst) + " dB, between them " + num (rest) + " dB (was " + num (was) + "), click " + num (click) + " (was " + num (base) + ")");
        }
    // 2b. a real line: vibrato, jitter, slides. The moves of the singer stay, only moved
    for (double move : { -9.0, 0.0, 9.0 }) for (double cents : { -150.0, 40.0, 150.0 })
    {
        std::vector<Note> notes { { 57, 0.1, 0.6 }, { 60, 0.6, 1.0 }, { 64, 1.1, 2.2 }, { 62, 2.2, 2.6 } }; for (auto& n : notes) n.midi += move; Settings q; q.vibrato = 35; q.vowel = 1; const auto a = sing (fs, 2.8, melody (notes, 0.08), q); const auto y = shift (a.x, fs, cents, 1, late); double off, wander, share, o2, w2, s2; pitchOf (y, a, fs, cents, off, wander, share); pitchOf (a.x, a, fs, 0, o2, w2, s2);
        check (std::abs (off) < 2 && wander < w2 + 5 && share > 0.95, "A line with vibrato and slides, " + num (move, 0) + " half tones, moved by " + num (cents, 0) + " cents", "off " + num (off) + ", wander " + num (wander) + " cents (the untouched line reads " + num (w2) + ")");
    }
    // 3. throat: the same notes, another size of voice
    for (double size : { 0.82, 0.9, 1.12, 1.25 })
    {
        Settings q; q.vowel = 1; q.jitter = false; const auto f = [] (double t) { return t > 0.1 && t < 1.7 ? 160.0 : 0.0; }; const auto a = sing (fs, 1.9, f, q); auto o = q; o.formant = size; const auto b = sing (fs, 1.9, f, o); const auto y = shift (a.x, fs, 0.5, size, late);
        double off, wander, share, worst, mean, w1, m1; pitchOf (y, a, fs, 0.5, off, wander, share); const auto cut = [&] (const Vec& v) { return Vec (v.begin() + (int) (0.3 * fs), v.begin() + (int) (1.3 * fs)); }; toneDiff (cut (b.x), cut (y), fs, 160, 160 * std::pow (2.0, 0.5 / 1200), worst, mean); toneDiff (cut (b.x), cut (a.x), fs, 160, 160, w1, m1);
        check (std::abs (off) < 2 && wander < 3 && mean < 2.0 && mean < 0.5 * m1, "Throat " + num (size, 2) + ": the notes stay, the voice becomes the one with that throat", "off " + num (off) + ", wander " + num (wander) + ", partials mean " + num (mean) + " dB from it, untouched it was " + num (m1));
    }
    // 4. cost
    { const auto t = sing (fs, 4.0, [] (double) { return 220.0; }); const auto a = std::chrono::steady_clock::now(); shift (t.x, fs, 80, 1.05, late); const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a).count(); check (ms / 4000 < 0.05, "Cost, mono at 48 kHz", num (ms / 40, 2) + " % of one core"); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED"); return failures ? 1 : 0;
}
