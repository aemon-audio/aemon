// Aemon Multiband engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "MbEngine.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace aemon::mb;
static int failures = 0;
static constexpr double PI = 3.14159265358979323846;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static double db (double g) { return 20 * std::log10 (std::max (g, 1e-15)); }
using Vec = std::vector<float>;
struct St { Vec l, r; int lat = 0; size_t n() const { return l.size(); } };
static St tone (double f, double level, double fs, double seconds) { St o; for (int i = 0; i < (int) (fs * seconds); ++i) { const float v = (float) (level * std::sin (2 * PI * f * i / fs)); o.l.push_back (v); o.r.push_back (v); } return o; }
static St music (double fs, double seconds, double gain = 1, int seed = 3)
{
    St o; std::mt19937 g ((unsigned) seed); std::normal_distribution<double> nd (0, 1); double ph = 0;
    for (int i = 0; i < (int) (fs * seconds); ++i)
    {
        const double t = i / fs, b = std::fmod (t, 0.5), h = std::fmod (t + 0.25, 0.5), hh = std::fmod (t, 0.125); ph += 2 * PI * (48 + 90 * std::exp (-b * 30)) / fs;
        double m = 0.5 * std::sin (ph) * std::exp (-b * 6) + 0.2 * nd (g) * std::exp (-h * 16) + 0.06 * nd (g) * std::exp (-hh * 50); for (double f : { 220.0, 277.2, 329.6, 1320.0 }) m += 0.05 * std::sin (2 * PI * f * t);
        o.l.push_back ((float) (gain * (m + 0.02 * std::sin (2 * PI * 3000 * t)))); o.r.push_back ((float) (gain * (m - 0.02 * std::sin (2 * PI * 3000 * t))));
    }
    return o;
}
static St run (const St& in, const Params& p, double fs, int block = 512, const std::function<void (Engine&, size_t)>& each = {}, const St* sc = nullptr)
{
    St o = in; Engine e; e.setParams (p); e.prepare (fs); o.lat = e.latencySamples();
    for (size_t q = 0; q < o.n(); q += (size_t) block) { if (each) each (e, q); const int c = (int) std::min<size_t> ((size_t) block, o.n() - q); e.process (o.l.data() + q, o.r.data() + q, c, sc ? sc->l.data() + q : nullptr, sc ? sc->r.data() + q : nullptr); }
    return o;
}
static double level (const Vec& v, size_t from, size_t to) { double s = 0; for (size_t i = from; i < to && i < v.size(); ++i) s += (double) v[i] * v[i]; return 10 * std::log10 (2 * s / (double) (to - from) + 1e-20); }
static Params flat (int bands = 4) { Params p; p.bands = bands; if (bands == 5) p.split = { 100, 400, 2000, 8000 }; return p; }
/** Same as at rest, but the band filters run: a tiny output gain that is taken off again when measuring. */
static Params running (int bands, bool straight, int slope) { auto p = flat (bands); p.straight = straight; p.slope = slope; p.band[0].ratio = 1.0000001; p.band[0].threshold = 40; return p; }

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000; const auto mus = music (fs, 4);

    // 1. At rest: bit-exact, any structure
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        bool ok = true; std::string lats;
        for (bool st : { false, true }) for (double la : { 0.0, 5.0 }) { auto p = flat (5); p.straight = st; p.lookaheadMs = la; const auto in = music (r, 0.6); auto o = run (in, p, r); for (size_t i = (size_t) o.lat; i < in.n(); ++i) ok &= o.l[i] == in.l[i - (size_t) o.lat] && o.r[i] == in.r[i - (size_t) o.lat]; lats += std::to_string (o.lat) + " "; auto q = p; for (auto& b : q.band) { b.ratio = 8; b.threshold = -40; } q.bypass = true; auto o2 = run (in, q, r); for (size_t i = (size_t) o2.lat; i < in.n(); ++i) ok &= o2.l[i] == in.l[i - (size_t) o2.lat]; }
        check (ok, "At rest and bypass are bit-exact, " + f2 (r / 1000, 1) + " kHz", "latency " + lats);
    }
    // 2. No compression, filters running: flat within 0.01 dB
    for (bool st : { false, true })
        for (int slope : { 24, 48 })
        {
            double worst = 0; for (int bands = 2; bands <= 5; ++bands) for (double f : { 25.0, 60.0, 100.0, 120.0, 400.0, 800.0, 2000.0, 5000.0, 8000.0, 15000.0, 20000.0 }) { auto p = running (bands, st, slope); auto in = tone (f, 0.3, fs, 1.2); auto o = run (in, p, fs); worst = std::max (worst, std::abs (level (o.l, 24000, 48000) - level (in.l, 24000, 48000))); }
            check (worst < 0.01, std::string ("No compression is flat, 2 to 5 bands, ") + (st ? "straight phase, " : "minimum phase, ") + std::to_string (slope) + " dB/oct", "worst " + f2 (worst, 5) + " dB");
        }
    {
        auto p = running (4, true, 24); auto o = run (mus, p, fs); double w = 0; for (size_t i = (size_t) o.lat + 5000; i < o.n(); ++i) w = std::max (w, (double) std::abs (o.l[i] - mus.l[i - (size_t) o.lat])); check (db (w) < -120, "Straight phase with nothing to do nulls against the input", f2 (db (w), 1) + " dB");
    }

    // 3. Static curve in every band, as accurate as the Compressor (tones in the middle of a band, steep splits: the curve, not the crossover)
    {
        const double f[] { 40, 310, 2000, 15000 }; double worst = 0; std::string where;
        for (int b = 0; b < 4; ++b) for (double R : { 1.5, 4.0, 20.0 }) for (double over : { -10.0, -2.0, 0.0, 3.0, 12.0 })
        {
            auto p = flat(); p.slope = 48; p.band[(size_t) b].threshold = -24; p.band[(size_t) b].ratio = R; p.band[(size_t) b].attackMs = 5; p.band[(size_t) b].releaseMs = 300; auto in = tone (f[b], std::pow (10.0, (-24 + over) / 20), fs, 2.5); auto o = run (in, p, fs); Engine e; (void) e;
            const double got = level (o.l, 72000, 96000) - level (in.l, 72000, 96000), want = downGain (-24 + over, -24, R, 6), d = std::abs (got - want); if (d > worst) { worst = d; where = f2 (f[b], 0) + " Hz, ratio " + f2 (R, 1) + ", " + f2 (over, 0) + " dB over"; }
        }
        check (worst < 0.25, "Static curve: down, every band", "worst " + f2 (worst, 3) + " dB, " + where);
        worst = 0; for (int b = 0; b < 4; ++b) for (double R : { 1.5, 3.0 }) for (double under : { -3.0, 2.0, 8.0, 20.0 }) { auto p = flat(); p.slope = 48; p.band[(size_t) b].upThreshold = -30; p.band[(size_t) b].upRatio = R; p.band[(size_t) b].attackMs = 5; p.band[(size_t) b].releaseMs = 100; auto in = tone (f[b], std::pow (10.0, (-30 - under) / 20), fs, 2.5); auto o = run (in, p, fs); const double got = level (o.l, 72000, 96000) - level (in.l, 72000, 96000), want = upGain (-30 - under, -30, R, 6, 12); worst = std::max (worst, std::abs (got - want)); }
        check (worst < 0.3, "Static curve: up, every band", "worst " + f2 (worst, 3) + " dB");
        check (upGain (-90, -30, 4, 6, 12) == 0 && upGain (-62, -30, 4, 6, 12) <= 12 && gainFor (-10, { -20, 4, -60, 1, 10, 100, 0, true, false }, 0, 12) == -7.5, "Near silence is not raised, never above the limit", "0 dB at -90, " + f2 (upGain (-62, -30, 4, 6, 12)) + " at -62");
    }
    // 4. Bands leave each other alone
    {
        auto p = flat(); p.slope = 48; p.band[0].threshold = -40; p.band[0].ratio = 10; p.band[0].attackMs = 1; auto lo = tone (60, 0.3, fs, 2), hi = tone (3000, 0.05, fs, 2); St in = lo; for (size_t i = 0; i < in.n(); ++i) { in.l[i] += hi.l[i]; in.r[i] += hi.r[i]; } auto o = run (in, p, fs);
        auto line = [&] (const Vec& v, double f) { double re = 0, im = 0; for (int i = 48000; i < 96000; ++i) { re += v[(size_t) i] * std::cos (2 * PI * f * i / fs); im += v[(size_t) i] * std::sin (2 * PI * f * i / fs); } return 20 * std::log10 (std::hypot (re, im) / 24000 + 1e-12); };
        const double l = line (o.l, 60) - line (in.l, 60), h = line (o.l, 3000) - line (in.l, 3000); check (l < -20 && std::abs (h) < 0.02, "Compressing the lows does not touch the highs", f2 (l) + " dB at 60 Hz, " + f2 (h, 4) + " dB at 3 kHz");
    }
    // 5. Attack and release
    {
        std::string d; bool ok = true;
        for (double a : { 5.0, 30.0, 100.0 })
        {
            auto p = flat(); p.kneeDb = 0; p.band[2].threshold = -30; p.band[2].ratio = 20; p.band[2].attackMs = a; p.band[2].releaseMs = 200; auto in = tone (2000, 0.0316, fs, 4); for (size_t i = 48000; i < 96000; ++i) { in.l[i] *= 10; in.r[i] *= 10; } std::vector<double> g; run (in, p, fs, 24, [&] (Engine& e, size_t) { g.push_back (e.takeFrame().gain[2]); });      // the gain itself, every half millisecond
            size_t ta = 2000; while (ta < 4000 && g[ta] > -20 * 0.632) ++ta; size_t tr = 4000; while (tr < g.size() - 1 && g[tr] < -20 * 0.368) ++tr; const double at = (double) (ta - 2000) / 2, rt = (double) (tr - 4000) / 2 - 4; ok &= std::abs (at - a) < 0.1 * a + 1.5 && std::abs (rt - 200) < 20; d += f2 (a, 0) + " = " + f2 (at, 1) + " ms, 200 = " + f2 (rt, 0) + "  ";
        }
        check (ok, "Attack and release are the times they say", d);
        auto p = flat(); p.kneeDb = 0; for (auto& q : p.band) { q.threshold = -30; q.ratio = 20; q.attackMs = 1; } p.lookaheadMs = 5; auto in = tone (2000, 0.01, fs, 1.5); for (size_t i = 36000; i < in.n(); ++i) { const double u = std::min (1.0, (double) (i - 36000) / 96.0); const float k = (float) (1 + 49 * (0.5 - 0.5 * std::cos (PI * u))); in.l[i] *= k; in.r[i] *= k; } auto a = run (in, p, fs); p.lookaheadMs = 0; auto b = run (in, p, fs); auto pk = [] (const Vec& v, size_t f, size_t t) { float m = 0; for (size_t i = f; i < t; ++i) m = std::max (m, std::abs (v[i])); return db (m); }; const double steady = pk (a.l, 60000, 66000), oa = pk (a.l, 35000, 40000) - steady, ob = pk (b.l, 35000, 40000) - steady;
        check (oa < 1.0 && ob > 5, "Lookahead catches the start", "overshoot " + f2 (oa) + " dB, without " + f2 (ob) + " dB");
    }
    // 6. Cleanliness: compressing a steady low note does not distort it
    {
        auto p = flat(); p.band[0].threshold = -30; p.band[0].ratio = 4; p.band[0].attackMs = 10; p.band[0].releaseMs = 120; auto in = tone (50, 0.3, fs, 3); auto o = run (in, p, fs); auto line = [&] (double f) { double re = 0, im = 0; for (int i = 48000; i < 96000; ++i) { re += o.l[(size_t) i] * std::cos (2 * PI * f * i / fs); im += o.l[(size_t) i] * std::sin (2 * PI * f * i / fs); } return re * re + im * im; }; double h = 0; for (int k = 2; k < 10; ++k) h += line (50.0 * k); const double thd = 100 * std::sqrt (h / line (50)); check (thd < 1.0, "A compressed 50 Hz note stays clean", "THD " + f2 (thd, 3) + " %");
    }
    // 7. Mix, amount, gain, solo, level match, sidechain
    {
        auto p = flat(); for (auto& b : p.band) { b.threshold = -40; b.ratio = 6; } auto q = p; q.mix = 0; auto o = run (mus, q, fs); bool same = true; for (size_t i = 0; i < mus.n(); ++i) same &= o.l[i] == mus.l[i]; auto a0 = p; a0.amount = 0; auto o2 = run (mus, a0, fs); for (size_t i = 0; i < mus.n(); ++i) same &= o2.l[i] == mus.l[i]; check (same, "Mix 0 and Amount 0 are bit-exact", same ? "identical" : "changed");
        // no combing at half mix: a tone right at a split keeps the level between wet and dry
        double worst = 0; for (double f : { 120.0, 800.0, 5000.0 }) { auto in = tone (f, 0.2, fs, 2); auto w = run (in, p, fs); auto hmix = p; hmix.mix = 0.5; auto m = run (in, hmix, fs); const double wl = std::pow (10.0, level (w.l, 48000, 90000) / 20), dl = std::pow (10.0, level (in.l, 48000, 90000) / 20), ml = std::pow (10.0, level (m.l, 48000, 90000) / 20); worst = std::max (worst, std::abs (db (ml / (0.5 * (wl + dl))))); } check (worst < 0.05, "Half mix does not comb", "worst " + f2 (worst, 4) + " dB");
        auto g = flat(); g.slope = 48; g.band[1].gainDb = 6; auto in = tone (310, 0.1, fs, 1.5); auto go = run (in, g, fs); check (std::abs (level (go.l, 48000, 70000) - level (in.l, 48000, 70000) - 6) < 0.15, "Band gain", f2 (level (go.l, 48000, 70000) - level (in.l, 48000, 70000)) + " dB for 6");
        auto so = flat(); so.slope = 48; so.band[3].solo = true; auto s1 = run (tone (60, 0.3, fs, 1), so, fs); auto s2 = run (tone (10000, 0.3, fs, 1), so, fs); check (level (s1.l, 20000, 40000) < -100 && std::abs (level (s2.l, 20000, 40000) - level (tone (10000, 0.3, fs, 1).l, 20000, 40000)) < 0.1, "Solo plays one band", f2 (level (s1.l, 20000, 40000), 1) + " dB of the lows left");
        auto al = p; al.autoLevel = true; const auto lm = music (fs, 14, 0.7); auto lo = run (lm, al, fs); KWeight k1, k2; k1.prepare (fs); k2.prepare (fs); double a = 0, b = 0; for (size_t i = 0; i < lm.n(); ++i) { const double x = k1.run (lm.l[i]), y = k2.run (lo.l[i]); if (i > lm.n() / 2) { a += x * x; b += y * y; } } check (std::abs (10 * std::log10 (b / a)) < 0.3, "Auto level comes out as loud as it went in", f2 (10 * std::log10 (b / a), 3) + " dB");
        auto sc = flat(); sc.external = true; sc.band[0].threshold = -30; sc.band[0].ratio = 10; sc.band[0].attackMs = 1; auto bass = tone (60, 0.2, fs, 2); St kick = bass; for (size_t i = 0; i < kick.n(); ++i) { const float v = i > 48000 ? 0.5f * (float) std::sin (2 * PI * 55 * (double) i / fs) : 0.0f; kick.l[i] = kick.r[i] = v; } for (bool st : { false, true }) { sc.straight = st; auto d = run (bass, sc, fs, 512, {}, &kick); const double before = level (d.l, 30000, 45000) - level (bass.l, 30000, 45000), after = level (d.l, 80000, 95000) - level (bass.l, 80000, 95000); check (std::abs (before) < 0.05 && after < -15, std::string ("Sidechain: the bass ducks for the kick, ") + (st ? "straight" : "minimum phase"), f2 (before) + " then " + f2 (after) + " dB"); }
    }
    // 8. Blocks, rates, silence, clicks
    {
        auto p = flat (5); for (auto& b : p.band) { b.threshold = -35; b.ratio = 4; b.upThreshold = -45; b.upRatio = 2; } p.autoLevel = true; p.lookaheadMs = 3; bool same = true; for (bool st : { false, true }) { p.straight = st; auto a = run (mus, p, fs, 512); for (int bl : { 1, 37, 3000 }) { auto b = run (mus, p, fs, bl); same &= a.l == b.l && a.r == b.r; } } check (same, "Bit-exact at block sizes 1, 37, 512, 3000", same ? "identical" : "different");
        std::string d; double ref = 0, w = 0; for (double r : { 48000.0, 44100.0, 96000.0, 192000.0 }) { const auto in = music (r, 4); auto q = p; q.autoLevel = false; q.straight = false; auto o = run (in, q, r); const double c = level (o.l, (size_t) r, (size_t) (3.9 * r)) - level (in.l, (size_t) r, (size_t) (3.9 * r)); if (r == 48000) ref = c; w = std::max (w, std::abs (c - ref)); d += f2 (c) + " "; } check (w < 0.6, "The same result at 48, 44.1, 96 and 192 kHz", d + "dB");
        St z; z.l.assign (48000, 0.0f); z.r = z.l; auto s = run (z, p, fs); bool quiet = true; for (float v : s.l) quiet &= v == 0; check (quiet, "Silence stays silence", quiet ? "yes" : "no"); St loud = mus; for (auto& v : loud.l) v *= 100; auto lo = run (loud, p, fs); bool fin = true; for (float v : lo.l) fin &= std::isfinite (v); check (fin, "Absurd levels stay finite", "ok");
    }
    for (bool st : { false, true })
    {
        St t = tone (220, 0.4, fs, 7); Params p = flat(); p.straight = st; for (auto& b : p.band) { b.threshold = -20; b.ratio = 2; b.attackMs = 20; }
        auto o = run (t, p, fs, 480, [&] (Engine& e, size_t pos) { const int k = (int) (pos / 480); auto q = p; if (k > 60) q.split = { 200, 1500, 8000, 12000 }; if (k > 120) q.split = { 60, 250, 3000, 12000 }; if (k > 200) q.band[1].gainDb = 6; if (k > 260) q.band[1].solo = true; if (k > 320) q.band[1].solo = false; if (k > 360) q.mix = 0.2; if (k > 400) q.amount = 0; if (k > 450) q.bypass = true; if (k > 500) { q = p; } if (k > 560) { q.band[0].on = q.band[1].on = false; } if (k > 600) q.autoLevel = true; e.setParams (q); });
        double step = 0, peak = 0; for (size_t i = 8000; i < o.n(); ++i) { step = std::max (step, (double) std::abs (o.l[i] - o.l[i - 1])); peak = std::max (peak, (double) std::abs (o.l[i])); } const double limit = peak * 2 * PI * 220 / fs * 1.5; check (step < limit, std::string ("Moving splits and every switch are silent, ") + (st ? "straight" : "minimum phase"), "largest step " + f2 (step, 4) + ", limit " + f2 (limit, 4));
    }
    // 9. Cost
    for (bool st : { false, true }) { const auto in = music (fs, 10); St o = in; Engine e; auto p = flat (5); p.straight = st; p.autoLevel = true; p.lookaheadMs = 2; for (auto& b : p.band) { b.threshold = -30; b.ratio = 3; b.upThreshold = -45; b.upRatio = 2; } e.setParams (p); e.prepare (fs); const auto t0 = std::chrono::steady_clock::now(); for (size_t q = 0; q < o.n(); q += 512) e.process (o.l.data() + q, o.r.data() + q, (int) std::min<size_t> (512, o.n() - q)); const double pc = 100 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 10; check (pc < 5, std::string ("CPU under 5 % of one core, 5 bands, ") + (st ? "straight" : "minimum phase"), f2 (pc) + " %"); }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
