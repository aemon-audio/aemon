// Aemon Saturation: signal engine. AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own. Allocation-free once prepared.
//
// tone before -> curve (clean distortion core) -> tone after, blended with the untouched sound, lined up to the
// sample. The curve grows out of a straight line, so at zero drive nothing is added and the sound is bit-exact.
// Level matching compares loudness (K-weighted) over about 1.5 seconds.
#pragma once

#include "aemon_Distortion.h"

namespace aemon::sat
{

enum class Character { warm = 0, thick, bright, broken };
enum class Focus { all = 0, lows, mids, highs };

struct Params
{
    double driveDb = 0.0;       // 0..30
    Character character = Character::warm;
    Focus focus = Focus::all;
    double bias = 0.5;          // 0..2: odd .. even
    double toneDb = 0.0;        // -6..6, darker or brighter result
    double mix = 1.0;
    double outputDb = 0.0;
    int oversampling = 8;
    bool match = true;
    bool bypass = false;
};

/** The shape, with its antiderivative. Slope 1 at the origin for every setting. */
struct Curve
{
    Character ch = Character::warm;
    double k = 0.0, b = 0.0, f0 = 0.0, P0 = 0.0, inv = 1.0, mk = 1.0, c1 = 1.0, c2 = 1.0, c3 = 0.0;

    static double shape (Character c, double x)
    {
        switch (c)
        {
            case Character::warm: return std::tanh (x);
            case Character::thick: return x / std::sqrt (1.0 + x * x);
            case Character::bright: return (2.0 / pi) * std::atan (0.5 * pi * x);
            case Character::broken: { const double u = 1.1 * x; return u > foldEnd ? std::sin (foldEnd) : (u < -foldEnd ? -std::sin (foldEnd) : std::sin (u)); }
        }
        return x;
    }
    static double integral (Character c, double x)
    {
        const double a = std::abs (x);
        switch (c)
        {
            case Character::warm: return a > 19.0 ? a - 0.69314718055994531 : a + std::log (0.5 + 0.5 * std::exp (-2.0 * a));
            case Character::thick: return std::sqrt (1.0 + x * x);
            case Character::bright: { const double u = 0.5 * pi * x; return (2.0 / pi) * (2.0 / pi) * (u * std::atan (u) - 0.5 * std::log1p (u * u)); }
            case Character::broken: { const double u = 1.1 * a; return (u > foldEnd ? (1.0 - std::cos (foldEnd)) + (u - foldEnd) * std::sin (foldEnd) : 1.0 - std::cos (u)) / 1.1; }
        }
        return 0.5 * x * x;
    }
    static double biasOf (Character c) { constexpr double v[] { 0.28, 0.08, 0.12, 0.18 }; return v[(int) c]; }

    void set (Character c, double driveDb, double bias)
    {
        ch = c;
        k = 1.6 * (std::pow (10.0, driveDb / 20.0) - 1.0);
        b = biasOf (c) * bias * 2.0;
        f0 = shape (c, b); P0 = integral (c, b);
        const double h = 1e-4, slope = (shape (c, b + h) - shape (c, b - h)) / (2 * h);
        inv = 1.0 / std::max (0.05, slope);
        mk = std::pow (std::max (1.0, k), 0.25);
        if (k >= 1e-4) { c1 = mk * inv / k; c2 = c1 / k; c3 = c1 * f0; }
    }
    bool flat() const { return k < 1e-4; }
    inline double f (double x) const { return flat() ? x : c1 * (shape (ch, k * x + b) - f0); }
    /** Antiderivative of f(x) - x. */
    inline double D (double x) const
    {
        if (flat()) return 0.0;
        return c2 * (integral (ch, k * x + b) - P0) - c3 * x - 0.5 * x * x;
    }
    static constexpr double pi = 3.14159265358979323846, foldEnd = 2.4;
};

/** Biquad with smooth coefficients (RBJ shapes), transposed direct form. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void clear() { z1 = z2 = 0; }
    inline double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    /** Level at a frequency, as a factor. */
    double gainAt (double f, double fs) const
    {
        const double w = 2 * Curve::pi * f / fs, c = std::cos (w), s = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c + b2 * c2, ni = b1 * s + b2 * s2, dr = 1 + a1 * c + a2 * c2, di = a1 * s + a2 * s2;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
    enum class T { peak, low, high };
    void set (T t, double f, double gainDb, double q, double fs)
    {
        if (std::abs (gainDb) < 1e-6) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }
        const double A = std::pow (10.0, gainDb / 40), w = 2 * Curve::pi * std::min (f, 0.45 * fs) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), s2 = 2 * std::sqrt (A) * al;
        double n0, n1, n2, d0, d1, d2;
        if (t == T::peak) { n0 = 1 + al * A; n1 = -2 * c; n2 = 1 - al * A; d0 = 1 + al / A; d1 = -2 * c; d2 = 1 - al / A; }
        else if (t == T::low) { n0 = A * ((A + 1) - (A - 1) * c + s2); n1 = 2 * A * ((A - 1) - (A + 1) * c); n2 = A * ((A + 1) - (A - 1) * c - s2); d0 = (A + 1) + (A - 1) * c + s2; d1 = -2 * ((A - 1) + (A + 1) * c); d2 = (A + 1) + (A - 1) * c - s2; }
        else { n0 = A * ((A + 1) + (A - 1) * c + s2); n1 = -2 * A * ((A - 1) + (A + 1) * c); n2 = A * ((A + 1) + (A - 1) * c - s2); d0 = (A + 1) - (A - 1) * c + s2; d1 = 2 * ((A - 1) - (A + 1) * c); d2 = (A + 1) - (A - 1) * c - s2; }
        b0 = n0 / d0; b1 = n1 / d0; b2 = n2 / d0; a1 = d1 / d0; a2 = d2 / d0;
    }
};

