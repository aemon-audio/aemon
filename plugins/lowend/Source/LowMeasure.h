// Aemon Low End: what kick and bass do to each other, measured. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <functional>
#include "LowEngine.h"

namespace aemon::low
{
/** Both tracks around 3 kHz, made by Tap: the same filter on both, so their sum is what it was. */
struct Tracks { std::vector<float> bass, kick; double fs = 3000; };

/** Turns the two tracks into Tracks while they play: steep low-pass at 600 Hz, then every n-th sample. */
class Tap
{
public:
    void prepare (double rate) { step = std::max (1, (int) std::lround (rate / 3000.0)); out = rate / step; for (auto* f : { &b1, &b2, &k1, &k2 }) { f->reset(); f->set (false, 600, rate); } n = 0; }
    double rate() const { return out; }
    /** True when a new pair is ready. */
    bool push (double bass, double kick, float& b, float& k) noexcept { const double x = b2.run (b1.run (bass)), y = k2.run (k1.run (kick)); if (++n < step) return false; n = 0; b = (float) x; k = (float) y; return true; }
private:
    Steep b1, b2, k1, k2; int step = 16, n = 0; double out = 3000;
};

/** Sum of the two in the lows, while the kick sounds, in dB over what they would have if they ignored each other: +3 the most, 0 nothing, below: they cancel. */
class Measure
{
public:
    struct Hit { int from = 0, to = 0; };
    std::vector<float> kick, bass; std::vector<Hit> hits; double fs = 3000;

    void prepare (const Tracks& t, double below)
    {
        fs = t.fs; kick = lows (t.kick, below); raw = t.bass; cut = below; bass = lows (raw, below); hits.clear();
        // hits of the kick: where its lows jump
        const int n = (int) kick.size(), hold = (int) (0.02 * fs); if (n < (int) (0.5 * fs)) return;
        std::vector<float> e ((size_t) n); SlidingMax m; m.prepare (hold); float top = 0; for (int i = 0; i < n; ++i) { e[(size_t) i] = (float) m.push (std::abs (kick[(size_t) i])); top = std::max (top, e[(size_t) i]); }
        if (top < 1e-4f) return;
        const int back = (int) (0.015 * fs), len = (int) (0.16 * fs), edge = (int) ((maxDelayMs + 2) / 1000 * fs) + 4; int last = -len;
        for (int i = std::max (edge, back) + back; i + len + edge < n; ++i)
            if (e[(size_t) i] > 0.35f * top && e[(size_t) (i - back)] < 0.5f * e[(size_t) i] && i - last > (int) (0.12 * fs)) { hits.push_back ({ i - back, i - back + len }); last = i; }
        for (size_t k = 0; k + 1 < hits.size(); ++k) hits[k].to = std::min (hits[k].to, hits[k + 1].from);
    }
    /** The bass, moved. */
    std::vector<float> moved (const Align& a) const
    {
        if (a == Align {}) return bass;
        std::vector<float> x = bass;
        if (a.turn > 0.01) { Turn t; t.set (a.turn, fs); for (auto& v : x) v = (float) t.run (v); }
        std::vector<float> y (x.size(), 0.0f); const double d = a.delayMs * fs / 1000.0; const float s = a.flip ? -1.0f : 1.0f; const int n = (int) x.size();
        for (int i = 0; i < n; ++i) y[(size_t) i] = s * at (x, (double) i - d);
        return y;
    }
    struct Energy { double k = 1e-15, b = 1e-15, r = 0; double db() const { return 10 * std::log10 (std::max (1e-12, (k + b + 2 * r) / (k + b))); } void add (const Energy& o) { k += o.k; b += o.b; r += o.r; } };
    Energy energy (const std::vector<float>& b, const Hit& h) const { Energy e; for (int i = h.from; i < h.to; ++i) { const double x = kick[(size_t) i], y = b[(size_t) i]; e.k += x * x; e.b += y * y; e.r += x * y; } return e; }
    Energy total (const std::vector<float>& b, int parity = -1) const { Energy t; for (size_t i = 0; i < hits.size(); ++i) if (parity < 0 || (int) (i & 1) == parity) t.add (energy (b, hits[i])); return t; }
    double reading (const Align& a) const { return total (moved (a)).db(); }
    /** The reading for every delay from -10 to 10 ms, the rest as given. */
    std::vector<float> curve (Align a, int points) const
    {
        std::vector<float> v ((size_t) points); a.delayMs = 0; const auto x = moved (a);
        for (int p = 0; p < points; ++p) { const double d = (-maxDelayMs + 2 * maxDelayMs * p / (points - 1)) * fs / 1000.0; Energy t; for (auto& h : hits) for (int i = h.from; i < h.to; ++i) { const double k = kick[(size_t) i], b = at (x, (double) i - d); t.k += k * k; t.b += b * b; t.r += k * b; } v[(size_t) p] = (float) t.db(); }
        return v;
    }
    /** The best way to move the bass for these hits. A shorter move is preferred when it costs next to nothing, a turn of the phase only when it is worth a quarter of a dB. */
    Align best (int parity, double& db) const
    {
        struct C { Align a; double db; }; std::vector<C> all;
        for (double turn : { 0.0, 30.0, 60.0, 90.0, 120.0, 150.0 })
        {
            Align a; a.turn = turn; const auto x = moved (a); const int m = (int) (maxDelayMs / 1000 * fs); std::vector<Energy> e ((size_t) (2 * m + 1));
            for (size_t h = 0; h < hits.size(); ++h) if (parity < 0 || (int) (h & 1) == parity)
                for (int d = -m; d <= m; ++d) { auto& t = e[(size_t) (d + m)]; for (int i = hits[h].from; i < hits[h].to; ++i) { const double k = kick[(size_t) i], b = x[(size_t) (i - d)]; t.k += k * k; t.b += b * b; t.r += k * b; } }
            for (int f = 0; f < 2; ++f) for (int d = -m; d <= m; ++d) { auto t = e[(size_t) (d + m)]; if (f) t.r = -t.r; Align q = a; q.flip = f != 0; q.delayMs = d * 1000.0 / fs; all.push_back ({ q, t.db() }); }
        }
        if (all.empty()) { db = 0; return {}; }
        std::sort (all.begin(), all.end(), [] (const C& a, const C& b) { return a.db > b.db; });
        auto cost = [] (const Align& a) { return std::abs (a.delayMs) * 0.02 + (a.flip ? 0.02 : 0.0) + (a.turn > 0 ? 0.25 : 0.0); };
        C pick = all.front(); for (auto& c : all) { if (c.db < all.front().db - 0.5) break; if (c.db - cost (c.a) > pick.db - cost (pick.a)) pick = c; }
        // finer than the grid
        for (double s : { 0.12, 0.04, 0.02 }) for (int k = 0; k < 8; ++k) { bool better = false; for (double sg : { -s, s }) { Align q = pick.a; q.delayMs = std::clamp (q.delayMs + sg, -maxDelayMs, maxDelayMs); const double v = total (moved (q), parity).db(); if (v > pick.db + 1e-7) { pick = { q, v }; better = true; break; } } if (! better) break; }
        pick.a.delayMs = std::round (pick.a.delayMs * 100) / 100; db = total (moved (pick.a), parity).db();
        return pick.a;
    }
private:
    std::vector<float> lows (const std::vector<float>& x, double below) const { Steep l; l.set (false, below, fs); Biquad h; h.set (true, 18, 0.7071, fs); std::vector<float> y (x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = (float) h.run (l.run (x[i])); return y; }
    static float at (const std::vector<float>& x, double p) { const int i = (int) std::floor (p), n = (int) x.size(); if (i < 1 || i + 2 >= n) return 0; const float t = (float) (p - i), a = x[(size_t) (i - 1)], b = x[(size_t) i], c = x[(size_t) (i + 1)], d = x[(size_t) (i + 2)]; return b + 0.5f * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a))); }
    std::vector<float> raw; double cut = 120;
};

