// Aemon Transient engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "TransEngine.h"
#include "DrumKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace aemon::trans;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
using Vec = std::vector<float>;
static Vec noise (int n, float amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<float> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = d (g); return v; }
static Vec mul (Vec v, double db) { const float g = (float) std::pow (10.0, db / 20.0); for (auto& x : v) x *= g; return v; }
struct Run { Vec l, r; std::vector<double> gain; int lat = 0; };
static Run render (const Vec& xl, const Vec& xr, const Params& p, double fs, int block = 8, const std::function<void (Engine&, size_t)>& each = {})
{
    Run o; o.l = xl; o.r = xr; Engine e; e.setParams (p); e.prepare (fs); o.lat = e.latencySamples();
    for (size_t pos = 0; pos < o.l.size(); pos += (size_t) block) { if (each) each (e, pos); e.process (o.l.data() + pos, o.r.data() + pos, (int) std::min<size_t> ((size_t) block, o.l.size() - pos)); o.gain.push_back (e.gainDb()); }
    return o;
}
static Run render (const Vec& x, const Params& p, double fs) { return render (x, x, p, fs); }
static double change (const Vec& in, const Run& o, int from, int to) { double a = 1e-20, b = 1e-20; for (int i = from; i < to && i + o.lat < (int) o.l.size(); ++i) { a += (double) in[(size_t) i] * in[(size_t) i]; b += (double) o.l[(size_t) (i + o.lat)] * o.l[(size_t) (i + o.lat)]; } return 10 * std::log10 (b / a); }
static const char* drums[] { "kick", "snare", "hat", "tom", "clap" };

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. 0 / 0 is bit-exact
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto fo : { Focus::all, Focus::lows, Focus::highs })
        {
            Params p; p.focus = fo; const auto a = noise (40000, 0.2f, 1), b = noise (40000, 0.2f, 2); Params q; q.attack = 12; q.body = -12; q.focus = fo; q.outDb = 4; q.bypass = true;
            bool ok = true; for (auto& s : { p, q }) { const auto o = render (a, b, s, r); for (size_t i = (size_t) o.lat; i < a.size(); ++i) ok &= o.l[i] == a[i - (size_t) o.lat] && o.r[i] == b[i - (size_t) o.lat]; }
            check (ok, "0 / 0 and bypass are bit-exact at " + f2 (r / 1000, 1) + " kHz, focus " + std::to_string ((int) fo), "");
        }
    { Engine e; e.prepare (fs); check (e.latencySamples() == 96, "Delay at 48 kHz", std::to_string (e.latencySamples()) + " samples, 2 ms"); }

    // 2. Never more than the knobs say, whatever comes in
    {
        std::mt19937 g (4); double over = -1e9;
        for (int k = 0; k < 40; ++k)
        {
            auto x = drumkit::loop (drums[(size_t) k % 5], fs, 6, 0.3); auto n = noise ((int) x.size(), 0.3f, k); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (x[i] * std::pow (10.0, -(double) (g() % 60) / 20 * (i % 9000 == 0 ? 1 : 0)) + (k % 3 == 0 ? n[i] * ((i / 2000) % 3 == 0) : 0.0f));
            Params p; p.attack = (double) (g() % 25) - 12; p.body = (double) (g() % 25) - 12; p.attackMs = 3 + (double) (g() % 78); p.bodyMs = 50 + (double) (g() % 750); const auto o = render (x, p, fs);
            for (double v : o.gain) over = std::max ({ over, v - std::max ({ 0.0, p.attack, p.body }), std::min ({ 0.0, p.attack, p.body }) - v });
        }
        check (over < 1e-9, "Never more than the knobs say (40 random settings and sounds)", "over by " + f2 (std::max (0.0, over), 9) + " dB");
    }
    // 3. The same at every level
    for (auto d : drums)
    {
        const auto x = drumkit::loop (d, fs, 6, 0.4); Params p; p.attack = 8; p.body = -8; const auto a = render (x, p, fs); double worst = 0; for (double db : { -20.0, -40.0, 6.0 }) { const auto b = render (mul (x, db), p, fs); for (size_t i = 0; i < a.gain.size(); ++i) { const size_t s = i * 8 > (size_t) a.lat ? i * 8 - (size_t) a.lat : 0; if (s > 200) { float m = 0; for (size_t j = s - 200; j < std::min (s, x.size()); ++j) m = std::max (m, std::abs (x[j])); if (m > 0.003f) worst = std::max (worst, std::abs (a.gain[i] - b.gain[i])); } } }
        check (worst < 0.1, std::string ("The same curve 40 dB quieter, 20 dB quieter and 6 dB louder: ") + d, "worst " + f2 (worst, 4) + " dB, where the sound is over -50 dB");
    }
    // 4. A steady tone, a chord, steady noise
    for (double f : { 25.0, 50.0, 110.0, 440.0, 5000.0 })
    {
        Vec x ((size_t) (3 * fs)); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.3 * std::sin (2 * pi * f * (double) i / fs)); Params p; p.attack = 12; p.body = 12; const auto o = render (x, p, fs); const double c = change (x, o, (int) fs, (int) (2.8 * fs)); double far = 0; for (size_t i = (size_t) fs / 8; i < o.gain.size(); ++i) far = std::max (far, std::abs (o.gain[i]));
        check (std::abs (c) < 0.05 && far < 0.05, "Steady " + f2 (f, 0) + " Hz tone untouched at +12 / +12", f2 (c, 4) + " dB, furthest " + f2 (far, 4));
    }
    {
        const auto x = noise ((int) (4 * fs), 0.1f, 9); Params p; p.attack = 6; p.body = 6; const auto o = render (x, p, fs); double far = 0; for (size_t i = (size_t) fs / 8; i < o.gain.size(); ++i) far = std::max (far, std::abs (o.gain[i]));
        const double c = change (x, o, (int) fs, (int) (3.8 * fs)); check (std::abs (c) < 0.5 && far < 2.0, "Steady noise hardly touched at +6 / +6", f2 (c) + " dB, furthest " + f2 (far));
    }
    // 5. What the knobs do: the hit, the body
    for (auto d : drums)
    {
        const bool shortOne = std::string (d) == "hat"; const auto x = drumkit::loop (d, fs, 8, 0.5); std::string list; bool ok = true;
        auto hits = [&] (const Run& o, double a, double b) { double s = 0; for (int k = 2; k < 8; ++k) s += change (x, o, (int) ((0.5 * k + a) * fs), (int) ((0.5 * k + b) * fs)) / 6; return s; };
        for (double v : { 6.0, -6.0 })
        {
            Params p; p.attack = v; const auto o = render (x, p, fs); const double h = hits (o, 0, 0.004), b = hits (o, 0.12, 0.3); ok &= std::abs (h - v) < 1.2 && std::abs (b) < 0.6; list += "attack " + f2 (v, 0) + ": hit " + f2 (h) + ", body " + f2 (b) + "; ";
            Params q; q.body = v; const auto o2 = render (x, q, fs); const double h2 = hits (o2, 0, 0.004), b2 = hits (o2, 0.10, 0.25); ok &= std::abs (h2) < 0.3 && b2 * (v > 0 ? 1 : -1) > (shortOne ? 3.0 : 2.5) && std::abs (b2) <= 6.001; list += "body " + f2 (v, 0) + ": hit " + f2 (h2) + ", body " + f2 (b2) + "; ";
        }
        check (ok, std::string ("Attack moves the hit by what it says, Body the rest: ") + d, list);
    }
    // the length of the attack is the length it says: the gain has fallen to a quarter within 1 to 3 times the length
    for (double ms : { 5.0, 20.0, 60.0 })
    {
        Vec x ((size_t) (2 * fs), 0.0f); for (size_t i = 48000; i < x.size(); ++i) x[i] = (float) (0.3 * std::sin (2 * pi * 300 * (double) (i - 48000) / fs)); Params p; p.attack = 8; p.attackMs = ms; const auto o = render (x, p, fs); double t = 0; bool seen = false; for (size_t i = 48000 / 8; i < o.gain.size(); ++i) { if (o.gain[i] > 7) seen = true; if (seen && o.gain[i] < 2) { t = ((double) i * 8 - 48000) / fs * 1000; break; } }
        check (t > 0.8 * ms && t < 3.0 * ms, "Attack length " + f2 (ms, 0) + " ms", "down to a quarter after " + f2 (t, 1) + " ms");
    }
    // 6. No clicks: the gain is a ramp
    {
        double worst = 0; for (auto d : drums) { const auto x = drumkit::loop (d, fs, 6, 0.25); Params p; p.attack = 12; p.body = -12; p.attackMs = 3; const auto o = render (x, p, fs); for (size_t i = 1; i < o.gain.size(); ++i) worst = std::max (worst, std::abs (o.gain[i] - o.gain[i - 1]) / (8 / fs * 1000)); }
        check (worst <= 24.0 + 0.1, "The gain never moves faster than a millisecond's ramp", "fastest " + f2 (worst, 1) + " dB per ms for a swing of 24 dB");
        Vec x ((size_t) (4 * fs)); for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.4 * std::sin (2 * pi * 100 * (double) i / fs)); Params p; p.attack = 9; p.body = 9; const auto o = render (x, x, p, fs, 480, [&] (Engine& e, size_t pos) { if (pos == 48000) { p.focus = Focus::lows; p.outDb = -3; } if (pos == 72000) { p.focus = Focus::highs; p.delta = true; } if (pos == 96000) { p.delta = false; p.attack = -9; } if (pos == 120000) p.bypass = true; if (pos == 144000) { p.bypass = false; p.focus = Focus::all; } e.setParams (p); });
        double s = 0; for (size_t i = 4000; i < o.l.size(); ++i) s = std::max (s, (double) std::abs (o.l[i] - o.l[i - 1])); check (s < 0.4 * 2 * pi * 100 / fs * 1.5, "Every switch is silent", "largest step " + f2 (s, 5) + ", tone " + f2 (0.4 * 2 * pi * 100 / fs, 5));
    }
    // 7. Focus: only the part that is meant
    {
        const auto kick = drumkit::loop ("kick", fs, 6, 0.5), hat = drumkit::loop ("hat", fs, 6, 0.5); Vec x (kick.size()); for (size_t i = 0; i < x.size(); ++i) x[i] = kick[i];
        for (size_t i = 12000; i < x.size(); ++i) x[i] += hat[i - 12000];      // hats between the kicks
        auto at = [&] (Focus f, double a) { Params p; p.attack = a; p.focus = f; return render (x, p, fs); };
        auto hits = [&] (const Run& o, double off, double len) { double s = 0; for (int k = 1; k < 5; ++k) s += change (x, o, (int) ((0.5 * k + off) * fs), (int) ((0.5 * k + off + len) * fs)) / 4; return s; };
        const auto hi = at (Focus::highs, 8), lo = at (Focus::lows, 8); Vec tone ((size_t) (2 * fs)); for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float) (0.5 * std::sin (2 * pi * 60 * (double) i / fs)); Vec both = tone; for (size_t i = 0; i < both.size(); ++i) both[i] += hat[i]; Params q; q.attack = 8; q.focus = Focus::highs; const auto o = render (both, q, fs);
        double re0 = 0, im0 = 0, re = 0, im = 0; for (int i = 48000; i < 48000 + 38400; ++i) { const double w = 2 * pi * 60 * i / fs; re0 += both[(size_t) i] * std::cos (w); im0 += both[(size_t) i] * std::sin (w); re += o.l[(size_t) (i + o.lat)] * std::cos (w); im += o.l[(size_t) (i + o.lat)] * std::sin (w); }
        const double bass = 10 * std::log10 ((re * re + im * im) / (re0 * re0 + im0 * im0));
        check (hits (hi, 0.25, 0.004) > 5 && std::abs (hits (lo, 0.25, 0.004)) < 0.5 && hits (lo, 0.0, 0.02) > 2 && std::abs (bass) < 0.1, "Focus: highs lift the hats and not the bass, lows lift the kick and not the hats", "hats " + f2 (hits (hi, 0.25, 0.004)) + " / " + f2 (hits (lo, 0.25, 0.004)) + ", kick " + f2 (hits (hi, 0.0, 0.02)) + " / " + f2 (hits (lo, 0.0, 0.02)) + ", 60 Hz under the hats " + f2 (bass, 4) + " dB");
    }
    // 8. Delta plus the original is the output
    {
        const auto x = drumkit::loop ("snare", fs, 6, 0.4); Params p; p.attack = 7; p.body = -5; const auto a = render (x, p, fs); p.delta = true; const auto d = render (x, p, fs); double w = 0; for (size_t i = (size_t) a.lat; i < x.size(); ++i) w = std::max (w, (double) std::abs (a.l[i] - d.l[i] - x[i - (size_t) a.lat])); check (w < 1e-6, "Hear the change plus the original is the output", f2 (w, 9));
    }
    // 9. Block sizes, rates, stereo
    {
        const auto x = drumkit::loop ("tom", fs, 5, 0.4); Params p; p.attack = 6; p.body = -6; const auto ref = render (x, p, fs); bool same = true; for (int b : { 1, 37, 2048 }) same &= render (x, x, p, fs, b).l == ref.l; check (same, "Same sound at every block size", "1, 37, 2048");
        std::string list; double worst = 0; for (double r : { 44100.0, 88200.0, 96000.0, 192000.0 }) { const auto y = drumkit::loop ("tom", r, 5, 0.4); const auto o = render (y, p, r); double w = 0; for (int k = 1; k < 4; ++k) for (double t : { 0.0, 0.1, 0.2 }) w = std::max (w, std::abs (change (y, o, (int) ((0.4 * k + t) * r), (int) ((0.4 * k + t + 0.02) * r)) - change (x, ref, (int) ((0.4 * k + t) * fs), (int) ((0.4 * k + t + 0.02) * fs)))); worst = std::max (worst, w); list += f2 (w) + " "; } check (worst < 0.3, "The same at 44.1, 88.2, 96 and 192 kHz", "differences " + list + "dB");
        const auto k = drumkit::loop ("kick", fs, 4, 0.5); Vec quiet = mul (k, -20); const auto o = render (k, quiet, p, fs); double w = 0; for (size_t i = (size_t) o.lat; i < k.size(); ++i) w = std::max (w, (double) std::abs (o.l[i] * 0.1f - o.r[i])); check (w < 1e-6, "Left and right move together: the image stays", f2 (w, 9));
    }
    // 10. CPU
    for (double r : { 48000.0, 192000.0 }) { const auto x = drumkit::loop ("snare", r, 20, 0.5); Params p; p.attack = 6; p.body = 6; p.focus = Focus::mids; Engine e; e.setParams (p); e.prepare (r); Vec l = x, rr = x; const auto t0 = std::chrono::steady_clock::now(); for (size_t pos = 0; pos + 512 <= l.size(); pos += 512) e.process (l.data() + pos, rr.data() + pos, 512); const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), cpu = 100 * s / ((double) x.size() / r); check (cpu < 5, "CPU at " + f2 (r / 1000, 0) + " kHz", f2 (cpu) + " % of one core"); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
