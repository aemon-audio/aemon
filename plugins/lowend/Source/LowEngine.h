// Aemon Low End: lines a bass up with a kick and ducks the lows that clash. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "aemon_LinearBands.h"

namespace aemon::low
{
constexpr double pi = 3.14159265358979323846, maxDelayMs = 10, turnHz = 60;

/** How the bass is moved: later (ms, negative: earlier), upside down, phase turned (degrees at 60 Hz). */
struct Align
{
    double delayMs = 0, turn = 0; bool flip = false;
    bool operator== (const Align& o) const { return delayMs == o.delayMs && turn == o.turn && flip == o.flip; }
};

struct Params
{
    Align align;
    double duck = 0;            // dB, the deepest
    double below = 120;         // Hz
    double releaseMs = 120;
    bool clash = true;          // only the lows where both are
    bool bypass = false;
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void set (bool high, double f, double q, double fs)
    {
        const double w = 2 * pi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al, k = high ? 1 + c : 1 - c;
        b0 = k / 2 / a0; b1 = (high ? -k : k) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};
/** Fourth-order low- or high-pass. */
struct Steep { Biquad a, b; void set (bool high, double f, double fs) { a.set (high, f, 0.5412, fs); b.set (high, f, 1.3066, fs); } double run (double x) noexcept { return b.run (a.run (x)); } void reset() { a.reset(); b.reset(); } };

/** First-order all-pass that turns 60 Hz by `degrees`. At 0 it is not there at all. */
struct Turn
{
    double c = 0, z = 0; bool on = false;
    void set (double degrees, double fs)
    {
        on = degrees > 0.01; if (! on) { z = 0; return; }
        const double f = turnHz / std::tan (std::min (degrees, 179.0) * pi / 360.0), t = std::tan (pi * std::min (f, fs * 0.49) / fs); c = (t - 1) / (t + 1);
    }
    double run (double x) noexcept { if (! on) return x; const double y = c * x + z; z = x - c * y; return y; }
};

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
        ++t; return q[(size_t) head].v;
    }
private:
    struct E { double v = 0; long long t = 0; };
    std::vector<E> q; int len = 1, head = 0, size = 0; long long t = 0;
};

constexpr int lowBands = 3;
inline double splitOf (int k, double below) { return below * (k == 0 ? 0.42 : (k == 1 ? 0.70 : 1.0)); }

class Engine
{
public:
    /** In the last display frame: levels of the lows, dB, and how deep the duck went per band (dB, positive). */
    struct Frame { float kick = -120, bass = -120; std::array<float, lowBands> cut {}; };

    void setParams (const Params& x) noexcept { p = x; }
    int latencySamples() const { return head + lin.latency(); }
    void prepare (double rate)
    {
        fs = rate; lin.prepare (fs); spec(); head = (int) std::ceil (fs * maxDelayMs / 1000.0);
        size = 1; while (size < 2 * head + lin.latency() + 64) size <<= 1;
        for (auto* d : { &rl, &rr, &dl, &dr }) d->assign ((size_t) size, 0.0f);
        w = 0; now.set (aligned(), fs); next = now; fading = 0; fadeLen = (int) (fs * 0.02);
        step = std::max (1, (int) std::lround (fs / 6000.0)); dt = step / fs; smooth = std::max (2, (int) std::lround (0.002 / dt)); lead = 2 * smooth * step;
        ahead = std::max (1, (int) std::lround (0.025 / dt)); opens.assign ((size_t) ahead, 0.0); openAt = 0; const int total = latencySamples() - lead - ahead * step; gainSize = 1; while (gainSize < total / step + 8) gainSize <<= 1; gainDelay = total / step; for (auto& g : gains) g.assign ((size_t) gainSize, 1.0f); gw = 0;
        for (auto& b : box) for (auto& q : b) q.assign ((size_t) smooth, 0.0); for (auto& s : sums) s = {}; at = 0; for (auto& s : still) s = 2 * smooth + 2;
        hold.prepare ((int) std::lround (0.012 / dt)); peakDb = -50; for (auto& b : weights) b = 0; kp.fill (1e-12); bp.fill (1e-12); falling.fill (0); count = 0; peak = 0; depth = p.bypass ? 0 : p.duck; filters();
        g0.fill (1); g1.fill (1); frame = {}; frame.kick = frame.bass = 0;
    }

