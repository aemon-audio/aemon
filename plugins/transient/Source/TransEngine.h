// Aemon Transient: more or less hit, more or less body, whatever the level. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::trans
{
enum class Focus { all, lows, mids, highs };
constexpr double focusLow = 250, focusMidLow = 250, focusMidHigh = 2500, focusHigh = 3000;

struct Params
{
    double attack = 0, body = 0;        // dB, the most the hit and the body are changed
    double attackMs = 20, bodyMs = 200;
    Focus focus = Focus::all;
    double outDb = 0;
    bool delta = false, bypass = false;
};

constexpr double pi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void first (bool high, double f, double fs) { const double k = std::tan (pi * std::min (f, fs * 0.45) / fs), n = 1 / (1 + k); b0 = high ? n : k * n; b1 = high ? -n : k * n; b2 = 0; a1 = (k - 1) * n; a2 = 0; }
    void set (bool high, double f, double q, double fs)
    {
        const double w = 2 * pi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al, k = high ? 1 + c : 1 - c;
        b0 = k / 2 / a0; b1 = (high ? -k : k) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

/** The highest of the last n values. */
class SlidingMax
{
public:
    void prepare (int n) { len = std::max (1, n); q.assign ((size_t) len + 1, {}); reset(); }
    void reset() { head = size = 0; t = 0; }
    double push (double v) noexcept
    {
        const int cap = (int) q.size();
        while (size > 0 && q[(size_t) ((head + size - 1) % cap)].v <= v) --size;
        q[(size_t) ((head + size) % cap)] = { v, t }; ++size;
        while (t - q[(size_t) head].t >= len) { head = (head + 1) % cap; --size; }
        ++t;
        return q[(size_t) head].v;
    }
private:
    struct E { double v = 0; long long t = 0; };
    std::vector<E> q; int len = 1, head = 0, size = 0; long long t = 0;
};

/** Level now against the level a moment ago, both in dB: only differences count, so the result is the same at every level. */
class Detector
{
public:
    static constexpr double floorDb = -140, span = 24;      // a hit out of silence counts as a jump of 24 dB
    void prepare (double stepSeconds) { dt = stepSeconds; hold.prepare ((int) std::lround (0.025 / dt)); reset(); }
    void reset() { hold.reset(); slow = late = floorDb; hit = tail = 0; }
    /** peak: the highest absolute value of the last step. Sets hit and tail, 0..1. */
    void step (double peak, const Params& p) noexcept
    {
        // 25 ms of hold: a steady tone down to 20 Hz is a steady level
        const double now = std::max (floorDb, 20.0 * std::log10 (hold.push (peak) + 1e-12));
        level = now;
        slow = std::max (std::min (slow, now), now - span); slow += (now - slow) * (1 - std::exp (-dt / (0.001 * p.attackMs * 0.5)));
        late = std::min (std::max (late, now), now + span); late += (now - late) * (1 - std::exp (-dt / (0.001 * p.bodyMs)));
        hit = amount (now - slow); tail = amount (late - now);
    }
    double hit = 0, tail = 0, level = floorDb;
private:
    /** Differences under 2 dB are the sound's own life, not a hit. */
    static double amount (double d) noexcept { return d <= 2 ? 0.0 : 1 - std::exp (-(d - 2) / 5); }
    double dt = 0.001, slow = floorDb, late = floorDb; SlidingMax hold;
};

class Engine
{
public:
    /** In the last display frame: the loudest level in and out (dB), the furthest the gain went for the hit and for the body. */
    struct Frame { float in = -120, out = -120, up = 0, down = 0, hit = 0, tail = 0; };

    /** One millisecond, for the picture of the hit: loudest level in and out, and whether the detector is at a hit (0..1). */
    struct Point { float in = 0, out = 0, hit = 0; };
    /** Where the points go: filled by process(), read and cleared by the caller between calls. Never grows. */
    std::array<Point, 512> points {}; int numPoints = 0;
    void setParams (const Params& x) noexcept { p = x; }
    int latencySamples() const { return look; }
    void prepare (double rate)
    {
        fs = rate; step = std::max (1, (int) std::lround (fs / 6000.0)); dt = step / fs; smooth = std::max (2, (int) std::lround (0.001 / dt)); look = 2 * smooth * step;
        det.prepare (dt); det.reset(); count = 0; peak = 0; w = 0; size = 1; while (size < look + 8) size <<= 1;
        for (auto* d : { &dl, &dr, &bl, &br }) d->assign ((size_t) size, 0.0f);
        b1.assign ((size_t) smooth, 0.0); b2 = b1; at = 0; s1 = s2 = 0; g0 = g1 = 1; outNow = p.bypass ? 1.0 : std::pow (10.0, p.outDb / 20); deltaMix = p.delta && ! p.bypass ? 1 : 0; atk = p.bypass ? 0.0 : p.attack; bod = p.bypass ? 0.0 : p.body; still = smooth * 2 + 2; focusNow = p.focus; focusFade = 1; pt = {}; ptCount = numPoints = 0; ptLen = std::max (1, (int) std::lround (fs / 1000.0)); hitLate.assign ((size_t) (2 * smooth), 0.0); hitAt = 0; fadeStep = 1 / (0.005 * fs); setFocus(); frame = {};
    }

    void process (float* L, float* R, int n) noexcept
    {
        const double glide = 1 - std::exp (-1 / (0.004 * fs)), outTo = std::pow (10.0, (p.bypass ? 0.0 : p.outDb) / 20.0), deltaTo = p.delta && ! p.bypass ? 1.0 : 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i]; double al = xl, ar = xr, pl = xl, pr = xr;
            // a change of focus: the change fades out, the filters switch, the change fades in
            if (focusNow != p.focus) { focusFade -= fadeStep; if (focusFade <= 0) { focusFade = 0; focusNow = p.focus; setFocus(); } } else if (focusFade < 1) focusFade = std::min (1.0, focusFade + fadeStep);
            if (focusNow != Focus::all)
            {
                pl = f2[0].run (f1[0].run (pl)); pr = f2[1].run (f1[1].run (pr));      // what is listened to
                al = h2[0].run (h1[0].run (al)); ar = h2[1].run (h1[1].run (ar));      // what is moved: first-order, so lifting a part never dips beside it
            }
            peak = std::max ({ peak, std::abs (pl), std::abs (pr) });
            if (count == 0) nextGain();
            dl[(size_t) w] = xl; dr[(size_t) w] = xr; bl[(size_t) w] = (float) al; br[(size_t) w] = (float) ar;
            const auto rd = (size_t) ((w - look + size) & (size - 1)); w = (w + 1) & (size - 1);
            const double g = g0 == g1 ? g1 : g0 + (g1 - g0) * (double) (count + 1) / step;
            if (++count >= step) count = 0;
            outNow = std::abs (outNow - outTo) < 1e-7 ? outTo : outNow + (outTo - outNow) * glide;
            deltaMix = std::abs (deltaMix - deltaTo) < 1e-5 ? deltaTo : deltaMix + (deltaTo - deltaMix) * glide;
            float yl = dl[rd], yr = dr[rd]; frame.in = std::max (frame.in, std::max (std::abs (yl), std::abs (yr)));
            const double c = (g - 1) * (focusNow == Focus::all && focusFade == 1 ? 1.0 : focusFade);
            if (c != 0) { yl = (float) (yl + c * bl[rd]); yr = (float) (yr + c * br[rd]); }
            if (outNow != 1) { yl = (float) (yl * outNow); yr = (float) (yr * outNow); }
            if (deltaMix > 0) { yl = (float) (yl * (1 - deltaMix) + deltaMix * c * bl[rd]); yr = (float) (yr * (1 - deltaMix) + deltaMix * c * br[rd]); }
            frame.out = std::max (frame.out, std::max (std::abs (yl), std::abs (yr)));
            L[i] = yl; R[i] = yr;
            pt.in = std::max (pt.in, std::max (std::abs (dl[rd]), std::abs (dr[rd]))); pt.out = std::max (pt.out, std::max (std::abs (yl), std::abs (yr))); pt.hit = std::max (pt.hit, (float) hitLate[(size_t) hitAt]);
            if (++ptCount >= ptLen) { ptCount = 0; if (numPoints < (int) points.size()) points[(size_t) numPoints++] = pt; pt = {}; }
        }
    }
    double gainDb() const noexcept { return g1 == 1 ? 0.0 : 20 * std::log10 (g1); }
    Frame takeFrame() noexcept { auto f = frame; auto db = [] (float v) { return v > 1e-6f ? 20.0f * std::log10 (v) : -120.0f; }; f.in = db (f.in); f.out = db (f.out); frame = {}; frame.in = frame.out = 0; return f; }

private:
    void setFocus()
    {
        for (int c = 0; c < 2; ++c)
        {
            f1[c] = {}; f2[c] = {}; h1[c] = {}; h2[c] = {};
            if (focusNow == Focus::lows) { f1[c].set (false, focusLow, 0.7071, fs); h1[c].first (false, focusLow, fs); }
            else if (focusNow == Focus::highs) { f1[c].set (true, focusHigh, 0.7071, fs); h1[c].first (true, focusHigh, fs); }
            else if (focusNow == Focus::mids) { f1[c].set (true, focusMidLow, 0.7071, fs); f2[c].set (false, focusMidHigh, 0.7071, fs); h1[c].first (true, focusMidLow, fs); h2[c].first (false, focusMidHigh, fs); }
        }
        std::fill (bl.begin(), bl.end(), 0.0f); std::fill (br.begin(), br.end(), 0.0f);
    }
    void nextGain() noexcept
    {
        // the knobs glide: a turn is heard as a turn
        const double k = 1 - std::exp (-dt / 0.02), aTo = p.bypass ? 0.0 : p.attack, bTo = p.bypass ? 0.0 : p.body;
        atk = std::abs (atk - aTo) < 1e-4 ? aTo : atk + (aTo - atk) * k; bod = std::abs (bod - bTo) < 1e-4 ? bTo : bod + (bTo - bod) * k;
        det.step (peak, p); peak = 0;
        hitLate[(size_t) hitAt] = det.hit; hitAt = hitAt + 1 == 2 * smooth ? 0 : hitAt + 1;      // as late as the sound
        const double want = atk * det.hit + bod * det.tail * (1 - det.hit);
        // two averages of a millisecond each, on the sound that is still to come: no step, and ready when the hit is
        if (want != 0) still = 0; else if (still < 2 * smooth + 2) ++still;
        s1 += want - b1[(size_t) at]; b1[(size_t) at] = want; const double m = s1 / smooth;
        s2 += m - b2[(size_t) at]; b2[(size_t) at] = m; at = at + 1 == smooth ? 0 : at + 1;
        if (at == 0) { s1 = s2 = 0; for (double v : b1) s1 += v; for (double v : b2) s2 += v; }
        double db = s2 / smooth; if (still >= 2 * smooth + 2) db = 0;
        g0 = g1; g1 = db == 0 ? 1.0 : std::pow (10.0, db / 20.0);
        frame.up = std::max (frame.up, (float) db); frame.down = std::min (frame.down, (float) db); frame.hit = std::max (frame.hit, (float) det.hit); frame.tail = std::max (frame.tail, (float) det.tail);
    }

    double fs = 48000, dt = 0.001; Params p; Detector det; Biquad f1[2], f2[2], h1[2], h2[2]; Focus focusNow = Focus::all;
    std::vector<float> dl, dr, bl, br; std::vector<double> b1, b2;
    int step = 8, smooth = 6, look = 96, size = 128, w = 0, count = 0, at = 0, still = 0;
    double peak = 0, s1 = 0, s2 = 0, g0 = 1, g1 = 1, outNow = 1, deltaMix = 0, atk = 0, bod = 0, focusFade = 1, fadeStep = 0.01;
    Frame frame; Point pt; int ptCount = 0, ptLen = 48, hitAt = 0; std::vector<double> hitLate = std::vector<double> (12, 0.0);
};
} // namespace aemon::trans
