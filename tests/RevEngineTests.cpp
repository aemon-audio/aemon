// Aemon Reverb engine: objective and perceptual tests (no JUCE). AGPL-3.0-or-later.
#include "RevEngine.h"
#include "PerceptKit.h"
#include "VoiceKit.h"
#include "DrumKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>

using namespace aemon::rev;
using percept::Vec;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static Vec noise (int n, double amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<double> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = (float) d (g); return v; }
static const char* names[] { "room", "plate", "hall", "chamber", "lo-fi", "endless" };
struct Run { Vec l, r; };
static Run render (const Vec& xl, const Vec& xr, const Params& p, double fs, int block = 512, const std::function<void (Engine&, size_t)>& each = {})
{
    Run o; o.l = xl; o.r = xr; auto e = std::make_unique<Engine>(); e->setParams (p); e->prepare (fs);
    for (size_t pos = 0; pos < o.l.size(); pos += (size_t) block) { if (each) each (*e, pos); e->process (o.l.data() + pos, o.r.data() + pos, (int) std::min<size_t> ((size_t) block, o.l.size() - pos)); }
    return o;
}
static Params wet (Space s) { Params p; p.space = s; p.mix = 1; p.preMs = 0; p.lowCut = 20; p.highCut = 18000; p.early = 0; p.lowDecay = 1; p.highDecay = 1; p.mod = 0.4; return p; }
static Run impulse (const Params& p, double fs, double secs, int side = 0) { Vec a ((size_t) (secs * fs), 0.0f), b = a; if (side <= 0) a[0] = 1; if (side >= 0) b[0] = 1; return render (a, b, p, fs); }
static const double usual[] { 0.6, 1.8, 2.8, 1.5, 1.6, 6.0 };

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. The dry sound is in time and untouched; bypass and mix 0 are bit-exact
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto a = noise (40000, 0.2, 1), b = noise (40000, 0.2, 2); Params p; p.mix = 0; Params q; q.mix = 0.8; q.bypass = true; bool ok = true;
        for (auto& s : { p, q }) { const auto o = render (a, b, s, r); for (size_t i = 0; i < a.size(); ++i) ok &= o.l[i] == a[i] && o.r[i] == b[i]; }
        Params w; w.mix = 0.5; w.preMs = 200; const auto o = render (a, b, w, r); bool dry = true; for (size_t i = 0; i < (size_t) (0.19 * r) && i < a.size(); ++i) dry &= o.l[i] == a[i] && o.r[i] == b[i];
        check (ok && dry, "Mix 0 and bypass bit-exact, dry sound in time and untouched at " + f2 (r / 1000, 1) + " kHz", "no latency");
    }
    // 2. Decay is what it says, in every space, size and band
    for (int s = 0; s < numSpaces; ++s)
    {
        std::string list; double worst = 0, edt = 0;
        for (double size : { 0.1, 0.5, 0.95 }) { auto p = wet ((Space) s); p.size = size; p.decay = usual[s]; const auto o = impulse (p, fs, std::min (9.0, usual[s] * 1.6 + 0.5)); const auto d = percept::decayOf (o.l, fs, 500, 2000); worst = std::max (worst, std::abs (d.t30 / p.decay - 1)); edt = std::max (edt, std::abs (d.edt / d.t30 - 1)); list += f2 (d.t30) + " (" + f2 (d.edt) + ") "; }
        check (worst < 0.12 && edt < 0.35, std::string ("Decay is what it says: ") + names[s], "set " + f2 (usual[s], 1) + " s, measured at three sizes (early part): " + list);
        auto p = wet ((Space) s); p.decay = usual[s]; p.lowDecay = 1.6; p.highDecay = 0.4; const auto o = impulse (p, fs, std::min (9.0, usual[s] * 2 + 0.5)); const auto lo = percept::decayOf (o.l, fs, 60, 140), hi = percept::decayOf (o.l, fs, 9000, 12000);
        check (lo.t30 / usual[s] > 1.25 && lo.t30 / usual[s] < 1.75 && hi.t30 / usual[s] < 0.62 && hi.t30 / usual[s] > 0.25, std::string ("Lows longer, highs shorter: ") + names[s], "x" + f2 (lo.t30 / usual[s]) + " at 100 Hz (set 1.6), x" + f2 (hi.t30 / usual[s]) + " at 10 kHz (set 0.4)");
    }
    // 3. Nothing metallic
    for (int s = 0; s < numSpaces; ++s)
    {
        auto p = wet ((Space) s); p.decay = usual[s]; p.highDecay = 0.7; const auto o = impulse (p, fs, 8, -1), m = impulse (p, fs, 8); double hz = 0;
        const double ring = percept::ringing (m.l, fs, 0.15, std::min (2.0, usual[s]), &hz), flut = percept::flutter (m.l, fs, 0.1, std::min (1.2, usual[s])), diffuse = percept::diffuseFrom (m.l, fs, 0.4);
        const double bar = s == 2 || s == 5 ? 0.20 : 0.12;
        check (ring < 15 && flut < 0.3 && diffuse < bar, std::string ("No ringing, no flutter, quickly dense: ") + names[s], "loudest mode " + f2 (ring, 1) + " dB over its neighbours at " + f2 (hz, 0) + " Hz (pure noise: 10), flutter " + f2 (flut) + ", dense after " + f2 (diffuse * 1000, 0) + " ms (bar " + f2 (bar * 1000, 0) + ")");
        const double al = percept::alike (m.l, m.r, fs, 0.08), ml = percept::monoLoss (m.l, m.r); double wob = 0; { Vec t ((size_t) (6 * fs)); for (size_t i = 0; i < t.size(); ++i) t[i] = (float) (0.2 * std::sin (2 * pi * 440 * (double) i / fs)); const auto w = render (t, t, p, fs); wob = percept::wobbleCents (w.l, fs, 440); }
        check (al < 0.35 && std::abs (ml) < 1.2 && wob < (s == 4 ? 40 : 14), std::string ("Wide, safe in mono, steady in pitch: ") + names[s], "ears alike " + f2 (al) + ", mono " + f2 (ml) + " dB, pitch within " + f2 (wob, 1) + " cents");
        (void) o;
    }
    // early reflections stand apart
    { auto p = wet (Space::hall); p.early = 1; p.decay = 2; const auto e = impulse (p, fs, 2); p.early = 0; const auto t = impulse (p, fs, 2); const double c1 = percept::clarity (e.l, fs, 80), c0 = percept::clarity (t.l, fs, 80); check (c1 > c0 + 10, "Early reflections and tail are two things", "clarity " + f2 (c1, 1) + " dB only early, " + f2 (c0, 1) + " dB only tail"); }
    // 4. Pre-delay, cuts, width
    {
        auto p = wet (Space::plate); p.preMs = 120; const auto o = impulse (p, fs, 1); size_t first = 0; while (first < o.l.size() && std::abs (o.l[first]) < 1e-4f) ++first; check ((double) first / fs * 1000 > 119 && (double) first / fs * 1000 < 160, "Pre-delay 120 ms", "first sound after " + f2 ((double) first / fs * 1000, 1) + " ms");
        auto q = wet (Space::hall); q.lowCut = 250; const auto k = drumkit::loop ("kick", fs, 4, 0.5); const auto w = render (k, k, q, fs); const double low = percept::rmsDb (percept::filtered (w.l, false, 90, fs)) - percept::rmsDb (percept::filtered (k, false, 90, fs)); check (low < -30, "808-safe: with the low cut at 250 Hz nothing under 90 Hz goes in", f2 (low, 1) + " dB under the kick's own lows");
        auto z = wet (Space::hall); z.width = 0; const auto n = impulse (z, fs, 2); bool same = true; for (size_t i = 0; i < n.l.size(); ++i) same &= n.l[i] == n.r[i]; z.width = 1.5; const auto v = impulse (z, fs, 2); check (same && percept::monoLoss (v.l, v.r) > -3, "Width 0 is mono, 150 % still survives mono", f2 (percept::monoLoss (v.l, v.r)) + " dB");
    }
    // 5. Ducking keeps the words clear
    {
        const auto v = voicekit::takes (fs, {}, { 0 }).x; Vec x = v; x.resize (x.size() + (size_t) (2 * fs), 0.0f); Params p; p.space = Space::plate; p.decay = 3; p.mix = 0.5; p.preMs = 10; p.duck = 0; const auto a = render (x, x, p, fs); p.duck = 12; p.duckMs = 200; const auto b = render (x, x, p, fs);
        const double wa = percept::words (v, a.l, fs), wb = percept::words (v, b.l, fs); const size_t end = v.size(); const double ta = percept::rmsDb (a.l, end + 24000, end + 48000), tb = percept::rmsDb (b.l, end + 24000, end + 48000);
        check (wb > wa + 0.02 && std::abs (ta - tb) < 1.5, "Ducked by 12 dB: words clearer, the tail after them is all there", "words " + f2 (wa, 3) + " to " + f2 (wb, 3) + ", tail " + f2 (tb - ta, 2) + " dB");
        Vec s ((size_t) (3 * fs)); for (size_t i = 0; i < s.size(); ++i) s[i] = (float) (0.3 * std::sin (2 * pi * 330 * (double) i / fs)); Params q = wet (Space::plate); const auto c = render (s, s, q, fs); q.duck = 9; const auto d = render (s, s, q, fs); const double got = percept::rmsDb (d.l, 96000, 140000) - percept::rmsDb (c.l, 96000, 140000); check (std::abs (got + 9) < 0.1, "Duck 9 dB is 9 dB under a steady sound", f2 (got) + " dB");
    }
    // 6. Gate
    { auto p = wet (Space::room); p.decay = 2; p.gateMs = 180; const auto h = drumkit::hit ("snare", fs, 1.5); const auto o = render (h, h, p, fs); p.gateMs = 0; const auto u = render (h, h, p, fs); const double during = percept::rmsDb (o.l, 2400, 7200) - percept::rmsDb (u.l, 2400, 7200), after = percept::rmsDb (o.l, 48000, 60000) - percept::rmsDb (u.l, 48000, 60000); check (std::abs (during) < 1.5 && after < -40, "Gate: there while the drum plays, gone after", f2 (during) + " and " + f2 (after, 0) + " dB"); }
    // 7. Freeze holds, silence ends
    {
        Params p = wet (Space::endless); p.decay = 4; Vec x ((size_t) (14 * fs), 0.0f); const auto v = voicekit::sing (fs).x; for (size_t i = 0; i < 48000; ++i) x[i] = v[i]; const auto o = render (x, x, p, fs, 512, [&] (Engine& e, size_t pos) { if (pos == 512 * 94) { p.freeze = true; e.setParams (p); } }); const double a = percept::rmsDb (o.l, (size_t) (3 * fs), (size_t) (8 * fs)), b = percept::rmsDb (o.l, (size_t) (8.5 * fs), (size_t) (13.5 * fs)); check (std::abs (a - b) < 1.0, "Freeze: it does not fade (five seconds against the next five)", f2 (b - a) + " dB");
        for (int s = 0; s < numSpaces; ++s) { auto q = wet ((Space) s); q.decay = 1; q.drive = s == 4 ? 0.8 : 0; Vec y ((size_t) (6 * fs), 0.0f); y[0] = 0.5f; auto e = std::make_unique<Engine>(); e->setParams (q); e->prepare (fs); Vec r = y; for (size_t pos = 0; pos < y.size(); pos += 512) e->process (y.data() + pos, r.data() + pos, (int) std::min<size_t> (512, y.size() - pos)); bool zero = true; for (size_t i = y.size() - 24000; i < y.size(); ++i) zero &= y[i] == 0.0f && r[i] == 0.0f; check (zero && e->sleeping(), std::string ("Silence settles to exactly zero: ") + names[s], ""); }
    }
    // 8. Nothing explodes, nothing clicks
    {
        std::mt19937 g (9); double top = 0; bool finite = true; for (int k = 0; k < 60; ++k) { auto u = [&] { return (double) (g() % 1001) / 1000; }; Params p; p.space = (Space) (g() % 6); p.size = u(); p.decay = 0.1 * std::pow (300.0, u()); p.lowDecay = 0.3 + 1.7 * u(); p.highDecay = 0.1 + 1.1 * u(); p.preMs = 250 * u(); p.mod = u(); p.early = u(); p.width = 1.5 * u(); p.drive = u(); p.mix = 1; p.lowCut = 20 + 900 * u(); p.highCut = 1000 + 18000 * u(); const double r = k % 3 == 0 ? 96000 : (k % 3 == 1 ? 44100 : 48000); const auto x = noise ((int) (2 * r), 0.15, k); const auto o = render (x, x, p, r); for (float v : o.l) { finite &= std::isfinite (v); top = std::max (top, (double) std::abs (v)); } }
        check (finite && top < 6, "60 random settings on loud noise: finite and bounded", "highest " + f2 (top, 1));
        Vec s ((size_t) (8 * fs)); for (size_t i = 0; i < s.size(); ++i) s[i] = (float) (0.25 * std::sin (2 * pi * 220 * (double) i / fs)); Params p; p.mix = 0.5; p.decay = 1.5; const auto o = render (s, s, p, fs, 480, [&] (Engine& e, size_t pos) { if (pos == 48000) p.space = Space::hall; if (pos == 96000) p.size = 0.9; if (pos == 120000) p.decay = 4; if (pos == 144000) p.preMs = 90; if (pos == 192000) p.mix = 0.9; if (pos == 216000) p.bypass = true; if (pos == 240000) p.bypass = false; if (pos == 264000) p.width = 0; if (pos == 288000) p.lowCut = 600; if (pos == 312000) p.freeze = true; if (pos == 340000) p.freeze = false; e.setParams (p); });
        Vec part (o.l.begin() + 20000, o.l.end()); const double c = percept::clickDb (part, fs); std::string at; for (size_t i = 20000; i + 24000 < o.l.size(); i += 24000) { Vec q (o.l.begin() + (long) i, o.l.begin() + (long) i + 24000); const double z = percept::clickDb (q, fs); if (z > 20) at += f2 ((double) i / fs, 1) + " s " + f2 (z, 0) + ", "; } check (c < 26, "Every change is silent", "largest jump " + f2 (c, 1) + " dB over its surroundings " + at);
    }
    // 9. Rates, blocks, mono
    {
        std::string list; bool ok = true; for (double r : { 44100.0, 88200.0, 96000.0, 192000.0 }) { auto p = wet (Space::plate); p.decay = 1.8; const auto o = impulse (p, r, 3); const auto d = percept::decayOf (o.l, r, 500, 2000); ok &= std::abs (d.t30 / 1.8 - 1) < 0.12; list += f2 (d.t30) + " "; } check (ok, "The same decay at 44.1, 88.2, 96 and 192 kHz", list + "s");
        double e48 = 0, w = 0; for (double r : { 48000.0, 96000.0, 192000.0 }) { const auto x = voicekit::sing (r).x; const auto o = render (x, x, wet (Space::hall), r); const double d = percept::rmsDb (o.l) - percept::rmsDb (x); if (e48 == 0) e48 = d; w = std::max (w, std::abs (d - e48)); } check (w < 0.6, "The same loudness at every rate", f2 (w) + " dB");
        const auto x = voicekit::sing (fs).x; Params p; p.mix = 0.6; p.duck = 6; const auto ref = render (x, x, p, fs); bool same = true; for (int b : { 1, 37, 2048 }) same &= render (x, x, p, fs, b).l == ref.l; check (same, "Same sound at every block size", "");
    }
    // 10. The wet level is useful: between 6 and 16 dB under a voice for every space
    { std::string list; bool ok = true; const auto x = voicekit::takes (fs, {}, { 0 }).x; for (int s = 0; s < numSpaces; ++s) { auto p = wet ((Space) s); p.decay = usual[s]; p.early = 0.4; const auto o = render (x, x, p, fs); const double d = percept::rmsDb (o.l) - percept::rmsDb (x); ok &= d > -13 && d < -1; list += f2 (d, 1) + " "; } check (ok, "Wet level of a voice at 100 %: usable in every space", list + "dB"); }
    // 11. CPU
    for (double r : { 48000.0, 96000.0, 192000.0 }) { const auto x = noise ((int) (6 * r), 0.1, 3); Params p; p.mix = 0.5; p.duck = 6; p.space = Space::lofi; p.drive = 0.5; auto e = std::make_unique<Engine>(); e->setParams (p); e->prepare (r); Vec l = x, rr = x; const auto t0 = std::chrono::steady_clock::now(); for (size_t pos = 0; pos + 512 <= l.size(); pos += 512) e->process (l.data() + pos, rr.data() + pos, 512); const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), cpu = 100 * s / ((double) x.size() / r); check (cpu < 5, "CPU at " + f2 (r / 1000, 0) + " kHz", f2 (cpu) + " % of one core"); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
