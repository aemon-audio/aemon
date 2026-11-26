// Aemon Low End engine and measurement: objective tests (no JUCE). AGPL-3.0-or-later.
#include "LowMeasure.h"
#include "DrumKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>

using namespace aemon::low;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
using Vec = std::vector<float>;
static Vec noise (int n, float amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<float> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = d (g); return v; }
static Vec sine (double fs, double secs, double f, double amp) { Vec v ((size_t) (secs * fs)); for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2 * pi * f * (double) i / fs)); return v; }
struct Run { Vec l, r; std::vector<std::array<double, 3>> cut; int lat = 0; };
static Run render (const Vec& xl, const Vec& xr, const Params& p, double fs, const Vec* kick = nullptr, int block = 8, const std::function<void (Engine&, size_t)>& each = {})
{
    Run o; o.l = xl; o.r = xr; auto e = std::make_unique<Engine>(); e->setParams (p); e->prepare (fs); o.lat = e->latencySamples();
    for (size_t pos = 0; pos < o.l.size(); pos += (size_t) block) { if (each) each (*e, pos); const int n = (int) std::min<size_t> ((size_t) block, o.l.size() - pos); e->process (o.l.data() + pos, o.r.data() + pos, n, kick ? kick->data() + pos : nullptr, kick ? kick->data() + pos : nullptr); o.cut.push_back ({ e->cutDb (0), e->cutDb (1), e->cutDb (2) }); }
    return o;
}
static Run render (const Vec& x, const Params& p, double fs, const Vec* kick = nullptr) { return render (x, x, p, fs, kick); }
static Tracks tracks (const Vec& bass, const Vec& kick, double fs) { Tracks t; Tap tap; tap.prepare (fs); t.fs = tap.rate(); for (size_t i = 0; i < bass.size(); ++i) { float b, k; if (tap.push (bass[i], kick[i], b, k)) { t.bass.push_back (b); t.kick.push_back (k); } } return t; }
/** An 808: a note with a start phase per hit. */
static Vec bass808 (double fs, int hits, double beat, double hz, double phase, double vary, double amp = 0.5, unsigned seed = 3) { std::mt19937 g (seed); std::uniform_real_distribution<double> u (-1, 1); Vec x; for (int k = 0; k < hits; ++k) { const double ph = phase + vary * u (g) * pi; const int n = (int) (beat * fs); for (int i = 0; i < n; ++i) { const double t = i / fs; x.push_back ((float) (amp * std::sin (2 * pi * hz * t + ph) * std::min (1.0, t / 0.004) * std::exp (-t * 2.0) * std::min (1.0, (beat - t) / 0.01))); } } return x; }
static Vec delayed (const Vec& x, double ms, double fs, bool flip = false) { Vec y (x.size(), 0.0f); const int d = (int) std::lround (ms * fs / 1000); for (int i = 0; i < (int) x.size(); ++i) { const int s = i - d; if (s >= 0 && s < (int) x.size()) y[(size_t) i] = (flip ? -1.0f : 1.0f) * x[(size_t) s]; } return y; }
/** Independent: energy of the sum against the energies apart, in the lows, in the same windows. */
static double measured (const Vec& bass, const Vec& kick, double fs, double below, const Measure& m)
{
    auto lp = [&] (const Vec& x) { Steep l, a, b; l.set (false, below, fs); a.set (false, 600, fs); b.set (false, 600, fs); Biquad h; h.set (true, 18, 0.7071, fs); Vec y (x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = (float) h.run (l.run (b.run (a.run (x[i])))); return y; };
    const auto b = lp (bass), k = lp (kick); double s = 0, e = 0; for (auto& q : m.hits) for (int i = (int) (q.from * fs / m.fs); i < (int) (q.to * fs / m.fs) && i < (int) b.size(); ++i) { s += std::pow ((double) b[(size_t) i] + k[(size_t) i], 2); e += std::pow (b[(size_t) i], 2) + std::pow (k[(size_t) i], 2); }
    return 10 * std::log10 (s / e);
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. At rest: bit-exact
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const int n = (int) (r * 0.6); const auto a = noise (n, 0.2f, 1), b = noise (n, 0.2f, 2), k = noise (n, 0.3f, 3); Params p; Params q; q.align = { 3, 60, true }; q.duck = 12; q.bypass = true; bool ok = true; int lat = 0;
        for (auto& s : { p, q }) { const auto o = render (a, b, s, r, &k, 512); lat = o.lat; for (size_t i = (size_t) o.lat; i < a.size(); ++i) ok &= o.l[i] == a[i - (size_t) o.lat] && o.r[i] == b[i - (size_t) o.lat]; }
        check (ok, "No move and no duck, and bypass: bit-exact at " + f2 (r / 1000, 1) + " kHz", "latency " + std::to_string (lat) + " samples, " + f2 (lat / r * 1000, 1) + " ms");
    }
    // 2. Moves are exact
    {
        const auto x = noise (60000, 0.2f, 4); bool ok = true; for (double ms : { -10.0, -3.5, 0.0, 2.25, 10.0 }) for (bool f : { false, true }) { Params p; p.align.delayMs = ms; p.align.flip = f; const auto o = render (x, p, fs); const int d = (int) std::lround (ms * 48); for (size_t i = (size_t) (o.lat + 600); i + 600 < x.size(); ++i) ok &= o.l[i] == (f ? -1.0f : 1.0f) * x[(size_t) ((int) i - o.lat - d)]; }
        check (ok, "Delay both ways and flip: sample-exact", "-10 to +10 ms");
        std::string list; bool okT = true; for (double deg : { 30.0, 90.0, 150.0 }) { const auto s = sine (fs, 2, turnHz, 0.5); Params p; p.align.turn = deg; const auto o = render (s, p, fs); double re = 0, im = 0; for (int i = 0; i < 48000 / 60 * 40; ++i) { const double w0 = 2 * pi * turnHz * i / fs; const size_t at = (size_t) (o.lat + 24000 + i); const double ph = 2 * pi * turnHz * (double) (24000) / fs; re += o.l[at] * std::sin (w0 + ph); im += o.l[at] * std::cos (w0 + ph); } const double got = -std::atan2 (im, re) * 180 / pi; list += f2 (got, 1) + " "; okT &= std::abs (got - deg) < 1; }
        check (okT, "Turn: 30, 90, 150 degrees at 60 Hz", list);
    }
    // 3. The reading is the measured sum
    {
        double worst = 0; std::string list; std::mt19937 g (7);
        for (int k = 0; k < 24; ++k)
        {
            const double beat = 0.45; const auto kick = drumkit::loop ("kick", fs, 14, beat); const double hz = 40 + (double) (g() % 40), ph = (double) (g() % 628) / 100, below = 90 + (double) (g() % 100); const auto b = bass808 (fs, 14, beat, hz, ph, k % 3 == 0 ? 1 : 0, 0.2 + 0.1 * (double) (g() % 6));
            Measure m; m.prepare (tracks (b, kick, fs), below); const double r = m.reading ({}), t = measured (b, kick, fs, below, m); worst = std::max (worst, std::abs (r - t)); if (k < 6) list += f2 (r) + "/" + f2 (t) + " "; if (m.hits.size() < 10) worst = 99;
        }
        check (worst < 0.1, "The reading is the measured sum, 24 pairs of kick and 808", list + "worst " + f2 (worst, 3) + " dB");
        // and with the bass moved by the real engine
        double w2 = 0; for (int k = 0; k < 8; ++k) { const auto kick = drumkit::loop ("kick", fs, 14, 0.45); const auto b = bass808 (fs, 14, 0.45, 48 + 3 * k, 0.7 * k, 0); Params p; p.align = { -6 + 1.6 * k, (double) (k % 4) * 40, k % 2 == 1 }; const auto o = render (b, p, fs); Vec out (b.size(), 0.0f); for (size_t i = 0; i + (size_t) o.lat < o.l.size(); ++i) out[i] = o.l[i + (size_t) o.lat]; Measure m; m.prepare (tracks (b, kick, fs), 120); auto inside = m; while (! inside.hits.empty() && inside.hits.back().to * fs / m.fs > (double) (b.size() - 4000)) inside.hits.pop_back(); const double said = [&] { const auto mv = inside.moved (p.align); return inside.total (mv).db(); }(); w2 = std::max (w2, std::abs (said - measured (out, kick, fs, 120, inside))); }
        check (w2 < 0.1, "The reading with a move is the sum the plugin really makes", "worst " + f2 (w2, 3) + " dB");
    }
    // 4. Finds a known delay and a flip
    {
        const auto kick = drumkit::loop ("kick", fs, 14, 0.45); bool ok = true; std::string list;
        for (double ms : { -7.3, -2.0, -0.4, 0.9, 3.7, 8.1 }) for (bool f : { false, true }) { const auto b = delayed (kick, ms, fs, f); Measure m; m.prepare (tracks (b, kick, fs), 120); const auto a = advise (m, {}); const bool good = std::abs (a.align.delayMs + ms) < 0.1 && a.align.flip == f && a.align.turn == 0 && (a.say == (a.now < 2.3 ? Advice::Say::helps : Advice::Say::fine)) && std::abs (a.then - 3.01) < 0.05; ok &= good; if (! good) list += f2 (ms, 1) + (f ? "f" : "") + " -> " + f2 (a.align.delayMs) + (a.align.flip ? "f" : "") + " " + f2 (a.align.turn, 0) + " say " + std::to_string ((int) a.say) + " gain " + f2 (a.gain) + "; "; }
        check (ok, "A known delay is found within 0.1 ms, a flip always (the kick against itself, 12 cases; under 0.7 dB to gain it is left as it is)", ok ? "all" : list);
    }
    // 5. When it helps, when it cannot
    {
        std::string list; bool ok = true; const auto kick = drumkit::loop ("kick", fs, 18, 0.45);
        for (double ph : { 0.0, 1.0, 2.0, 3.1, 4.2, 5.2 }) { const auto b = bass808 (fs, 18, 0.45, 50, ph, 0); Measure m; m.prepare (tracks (b, kick, fs), 120); const auto a = advise (m, {}); if (a.say == Advice::Say::helps) { Params p; p.align = a.align; const auto o = render (b, p, fs); Vec out (b.size(), 0.0f); for (size_t i = 0; i + (size_t) o.lat < o.l.size(); ++i) out[i] = o.l[i + (size_t) o.lat]; auto inside = m; while (! inside.hits.empty() && inside.hits.back().to * fs / m.fs > (double) (b.size() - 4000)) inside.hits.pop_back(); const double before = measured (b, kick, fs, 120, inside), after = measured (out, kick, fs, 120, inside); ok &= after > before + 0.6; list += f2 (before) + ">" + f2 (after) + " "; } else { ok &= a.then - a.now < 0.8; list += "fine(" + f2 (a.now) + ") "; } }
        check (ok, "The same note on every hit: lined up it really is louder, or it says it is fine", list);
        int varies = 0, wrong = 0; std::string l2; for (unsigned s = 1; s <= 6; ++s) { const auto b = bass808 (fs, 18, 0.45, 52, 0, 1, 0.5, s); Measure m; m.prepare (tracks (b, kick, fs), 120); const auto a = advise (m, {}); varies += a.say == Advice::Say::varies; wrong += a.say == Advice::Say::helps; l2 += f2 (a.now) + "/" + f2 (a.gain) + " "; }
        check (varies >= 5 && wrong == 0, "A free-running 808 (another phase on every hit): says it cannot help", std::to_string (varies) + " of 6 say so, " + std::to_string (wrong) + " wrongly promise; reading/proven " + l2);
        const auto quiet = bass808 (fs, 18, 0.45, 50, 3, 0, 0.02); Measure m; m.prepare (tracks (quiet, kick, fs), 120); const auto a = advise (m, {}); Measure n; n.prepare (tracks (Vec (kick.size(), 0.0f), kick, fs), 120); const auto c = advise (n, {}); Measure e; e.prepare (tracks (quiet, Vec (kick.size(), 0.0f), fs), 120); const auto d = advise (e, {});
        check (a.say == Advice::Say::noOverlap && c.say == Advice::Say::noOverlap && d.say == Advice::Say::wait, "A bass that is hardly there, no bass, no kick: says so", "");
        double ms = 0; { const auto b = bass808 (fs, 18, 0.45, 50, 2, 1); Measure q; q.prepare (tracks (b, kick, fs), 120); const auto t0 = std::chrono::steady_clock::now(); (void) advise (q, {}); ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count(); } check (ms < 250, "Checking takes a moment", f2 (ms, 0) + " ms for 8 seconds");
    }
    // 6. Duck
    {
        const auto kick = drumkit::loop ("kick", fs, 12, 0.5); Vec bass = sine (fs, 6, 50, 0.3); const auto high = sine (fs, 6, 400, 0.2); for (size_t i = 0; i < bass.size(); ++i) bass[i] += high[i];
        std::string list; bool ok = true; for (double depth : { 3.0, 6.0, 12.0, 24.0 }) { Params p; p.duck = depth; const auto o = render (bass, p, fs, &kick); double deep = 0; for (auto& c : o.cut) for (double v : c) deep = std::min (deep, v); ok &= -deep <= depth + 1e-4 && -deep > depth - 0.3; list += f2 (-deep) + " "; }
        check (ok, "Never deeper than it says, and that deep on the kick", list);
        Params p; p.duck = 12; const auto o = render (bass, p, fs, &kick); double worst = 0; for (int f : { 250, 400, 1000, 5000 }) { auto tone = sine (fs, 6, f, 0.2); const auto b50 = sine (fs, 6, 50, 0.3); for (size_t i = 0; i < tone.size(); ++i) tone[i] += b50[i]; const auto q = render (tone, p, fs, &kick); double re = 0, im = 0, r0 = 0, i0 = 0; for (int k = 2; k < 10; ++k) { const int from = (int) (0.5 * k * fs); re = im = r0 = i0 = 0; for (int i = 0; i < 4800; ++i) { const double w = 2 * pi * f * i / fs; const double y = q.l[(size_t) (from + q.lat + i)], x = tone[(size_t) (from + i)]; re += y * std::cos (w); im += y * std::sin (w); r0 += x * std::cos (w); i0 += x * std::sin (w); } worst = std::max (worst, std::abs (10 * std::log10 ((re * re + im * im) / (r0 * r0 + i0 * i0)))); } }
        check (worst < 0.1, "An octave over the line nothing moves (line 120 Hz, 250 Hz to 5 kHz)", "worst " + f2 (worst, 4) + " dB");
        // the duck sits on the kick: deepest within the first 60 ms, gone before the next
        double atHit = 0, later = 0; for (int k = 2; k < 11; ++k) { const size_t h = (size_t) ((0.5 * k * fs + o.lat) / 8); for (size_t i = h; i < h + 360; ++i) for (double v : o.cut[std::min (i, o.cut.size() - 1)]) atHit = std::min (atHit, v); const size_t e = (size_t) (((0.5 * k + 0.47) * fs + o.lat) / 8); if (e < o.cut.size()) for (double v : o.cut[e]) later = std::min (later, v); }
        check (atHit < -11 && later > -1.5, "Ducks on the kick and is back before the next", f2 (atHit) + " dB on the hit, " + f2 (later) + " dB just before the next");
        auto qk = kick; for (auto& v : qk) v *= 0.1f; const auto o2 = render (bass, p, fs, &qk); double diff = 0; for (size_t i = 4000; i < o.cut.size(); ++i) for (size_t b = 0; b < 3; ++b) diff = std::max (diff, std::abs (o.cut[i][b] - o2.cut[i][b])); check (diff < 0.1, "A kick 20 dB quieter ducks the same", f2 (diff, 4) + " dB");
        double step = 0; for (size_t i = 1; i < o.cut.size(); ++i) for (size_t b = 0; b < 3; ++b) step = std::max (step, std::abs (o.cut[i][b] - o.cut[i - 1][b])); check (step / (8 / fs * 1000) < 12.0 / 2.0 + 0.5, "The duck is a ramp, not a step", f2 (step / (8 / fs * 1000)) + " dB per ms at 12 dB");
        // release
        std::string lr; bool okr = true; double prev = 0; for (double rel : { 30.0, 120.0, 350.0 }) { Vec click (sine (fs, 3, 60, 0.0)); for (int i = 0; i < 2400; ++i) click[(size_t) (48000 + i)] = (float) (0.5 * std::sin (2 * pi * 60 * i / fs)); Params q; q.duck = 12; q.clash = false; q.releaseMs = rel; Vec b3 (bass.begin(), bass.begin() + 144000); const auto r2 = render (b3, q, fs, &click); size_t last = 0; for (size_t i = 0; i < r2.cut.size(); ++i) if (r2.cut[i][0] < -0.3) last = i; const double t = ((double) last * 8 - r2.lat - 48000 - 2400) / fs * 1000; okr &= t > rel * 0.8 && t < rel + 60 && t > prev; prev = t; lr += f2 (t, 0) + " "; } check (okr, "Release 30, 120, 350 ms", lr + "ms after the kick ends");
    }
    // 7. Only what clashes
    {
        const auto kick = drumkit::loop ("kick", fs, 12, 0.5); Params p; p.duck = 12; p.below = 160; Vec sub (kick.size(), 0.0f); for (int k = 0; k < 12; ++k) for (int i = 0; i < 12000; ++i) { const double t = i / fs; sub[(size_t) (k * 24000 + i)] = (float) (0.7 * std::sin (2 * pi * 50 * t) * std::min (1.0, t / 0.01) * std::exp (-t * 8)); }
        auto deepest = [&] (const Vec& kk, double hz, bool clash, int w0, int w1) { p.clash = clash; const auto b = sine (fs, 6, hz, 0.3); const auto o = render (b, p, fs, &kk); double lo = 0; for (int k = 3; k < 10; ++k) for (int w = w0; w < w1; ++w) { double x = 0, y = 0; for (int i = 0; i < 960; ++i) { const size_t s = (size_t) (0.5 * k * fs) + (size_t) (w * 960 + i); x += b[s] * b[s]; y += o.l[s + (size_t) o.lat] * o.l[s + (size_t) o.lat]; } lo = std::min (lo, 10 * std::log10 (y / x)); } return lo; };
        const double same = deepest (sub, 52, true, 0, 12), apart = deepest (sub, 140, true, 0, 12), all = deepest (sub, 140, false, 0, 12);
        check (same < -9 && apart > -1.0 && all < -6, "Only what clashes: a bass where the kick is goes down, a bass over it stays", f2 (same) + " dB at 52 Hz, " + f2 (apart) + " dB at 140 Hz (" + f2 (all) + " without the switch)");
        const double s2 = deepest (kick, 52, true, 4, 6), a2 = deepest (kick, 140, true, 4, 6); check (s2 < -6 && a2 > -2.5, "A kick that sweeps down: the bass over it is back when the sweep has passed (80 to 120 ms)", f2 (s2) + " dB at 52 Hz, " + f2 (a2) + " dB at 140 Hz");
    }
    // 8. No kick, no duck; clicks; blocks; rates
    {
        const auto b = sine (fs, 3, 50, 0.3); Params p; p.duck = 24; const auto o = render (b, p, fs); Vec silent (b.size(), 0.0f); const auto o2 = render (b, p, fs, &silent); bool exact = true; for (size_t i = (size_t) o.lat; i < b.size(); ++i) exact &= o.l[i] == b[i - (size_t) o.lat] && o2.l[i] == o.l[i]; check (exact, "Nothing on the sidechain, or silence: bit-exact", "");
        const auto kick = drumkit::loop ("kick", fs, 8, 0.5); const auto s = sine (fs, 4, 80, 0.4); const auto r = render (s, s, p, fs, nullptr, 480, [&] (Engine& e, size_t pos) { if (pos == 48000) p.align.delayMs = 5; if (pos == 72000) p.align.flip = true; if (pos == 96000) p.align.turn = 90; if (pos == 120000) p.bypass = true; if (pos == 144000) p.bypass = false; e.setParams (p); }); double st = 0; for (size_t i = 5000; i < r.l.size(); ++i) st = std::max (st, (double) std::abs (r.l[i] - r.l[i - 1])); check (st < 0.4 * 2 * pi * 80 / fs * 2.2, "Moving the bass while it plays does not click", "largest step " + f2 (st, 5) + ", tone " + f2 (0.4 * 2 * pi * 80 / fs, 5));
        Params q; q.duck = 9; q.align = { 2, 45, true }; const auto ref = render (s, s, q, fs, &kick, 512); bool same = true; for (int bl : { 1, 37, 2048 }) same &= render (s, s, q, fs, &kick, bl).l == ref.l; check (same, "Same sound at every block size", "");
        std::string list; bool ok = true; for (double rr : { 44100.0, 96000.0, 192000.0 }) { const auto k2 = drumkit::loop ("kick", rr, 8, 0.5); const auto b2 = sine (rr, 4, 50, 0.3); Params d; d.duck = 12; const auto o3 = render (b2, b2, d, rr, &k2, 64); double deep = 0; for (auto& c : o3.cut) for (double v : c) deep = std::min (deep, v); ok &= std::abs (deep + 12) < 0.3; list += f2 (deep) + " "; } check (ok, "The same duck at 44.1, 96 and 192 kHz", list);
    }
    // 9. CPU
    for (double r : { 48000.0, 192000.0 }) { const auto kick = drumkit::loop ("kick", r, 20, 0.4); const auto b = sine (r, 8, 50, 0.3); Params p; p.duck = 9; p.align = { 2, 45, true }; auto e = std::make_unique<Engine>(); e->setParams (p); e->prepare (r); Vec l = b, rr = b; const auto t0 = std::chrono::steady_clock::now(); for (size_t pos = 0; pos + 512 <= l.size(); pos += 512) e->process (l.data() + pos, rr.data() + pos, 512, kick.data() + pos, kick.data() + pos); const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), cpu = 100 * s / ((double) b.size() / r); check (cpu < (r < 50000 ? 5 : 15), "CPU at " + f2 (r / 1000, 0) + " kHz", f2 (cpu) + " % of one core"); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
