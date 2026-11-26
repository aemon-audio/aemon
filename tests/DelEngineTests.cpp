// Aemon Delay engine: objective and perceptual tests (no JUCE). AGPL-3.0-or-later.
#include "DelEngine.h"
#include "PerceptKit.h"
#include "VoiceKit.h"
#include "DrumKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>

using namespace aemon::del;
using percept::Vec;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static Vec noise (int n, double amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<double> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = (float) d (g); return v; }
struct Run { Vec l, r; };
static Run render (const Vec& xl, const Vec& xr, const Params& p, double fs, int block = 512, const std::function<void (Engine&, size_t)>& each = {})
{
    Run o; o.l = xl; o.r = xr; auto e = std::make_unique<Engine>(); e->setParams (p); e->prepare (fs);
    for (size_t pos = 0; pos < o.l.size(); pos += (size_t) block) { if (each) each (*e, pos); e->process (o.l.data() + pos, o.r.data() + pos, (int) std::min<size_t> ((size_t) block, o.l.size() - pos)); }
    return o;
}
static Params wet() { Params p; p.mix = 1; p.ms = 300; p.lowCut = 20; p.highCut = 20000; p.feedback = 0.5; return p; }
/** A short burst that has a clear place: 1 kHz, 10 ms. */
static Vec ping (double fs, double secs, double amp = 0.5) { Vec v ((size_t) (secs * fs), 0.0f); for (int i = 0; i < (int) (0.01 * fs); ++i) v[(size_t) i] = (float) (amp * std::sin (2 * pi * 1000 * i / fs) * std::sin (pi * i / (0.01 * fs))); return v; }
/** Where the ping comes again around a time (seconds), and how loud: by correlation. */
static std::pair<double, double> find (const Vec& y, const Vec& p, double fs, double around, double range = 0.03) { const int n = (int) (0.01 * fs); double best = 0; int at = 0; for (int s = (int) ((around - range) * fs); s < (int) ((around + range) * fs); ++s) { if (s < 0 || s + n >= (int) y.size()) continue; double c = 0; for (int i = 0; i < n; ++i) c += (double) y[(size_t) (s + i)] * p[(size_t) i]; if (c > best) { best = c; at = s; } } double e = 0; for (int i = 0; i < n; ++i) e += (double) p[(size_t) i] * p[(size_t) i]; return { at / fs, 20 * std::log10 (best / e + 1e-9) }; }

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto a = noise (60000, 0.2, 1), b = noise (60000, 0.2, 2); Params p; p.mix = 0; Params q; q.mix = 0.9; q.bypass = true; bool ok = true; for (auto& s : { p, q }) { const auto o = render (a, b, s, r); for (size_t i = 0; i < a.size(); ++i) ok &= o.l[i] == a[i] && o.r[i] == b[i]; }
        Params w; w.mix = 0.5; w.ms = 280; const auto o = render (a, b, w, r); for (size_t i = 0; i < (size_t) (0.27 * r) && i < a.size(); ++i) ok &= o.l[i] == a[i] && o.r[i] == b[i];
        check (ok, "Mix 0 and bypass bit-exact, dry sound in time and untouched at " + f2 (r / 1000, 1) + " kHz", "");
    }
    // 1. Echoes land where they should, at the level the feedback says
    for (double ms : { 20.0, 125.0, 375.0, 1000.0, 2400.0 })
    {
        auto p = wet(); p.ms = ms; p.feedback = 0.6; const auto x = ping (fs, 5 * ms / 1000 + 0.2); const auto o = render (x, x, p, fs); double worst = 0, lev = 0; std::string list; for (int k = 1; k <= 4; ++k) { const auto [t, d] = find (o.l, x, fs, k * ms / 1000, std::min (0.02, ms * 0.0004)); worst = std::max (worst, std::abs (t * 1000 - k * ms)); lev = std::max (lev, std::abs (d - 20 * std::log10 (0.6) * (k - 1))); list += f2 (t * 1000, 2) + " ms " + f2 (d, 1) + "; "; }
        check (worst < 0.1 && lev < 0.6, "Four echoes at " + f2 (ms, 0) + " ms, feedback 60 %", list + "timing within " + f2 (worst, 3) + " ms, level within " + f2 (lev) + " dB");
    }
    { auto p = wet(); p.style = Style::stereo; p.right = 0.75; p.ms = 400; const auto x = ping (fs, 2); const auto o = render (x, x, p, fs); const auto l = find (o.l, x, fs, 0.4), r = find (o.r, x, fs, 0.3); check (std::abs (l.first - 0.4) < 1e-4 && std::abs (r.first - 0.3) < 1e-4, "Right side at its own time", f2 (l.first * 1000, 2) + " and " + f2 (r.first * 1000, 2) + " ms"); }
    { auto p = wet(); p.style = Style::pingpong; p.ms = 250; p.feedback = 0.7; const auto x = ping (fs, 2); const auto o = render (x, x, p, fs); std::string list; bool ok = true; for (int k = 1; k <= 4; ++k) { const auto l = find (o.l, x, fs, 0.25 * k, 0.01), r = find (o.r, x, fs, 0.25 * k, 0.01); const bool left = k % 2 == 1; ok &= left ? l.second > r.second + 30 : r.second > l.second + 30; list += left ? "L " + f2 (l.second, 1) + " " : "R " + f2 (r.second, 1) + " "; } check (ok, "Ping-pong alternates left and right", list); }
    { auto p = wet(); p.style = Style::mono; const auto a = ping (fs, 1), z = Vec (a.size(), 0.0f); const auto o = render (a, z, p, fs); bool same = true; for (size_t i = 0; i < o.l.size(); ++i) same &= o.l[i] == o.r[i]; check (same, "Mono is the same on both sides", ""); }
    // 2. Characters
    {
        const auto x = noise ((int) (0.4 * fs), 0.15, 5); Vec y = x; y.resize ((size_t) (5 * fs), 0.0f); std::string list; double top[3][3]; for (int c = 0; c < 3; ++c) { auto p = wet(); p.ms = 500; p.feedback = 0.9; p.character = (Character) c; const auto o = render (y, y, p, fs); for (int k = 0; k < 3; ++k) { const int from = (int) ((0.5 + 0.5 * k) * fs); Vec e (o.l.begin() + from, o.l.begin() + from + (int) (0.35 * fs)), d (y.begin(), y.begin() + (int) (0.35 * fs)); top[c][k] = percept::rangeChange (percept::tone (d, fs), percept::tone (e, fs), 5000, 10000) - percept::rangeChange (percept::tone (d, fs), percept::tone (e, fs), 600, 1500); } list += f2 (top[c][0], 1) + "/" + f2 (top[c][2], 1) + " "; }
        check (std::abs (top[0][2]) < 1.5 && top[1][0] < -1 && top[1][2] < 2.2 * top[1][0] && top[2][0] < top[1][0] - 2, "Digital keeps its top, tape loses it repeat by repeat, lo-fi at once", "first/third echo, 5 to 10 kHz: " + list + "dB");
        Vec t ((size_t) (8 * fs)); for (size_t i = 0; i < t.size(); ++i) t[i] = (float) (0.2 * std::sin (2 * pi * 440 * (double) i / fs)); auto q = wet(); q.feedback = 0; q.character = Character::tape; q.wobble = 0.5; const double wob = percept::wobbleCents (render (t, t, q, fs).l, fs, 440); q.wobble = 0; const double none = percept::wobbleCents (render (t, t, q, fs).l, fs, 440); check (wob > 2 && wob < 15 && none < 0.2, "Wobble at half: 2 to 15 cents, none at 0", f2 (wob, 1) + " and " + f2 (none, 2));
    }
    // 3. Filters work on every repeat, and the first echo is clean
    { auto p = wet(); p.ms = 400; const auto v = voicekit::takes (fs, {}, { 0 }).x; p.feedback = 0; const auto o = render (v, v, p, fs); const int lat = (int) (0.4 * fs); double e = 0, s = 0; for (size_t i = (size_t) lat + 2000; i < v.size(); ++i) { const double d = o.l[i] - v[i - (size_t) lat]; e += d * d; s += (double) v[i - (size_t) lat] * v[i - (size_t) lat]; } check (percept::db10 (e / s) < -80, "A digital echo is the sound again", "difference " + f2 (percept::db10 (e / s), 1) + " dB (filters at their ends)");
      auto q = wet(); q.lowCut = 400; q.highCut = 3000; const auto w = render (v, v, q, fs); const auto a = percept::tone (v, fs), b = percept::tone (w.l, fs); check (percept::rangeChange (a, b, 60, 150) - percept::rangeChange (a, b, 800, 1500) < -15 && percept::rangeChange (a, b, 9000, 14000) - percept::rangeChange (a, b, 800, 1500) < -12, "Telephone filters", f2 (percept::rangeChange (a, b, 60, 150) - percept::rangeChange (a, b, 800, 1500), 1) + " and " + f2 (percept::rangeChange (a, b, 9000, 14000) - percept::rangeChange (a, b, 800, 1500), 1) + " dB"); }
    // 4. Feedback over 100 % sings but stays safe; it stops when told to
    {
        double worst = 0; std::mt19937 g (4); for (int k = 0; k < 24; ++k) { auto u = [&] { return (double) (g() % 1001) / 1000; }; auto p = wet(); p.feedback = 1.1; p.ms = 20 + 600 * u(); p.drive = u(); p.wobble = u(); p.character = (Character) (g() % 3); p.style = (Style) (g() % 3); p.lowCut = 20 + 400 * u(); p.highCut = 1500 + 15000 * u(); p.reverse = k % 6 == 5; Vec x ((size_t) (20 * fs), 0.0f); const auto v = noise (24000, 0.25, k); std::copy (v.begin(), v.end(), x.begin()); const auto o = render (x, x, p, fs); for (size_t i = (size_t) (3 * fs); i < x.size(); ++i) worst = std::max (worst, (double) std::max (std::abs (o.l[i]), std::abs (o.r[i]))); }
        check (worst < 0.85 && std::isfinite (worst), "Feedback 110 % for 20 seconds, 24 settings: never over the safe level", "highest " + f2 (20 * std::log10 (worst), 1) + " dB");
        auto p = wet(); p.feedback = 1.1; p.ms = 200; Vec x ((size_t) (26 * fs), 0.0f); const auto v = noise (12000, 0.2, 2); std::copy (v.begin(), v.end(), x.begin()); const auto o = render (x, x, p, fs, 512, [&] (Engine& e, size_t pos) { if (pos == 512 * 750) { p.feedback = 0.3; e.setParams (p); } }); const double loud = percept::rmsDb (o.l, (size_t) (6 * fs), (size_t) (7.9 * fs)), end = percept::rmsDb (o.l, (size_t) (12 * fs), (size_t) (13 * fs)); bool zero = true; for (size_t i = (size_t) (24 * fs); i < x.size(); ++i) zero &= o.l[i] == 0.0f; check (loud > -20 && end < -80 && zero, "Turned down it dies, and silence settles to exactly zero", f2 (loud, 0) + " dB singing, then " + f2 (end, 0));
    }
    // 5. Throw: only what is thrown
    {
        auto p = wet(); p.throwOnly = true; p.feedback = 0; Vec x ((size_t) (3 * fs)); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.3 * std::sin (2 * pi * 500 * (double) i / fs)); const auto o = render (x, x, p, fs, 480, [&] (Engine& e, size_t pos) { if (pos == 48000) p.throwNow = true; if (pos == 72000) p.throwNow = false; e.setParams (p); }); const double before = percept::rmsDb (o.l, 0, 48000), on = percept::rmsDb (o.l, 66000, 84000), after = percept::rmsDb (o.l, 90000, 140000); check (before < -200 && on > -16 && after < -60, "Throw: only what is thrown echoes", f2 (before, 0) + ", " + f2 (on, 1) + ", " + f2 (after, 0) + " dB");
    }
    // 6. Changing the time: glide bends the pitch, fade does not, neither clicks
    {
        Vec t ((size_t) (6 * fs)); for (size_t i = 0; i < t.size(); ++i) t[i] = (float) (0.25 * std::sin (2 * pi * 300 * (double) i / fs)); std::string list; double bend[2], click[2]; for (int c = 0; c < 2; ++c) { auto p = wet(); p.feedback = 0; p.ms = 300; p.change = (Change) c; const auto o = render (t, t, p, fs, 480, [&] (Engine& e, size_t pos) { if (pos == 96000) p.ms = 420; if (pos == 144000) p.ms = 180; e.setParams (p); }); Vec part (o.l.begin() + 40000, o.l.end()); bend[c] = percept::wobbleCents (part, fs, 300); click[c] = percept::clickDb (part, fs); }
        check (bend[0] > 100 && click[0] < 20 && click[1] < 26, "Glide bends the pitch like tape, fade changes over, neither clicks", "bend " + f2 (bend[0], 0) + " cents gliding; clicks " + f2 (click[0], 1) + " and " + f2 (click[1], 1) + " dB");
        Vec s = t; Params p; p.mix = 0.5; p.ms = 250; const auto o = render (s, s, p, fs, 480, [&] (Engine& e, size_t pos) { if (pos == 48000) p.style = Style::pingpong; if (pos == 72000) p.character = Character::tape; if (pos == 96000) p.reverse = true; if (pos == 120000) p.reverse = false; if (pos == 144000) p.bypass = true; if (pos == 168000) p.bypass = false; if (pos == 192000) p.lowCut = 700; if (pos == 216000) p.drive = 0.8; if (pos == 240000) p.mix = 0.9; e.setParams (p); }); Vec part (o.l.begin() + 20000, o.l.end()); check (percept::clickDb (part, fs) < 26, "Every switch is silent", f2 (percept::clickDb (part, fs), 1) + " dB");
    }
    // 7. Reverse: what dies away swells
    { const auto h = drumkit::hit ("snare", fs, 0.5); Vec x ((size_t) (4 * fs), 0.0f); for (int k = 0; k < 3; ++k) std::copy (h.begin(), h.end(), x.begin() + (int) (0.5 * k * fs)); auto p = wet(); p.reverse = true; p.ms = 500; p.feedback = 0; const auto o = render (x, x, p, fs); std::vector<double> e; for (int i = 0; i < 30; ++i) e.push_back (percept::rmsDb (o.l, (size_t) ((0.5 + 0.05 * i) * fs), (size_t) ((0.55 + 0.05 * i) * fs))); int up = 0, down = 0; for (size_t i = 1; i < e.size(); ++i) { if (e[i] > e[i - 1] + 0.5) ++up; if (e[i] < e[i - 1] - 0.5) ++down; } check (up > down, "Reverse: hits swell instead of dying away", std::to_string (up) + " pieces rise, " + std::to_string (down) + " fall"); }
    // 8. Ducking
    { const auto v = voicekit::takes (fs, {}, { 0 }).x; Params p; p.mix = 0.5; p.ms = 330; p.feedback = 0.5; const auto a = render (v, v, p, fs); p.duck = 12; const auto b = render (v, v, p, fs); const double wa = percept::words (v, a.l, fs), wb = percept::words (v, b.l, fs); check (wb > wa + 0.02, "Ducked by 12 dB: words clearer", f2 (wa, 3) + " to " + f2 (wb, 3)); Vec s ((size_t) (3 * fs)); for (size_t i = 0; i < s.size(); ++i) s[i] = (float) (0.3 * std::sin (2 * pi * 330 * (double) i / fs)); auto q = wet(); const auto c = render (s, s, q, fs); q.duck = 9; const auto d = render (s, s, q, fs); const double got = percept::rmsDb (d.l, 96000, 140000) - percept::rmsDb (c.l, 96000, 140000); check (std::abs (got + 9) < 0.1, "Duck 9 dB is 9 dB under a steady sound", f2 (got)); }
    // 9. Grit does not alias: what is added under the echo is hidden by it
    { Vec t ((size_t) (3 * fs)); for (size_t i = 0; i < t.size(); ++i) t[i] = (float) (0.4 * std::sin (2 * pi * 5100 * (double) i / fs)); auto p = wet(); p.feedback = 0; p.drive = 0.8; const auto o = render (t, t, p, fs); double worst = -200; for (double f : { 2400.0, 3000.0, 7500.0, 1200.0, 900.0 }) { double re = 0, im = 0, r0 = 0, i0 = 0; for (int i = 0; i < 96000; ++i) { const double w = 2 * pi * f * i / fs, v = 2 * pi * 5100 * i / fs; re += o.l[(size_t) (40000 + i)] * std::cos (w); im += o.l[(size_t) (40000 + i)] * std::sin (w); r0 += o.l[(size_t) (40000 + i)] * std::cos (v); i0 += o.l[(size_t) (40000 + i)] * std::sin (v); } worst = std::max (worst, percept::db10 ((re * re + im * im) / (r0 * r0 + i0 * i0))); } check (worst < -70, "Grit at 80 % on a 5.1 kHz tone: false tones", f2 (worst, 1) + " dB"); }
    // 10. Rates, blocks, width, CPU
    {
        std::string list; bool ok = true; for (double r : { 44100.0, 96000.0, 192000.0 }) { auto p = wet(); const auto x = ping (r, 1.5); const auto o = render (x, x, p, r); const auto [t, d] = find (o.l, x, r, 0.6, 0.02); ok &= std::abs (t - 0.6) < 1.5e-4 && std::abs (d + 6.02) < 0.6; list += f2 (t * 1000, 2) + " ms " + f2 (d, 1) + "; "; } check (ok, "The second echo at 44.1, 96 and 192 kHz", list);
        const auto x = voicekit::sing (fs).x; Params p; p.mix = 0.6; p.duck = 6; p.drive = 0.4; p.character = Character::tape; p.wobble = 0.5; const auto ref = render (x, x, p, fs); bool same = true; for (int b : { 1, 37, 2048 }) same &= render (x, x, p, fs, b).l == ref.l; check (same, "Same sound at every block size", "");
        for (double r : { 48000.0, 192000.0 }) { const auto y = noise ((int) (6 * r), 0.1, 3); Params q; q.mix = 0.5; q.character = Character::lofi; q.drive = 0.6; q.wobble = 0.5; q.duck = 6; auto e = std::make_unique<Engine>(); e->setParams (q); e->prepare (r); Vec l = y, rr = y; const auto t0 = std::chrono::steady_clock::now(); for (size_t pos = 0; pos + 512 <= l.size(); pos += 512) e->process (l.data() + pos, rr.data() + pos, 512); const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), cpu = 100 * s / ((double) y.size() / r); check (cpu < 5, "CPU at " + f2 (r / 1000, 0) + " kHz", f2 (cpu) + " % of one core"); }
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