/** What to tell and what to do. */
struct Advice
{
    enum class Say { wait, noOverlap, fine, varies, helps } say = Say::wait;
    int hits = 0;
    double now = 0;             // the reading as it is set
    double then = 0;            // the reading with `align`
    double gain = 0;            // improvement proven on hits that were not used to find it
    double overlap = 0;         // bass against kick while the kick sounds, dB
    Align align;
};
/** stop: asked between the steps; when it says so the search ends early and its answer means nothing. */
inline Advice advise (const Measure& m, const Align& current, const std::function<bool()>& stop = {})
{
    Advice o; o.hits = (int) m.hits.size(); if (o.hits < 4) return o;
    const auto nowBass = m.moved (current); const auto t = m.total (nowBass); o.now = t.db(); o.overlap = 10 * std::log10 (t.b / t.k);
    if (std::abs (o.overlap) > 15) { o.say = Advice::Say::noOverlap; return o; }
    const auto enough = [&stop] { return stop && stop(); };
    if (enough()) return o;
    double in = 0; o.align = m.best (-1, in); o.then = in;
    // found on one half of the hits, judged on the other
    double proven = 0; for (int q = 0; q < 2; ++q) { if (enough()) return o; double x = 0; const auto a = m.best (q, x); proven += 0.5 * (m.total (m.moved (a), 1 - q).db() - m.total (nowBass, 1 - q).db()); }
    o.gain = std::min (proven, o.then - o.now);
    // what every hit could reach on its own
    double own = 0; { Measure one = m; for (auto& h : m.hits) { if (enough()) return o; one.hits = { h }; double x = 0; one.best (-1, x); own += (x - m.energy (nowBass, h).db()) / (double) m.hits.size(); } }
    if (o.gain >= 0.7) o.say = Advice::Say::helps;
    else if (own > 1.5 && own > 2.5 * std::max (0.0, o.gain)) o.say = Advice::Say::varies;
    else o.say = Advice::Say::fine;
    return o;
}
} // namespace aemon::low
