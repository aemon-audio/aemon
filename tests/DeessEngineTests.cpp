// Aemon De-esser engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "DeessEngine.h"
#include "VoiceKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace aemon::deess;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
using Vec = std::vector<float>;
static Vec noise (int n, float amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<float> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = d (g); return v; }

/** Runs a mono sound through both sides. cut gets the depth in dB at every sample of the input's own time, measured from the engine's frames. */
struct Run { Vec l, r; std::vector<float> cut; int lat = 0; };
static Run render (const Vec& xl, const Vec& xr, const Params& p, double fs, int block = 512, const std::function<void (Engine&, size_t)>& each = {})
{
    Run o; o.l = xl; o.r = xr; Engine e; e.setParams (p); e.prepare (fs); o.lat = e.latencySamples();
    for (size_t pos = 0; pos < o.l.size(); pos += (size_t) block) { if (each) each (e, pos); e.process (o.l.data() + pos, o.r.data() + pos, (int) std::min<size_t> ((size_t) block, o.l.size() - pos)); }
    return o;
}
static Run render (const Vec& x, const Params& p, double fs) { return render (x, x, p, fs); }
/** Change of a stretch in dB, output against input, lined up. */
static double change (const Vec& in, const Run& o, int from, int to) { double a = 0, b = 0; for (int i = from; i < to && i + o.lat < (int) o.l.size(); ++i) { a += (double) in[(size_t) i] * in[(size_t) i]; b += (double) o.l[(size_t) (i + o.lat)] * o.l[(size_t) (i + o.lat)]; } return 10 * std::log10 ((b + 1e-20) / (a + 1e-20)); }
static Params good() { Params p; p.amount = 0.65; p.freq = 6850; p.line = -6; p.range = 10; return p; }

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. Nothing to do: bit-exact
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Params p; auto a = noise (40000, 0.2f, 1), b = noise (40000, 0.2f, 2); auto o = render (a, b, p, r); bool ok = true;
        for (size_t i = (size_t) o.lat; i < a.size(); ++i) ok &= o.l[i] == a[i - (size_t) o.lat] && o.r[i] == b[i - (size_t) o.lat];
        Params q = good(); q.bypass = true; auto o2 = render (a, b, q, r); for (size_t i = (size_t) o.lat; i < a.size(); ++i) ok &= o2.l[i] == a[i - (size_t) o.lat];
        check (ok, "Amount 0 and bypass are bit-exact at " + f2 (r / 1000, 1) + " kHz", "latency " + std::to_string (o.lat) + " samples, " + f2 (1000.0 * o.lat / r, 2) + " ms");
    }

    // 2. Vowels: bit-exact. S sounds: cut.
    for (auto [name, kind] : { std::pair { "middle voice", voicekit::Kind {} }, { "low voice", voicekit::Kind { 0.5, 0.75, 1.0, 0.92 } }, { "high voice", voicekit::Kind { 1.8, 1.25, 1.0, 1.15 } }, { "dull voice, soft S", voicekit::Kind { 0.8, 0.9, 0.5 } } })
    {
        auto v = voicekit::sing (fs, kind); auto p = good(); p.freq = 6850 * kind.sFreq; auto o = render (v.x, p, fs);
        bool exact = true; double worst = 0, sharp = 0; int n = 0;
        for (auto& s : v.sounds) if (s.kind == 'v') { for (int i = s.from; i < s.to; ++i) exact &= o.l[(size_t) (i + o.lat)] == v.x[(size_t) i]; worst = std::max (worst, std::abs (change (v.x, o, s.from, s.to))); }
                                 else if (s.kind == 'S') { sharp += change (v.x, o, s.from, s.to); ++n; }
        check (exact && worst < 0.1, std::string ("Vowels are bit-exact: ") + name, "worst change " + f2 (worst, 4) + " dB, sharp S " + f2 (sharp / n) + " dB");
        if (kind.sGain >= 1) check (sharp / n < -3 && sharp / n > -8, std::string ("Sharp S sounds come down: ") + name, f2 (sharp / n) + " dB");
    }

    // 3. Same S at any level of the singer
    {
        auto v = voicekit::sing (fs); auto p = good(); const auto ref = render (v.x, p, fs); std::string d; double worst = 0;
        for (double g : { -30.0, -12.0, 4.0 })
        {
            Vec x = v.x; for (auto& q : x) q *= (float) std::pow (10.0, g / 20); auto o = render (x, p, fs); double w = 0;
            for (auto& s : v.sounds) if (s.kind != 'v') w = std::max (w, std::abs (change (x, o, s.from, s.to) - change (v.x, ref, s.from, s.to)));
            worst = std::max (worst, w); d += f2 (g, 0) + " dB: " + f2 (w, 3) + "  ";
        }
        check (worst < 0.1, "The same cut whatever the singer's level", "largest difference, " + d);
    }
    {
        auto v = voicekit::takes (fs, {}, { 0, -12, 0, -6, 6 }); auto p = good(); auto o = render (v.x, p, fs); const size_t per = v.sounds.size() / 5; double worst = 0; std::string d;
        for (size_t t = 1; t < 5; ++t) { double w = 0; for (size_t i = 0; i < per; ++i) { auto &a = v.sounds[i], &b = v.sounds[t * per + i]; if (a.kind != 'v') w = std::max (w, std::abs (change (v.x, o, a.from, a.to) - change (v.x, o, b.from, b.to))); } worst = std::max (worst, w); d += f2 (w) + " "; }
        check (worst < 0.5, "One setting through a take that jumps 12 down, 12 up, 6 down, 12 up", "largest difference per jump " + d + "dB");
    }
    {
        // after a pause with room noise 45 dB under the voice, the first S is treated as before
        auto v = voicekit::sing (fs); Vec x = v.x; auto hiss = noise ((int) (2 * fs), 0.0006f, 4); x.insert (x.end(), hiss.begin(), hiss.end());
        const int at = (int) x.size(); const auto S = *std::find_if (v.sounds.begin(), v.sounds.end(), [] (auto& s) { return s.kind == 'S'; });
        for (int i = S.from - 200; i < (int) v.x.size(); ++i) x.push_back (v.x[(size_t) i]);
        auto o = render (x, good(), fs); const double a = change (x, o, S.from, S.to), b = change (x, o, at + 200, at + 200 + S.to - S.from);
        check (std::abs (a - b) < 1.0, "First sound after a two second pause is an S: judged by the singer from before", f2 (a) + " and " + f2 (b) + " dB");
    }

    // 4. The cut is the drawn curve
    {
        double worst = 0; std::string where;
        for (double r : { 44100.0, 48000.0, 96000.0 })
            for (auto shape : { Shape::above, Shape::justS, Shape::whole })
                for (double gd : { 1.0, 6.0, 12.0, 24.0 })
                    for (double f : { 200.0, 1000.0, 3000.0, 4900.0, 6800.0, 9000.0, 14000.0, 18000.0 })
                    {
                        Cut c; const double w0 = Cut::warp (6800, shape, r); double re = 0, im = 0; const int n = (int) r;
                        for (int i = 0; i < 2 * n; ++i) { const double w = 2 * pi * f * i / r, y = c.run (0.5 * std::sin (w), gd, shape, w0); if (i >= n) { re += y * std::cos (w); im += y * std::sin (w); } }
                        const double got = 20 * std::log10 (4.0 / n * std::hypot (re, im)), want = Cut::responseDb (f, gd, shape, 6800, r); if (std::abs (got - want) > worst) { worst = std::abs (got - want); where = f2 (f, 0) + " Hz"; }
                    }
        check (worst < 0.01, "The cut is exactly the curve shown, every shape, depth and rate", "worst " + f2 (worst, 5) + " dB at " + where);
        check (std::abs (Cut::responseDb (300, 10, Shape::above, 6800, fs)) < 0.05 && std::abs (Cut::responseDb (16000, 10, Shape::above, 6800, fs) + 10) < 0.6, "Above: full depth up top, the body of the voice untouched", f2 (Cut::responseDb (300, 10, Shape::above, 6800, fs), 3) + " dB at 300 Hz, " + f2 (Cut::responseDb (16000, 10, Shape::above, 6800, fs)) + " at 16 kHz");
        check (Cut::responseDb (16000, 10, Shape::justS, 6800, fs) > -2.5 && std::abs (Cut::responseDb (6800, 10, Shape::justS, 6800, fs) + 10) < 0.01, "Just S: full depth on the S, the air stays", f2 (Cut::responseDb (6800, 10, Shape::justS, 6800, fs)) + " dB at the S, " + f2 (Cut::responseDb (16000, 10, Shape::justS, 6800, fs)) + " at 16 kHz");
    }

    // 5. At most is a ceiling, Amount a slope
    {
        auto v = voicekit::sing (fs, { 1, 1, 2.0 }); Params p = good(); p.amount = 1; p.line = -24; double deepest[3]; int k = 0;
        for (double rg : { 4.0, 9.0, 20.0 }) { p.range = rg; float d = 0; Vec l = v.x, r = v.x; Engine e; e.setParams (p); e.prepare (fs); for (size_t q = 0; q < l.size(); q += 512) { e.process (l.data() + q, r.data() + q, (int) std::min<size_t> (512, l.size() - q)); d = std::max (d, e.takeFrame().cut); } deepest[k++] = d; }
        check (deepest[0] <= 4.0001 && deepest[0] > 3.9 && deepest[1] <= 9.0001 && deepest[1] > 8.9 && deepest[2] <= 20.0001, "At most is never passed", f2 (deepest[0]) + ", " + f2 (deepest[1]) + ", " + f2 (deepest[2]) + " for 4, 9, 20");
    }

    // 6. The first instant of an S is caught
    {
        auto v = voicekit::sing (fs); std::string d; double first[2]; int k = 0; const auto S = *std::find_if (v.sounds.begin(), v.sounds.end(), [] (auto& s) { return s.kind == 'S'; });
        for (double la : { 0.0, 2.0 }) { auto p = good(); p.lookMs = la; auto o = render (v.x, p, fs); first[k] = change (v.x, o, S.from + (int) (0.004 * fs), S.from + (int) (0.012 * fs)); d += f2 (la, 0) + " ms: " + f2 (first[k]) + " dB  "; ++k; }
        check (first[1] <= first[0] + 0.01 && first[1] < -2.0, "Lookahead catches the start of an S", "5 to 12 ms into it, " + d);
    }

    // 7. Listening
    {
        auto v = voicekit::sing (fs); auto p = good(); const auto wet = render (v.x, p, fs); p.removed = true; const auto rem = render (v.x, p, fs); p.removed = false; p.solo = true; const auto solo = render (v.x, p, fs);
        double worst = 0; for (size_t i = (size_t) wet.lat; i < v.x.size(); ++i) worst = std::max (worst, std::abs ((double) wet.l[i] + rem.l[i] - v.x[i - (size_t) wet.lat]));
        double vow = 0, ess = 1e9; for (auto& s : v.sounds) if (s.kind == 'v') { for (int i = s.from; i < s.to; ++i) vow = std::max (vow, (double) std::abs (solo.l[(size_t) (i + solo.lat)])); } else if (s.kind != 't' && s.kind != 'z') ess = std::min (ess, change (v.x, solo, s.from, s.to) + 100);
        check (worst < 1e-6, "Output plus removed is the input", "worst " + f2 (worst * 1e9, 1) + "e-9");
        check (vow == 0.0 && ess > 97, "Hear only the S: vowels are silent, every S is there", "vowel peak " + f2 (vow, 6) + ", quietest S " + f2 (ess - 100) + " dB");
    }

    // 8. Middle and sides
    {
        auto v = voicekit::sing (fs); auto w = voicekit::sing (fs, { 1.5, 1, 1, 1, 0.3, 9 }); Vec a = v.x, b = v.x; for (size_t i = 0; i < a.size(); ++i) { a[i] += w.x[i]; b[i] -= w.x[i]; }
        bool ok = true; double cutDb = 0; std::string d;
        for (auto on : { On::mid, On::side })
        {
            auto p = good(); p.on = on; auto o = render (a, b, p, fs); double other = 0, e0 = 0, e1 = 0;
            for (size_t i = (size_t) o.lat; i < a.size(); ++i) { const double m0 = (double) a[i - (size_t) o.lat] + b[i - (size_t) o.lat], s0 = (double) a[i - (size_t) o.lat] - b[i - (size_t) o.lat], m1 = (double) o.l[i] + o.r[i], s1 = (double) o.l[i] - o.r[i]; other = std::max (other, std::abs (on == On::mid ? s1 - s0 : m1 - m0)); e0 += on == On::mid ? m0 * m0 : s0 * s0; const double q = on == On::mid ? m1 - m0 : s1 - s0; e1 += q * q; }
            cutDb = 10 * std::log10 (e1 / e0 + 1e-20); ok &= other < 2.5e-7 && cutDb > -60; d += f2 (other * 1e9, 0) + "e-9, removed " + f2 (cutDb, 1) + " dB  ";
        }
        check (ok, "Mid leaves the sides alone, Side leaves the middle alone", d);
    }

    // 9. Every rate, every block size
    {
        std::string d; double worst = 0, ref = 0; for (double r : { 48000.0, 44100.0, 88200.0, 96000.0, 192000.0 }) { auto v = voicekit::sing (r); auto o = render (v.x, good(), r); double c = 0; int n = 0; for (auto& s : v.sounds) if (s.kind == 'S') { c += change (v.x, o, s.from, s.to); ++n; } c /= n; if (r == 48000) ref = c; worst = std::max (worst, std::abs (c - ref)); d += f2 (c) + " "; }
        check (worst < 0.4, "The same cut at 48, 44.1, 88.2, 96 and 192 kHz", d + "dB");
        auto v = voicekit::sing (fs); const auto a = render (v.x, v.x, good(), fs, 512); bool same = true; for (int bl : { 1, 37, 4096 }) { const auto b = render (v.x, v.x, good(), fs, bl); same &= a.l == b.l && a.r == b.r; }
        check (same, "Bit-exact at block sizes 1, 37, 512, 4096", same ? "identical" : "different");
    }

    // 10. Switching anything does not click: a steady tone with a steady S over it, everything changes
    {
        Vec x = noise ((int) (8 * fs), 0.05f, 3); { voicekit::Bp b; for (auto& q : x) q = (float) b.run (q, 7000, 1.2, fs); } for (size_t i = 0; i < x.size(); ++i) x[i] += 0.02f * (float) std::sin (2 * pi * 300 * (double) i / fs);
        Params p = good(); p.fixedLine = true; p.line = -8; p.amount = 1;
        // the tone alone shows a step: read it with a low-pass
        auto low = [&] (const Vec& v) { double z1 = 0, z2 = 0, prev = 0, w = 0; for (size_t i = 0; i < v.size(); ++i) { z1 += 0.05 * (v[i] - z1); z2 += 0.05 * (z1 - z2); if (i > 4000) w = std::max (w, std::abs (z2 - prev)); prev = z2; } return w; };
        const auto still = render (x, x, p, fs);
        const auto moved = render (x, x, p, fs, 480, [&] (Engine& e, size_t pos) { const int k = (int) (pos / 480); Params q = p; if (k > 100) q.shape = Shape::justS; if (k > 160) q.shape = Shape::above; if (k > 220) q.on = On::mid; if (k > 280) q.on = On::stereo; if (k > 340) q.bypass = true; if (k > 400) q.bypass = false; if (k > 460) q.freq = 4000; if (k > 520) q.freq = 9000; if (k > 580) q.amount = 0; if (k > 640) q.range = 2; e.setParams (q); });
        const double a = low (still.l), b = low (moved.l);
        check (b < a * 1.35, "Every switch is silent", "largest step " + f2 (b * 1000, 3) + "e-3, without switching " + f2 (a * 1000, 3) + "e-3");
        const auto mon = render (x, x, p, fs, 480, [&] (Engine& e, size_t pos) { const int k = (int) (pos / 480); Params q = p; q.solo = k > 200 && k < 300; q.removed = k > 400 && k < 500; e.setParams (q); });
        check (low (mon.l) < a * 1.35, "Listening modes fade in and out", "largest step " + f2 (low (mon.l) * 1000, 3) + "e-3");
    }

    // 11. Strictness, fixed line, silence
    {
        auto v = voicekit::sing (fs); auto p = good(); p.line = -20; p.amount = 1; double t[2], z[2]; int k = 0;
        for (double st : { 0.0, 1.0 }) { p.strictness = st; auto o = render (v.x, p, fs); t[k] = z[k] = 0; for (auto& s : v.sounds) { if (s.kind == 't') t[k] += change (v.x, o, s.from, s.to); if (s.kind == 'z') z[k] += change (v.x, o, s.from, s.to); } ++k; }
        check (t[1] <= t[0] && z[1] <= z[0] && z[1] < -3.0, "Strictness decides about T and Z", "T " + f2 (t[0]) + " to " + f2 (t[1]) + " dB, Z " + f2 (z[0]) + " to " + f2 (z[1]) + " dB");
        Vec hat ((size_t) (3 * fs), 0.0f); auto n = noise ((int) hat.size(), 0.5f, 8); voicekit::Bp b; for (size_t i = 0; i < hat.size(); ++i) hat[i] = (float) (b.run (n[i], 9000, 0.7, fs) * std::exp (-std::fmod ((double) i / fs, 0.25) * 60));
        Params h; h.amount = 0.6; h.line = 4; h.range = 6; h.fixedLine = true; h.strictness = 0.7; h.freq = 8500; auto o = render (hat, h, fs); Vec soft = hat; for (auto& q : soft) q *= 0.1f; auto o2 = render (soft, h, fs);
        check (change (hat, o, 0, (int) hat.size() - 1000) < -1.5 && change (soft, o2, 0, (int) hat.size() - 1000) > -0.05, "Fixed line: loud hats come down, quiet ones do not", f2 (change (hat, o, 0, (int) hat.size() - 1000)) + " and " + f2 (change (soft, o2, 0, (int) hat.size() - 1000)) + " dB");
        Vec quiet ((size_t) fs, 0.0f); auto q = render (quiet, good(), fs); double pk = 0; for (float s : q.l) pk = std::max (pk, (double) std::abs (s)); check (pk == 0, "Silence stays silence", f2 (pk));
        auto loud = noise ((int) fs, 8.0f, 12); auto lo = render (loud, good(), fs); bool fin = true; for (float s : lo.l) fin &= std::isfinite (s) && std::abs (s) < 200; check (fin, "Absurd levels stay finite", "ok");
    }

    // 12. Cost
    {
        auto v = voicekit::takes (fs, {}, { 0, 0, 0 }); Vec l = v.x, r = v.x; Engine e; auto p = good(); p.line = -20; e.setParams (p); e.prepare (fs); const auto t0 = std::chrono::steady_clock::now(); for (size_t q = 0; q < l.size(); q += 512) e.process (l.data() + q, r.data() + q, (int) std::min<size_t> (512, l.size() - q));
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), pc = 100 * s * fs / (double) l.size(); check (pc < 5.0, "CPU under 5 % of one core at 48 kHz", f2 (pc) + " %");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
