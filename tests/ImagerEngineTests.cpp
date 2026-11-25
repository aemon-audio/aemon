// Aemon Imager engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "ImagerEngine.h"
#include "VoiceKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace aemon::img;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static double db (double g) { return 20 * std::log10 (std::max (g, 1e-15)); }
using Vec = std::vector<float>;
struct St { Vec l, r; int lat = 0; size_t n() const { return l.size(); } };
/** Music with everything in it: centred kick and voice-like tones, wide pads, stereo noise hits, a wide low end. */
static St music (double fs, double seconds, double sideGain = 1.0, int seed = 3)
{
    St o; const int n = (int) (fs * seconds); o.l.resize ((size_t) n); o.r.resize ((size_t) n); std::mt19937 g ((unsigned) seed); std::normal_distribution<double> nd (0, 1);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, b = std::fmod (t, 0.5), h = std::fmod (t + 0.25, 0.5), m = 0.5 * std::sin (2 * pi * (50 * b + 3 * (1 - std::exp (-b * 30)))) * std::exp (-b * 7) + 0.1 * std::sin (2 * pi * 440 * t) + 0.06 * nd (g) * std::exp (-h * 20);
        const double s = sideGain * (0.07 * std::sin (2 * pi * 331 * t) + 0.05 * std::sin (2 * pi * 2470 * t) + 0.04 * nd (g) * std::exp (-h * 12) + 0.06 * std::sin (2 * pi * 62 * t));
        o.l[(size_t) i] = (float) (m + s); o.r[(size_t) i] = (float) (m - s);
    }
    return o;
}
static St run (const St& in, const Params& p, double fs, int block = 512, const std::function<void (Engine&, size_t)>& each = {})
{
    St o = in; Engine e; e.setParams (p); e.prepare (fs); o.lat = e.latencySamples();
    for (size_t q = 0; q < o.n(); q += (size_t) block) { if (each) each (e, q); e.process (o.l.data() + q, o.r.data() + q, (int) std::min<size_t> ((size_t) block, o.n() - q)); }
    return o;
}
/** Largest difference between the mono sums, and the share of samples whose float sum is the same. */
static double monoError (const St& in, const St& o, double* same = nullptr, size_t from = 0)
{
    double w = 0; size_t eq = 0, c = 0;
    for (size_t i = std::max (from, (size_t) o.lat); i < o.n(); ++i, ++c) { const double a = (double) o.l[i] + o.r[i], b = (double) in.l[i - (size_t) o.lat] + in.r[i - (size_t) o.lat]; w = std::max (w, std::abs (a - b)); eq += (o.l[i] + o.r[i]) == (in.l[i - (size_t) o.lat] + in.r[i - (size_t) o.lat]); }
    if (same) *same = 100.0 * (double) eq / (double) std::max<size_t> (1, c);
    return w;
}
static void power (const St& a, double& m, double& s, size_t from = 0) { m = s = 0; for (size_t i = from; i < a.n(); ++i) { const double x = 0.5 * ((double) a.l[i] + a.r[i]), y = 0.5 * ((double) a.l[i] - a.r[i]); m += x * x; s += y * y; } }
/** A tone in the side (and in the middle): level of the side at the output, dB against the input. */
static double sideGainAt (double f, Params p, double fs)
{
    St in; const int n = (int) (fs * 2); for (int i = 0; i < n; ++i) { const float v = (float) (0.3 * std::sin (2 * pi * f * i / fs)); in.l.push_back (v); in.r.push_back (-v); }
    auto o = run (in, p, fs); double e = 0, g = 0; for (int i = n / 2; i < n; ++i) { const double y = 0.5 * ((double) o.l[(size_t) i] - o.r[(size_t) i]); e += y * y; g += (double) in.l[(size_t) i] * in.l[(size_t) i]; }
    return 10 * std::log10 (e / g + 1e-15);
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;
    const auto mus = music (fs, 4);

    // 1. Nothing to do: bit-exact
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (bool z : { false, true })
        {
            Params p; p.zeroDelay = z; const auto in = music (r, 1.0); auto o = run (in, p, r); bool ok = true; for (size_t i = (size_t) o.lat; i < in.n(); ++i) ok &= o.l[i] == in.l[i - (size_t) o.lat] && o.r[i] == in.r[i - (size_t) o.lat];
            Params q = p; q.width = 2; q.make = 1; q.bypass = true; auto o2 = run (in, q, r); for (size_t i = (size_t) o2.lat; i < in.n(); ++i) ok &= o2.l[i] == in.l[i - (size_t) o2.lat];
            check (ok, std::string ("Neutral and bypass are bit-exact, ") + (z ? "zero delay, " : "straight, ") + f2 (r / 1000, 1) + " kHz", "latency " + std::to_string (o.lat) + ", " + f2 (1000.0 * o.lat / r, 1) + " ms");
        }

    // 2. The promise: the mono sum is the input's, whatever is set
    {
        std::mt19937 g (5); std::uniform_real_distribution<double> u (0, 1); double worst = 0, sameMin = 100; const int trials = 60;
        for (int t = 0; t < trials; ++t)
        {
            Params p; p.width = 2 * u (g); for (auto& b : p.band) b = 2 * u (g); p.monoHz = u (g) < 0.4 ? 20 : 20 + 380 * u (g); p.make = u (g) < 0.4 ? 0 : u (g); p.balance = u (g) < 0.4 ? 0 : u (g) - 0.5; p.safe = u (g) < 0.5; p.limitDb = 1 + 5 * u (g); p.zeroDelay = u (g) < 0.5; p.split = { 60 + 200 * u (g), 600 + 1500 * u (g), 4000 + 6000 * u (g) };
            const auto in = music (fs, 0.8, 0.3 + 2 * u (g), t + 1); double same = 0; worst = std::max (worst, monoError (in, run (in, p, fs), &same)); sameMin = std::min (sameMin, same);
        }
        check (db (worst) < -140, "The mono sum is the input's, 60 random settings", "worst difference " + f2 (db (worst), 1) + " dB, samples with identical float sums: at least " + f2 (sameMin, 1) + " %");
        // ... and while everything moves
        double w = 0; { Params p; auto o = run (mus, p, fs, 96, [&] (Engine& e, size_t pos) { Params q; const double t = (double) pos / fs; q.width = 1 + std::sin (t * 7); q.make = 0.5 + 0.5 * std::sin (t * 5); q.balance = 0.4 * std::sin (t * 3); q.monoHz = 120 + 100 * std::sin (t * 4); q.split = { 150 + 60 * std::sin (t * 6), 1500 + 500 * std::sin (t * 2), 6000 }; q.safe = std::fmod (t, 1.0) > 0.5; e.setParams (q); }); w = monoError (mus, o); }
        check (db (w) < -140, "The mono sum is the input's while everything moves", "worst difference " + f2 (db (w), 1) + " dB");
    }

    // 3. Bands
    for (bool z : { false, true })
    {
        Params p; p.zeroDelay = z; p.safe = true; p.limitDb = 200; auto o = run (mus, p, fs); double w = 0; for (size_t i = (size_t) o.lat + 6000; i < mus.n(); ++i) w = std::max (w, (double) std::abs (o.l[i] - mus.l[i - (size_t) o.lat]));
        check (db (w) < -140, std::string ("Untouched bands add back to the input: ") + (z ? "zero delay" : "straight"), "worst " + f2 (db (w), 1) + " dB");
    }
    {
        double worst = 0; std::string where; Params p; p.band = { 0.3, 1.5, 0.6, 1.9 };
        for (double r : { 44100.0, 48000.0, 96000.0 }) for (double f : { 40.0, 80.0, 150.0, 300.0, 700.0, 1500.0, 3000.0, 6000.0, 10000.0, 16000.0 })
        { double want = 0; for (int b = 0; b < bands; ++b) want += p.band[(size_t) b] * bandGain (b, f, p.split, 20); const double got = sideGainAt (f, p, r), d = std::abs (got - db (want)); if (d > worst) { worst = d; where = f2 (f, 0) + " Hz at " + f2 (r / 1000, 1); } }
        check (worst < 0.1, "Width per band is what the numbers say, 40 Hz to 16 kHz", "worst " + f2 (worst, 3) + " dB, " + where);
        // isolation
        std::string d; double leak = -200; for (auto [f, b] : { std::pair { 60.0, 0 }, { 450.0, 1 }, { 3000.0, 2 }, { 14000.0, 3 } }) for (int k = 0; k < bands; ++k) if (std::abs (k - b) >= 2) { Params q; q.band = { 0, 0, 0, 0 }; q.band[(size_t) k] = 1; leak = std::max (leak, sideGainAt (f, q, fs)); }
        check (leak < -40, "Bands stay out of each other's way (two bands apart)", "worst " + f2 (leak, 1) + " dB");
    }
    {
        Params p; p.monoHz = 100; const double a = sideGainAt (50, p, fs), b = sideGainAt (200, p, fs), c = sideGainAt (1000, p, fs); Params z = p; z.zeroDelay = true; const double az = sideGainAt (50, z, fs);
        check (a < -40 && std::abs (b) < 0.1 && std::abs (c) < 0.01 && az < -11, "Low end to the middle: gone an octave below, untouched an octave above", "straight " + f2 (a, 1) + " dB at 50, " + f2 (b, 3) + " at 200 Hz; zero delay " + f2 (az, 1) + " at 50");
    }
    {
        // straight phase: the middle and the sides stay lined up, a hard-left sound stays hard left when all widths are equal
        Params p; p.band = { 1, 1, 1, 1 }; p.monoHz = 20; p.width = 0.999; St in = music (fs, 1.0, 0); for (auto& v : in.r) v = 0; auto o = run (in, p, fs); double l = 0, r = 0; for (size_t i = (size_t) o.lat + 5000; i < o.n(); ++i) { l += o.l[i] * o.l[i]; r += o.r[i] * o.r[i]; }
        check (10 * std::log10 (r / l) < -60, "Hard left stays hard left", f2 (10 * std::log10 (r / l), 1) + " dB on the right");
    }

    // 4. Width, balance, make stereo
    {
        double m0, s0, m1, s1; power (mus, m0, s0); std::string d; bool ok = true;
        for (double w : { 0.0, 0.5, 1.5, 2.0 }) { Params p; p.width = w; auto o = run (mus, p, fs); power (o, m1, s1, 20000); double a, b; power (mus, a, b, 20000 - 2304); const double got = 10 * std::log10 (s1 / m1 / (s0 / m0) + 1e-15); ok &= w == 0 ? got < -120 : std::abs (got - db (w)) < 0.3; d += f2 (100 * w, 0) + " %: " + f2 (got, 1) + "  "; }
        check (ok, "Width turns the sides up and down", d + "dB");
        St lop = mus; for (auto& v : lop.l) v *= 1.3f; for (auto& v : lop.r) v *= 0.75f; double c = 0, v = 0; for (size_t i = 0; i < lop.n(); ++i) { const double m = 0.5 * ((double) lop.l[i] + lop.r[i]), s = 0.5 * ((double) lop.l[i] - lop.r[i]); c += m * s; v += m * m; }
        Params p; p.balance = c / v; auto o = run (lop, p, fs); double a = 0, b = 0, a0 = 0, b0 = 0; for (size_t i = 24000; i < o.n(); ++i) { a += o.l[i] * o.l[i]; b += o.r[i] * o.r[i]; a0 += lop.l[i] * lop.l[i]; b0 += lop.r[i] * lop.r[i]; }
        check (std::abs (10 * std::log10 (a / b)) < 0.2 && db (monoError (lop, o)) < -140, "Balance centres a lopsided recording, mono unchanged", "from " + f2 (10 * std::log10 (a0 / b0)) + " to " + f2 (10 * std::log10 (a / b)) + " dB");
    }
    {
        const auto voice = voicekit::sing (fs); St in; in.l = in.r = voice.x; std::string d; bool ok = true; double last = -200;
        for (double mk : { 0.2, 0.5, 1.0 }) { Params p; p.make = mk; auto o = run (in, p, fs); double m, s; power (o, m, s); const double r = 10 * std::log10 (s / m); ok &= r > last && db (monoError (in, o)) < -140; last = r; d += f2 (100 * mk, 0) + " %: " + f2 (r, 1) + " dB  "; }
        double lr = 0, ll = 0, rr = 0; { Params p; p.make = 1; auto o = run (in, p, fs); for (size_t i = 0; i < o.n(); ++i) { lr += o.l[i] * o.r[i]; ll += o.l[i] * o.l[i]; rr += o.r[i] * o.r[i]; } }
        const double corr = lr / std::sqrt (ll * rr), bal = 10 * std::log10 (ll / rr);
        check (ok && corr > -0.1 && corr < 0.6 && std::abs (bal) < 1.0, "Make stereo: grows evenly, stays centred, mono is the dry voice", d + "full: correlation " + f2 (corr) + ", balance " + f2 (bal) + " dB");
        Params p; p.make = 1; St low; for (int i = 0; i < (int) fs; ++i) { const float q = (float) (0.3 * std::sin (2 * pi * 50 * i / fs)); low.l.push_back (q); low.r.push_back (q); } auto o = run (low, p, fs); double m, s; power (o, m, s, 24000);
        check (10 * std::log10 (s / m) < -20, "Make stereo leaves the bass in the middle", f2 (10 * std::log10 (s / m), 1) + " dB of side at 50 Hz");
    }

    // 5. Mono-safe
    {
        std::string d; bool ok = true;
        for (bool z : { false, true }) for (double lim : { 1.0, 2.5, 5.0 })
        {
            const auto wide = music (fs, 6, 3.0); Params p; p.width = 1.6; p.safe = true; p.limitDb = lim; p.zeroDelay = z; auto o = run (wide, p, fs); double m, s; power (o, m, s, 48000); const double loss = 10 * std::log10 ((m + s) / m); ok &= loss < lim + 0.3; d += f2 (lim, 1) + ": " + f2 (loss) + "  ";
        }
        check (ok, "Mono-safe holds the loss at the limit", d + "dB");
        Params p; p.safe = true; p.limitDb = 3; const auto narrow = music (fs, 3, 0.25); Params q; q.safe = true; q.limitDb = 200; auto a = run (narrow, p, fs), b = run (narrow, q, fs); double w = 0; for (size_t i = 30000; i < a.n(); ++i) w = std::max (w, (double) std::abs (a.l[i] - b.l[i]));
        double peak = 0; for (float v : narrow.l) peak = std::max (peak, (double) std::abs (v));
        check (db (w / peak) < -24, "Mono-safe leaves a safe sound nearly alone", f2 (db (w / peak), 1) + " dB of change");
    }

    // 6. Listening, silence, blocks, clicks
    {
        Params p; p.width = 1.7; auto o = run (mus, p, fs); p.hearMono = true; auto mono = run (mus, p, fs); p.hearMono = false; p.hearSide = true; auto side = run (mus, p, fs); double w = 0, w2 = 0; for (size_t i = (size_t) o.lat; i < o.n(); ++i) { w = std::max (w, std::abs (0.5 * ((double) mus.l[i - (size_t) o.lat] + mus.r[i - (size_t) o.lat]) - mono.l[i])); w2 = std::max (w2, std::abs (0.5 * ((double) o.l[i] - o.r[i]) - side.l[i])); }
        check (db (w) < -138 && db (w2) < -138 && mono.l == mono.r, "Hear it in mono is the input's mono, only the sides is the rest", f2 (db (w), 1) + " and " + f2 (db (w2), 1) + " dB");
        St z; z.l.assign (48000, 0.0f); z.r = z.l; Params q; q.width = 2; q.make = 1; q.safe = true; auto s = run (z, q, fs); bool quiet = true; for (float v : s.l) quiet &= v == 0; for (float v : s.r) quiet &= v == 0; check (quiet, "Silence stays silence", quiet ? "yes" : "no");
        Params b; b.width = 1.5; b.make = 0.3; b.monoHz = 120; b.safe = true; bool same = true; for (bool zd : { false, true }) { b.zeroDelay = zd; const auto r0 = run (mus, b, fs, 512); for (int bl : { 1, 37, 3000 }) { const auto r1 = run (mus, b, fs, bl); same &= r0.l == r1.l && r0.r == r1.r; } } check (same, "Bit-exact at block sizes 1, 37, 512, 3000", same ? "identical" : "different");
        St loud = music (fs, 1, 1); for (auto& v : loud.l) v *= 100; for (auto& v : loud.r) v *= 100; auto l2 = run (loud, b, fs); bool fin = true; for (float v : l2.l) fin &= std::isfinite (v); check (fin, "Absurd levels stay finite", "ok");
    }
    for (bool z : { false, true })
    {
        St t; for (int i = 0; i < (int) (6 * fs); ++i) { t.l.push_back ((float) (0.3 * std::sin (2 * pi * 220 * i / fs))); t.r.push_back ((float) (0.3 * std::sin (2 * pi * 220 * i / fs + 0.8))); }
        Params p; p.zeroDelay = z;
        auto o = run (t, p, fs, 480, [&] (Engine& e, size_t pos) { const int k = (int) (pos / 480); Params q = p; if (k > 60) q.width = 2; if (k > 110) q.band[1] = 0; if (k > 160) q.make = 1; if (k > 210) q.balance = 0.5; if (k > 260) q.split = { 400, 3000, 9000 }; if (k > 310) q.monoHz = 400; if (k > 360) q.safe = true; if (k > 410) q.hearMono = true; if (k > 460) { q.hearMono = false; q.hearSide = true; } if (k > 510) q = p; if (k > 550) q.bypass = true; e.setParams (q); });
        double step = 0, peak = 0; for (size_t i = 6000; i < o.n(); ++i) { step = std::max (step, (double) std::abs (o.l[i] - o.l[i - 1])); peak = std::max (peak, (double) std::abs (o.l[i])); }
        const double limit = peak * 2 * pi * 220 / fs * 1.6; check (step < limit, std::string ("Every change is silent, ") + (z ? "zero delay" : "straight"), "largest step " + f2 (step, 4) + ", limit " + f2 (limit, 4));
    }
    // 7. Cost
    {
        const auto in = music (fs, 10); for (bool moving : { false, true }) { St o = in; Engine e; Params p; p.width = 1.4; p.make = 0.5; p.safe = true; e.setParams (p); e.prepare (fs); const auto t0 = std::chrono::steady_clock::now(); double slowest = 0;
            for (size_t q = 0; q < o.n(); q += 512) { if (moving) { p.split[1] = 1500 + 400 * std::sin ((double) q / fs * 3); e.setParams (p); } const auto a = std::chrono::steady_clock::now(); e.process (o.l.data() + q, o.r.data() + q, (int) std::min<size_t> (512, o.n() - q)); slowest = std::max (slowest, std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count()); }
            const double pc = 100 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 10; check (pc < (moving ? 20.0 : 5.0), moving ? "CPU while a divider moves" : "CPU under 5 % of one core at 48 kHz", f2 (pc) + " %, slowest block " + f2 (100 * slowest / (512 / fs), 0) + " % of its time"); }
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