/** Tone shaping around the curve: before it, after it, and the Tone control. */
struct Shaping
{
    Biquad pre, post, tone;
    void set (Character ch, Focus focus, double driveDb, double toneDb, double fs)
    {
        // comes in with the drive, so that a little drive changes a little
        const double a = 1.0 - std::exp (-driveDb / 5.0);
        struct E { Biquad::T t; double f, g, q; };
        static constexpr E chPre[] { { Biquad::T::high, 5000, -2.5, 0.7071 }, { Biquad::T::low, 180, 4, 0.7071 }, { Biquad::T::high, 3000, 6, 0.7071 }, { Biquad::T::peak, 900, 3, 0.6 } };
        static constexpr E chPost[] { { Biquad::T::low, 160, 1, 0.7071 }, { Biquad::T::low, 180, -2.5, 0.7071 }, { Biquad::T::high, 3000, -4, 0.7071 }, { Biquad::T::high, 8000, -3, 0.7071 } };
        static constexpr E foc[] { { Biquad::T::peak, 1000, 0, 1 }, { Biquad::T::low, 250, 9, 0.7071 }, { Biquad::T::peak, 1200, 9, 0.5 }, { Biquad::T::high, 4000, 9, 0.7071 } };
        const bool f = focus != Focus::all;
        const E& e = f ? foc[(int) focus] : chPre[(int) ch];
        const E& o = f ? foc[(int) focus] : chPost[(int) ch];
        pre.set (e.t, e.f, e.g * a, e.q, fs);
        post.set (o.t, o.f, (f ? -0.75 * o.g : o.g) * a, o.q, fs);
        tone.set (Biquad::T::high, 2500, toneDb, 0.7071, fs);
    }
};