    void process (float* L, float* R, int n, const float* kl = nullptr, const float* kr = nullptr) noexcept
    {
        spec(); filters();
        std::array<lin::Bands::C, lin::maxBands> band;
        for (int i = 0; i < n; ++i)
        {
            // detector, at the time of the input
            const double k = kl != nullptr ? 0.5 * ((double) kl[i] + (kr != nullptr ? kr[i] : kl[i])) : 0.0, b = 0.5 * ((double) L[i] + R[i]);
            const double kLow = kickLow.run (k), bLow = bassLow.run (b);
            peak = std::max (peak, std::abs (kLow)); frame.kick = std::max (frame.kick, (float) std::abs (kLow)); frame.bass = std::max (frame.bass, (float) std::abs (bLow));
            double kk = kLow, bb = bLow; const double c15 = 1 - std::exp (-1 / (0.015 * fs));
            powers (kk, bb, c15);
            if (count == 0) control (kl != nullptr);
            // the bass, moved
            rl[(size_t) w] = L[i]; rr[(size_t) w] = R[i];
            if (fading == 0 && ! (aligned() == now.a)) { next.set (aligned(), fs); fading = fadeLen; }
            double al = 0, ar = 0; now.run (rl, rr, w, head, size, al, ar);
            if (fading > 0)
            {
                double ql = 0, qr = 0; next.run (rl, rr, w, head, size, ql, qr); const double t = 0.5 + 0.5 * std::cos (pi * (double) fading / fadeLen);
                al += (ql - al) * t; ar += (qr - ar) * t;
                if (--fading == 0) now = next;
            }
            // the lows apart, straight phase, and the duck on them
            lin.push (al, ar, band.data());
            dl[(size_t) w] = (float) al; dr[(size_t) w] = (float) ar;
            const auto rd = (size_t) ((w - lin.latency() + size) & (size - 1));
            double yl = dl[rd], yr = dr[rd]; const double t = (double) (count + 1) / step;
            for (int q = 0; q < lowBands; ++q) { const double g = g0[(size_t) q] == g1[(size_t) q] ? g1[(size_t) q] : g0[(size_t) q] + (g1[(size_t) q] - g0[(size_t) q]) * t; if (g != 1.0) { yl += (g - 1) * band[(size_t) q].real(); yr += (g - 1) * band[(size_t) q].imag(); } }
            L[i] = (float) yl; R[i] = (float) yr;
            w = (w + 1) & (size - 1); if (++count >= step) count = 0;
        }
    }
    /** Depth heard now per band, dB (negative or 0). */
    double cutDb (int band) const { const double g = g1[(size_t) band]; return g == 1 ? 0.0 : 20 * std::log10 (g); }
    Frame takeFrame() noexcept { auto f = frame; auto db = [] (float v) { return v > 1e-6f ? 20.0f * std::log10 (v) : -120.0f; }; f.kick = db (f.kick); f.bass = db (f.bass); frame = {}; frame.kick = frame.bass = 0; return f; }

private:
    struct Voice
    {
        Align a; Turn tl, tr; int d = 0; double sign = 1;
        void set (const Align& x, double fs) { a = x; d = (int) std::lround (std::clamp (x.delayMs, -maxDelayMs, maxDelayMs) * fs / 1000.0); sign = x.flip ? -1 : 1; tl = {}; tr = {}; tl.set (x.turn, fs); tr.set (x.turn, fs); }
        void run (const std::vector<float>& l, const std::vector<float>& r, int w, int head, int size, double& ol, double& or_) noexcept { const auto i = (size_t) ((w - head - d + 2 * size) & (size - 1)); ol = sign * tl.run (l[i]); or_ = sign * tr.run (r[i]); }
    };
    Align aligned() const { return p.bypass ? Align {} : p.align; }
    void spec() { lin::Spec s; s.bands = lowBands + 1; s.order = 8; for (int k = 0; k < lowBands; ++k) s.split[(size_t) k] = splitOf (k, p.below); lin.design (s); }
    void filters()
    {
        if (p.below == filtered && fs == filteredFs) return;
        filtered = p.below; filteredFs = fs; kickLow.set (false, p.below, fs); bassLow.set (false, p.below, fs);
        for (int k = 0; k < lowBands - 1; ++k) for (auto* f : { &kickSplit[k], &bassSplit[k] }) { f[0].set (false, splitOf (k, p.below), fs); f[2].set (true, splitOf (k, p.below), fs); }
    }
    /** Powers of the three low bands of both, 15 ms. */
    void powers (double kk, double bb, double c) noexcept
    {
        const double ks[] { kickSplit[0].run (kk), kickSplit[1].run (kickSplit[2].run (kk)), kickSplit[3].run (kk) }, bs[] { bassSplit[0].run (bb), bassSplit[1].run (bassSplit[2].run (bb)), bassSplit[3].run (bb) };
        for (size_t q = 0; q < lowBands; ++q) { kp[q] += (ks[q] * ks[q] - kp[q]) * c; bp[q] += (bs[q] * bs[q] - bp[q]) * c; }
    }
    static double ramp (double x, double lo, double hi) { const double t = std::clamp ((x - lo) / (hi - lo), 0.0, 1.0); return t * t * (3 - 2 * t); }
    void control (bool connected) noexcept
    {
        const double k = 1 - std::exp (-dt / 0.02), to = p.bypass ? 0.0 : p.duck; depth = std::abs (depth - to) < 1e-4 ? to : depth + (to - depth) * k;
        const double now_ = 20 * std::log10 (hold.push (peak) + 1e-12); peak = 0;
        // against the kick's own hits: the same at every level
        peakDb = std::max (std::max (now_, peakDb - 3 * dt), -55.0);
        const double seen = connected && now_ > -54 ? std::clamp ((now_ - (peakDb - 18)) / 18, 0.0, 1.0) : 0.0;
        // how loud each band is takes a moment to know: the opening waits for it. There is time, the filters need more
        const double open = opens[(size_t) openAt]; opens[(size_t) openAt] = seen; openAt = openAt + 1 == ahead ? 0 : openAt + 1;
        double kt = 1e-12, bt = 1e-12; for (size_t q = 0; q < lowBands; ++q) { kt += kp[q]; bt += bp[q]; }
        for (size_t q = 0; q < lowBands; ++q)
        {
            const double kd = 10 * std::log10 (kp[q] / kt + 1e-9), bd = 10 * std::log10 (bp[q] / bt + 1e-9);
            const double want = p.clash ? ramp (kd, -15, -7) * ramp (bd, -15, -7) * (bt > 1e-9 ? 1.0 : 0.0) : 1.0;
            if (seen > 0 || open > 0) weights[q] += (want - weights[q]) * (1 - std::exp (-dt / 0.005));
            falling[q] = std::max (open, falling[q] - dt / (0.001 * p.releaseMs)); if (falling[q] < 1e-9) falling[q] = 0;
            double db = -depth * falling[q] * (p.clash ? weights[q] : 1.0); if (db > -1e-5) db = 0;
            if (db != 0) still[q] = 0; else if (still[q] < 2 * smooth + 2) ++still[q];
            auto& s = sums[q]; auto& b = box[q];
            s[0] += db - b[0][(size_t) at]; b[0][(size_t) at] = db; const double m = s[0] / smooth; s[1] += m - b[1][(size_t) at]; b[1][(size_t) at] = m;
            double v = s[1] / smooth; if (still[q] >= 2 * smooth + 2) v = 0;
            gains[q][(size_t) gw] = v == 0 ? 1.0f : (float) std::pow (10.0, std::min (0.0, v) / 20.0);
            const float g = gains[q][(size_t) ((gw - gainDelay + gainSize) & (gainSize - 1))]; g0[q] = g1[q]; g1[q] = g;
            frame.cut[q] = std::max (frame.cut[q], g == 1.0f ? 0.0f : -20.0f * std::log10 (g));
        }
        gw = (gw + 1) & (gainSize - 1); at = at + 1 == smooth ? 0 : at + 1;
        if (at == 0) for (size_t q = 0; q < lowBands; ++q) for (size_t j = 0; j < 2; ++j) { sums[q][j] = 0; for (double v : box[q][j]) sums[q][j] += v; }
    }

    double fs = 48000, dt = 0.001, filtered = 0, filteredFs = 0; Params p; lin::Bands lin; Voice now, next;
    std::vector<float> rl, rr, dl, dr; std::array<std::vector<float>, lowBands> gains; std::array<std::array<std::vector<double>, 2>, lowBands> box; std::array<std::array<double, 2>, lowBands> sums {}; std::array<int, lowBands> still {};
    int head = 480, size = 1, w = 0, fading = 0, fadeLen = 960, step = 8, smooth = 12, lead = 192, gainSize = 1, gainDelay = 0, gw = 0, count = 0, at = 0, ahead = 1, openAt = 0; std::vector<double> opens = std::vector<double> (1, 0.0);
    Steep kickLow, bassLow, kickSplit[4], bassSplit[4]; SlidingMax hold;
    double peak = 0, peakDb = -50, depth = 0; std::array<double, lowBands> weights {}, kp {}, bp {}, falling {}, g0 {}, g1 {};
    Frame frame;
};
} // namespace aemon::low
