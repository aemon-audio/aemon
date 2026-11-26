// Aemon Rider engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "RiderEngine.h"
#include "VoiceKit.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>

using namespace aemon::rider;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
using Vec = std::vector<float>;
static Vec noise (int n, float amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<float> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = d (g); return v; }
static Vec mul (Vec v, double db) { const float g = (float) std::pow (10.0, db / 20.0); for (auto& x : v) x *= g; return v; }
static void add (Vec& a, const Vec& b, size_t at) { for (size_t i = 0; i < b.size() && at + i < a.size(); ++i) a[at + i] += b[i]; }

/** gain[k] = the ride in dB at sample k * 16 of the output. */
struct Run { Vec l, r; std::vector<double> gain; int lat = 0; };
static Run render (const Vec& xl, const Vec& xr, const Params& p, double fs, const Vec* side = nullptr, int block = 16, const std::function<void (Engine&, size_t)>& each = {})
{
    Run o; o.l = xl; o.r = xr; Engine e; e.setParams (p); e.prepare (fs); o.lat = e.latencySamples();
    for (size_t pos = 0; pos < o.l.size(); pos += (size_t) block)
    {
        if (each) each (e, pos);
        const int n = (int) std::min<size_t> ((size_t) block, o.l.size() - pos);
        e.process (o.l.data() + pos, o.r.data() + pos, n, side ? side->data() + pos : nullptr, side ? side->data() + pos : nullptr);
        o.gain.push_back (e.gainNow());
    }
    return o;
}
static Run render (const Vec& x, const Params& p, double fs, const Vec* side = nullptr) { return render (x, x, p, fs, side); }
static Params good() { Params p; p.even = 1; p.target = -20; p.up = p.down = 12; p.speed = 40; return p; }
/** Body level of a stretch, the measure the rider works with. */
static double level (const Vec& x, double fs, int from, int to) { Biquad h, l; h.set (true, 100, 0.7071, fs); l.set (false, 4000, 0.7071, fs); double s = 0; int n = 0; for (int i = std::max (0, from - (int) (0.2 * fs)); i < to && i < (int) x.size(); ++i) { const double y = l.run (h.run (x[(size_t) i])); if (i >= from) { s += y * y; ++n; } } return dbOf (s / std::max (1, n)); }
static Vec atLevel (Vec v, double fs, double want) { return mul (v, want - level (v, fs, 0, (int) v.size())); }
/** A breath: noise with the colour of one. */
static Vec breath (double fs, double secs, double db, int seed) { auto v = noise ((int) (fs * secs), 1, seed); voicekit::Bp a, b; const int n = (int) v.size(); for (int i = 0; i < n; ++i) { const double x = v[(size_t) i]; v[(size_t) i] = (float) ((a.run (x, 1700, 0.7, fs) + 0.8 * b.run (x, 4800, 0.6, fs)) * std::sin (pi * i / n)); } return atLevel (v, fs, db); }
static double mostMove (const std::vector<double>& g, size_t a, size_t b) { double lo = 1e9, hi = -1e9; for (size_t i = a; i < b && i < g.size(); ++i) { lo = std::min (lo, g[i]); hi = std::max (hi, g[i]); } return hi - lo; }
struct Named { const char* name; voicekit::Kind kind; };
static const Named voices[] { { "middle", {} }, { "low", { 0.5, 0.75, 1.0, 0.92, 0.5, 7 } }, { "high", { 1.8, 1.2, 1.2, 1.15, 0.3, 11 } }, { "soft S", { 0.9, 0.95, 0.55, 1.0, 0.6, 3 } }, { "very sharp S", { 1.2, 1.1, 2.2, 1.0, 0.4, 21 } } };

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. Nothing to do: bit-exact, delayed by the reported latency
    for (double r : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto a = noise (60000, 0.2f, 1), b = noise (60000, 0.2f, 2); bool ok = true;
        Params p; auto o = render (a, b, p, r); Params q = good(); q.bypass = true; auto o2 = render (a, b, q, r); Params t = good(); t.mode = Mode::play; auto o3 = render (a, b, t, r);
        for (auto* x : { &o, &o2, &o3 }) for (size_t i = (size_t) x->lat; i < a.size(); ++i) ok &= x->l[i] == a[i - (size_t) x->lat] && x->r[i] == b[i - (size_t) x->lat];
        check (ok && o.lat == (int) std::lround (0.15 * r), "Even 0, bypass, and an empty written ride: bit-exact at " + f2 (r / 1000, 1) + " kHz", "latency " + std::to_string (o.lat));
    }

    // 2. Never faster, never further, whatever comes in
    {
        std::mt19937 g (5); double worstSlope = -1e9, worstFar = -1e9; int runs = 0;
        for (int k = 0; k < 24; ++k)
        {
            auto v = voicekit::takes (fs, voices[(size_t) k % 5].kind, { 0, -15, 6, -24 }).x; for (size_t i = 0; i < v.size(); i += 12000) { const float q = (float) std::pow (10.0, (double) ((int) (g() % 60) - 40) / 20.0); for (size_t j = i; j < std::min (v.size(), i + 12000); ++j) v[j] *= q; }
            auto side = noise ((int) v.size(), 0.1f, k); Params p; p.even = (double) (g() % 101) / 100; p.follow = (double) (g() % 101) / 100; p.up = (double) (g() % 13); p.down = (double) (g() % 13); p.speed = 2 + (double) (g() % 60); p.words = 100 + (double) (g() % 1500); p.hears = k % 3 == 0 ? Hears::anything : Hears::voice; p.gaps = k % 2 == 0 ? Gaps::hold : Gaps::back; p.target = -24;
            const auto o = render (v, p, fs, k % 4 < 2 ? &side : nullptr); ++runs;
            for (size_t i = 1; i < o.gain.size(); ++i) { worstSlope = std::max (worstSlope, std::abs (o.gain[i] - o.gain[i - 1]) * fs / 16 - p.speed); worstFar = std::max ({ worstFar, o.gain[i] - p.up, -p.down - o.gain[i] }); }
        }
        check (worstSlope < 1e-6 && worstFar < 1e-9, "Never faster than Speed, never further than At most (" + std::to_string (runs) + " random settings and sounds)", "over the speed by " + f2 (std::max (0.0, worstSlope), 6) + " dB/s, over the limit by " + f2 (std::max (0.0, worstFar), 6) + " dB");
    }
    // measured on the sound itself: a tone through the same ride
    {
        Vec v ((size_t) (8 * fs)); for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (0.2 * std::sin (2 * pi * 200 * (double) i / fs) * (((int) ((double) i / fs)) % 2 ? 0.25 : 1.0));
        auto p = good(); p.speed = 10; p.lookMs = 0; const auto o = render (v, p, fs); double worst = 0; const int N = 480; std::vector<double> e; for (size_t i = 0; i + N < v.size(); i += N) { double a = 0, b = 0; for (size_t j = i; j < i + N; ++j) { a += (double) v[j] * v[j]; b += (double) o.l[j] * o.l[j]; } e.push_back (10 * std::log10 (b / a)); }
        for (size_t i = 1; i < e.size(); ++i) worst = std::max (worst, std::abs (e[i] - e[i - 1]) / 0.01);
        check (worst < 10.1 && worst > 8, "Speed measured on the sound: 10 dB per second", f2 (worst) + " dB/s");
    }

    // 3. Words at different levels come together by what Even says
    for (auto& nv : voices)
    {
        // one take, every phrase (a whole take) at another level; speed is there
        const auto one = voicekit::sing (fs, nv.kind).x; const double base = level (one, fs, 0, (int) one.size()); std::string list; bool ok = true;
        for (double even : { 1.0, 0.5 })
        {
            Vec x; std::vector<int> starts; for (double d : { 0.0, -6.0, 6.0, -4.0, 2.0 }) { starts.push_back ((int) x.size()); const auto t = mul (one, -20 - base + d); x.insert (x.end(), t.begin(), t.end()); }
            auto p = good(); p.even = even; p.words = 2500; const auto o = render (x, p, fs); std::vector<double> in, out;
            for (int s : starts) { in.push_back (level (x, fs, s + (int) (2.8 * fs), s + (int) one.size() - 2000)); out.push_back (level (o.l, fs, s + (int) (2.8 * fs) + o.lat, s + (int) one.size() - 2000 + o.lat)); }
            const double spreadIn = *std::max_element (in.begin(), in.end()) - *std::min_element (in.begin(), in.end()), spreadOut = *std::max_element (out.begin(), out.end()) - *std::min_element (out.begin(), out.end()), want = spreadIn * (1 - even);
            ok &= std::abs (spreadOut - want) < 0.7; list += f2 (spreadIn, 1) + " to " + f2 (spreadOut) + " (want " + f2 (want, 1) + ") ";
        }
        check (ok, std::string ("Phrases 12 dB apart come together, ") + nv.name + " voice", list);
    }
    // the same result whatever the level, when the target moves along
    {
        const auto x = atLevel (voicekit::takes (fs, {}, { 0, -8 }).x, fs, -20); auto p = good(); const auto a = render (x, p, fs); p.target = -50; const auto b = render (mul (x, -30), p, fs); double w = 0; for (size_t i = 0; i < a.gain.size(); ++i) w = std::max (w, std::abs (a.gain[i] - b.gain[i]));
        check (w < 0.01, "Everything 30 dB quieter: the same ride", f2 (w, 5) + " dB");
    }

    // 4. Breaths, S, silence, bleed: the ride does not move because of them
    for (auto& nv : voices)
    {
        const auto v = voicekit::sing (fs, nv.kind); const int n = (int) v.x.size(); Vec x = atLevel (v.x, fs, -20);
        double worst = 0; std::string list;
        struct Thing { const char* name; Vec s; };
        Vec bleed ((size_t) (1.5 * fs)); for (size_t i = 0; i < bleed.size(); ++i) { const double t = (double) i / fs; bleed[i] = (float) (std::sin (2 * pi * 110 * t) + 0.5 * std::sin (2 * pi * 220.5 * t) + 0.4 * std::sin (2 * pi * 330 * t)); }
        for (auto& th : { Thing { "quiet breath", breath (fs, 0.5, -38, 3) }, Thing { "loud breath", breath (fs, 0.6, -24, 4) }, Thing { "breath as loud as the voice", breath (fs, 0.6, -20, 5) }, Thing { "silence", Vec ((size_t) (1.5 * fs), 0.0f) }, Thing { "bleed 24 dB down", atLevel (bleed, fs, -44) } })
        {
            Vec y = x; y.resize ((size_t) n + (size_t) (2 * fs), 0.0f); add (y, th.s, (size_t) n + 9600);
            auto p = good(); p.lookMs = 0; const auto o = render (y, p, fs); const double m = mostMove (o.gain, (size_t) (n + 9600) / 16, y.size() / 16); worst = std::max (worst, m); list += std::string (th.name) + " " + f2 (m, 3) + ", ";
        }
        check (worst < 0.05, std::string ("Breaths, silence and bleed do not move it, ") + nv.name + " voice", list + "dB");
        // S sounds inside words: the ride that would have been there without them
        Vec quietS = x, loudS = x; double worstS = 0; for (auto& s : v.sounds) if (s.kind == 's' || s.kind == 'S' || s.kind == 'x') for (int i = s.from; i < s.to; ++i) { loudS[(size_t) i] = (float) (x[(size_t) i] * 2.0); quietS[(size_t) i] = (float) (x[(size_t) i] * 0.5); }
        auto p = good(); const auto a = render (quietS, p, fs), b = render (loudS, p, fs); double rms = 0; for (size_t i = 0; i < a.gain.size(); ++i) { const double d = std::abs (a.gain[i] - b.gain[i]); worstS = std::max (worstS, d); rms += d * d; }
        check (worstS < 1.5 && std::sqrt (rms / (double) a.gain.size()) < 0.4, std::string ("S sounds 12 dB louder hardly change the ride, ") + nv.name + " voice", "most " + f2 (worstS) + ", usual " + f2 (std::sqrt (rms / (double) a.gain.size())) + " dB");
    }
    // a whisper is ignored on purpose; "Anything" hears it
    {
        Vec x = breath (fs, 4, -26, 8); auto p = good(); const auto a = render (x, p, fs); p.hears = Hears::anything; const auto b = render (x, p, fs);
        check (mostMove (a.gain, 0, a.gain.size()) < 0.01 && mostMove (b.gain, 0, b.gain.size()) > 3, "Noise: Voice leaves it alone, Anything rides it", f2 (mostMove (a.gain, 0, a.gain.size())) + " and " + f2 (mostMove (b.gain, 0, b.gain.size())) + " dB");
    }
    // gaps
    {
        Vec x = atLevel (voicekit::sing (fs).x, fs, -26); x.resize (x.size() + (size_t) (3 * fs), 0.0f); auto p = good(); const auto a = render (x, p, fs); p.gaps = Gaps::back; const auto b = render (x, p, fs);
        check (a.gain.back() > 3 && b.gain.back() == 0.0, "Gaps: hold stays, return comes back to exactly 0", f2 (a.gain.back()) + " and " + f2 (b.gain.back()) + " dB");
    }

    // 5. A new phrase is ready before it starts: the lookahead
    {
        const auto v = atLevel (voicekit::sing (fs).x, fs, -20); Vec x = v; x.resize (x.size() + 48000, 0.0f); const size_t at = x.size(); const auto q = mul (v, -8); x.insert (x.end(), q.begin(), q.end());
        auto p = good(); p.speed = 60; auto first = [&] (double ms) { p.lookMs = ms; const auto o = render (x, p, fs); return level (o.l, fs, (int) at + o.lat + 2400, (int) at + o.lat + 2400 + 9600) - level (x, fs, (int) at + 2400, (int) at + 2400 + 9600); };
        const double with = first (150), without = first (0);
        check (with > without + 1.0, "Lookahead: the first word of a quiet phrase is lifted earlier", f2 (with) + " dB against " + f2 (without) + " without");
    }

    // 6. Follow the beat
    {
        Vec v; for (int k = 0; k < 3; ++k) { const auto t = atLevel (voicekit::sing (fs).x, fs, -20); v.insert (v.end(), t.begin(), t.end()); }
        auto beat = atLevel (noise ((int) v.size(), 0.05f, 2), fs, -20); const size_t third = v.size() / 3; for (size_t i = third; i < 2 * third; ++i) beat[i] *= 2.0f;      // 6 dB louder
        Params p; p.follow = 1; p.beat = -20; p.up = p.down = 8; p.speed = 30; const auto o = render (v, p, fs, &beat); const double a = o.gain[(third - 4800) / 16], b = o.gain[(2 * third - 4800) / 16], c = o.gain[(3 * third - 4800) / 16];
        p.follow = 0.5; const double half = render (v, p, fs, &beat).gain[(2 * third - 4800) / 16];
        check (std::abs (a) < 0.4 && std::abs (b - 6.02) < 0.4 && std::abs (c) < 0.4 && std::abs (half - 3.0) < 0.3, "Follow the beat: 6 dB louder beat, 6 dB more voice (3 at half)", f2 (a) + ", " + f2 (b) + ", " + f2 (c) + "; half " + f2 (half));
        p.follow = 1; for (size_t i = third; i < 2 * third; ++i) beat[i] *= 0.0f; const auto br = render (v, p, fs, &beat);
        double far = 0; for (double g : br.gain) far = std::max (far, std::abs (g));
        check (far < 4.0 && std::abs (br.gain.back()) < 0.4, "A break in the beat is not followed", "furthest " + f2 (far) + " dB (8 allowed), at the end " + f2 (br.gain.back()));
        p.follow = 1; const auto none = render (v, p, fs); double any = 0; for (double g : none.gain) any = std::max (any, std::abs (g)); check (any == 0, "Follow without a beat connected does nothing", f2 (any));
    }

    // 7. Write and play: the values sent to the DAW give the same sound back
    for (double speed : { 12.0, 25.0 })
    {
        const auto x = atLevel (voicekit::takes (fs, {}, { 0, -7, 4 }).x, fs, -20); auto p = good(); p.speed = speed; p.mode = Mode::write; p.words = 300;
        // the window sends a value about every 20 ms, at uneven moments, rounded to 0.01 dB; a block is 512 samples
        std::mt19937 g (3); std::vector<std::pair<size_t, double>> sent; size_t next = 0; const int block = 512;
        const auto a = render (x, x, p, fs, nullptr, block, [&] (Engine& e, size_t pos) { if (pos >= next) { sent.push_back ({ pos, std::round (e.gainToWrite() * 100) / 100 }); next = pos + (size_t) (fs * (0.012 + 0.016 * (double) (g() % 100) / 100)); } });
        auto q = p; q.mode = Mode::play; size_t k = 0;
        const auto b = render (x, x, q, fs, nullptr, block, [&] (Engine& e, size_t pos) { while (k < sent.size() && sent[k].first <= pos) { q.played = sent[k].second; ++k; } e.setParams (q); });
        double worst = 0; for (size_t i = 0; i < a.gain.size(); ++i) worst = std::max (worst, std::abs (a.gain[i] - b.gain[i]));
        // the engine's gain at the block ends: how far apart are they at most
        check (worst < 0.3 * std::max (1.0, speed / 12.0) && sent.size() > 200, "Written and played back, speed " + f2 (speed, 0) + " dB/s", "worst " + f2 (worst, 3) + " dB over " + std::to_string (sent.size()) + " points");
    }
    {
        auto p = good(); p.mode = Mode::play; p.played = 5; const auto x = noise (48000, 0.1f, 4); double step = 0; const auto o = render (x, x, p, fs, nullptr, 16, [&] (Engine& e, size_t pos) { if (pos == 16000) { p.played = -9; e.setParams (p); } if (pos == 32000) { p.mode = Mode::ride; e.setParams (p); } });
        for (size_t i = 1; i < o.gain.size(); ++i) step = std::max (step, std::abs (o.gain[i] - o.gain[i - 1]));
        check (step < 0.6 && std::abs (o.gain[15000 / 16] - 5) < 0.01 && std::abs (o.gain[30000 / 16] + 9) < 0.01, "Played values glide, and switching modes does not jump", "largest step in 16 samples " + f2 (step, 3) + " dB");
    }

    // 8. No clicks: the sound of the ride is smooth
    {
        Vec v ((size_t) (4 * fs)); for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (0.4 * std::sin (2 * pi * 150 * (double) i / fs) * (((int) ((double) i / fs * 2)) % 2 ? 0.2 : 1.0)); auto p = good(); p.speed = 60; p.lookMs = 0;
        Vec steady ((size_t) (4 * fs)); for (size_t i = 0; i < steady.size(); ++i) steady[i] = (float) (0.4 * std::sin (2 * pi * 150 * (double) i / fs)); p.target = -30;
        const auto o = render (steady, steady, p, fs, nullptr, 480, [&] (Engine& e, size_t pos) { if (pos == 48000) { p.bypass = true; e.setParams (p); } if (pos == 96000) { p.bypass = false; e.setParams (p); } if (pos == 144000) { p.target = -10; e.setParams (p); } }); double s = 0; for (size_t i = 1; i < o.l.size(); ++i) s = std::max (s, (double) std::abs (o.l[i] - o.l[i - 1])); double top = 0; for (float q : o.l) top = std::max (top, (double) std::abs (q));
        const double limit = top * 2 * pi * 150 / fs * 1.1; check (s < limit, "Bypass and setting changes do not click", "largest step " + f2 (s, 5) + ", limit " + f2 (limit, 5));
    }
    // 9. Same ride at every block size and rate
    {
        const auto x = atLevel (voicekit::takes (fs, {}, { 0, -9 }).x, fs, -20); const auto p = good(); const auto ref = render (x, p, fs); bool same = true; for (int b : { 1, 37, 2048 }) { const auto o = render (x, x, p, fs, nullptr, b); same &= o.l == ref.l; } check (same, "Same sound at every block size", "1, 37, 2048 samples");
        std::string list; double worst = 0; std::vector<double> at48; for (double r : { 48000.0, 44100.0, 96000.0, 192000.0 }) { const auto y = atLevel (voicekit::takes (r, {}, { 0, -9 }).x, r, -20); const auto o = render (y, p, r); std::vector<double> g; for (double t = 1; t < 11; t += 0.05) g.push_back (o.gain[std::min (o.gain.size() - 1, (size_t) (t * r / 16))]); if (at48.empty()) at48 = g; double w = 0; for (size_t i = 0; i < g.size(); ++i) w = std::max (w, std::abs (g[i] - at48[i])); worst = std::max (worst, w); list += f2 (w) + " "; } check (worst < 1.0, "The same ride at 44.1, 96 and 192 kHz", "differences from 48 kHz: " + list + "dB (the synthetic voice itself differs a little)");
    }
    // 10. "Set it for me" hears the level of the words
    for (auto& nv : voices)
    {
        const auto x = atLevel (voicekit::takes (fs, nv.kind, { 0, 0 }).x, fs, -27); const auto h = hear (x, x, fs, Hears::voice); auto p = good(); p.target = h.target; const auto o = render (x, p, fs); const auto after = hear (o.l, o.r, fs, Hears::voice);
        check (h.ok && std::abs (after.target - h.target) < 1.0 && after.spread < 0.667 * h.spread, std::string ("Set to the level of the words: that is where they end up, a third closer or better, ") + nv.name + " voice", "set to " + f2 (h.target, 1) + ", ends at " + f2 (after.target, 1) + " dB, spread " + f2 (h.spread, 1) + " to " + f2 (after.spread, 1));
    }
    {
        const auto n = breath (fs, 8, -24, 3); Vec quiet ((size_t) (5 * fs), 0.0f); check (! hear (n, n, fs, Hears::voice).ok && ! hear (quiet, quiet, fs, Hears::voice).ok, "Hears no voice in breath and silence", "");
    }
    // 11. Odd numbers
    {
        Vec x = noise (48000, 0.2f, 1); Params p = good(); Engine e; e.setParams (p); e.prepare (fs); Vec y = x; e.process (y.data(), y.data(), 0); p.up = p.down = 0; const auto o = render (x, p, fs); bool exact = true; for (size_t i = (size_t) o.lat; i < x.size(); ++i) exact &= o.l[i] == x[i - (size_t) o.lat]; check (exact, "At most 0 dB: bit-exact", "");
    }
    // 12. CPU
    for (double r : { 48000.0, 192000.0 })
    {
        const auto x = atLevel (voicekit::takes (r, {}, { 0, 0 }).x, r, -20); const auto p = good(); Engine e; e.setParams (p); e.prepare (r); Vec l = x, rr = x; const auto t0 = std::chrono::steady_clock::now(); for (size_t pos = 0; pos + 512 <= l.size(); pos += 512) e.process (l.data() + pos, rr.data() + pos, 512, x.data() + pos, x.data() + pos); const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), cpu = 100 * s / ((double) x.size() / r); check (cpu < 5, "CPU at " + f2 (r / 1000, 0) + " kHz with a beat connected", f2 (cpu) + " % of one core");
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