class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // K-weighting (BS.1770) for level matching, for any rate
        for (auto* kw : { &kIn, &kOut }) for (auto& c : *kw) { c[0].set (Biquad::T::high, 1682, 4.0, 0.7071, fs); highpass (c[1], 38.1, 0.5003); }
        setup(); reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) { os[c].clear(); sh[c].reset(); std::fill (delay[c].begin(), delay[c].end(), 0.0); for (auto* q : { &pre[c], &post[c], &tone[c] }) q->clear(); for (auto& q : kIn[c]) q.clear(); for (auto& q : kOut[c]) q.clear(); }
        pos = 0; tick = 0;
        drive = p.driveDb; bias = p.bias; toneNow = p.toneDb; mix = p.mix; out = gainOf (p.outputDb); byp = p.bypass ? 1 : 0; wetOn = p.driveDb > 0 ? 1 : 0;
        ch = p.character; focus = p.focus; swap = 1.0; pending = false; rest = 0;
        pIn = pOut = 1e-12; matchGain = 1.0; settle = 0;
        update();
    }
    void setParams (const Params& np)
    {
        const bool factor = np.oversampling != p.oversampling;
        p = np;
        if (factor && fs > 0) { setup(); reset(); }
    }
    bool changesLatency (const Params& np) const { return np.oversampling != p.oversampling; }
    int latencySamples() const { return dist::Oversampler::latencyFor (p.oversampling); }
    double matchDb() const { return 20 * std::log10 (std::max (1e-6, matchGain)); }

    void process (float* Lp, float* Rp, int n)
    {
        const double g20 = 1.0 - std::exp (-1.0 / (0.02 * fs)), avg = 1.0 - std::exp (-1.0 / (1.5 * fs)), gm = 1.0 - std::exp (-1.0 / (0.25 * fs));
        const double tOut = gainOf (p.outputDb);
        for (int i = 0; i < n; ++i)
        {
            mix = move (mix, p.mix, g20); out = move (out, tOut, g20); byp = move (byp, p.bypass ? 1 : 0, g20);
            wetOn = move (wetOn, p.driveDb > 0.0 ? 1 : 0, g20);
            // a new character or focus: fade the added part out, switch, let the old sound leave the delays, fade in
            if (! pending && (ch != p.character || focus != p.focus)) pending = true;
            if (pending) { swap = move (swap, 0.0, 3 * g20); if (swap <= 0.0) { ch = p.character; focus = p.focus; pending = false; rest = latencySamples() + (int) (0.004 * fs); for (int c = 0; c < 2; ++c) { pre[c].clear(); post[c].clear(); } tick = 0; } }
            else if (rest > 0) --rest;
            else swap = move (swap, 1.0, 3 * g20);
            if (--tick <= 0)
            {
                tick = 16;
                const double k = 0.2; bool moved = tick0;
                auto glide = [&] (double& v, double t) { if (v != t) { v += k * (t - v); if (std::abs (v - t) < 1e-4) v = t; moved = true; } };
                glide (drive, p.driveDb); glide (bias, p.bias); glide (toneNow, p.toneDb);
                if (moved || usedCh != ch || usedFocus != focus) update();
                tick0 = false;
            }
            const double in[2] { Lp[i], Rp[i] };
            for (int c = 0; c < 2; ++c) delay[c][(size_t) pos] = in[c];
            pos = (pos + 1) % (int) delay[0].size();
            double dry[2], y[2];
            const double w = mix * wetOn * swap;
            for (int c = 0; c < 2; ++c)
            {
                dry[c] = delay[c][(size_t) pos];
                y[c] = dry[c];
                if (wetOn > 0.0)
                {
                    const double x = pre[c].run (in[c]);
                    const double diff = os[c].process (x, [this, c] (double v) { return sh[c] (v, curve); });
                    preDelay[c][(size_t) pp] = x;
                    const double lined = preDelay[c][(size_t) ((pp + 1) % (int) preDelay[c].size())];
                    const double wet = tone[c].run (post[c].run (lined + diff));
                    y[c] = dry[c] + w * (wet - dry[c]);
                }
            }
            pp = (pp + 1) % (int) preDelay[0].size();
            // loudness matching
            double a = 0, b = 0;
            for (int c = 0; c < 2; ++c) { const double u = kIn[c][1].run (kIn[c][0].run (dry[c])), v = kOut[c][1].run (kOut[c][0].run (y[c])); a += u * u; b += v * v; }
            if (a > 1e-9) { const double q = settle < fs * 1.5 ? std::max (avg, 1.0 / (settle + 1.0)) : avg; settle += 1; pIn += q * (a - pIn); pOut += q * (b - pOut); }
            const double want = p.match && w > 0.0 && pOut > 1e-12 ? std::sqrt (pIn / pOut) : 1.0;
            matchGain = move (matchGain, std::clamp (want, 0.03, 30.0), gm);
            for (int c = 0; c < 2; ++c)
            {
                double z = y[c] * out;
                if (matchGain != 1.0) z *= matchGain;
                if (byp > 0.0) z = byp >= 1.0 ? dry[c] : z + byp * (dry[c] - z);
                (c ? Rp : Lp)[i] = (float) z;
            }
        }
    }
    /** The curve in use, for the display. */
    Curve currentCurve() const { Curve c; c.set (p.character, p.driveDb, p.bias); return c; }

private:
    static double gainOf (double db) { return std::pow (10.0, db / 20.0); }
    static double move (double v, double t, double k) { v += k * (t - v); return std::abs (v - t) < 1e-6 ? t : v; }
    void highpass (Biquad& q, double f, double Q) { const double w = 2 * Curve::pi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * Q), d = 1 + al; q.b0 = (1 + c) / 2 / d; q.b1 = -(1 + c) / d; q.b2 = q.b0; q.a1 = -2 * c / d; q.a2 = (1 - al) / d; }
    void setup()
    {
        for (int c = 0; c < 2; ++c) { os[c].prepare (p.oversampling); delay[c].reserve (160); preDelay[c].reserve (160); delay[c].assign ((size_t) os[c].latency() + 1, 0.0); preDelay[c].assign ((size_t) os[c].latency() + 1, 0.0); }
        pos = pp = 0;
    }
    void update()
    {
        usedCh = ch; usedFocus = focus;
        curve.set (ch, drive, bias);
        for (auto& s : sh) s.refresh (curve);
        shaping.set (ch, focus, drive, toneNow, fs);
        for (int c = 0; c < 2; ++c) { copy (pre[c], shaping.pre); copy (post[c], shaping.post); copy (tone[c], shaping.tone); }
    }
    /** New coefficients, same memory. */
    static void copy (Biquad& to, const Biquad& from) { to.b0 = from.b0; to.b1 = from.b1; to.b2 = from.b2; to.a1 = from.a1; to.a2 = from.a2; }

    Params p;
    double fs = 0;
    Curve curve;
    dist::Oversampler os[2];
    dist::DeltaShaper<Curve> sh[2];
    Biquad pre[2], post[2], tone[2];
    Shaping shaping;
    std::array<std::array<Biquad, 2>, 2> kIn, kOut;
    std::vector<double> delay[2], preDelay[2];
    int pos = 0, pp = 0, tick = 0, rest = 0;
    bool tick0 = true, pending = false;
    Character ch = Character::warm, usedCh = Character::warm;
    Focus focus = Focus::all, usedFocus = Focus::all;
    double drive = 0, bias = 0.5, toneNow = 0, mix = 1, out = 1, byp = 0, wetOn = 0, swap = 1, pIn = 1e-12, pOut = 1e-12, matchGain = 1, settle = 0;
};

} // namespace aemon::sat
