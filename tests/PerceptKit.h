// What can be said about a sound without listening: a model of hearing, words, tone, reverb. Plain C++, no JUCE. AGPL-3.0-or-later.
// Every measure is checked in PerceptTests.cpp against known values and against sounds made to be bad. A pass here is a
// measurement. It is never "listened to".
#pragma once
#include "aemon_LinearBands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace percept
{
using Vec = std::vector<float>;
using C = std::complex<double>;
constexpr double pi = 3.14159265358979323846;
inline double db10 (double p) { return 10.0 * std::log10 (p + 1e-30); }
inline double barkOf (double f) { return 13.0 * std::atan (0.00076 * f) + 3.5 * std::atan (std::pow (f / 7500.0, 2)); }
/** Threshold of hearing in quiet, dB SPL (Terhardt). */
inline double quietDb (double f) { const double k = std::max (0.02, f / 1000.0); return 3.64 * std::pow (k, -0.8) - 6.5 * std::exp (-0.6 * std::pow (k - 3.3, 2)) + 0.001 * std::pow (k, 4); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void set (bool high, double f, double q, double fs) { const double w = 2 * pi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al, k = high ? 1 + c : 1 - c; b0 = k / 2 / a0; b1 = (high ? -k : k) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; }
};
inline Vec filtered (Vec x, bool high, double f, double fs) { for (double q : { 0.5412, 1.3066 }) { Biquad b; b.set (high, f, q, fs); for (auto& v : x) v = (float) b.run (v); } return x; }
/** What a phone speaker keeps: mono, 250 Hz to 10 kHz. */
inline Vec phone (const Vec& l, const Vec& r, double fs) { Vec m (l.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (l[i] + r[i]); return filtered (filtered (m, true, 250, fs), false, 10000, fs); }
inline double rmsDb (const Vec& x, size_t from = 0, size_t to = (size_t) -1) { double s = 0; to = std::min (to, x.size()); for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i]; return db10 (s / (double) std::max<size_t> (1, to - from)); }
/** Sinc resampling, good enough for analysis. */
inline Vec resample (const Vec& x, double from, double to)
{
    const double r = to / from, cut = std::min (1.0, r) * 0.94; const int half = (int) std::ceil (24 / std::min (1.0, r)); Vec y ((size_t) ((double) x.size() * r));
    for (size_t i = 0; i < y.size(); ++i) { const double p = (double) i / r; const int c = (int) std::floor (p); double s = 0; for (int k = c - half + 1; k <= c + half; ++k) { if (k < 0 || k >= (int) x.size()) continue; const double d = p - k, w = 0.5 + 0.5 * std::cos (pi * d / half), a = pi * d * cut; s += x[(size_t) k] * w * (std::abs (a) < 1e-9 ? cut : cut * std::sin (a) / a); } y[i] = (float) s; }
    return y;
}

// ---- hearing -----------------------------------------------------------------------------------------------------------
/** Spectrum on the ear's scale, every 10 ms, at a playback level: a full-scale sine is `loud` dB SPL. */
class Ear
{
public:
    static constexpr int N = 2048, hop = 512, B = 100;      // quarter-Bark bands, 0 to 25
    explicit Ear (double rate = 48000, double loud = 90) : fs (rate), cal (loud)
    {
        fft.init (N); win.resize (N); double s = 0; for (int i = 0; i < N; ++i) { win[(size_t) i] = 0.5 - 0.5 * std::cos (2 * pi * i / N); s += win[(size_t) i]; } norm = 4.0 / (s * s) / 1.5;      // a sine's main lobe adds up to 1.5 of its peak bin
        bandOf.resize (N / 2, 0); for (int k = 1; k < N / 2; ++k) bandOf[(size_t) k] = std::clamp ((int) (barkOf (k * fs / N) * 4), 0, B - 1);
        for (int b = 0; b < B; ++b) { const double z = (b + 0.5) / 4; double lo = 10, hi = 20000; for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); (barkOf (m) < z ? lo : hi) = m; } centre[(size_t) b] = lo; }
    }
    /** One frame: power per band in dB SPL, and how tonal each band is (0 noise, 1 tone; set by steadiness()). */
    struct Frame { std::array<double, B> level {}, tonal {}; };
    Frame frame (const Vec& x, long from) const
    {
        std::vector<C> z (N); for (int i = 0; i < N; ++i) { const long p = from + i; z[(size_t) i] = (p >= 0 && p < (long) x.size() ? x[(size_t) p] : 0.0f) * win[(size_t) i]; }
        fft.forward (z.data()); Frame o; std::array<double, B> p {};
        for (int k = 1; k < N / 2; ++k) { const double v = std::norm (z[(size_t) k]) * norm; const auto b = (size_t) bandOf[(size_t) k]; p[b] += v; }
        for (size_t b = 0; b < B; ++b) o.level[b] = db10 (p[b]) + cal;
        return o;
    }
    /** What this frame hides, per band: anything under it is not heard. */
    std::array<double, B> hides (const Frame& f) const
    {
        std::array<double, B> t; for (int b = 0; b < B; ++b) t[(size_t) b] = std::pow (10.0, (quietDb (centre[(size_t) b]) - 6) / 10);      // quarter-Bark bands share the threshold of a Bark
        for (int m = 0; m < B; ++m)
        {
            const double L = f.level[(size_t) m]; if (L < -10) continue;
            const double z = (m + 0.5) / 4, offset = f.tonal[(size_t) m] * (14.5 + z) + (1 - f.tonal[(size_t) m]) * 5.5;
            const double upper = std::min (-5.0, -(24 + 230 / centre[(size_t) m]) + 0.2 * L);
            for (int b = 0; b < B; ++b) { const double d = (b - m) / 4.0, s = d < 0 ? 27 * d : upper * d; const double v = L + s - offset; if (v > -20) t[(size_t) b] += std::pow (10.0, v / 10); }
        }
        std::array<double, B> o; for (size_t b = 0; b < B; ++b) o[b] = db10 (t[b]); return o;
    }
    /** A tone's band moves evenly from frame to frame, a noise's band jumps. Sets `tonal` of a run of frames. */
    static void steadiness (std::vector<Frame>& f)
    {
        const int n = (int) f.size();
        for (int i = 0; i < n; ++i) for (size_t b = 0; b < B; ++b) { double s = 0; int c = 0; for (int k = std::max (2, i - 4); k <= std::min (n - 1, i + 4); ++k) { const double d = f[(size_t) k].level[b] - 2 * f[(size_t) (k - 1)].level[b] + f[(size_t) (k - 2)].level[b]; s += d * d; ++c; } const double sd = c ? std::sqrt (s / c / 6) : 9; f[(size_t) i].tonal[b] = std::clamp ((2.2 - sd) / 1.8, 0.0, 1.0); }
    }
    const std::array<double, B>& centres() const { return centre; }
    /** Loudness per band (sone per Bark, simplified power law over the threshold). */
    std::array<double, B> specific (const Frame& f) const { std::array<double, B> n {}; const auto spread = excite (f); for (size_t b = 0; b < B; ++b) { const double q = std::pow (10.0, (quietDb (centre[b]) - 6) / 10), e = std::pow (10.0, spread[b] / 10); n[b] = 0.064 * std::pow (q, 0.23) * (std::pow (0.5 + 0.5 * e / q, 0.23) - 1); if (n[b] < 0) n[b] = 0; } return n; }
    double loudness (const Frame& f) const { double s = 0; for (double v : specific (f)) s += v * 0.25; return s * 4.0; }
    /** acum: 1 for a band of noise around 1 kHz. */
    double sharpness (const Frame& f) const { const auto n = specific (f); double a = 0, s = 0; for (int b = 0; b < B; ++b) { const double z = (b + 0.5) / 4, g = z < 15.8 ? 1.0 : 0.066 * std::exp (0.171 * z) * 1.0 + 0.0; s += n[(size_t) b]; a += n[(size_t) b] * g * z; } return s > 1e-9 ? 0.11 * a / s : 0; }
    double fs, cal;
private:
    std::array<double, B> excite (const Frame& f) const { std::array<double, B> t {}; for (int m = 0; m < B; ++m) { const double L = f.level[(size_t) m]; if (L < -10) continue; const double upper = std::min (-5.0, -(24 + 230 / centre[(size_t) m]) + 0.2 * L); for (int b = 0; b < B; ++b) { const double d = (b - m) / 4.0, v = L + (d < 0 ? 27 * d : upper * d); if (v > -20) t[(size_t) b] += std::pow (10.0, v / 10); } } for (auto& v : t) v = db10 (v); return t; }
    aemon::lin::Fft fft; std::vector<double> win; std::vector<int> bandOf; std::array<double, B> centre {}; double norm = 1;
};

/** How far an unwanted sound pokes out of what the wanted one hides, dB. Under 0: not heard. Both are added up over 100 ms, as the ear does (clicks
    have their own measure). The masker also covers a little before, and fading, 100 ms after. */
struct Audible { double worst = -99, p999 = -99, share = 0; double atSec = 0, atHz = 0; };
inline Audible audible (const Vec& wanted, const Vec& unwanted, double fs, double loud = 90)
{
    const Ear ear (fs, loud); Audible o; std::vector<double> all; int heard = 0;
    const long n = (long) std::min (wanted.size(), unwanted.size()), hop = Ear::hop;
    std::vector<Ear::Frame> w, u; for (long p = 0; p + Ear::N <= n; p += hop) { w.push_back (ear.frame (wanted, p)); u.push_back (ear.frame (unwanted, p)); }
    if (w.empty()) return o;
    Ear::steadiness (w); const int count = (int) w.size(), half = std::max (1, (int) (0.05 * fs / (double) hop));
    std::vector<std::array<double, Ear::B>> mask; std::array<double, Ear::B> held; held.fill (-99);
    for (auto& f : w) { const auto h = ear.hides (f); for (size_t b = 0; b < Ear::B; ++b) held[b] = std::max (h[b], held[b] - 30.0 * (double) hop / fs / 0.1); mask.push_back (held); }
    for (int i = 0; i < count; ++i)
    {
        double worst = -99;
        for (size_t b = 0; b < Ear::B; ++b) { double e = 0, m = 0; for (int k = std::max (0, i - half); k <= std::min (count - 1, i + half); ++k) { e += std::pow (10.0, u[(size_t) k].level[b] / 10); m += std::pow (10.0, mask[(size_t) k][b] / 10); } const double d = db10 (e) - db10 (m); all.push_back (d); worst = std::max (worst, d); if (d > o.worst) { o.worst = d; o.atSec = (double) i * (double) hop / fs; o.atHz = ear.centres()[b]; } }
        heard += worst > 0;
    }
    const size_t k = (size_t) (0.999 * (double) (all.size() - 1)); std::nth_element (all.begin(), all.begin() + (long) k, all.end()); o.p999 = all[k];
    o.share = (double) heard / count; return o;
}
/** Short clicks stand out of the 43 ms frames badly: the largest jump of the second difference against its own surroundings, dB. */
inline double clickDb (const Vec& x, double fs)
{
    const int w = (int) (0.004 * fs); const int n = (int) x.size(); if (n < 8 * w) return 0; std::vector<double> d ((size_t) n, 0.0); for (int i = 2; i < n; ++i) d[(size_t) i] = std::abs ((double) x[(size_t) i] - 2.0 * x[(size_t) (i - 1)] + x[(size_t) (i - 2)]);
    std::vector<double> sum ((size_t) n + 1, 0.0); for (int i = 0; i < n; ++i) sum[(size_t) (i + 1)] = sum[(size_t) i] + d[(size_t) i] * d[(size_t) i];
    double worst = 0; for (int i = 4 * w; i + 4 * w < n; ++i) { const double around = (sum[(size_t) (i - 8)] - sum[(size_t) (i - 4 * w)] + sum[(size_t) (i + 4 * w)] - sum[(size_t) (i + 8)]) / (8.0 * w - 16); const double v = d[(size_t) i] * d[(size_t) i] / (around + 1e-12); worst = std::max (worst, v); }
    return db10 (worst);
}
struct Colour { double loudness = 0, sharpness = 0; };
inline Colour colourOf (const Vec& x, double fs, double loud = 80) { const Ear ear (fs, loud); Colour c; double w = 0; for (long p = 0; p + Ear::N < (long) x.size(); p += 2 * Ear::hop) { const auto f = ear.frame (x, p); const double l = ear.loudness (f); c.loudness += l * l; c.sharpness += ear.sharpness (f) * l; w += l; } if (w > 0) { c.loudness /= w; c.sharpness /= w; } return c; }
/** asper, simplified: modulation of the ear bands between 20 and 200 Hz. 1 for a 1 kHz tone fully modulated at 70 Hz. */
inline double roughness (const Vec& x, double fs)
{
    double total = 0; const int hopLen = std::max (1, (int) (fs / 1000)); double loudSum = 0;
    for (double z = 1.5; z < 22; z += 1)
    {
        double lo = 10, hi = 20000; for (int i = 0; i < 40; ++i) { const double m = 0.5 * (lo + hi); (barkOf (m) < z ? lo : hi) = m; }
        if (lo > fs * 0.42) break;
        Biquad a, b; const double bw = 0.35 * lo < 60 ? 60 : 0.23 * lo; a.set (true, std::max (20.0, lo - bw), 0.7071, fs); b.set (false, lo + bw, 0.7071, fs);
        std::vector<double> env; double acc = 0; int c = 0; for (float v : x) { const double y = b.run (a.run (v)); acc += y * y; if (++c == hopLen) { env.push_back (std::sqrt (acc / c)); acc = 0; c = 0; } }
        // band-pass of the envelope: most at 70 Hz
        const double fe = fs / hopLen; Biquad h, l; h.set (true, 35, 0.7071, fe); l.set (false, 140, 0.7071, fe); double m = 0, dc = 0; for (size_t i = 0; i < env.size(); ++i) { const double y = l.run (h.run (env[i])); if (i > 60) { m += y * y; dc += env[i]; } }
        const double n = (double) std::max<size_t> (1, env.size() - 60); dc /= n; const double depth = dc > 1e-7 ? std::min (1.0, std::sqrt (2 * m / n) / dc) : 0, w = std::pow (dc, 0.46); total += depth * depth * w; loudSum += w;
    }
    return loudSum > 1e-9 ? 1.17 * total / loudSum : 0;
}

// ---- words -------------------------------------------------------------------------------------------------------------
/** Short-time objective intelligibility: how much of the words' shape is left, 0 to 1. clean: the dry voice, heard: what reaches the ear, lined up. */
inline double words (const Vec& clean48, const Vec& heard48, double fs)
{
    const Vec x = resample (clean48, fs, 10000), y = resample (heard48, fs, 10000); constexpr int F = 256, H = 128, N = 512, J = 15, S = 30; aemon::lin::Fft fft; fft.init (N);
    std::vector<double> w (F); for (int i = 0; i < F; ++i) w[(size_t) i] = 0.5 - 0.5 * std::cos (2 * pi * (i + 1) / (F + 1));
    const int frames = std::max (0, ((int) std::min (x.size(), y.size()) - F) / H); std::vector<std::array<double, J>> X, Y; std::vector<double> e ((size_t) frames); double top = -200;
    for (int f = 0; f < frames; ++f) { double s = 0; for (int i = 0; i < F; ++i) s += std::pow (x[(size_t) (f * H + i)] * w[(size_t) i], 2); e[(size_t) f] = db10 (s); top = std::max (top, e[(size_t) f]); }
    std::array<std::pair<int, int>, J> range; for (int j = 0; j < J; ++j) { const double c = 150 * std::pow (2.0, j / 3.0); range[(size_t) j] = { (int) std::lround (c * std::pow (2.0, -1.0 / 6) * N / 10000), (int) std::lround (c * std::pow (2.0, 1.0 / 6) * N / 10000) }; }
    for (int f = 0; f < frames; ++f)
    {
        if (e[(size_t) f] < top - 40) continue;
        std::vector<C> a (N), b (N); for (int i = 0; i < F; ++i) { a[(size_t) i] = x[(size_t) (f * H + i)] * w[(size_t) i]; b[(size_t) i] = y[(size_t) (f * H + i)] * w[(size_t) i]; }
        fft.forward (a.data()); fft.forward (b.data()); std::array<double, J> p {}, q {};
        for (int j = 0; j < J; ++j) { for (int k = range[(size_t) j].first; k < std::max (range[(size_t) j].second, range[(size_t) j].first + 1); ++k) { p[(size_t) j] += std::norm (a[(size_t) k]); q[(size_t) j] += std::norm (b[(size_t) k]); } p[(size_t) j] = std::sqrt (p[(size_t) j]); q[(size_t) j] = std::sqrt (q[(size_t) j]); }
        X.push_back (p); Y.push_back (q);
    }
    double sum = 0; int count = 0; const double clip = 1 + std::pow (10.0, 15.0 / 20);
    for (int m = S; m <= (int) X.size(); ++m) for (size_t j = 0; j < J; ++j)
    {
        double ex = 1e-12, ey = 1e-12; for (int i = m - S; i < m; ++i) { ex += X[(size_t) i][j] * X[(size_t) i][j]; ey += Y[(size_t) i][j] * Y[(size_t) i][j]; }
        const double al = std::sqrt (ex / ey); double mx = 0, my = 0; std::array<double, S> yc; for (int i = 0; i < S; ++i) { const double xv = X[(size_t) (m - S + i)][j]; yc[(size_t) i] = std::min (al * Y[(size_t) (m - S + i)][j], clip * xv); mx += xv / S; my += yc[(size_t) i] / S; }
        double xy = 0, xx = 1e-12, yy = 1e-12; for (int i = 0; i < S; ++i) { const double u = X[(size_t) (m - S + i)][j] - mx, v = yc[(size_t) i] - my; xy += u * v; xx += u * u; yy += v * v; } sum += xy / std::sqrt (xx * yy); ++count;
    }
    return count ? sum / count : 0;
}

// ---- tone --------------------------------------------------------------------------------------------------------------
/** Level of third-octave bands, dB, 25 Hz to 16 kHz. */
constexpr int thirds = 29;
inline double thirdHz (int k) { return 25 * std::pow (2.0, k / 3.0); }
inline std::array<double, thirds> tone (const Vec& x, double fs)
{
    constexpr int N = 8192; aemon::lin::Fft fft; fft.init (N); std::array<double, thirds> p {}; int c = 0;
    for (size_t at = 0; at + N <= x.size(); at += N / 2, ++c) { std::vector<C> z (N); for (int i = 0; i < N; ++i) z[(size_t) i] = x[at + (size_t) i] * (0.5 - 0.5 * std::cos (2 * pi * i / N)); fft.forward (z.data()); for (int k = 1; k < N / 2; ++k) { const int b = (int) std::lround (3 * std::log2 (k * fs / N / 25.0)); if (b >= 0 && b < thirds) p[(size_t) b] += std::norm (z[(size_t) k]); } }
    for (auto& v : p) v = db10 (v / std::max (1, c)); return p;
}
/** Change of a range in dB, against the change of the whole. */
inline double rangeChange (const std::array<double, thirds>& a, const std::array<double, thirds>& b, double lo, double hi) { double pa = 0, pb = 0; for (int k = 0; k < thirds; ++k) if (thirdHz (k) >= lo && thirdHz (k) <= hi) { pa += std::pow (10.0, a[(size_t) k] / 10); pb += std::pow (10.0, b[(size_t) k] / 10); } return db10 (pb) - db10 (pa); }

// ---- reverb ------------------------------------------------------------------------------------------------------------
/** Backward integrated decay, dB, 0 at the start. */
inline std::vector<double> decay (const Vec& ir) { std::vector<double> e (ir.size() + 1, 0.0); for (size_t i = ir.size(); i-- > 0;) e[i] = e[i + 1] + (double) ir[i] * ir[i]; const double t = e[0] + 1e-30; for (auto& v : e) v = db10 (v / t); return e; }
/** Time for 60 dB, from the slope between two levels. */
inline double decayTime (const std::vector<double>& e, double fs, double from, double to)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0; for (size_t i = 0; i < e.size(); ++i) if (e[i] <= from && e[i] >= to) { const double t = (double) i / fs; sx += t; sy += e[i]; sxx += t * t; sxy += t * e[i]; ++n; }
    const double s = n > 2 ? (n * sxy - sx * sy) / (n * sxx - sx * sx) : 0; return s < -1e-9 ? -60 / s : 0;
}
struct Decay { double t30 = 0, edt = 0; };
inline Decay decayOf (const Vec& ir, double fs, double lo = 0, double hi = 0) { Vec x = ir; if (lo > 0) x = filtered (x, true, lo, fs); if (hi > 0) x = filtered (x, false, hi, fs); const auto e = decay (x); return { decayTime (e, fs, -5, -35), decayTime (e, fs, -0.1, -10) }; }
/** Normalised echo density of 20 ms pieces: about 1 for a diffuse tail, near 0 for single echoes. Returns the time from which it stays over 0.8. */
inline double echoDensity (const Vec& ir, double fs, double at) { const int w = (int) (0.010 * fs), c = (int) (at * fs); double s = 0, n = 0; for (int i = std::max (0, c - w); i < std::min ((int) ir.size(), c + w); ++i) { s += (double) ir[(size_t) i] * ir[(size_t) i]; ++n; } if (n < 8 || s < 1e-20) return 0; s = std::sqrt (s / n); double k = 0; for (int i = std::max (0, c - w); i < std::min ((int) ir.size(), c + w); ++i) k += std::abs (ir[(size_t) i]) > s; return k / n / 0.3173; }
inline double diffuseFrom (const Vec& ir, double fs, double until) { double t = 0; for (double a = 0.01; a < until; a += 0.005) if (echoDensity (ir, fs, a) < 0.8) t = a + 0.005; return t; }
/** The mode that stands out of the tail: the highest peak of the long spectrum over its surroundings (a third of an octave), dB. Noise gives about 8 to 11. */
inline double ringing (const Vec& ir, double fs, double from, double secs, double* hz = nullptr)
{
    int N = 1024; while (N < (int) (secs * fs) && N < 131072) N <<= 1; aemon::lin::Fft fft; fft.init (N); std::vector<C> z ((size_t) N); const int a = (int) (from * fs);
    // even out the decay first, so that the whole stretch counts
    const auto e = decay (ir); const double t60 = std::max (0.05, decayTime (e, fs, -5, -35)); for (int i = 0; i < N; ++i) { const int p = a + i; const double w = 0.5 - 0.5 * std::cos (2 * pi * i / N); z[(size_t) i] = p < (int) ir.size() ? ir[(size_t) p] * w * std::pow (10.0, std::min (3.0, (double) i / fs / t60 * 3 * 0.9)) : 0.0; }
    fft.forward (z.data()); std::vector<double> sum ((size_t) (N / 2 + 1), 0.0); for (int k = 0; k < N / 2; ++k) sum[(size_t) (k + 1)] = sum[(size_t) k] + std::norm (z[(size_t) k]);
    double worst = 0; for (int k = (int) (80 * N / fs); k < (int) (std::min (12000.0, fs * 0.4) * N / fs); ++k) { const int l = std::max (1, (int) (k / 1.122)), h = std::min (N / 2, (int) (k * 1.122) + 2); if (h - l < 24) continue; const double mean = (sum[(size_t) h] - sum[(size_t) l]) / (h - l); const double v = std::norm (z[(size_t) k]) / (mean + 1e-30); if (v > worst) { worst = v; if (hz) *hz = k * fs / N; } }
    return db10 (worst);
}
/** Flutter: the strongest repeat in the tail between 8 and 120 ms, 0 (none) to 1. */
inline double flutter (const Vec& ir, double fs, double from, double secs)
{
    // of the envelope and of the sound itself
    const int a = (int) (from * fs), n = std::min ((int) (secs * fs), (int) ir.size() - a); if (n < 4096) return 0; int N = 4096; while (N < 2 * n) N <<= 1; aemon::lin::Fft fft; fft.init (N); double best = 0;
    for (int kind = 0; kind < 2; ++kind)
    {
        const int dec = kind ? 8 : 1; std::vector<C> z ((size_t) N); double m = 0; const int cnt = n / dec; std::vector<double> x ((size_t) cnt); for (int i = 0; i < cnt; ++i) { double v = 0; if (kind) for (int j = 0; j < dec; ++j) v += std::abs (ir[(size_t) (a + i * dec + j)]); else v = ir[(size_t) (a + i)]; x[(size_t) i] = v; m += v / cnt; }
        if (kind) { double slow = m; const double c = 1 - std::exp (-1 / (0.08 * fs / dec)); for (int i = 0; i < cnt / 4; ++i) slow += (x[(size_t) i] - slow) * c; Biquad h, l; h.set (true, 25, 0.7071, fs / dec); l.set (false, 400, 0.7071, fs / dec); for (auto& v : x) { slow += (v - slow) * c; v = slow > 1e-12 ? l.run (h.run (v / slow)) : 0; } }
        for (int i = 0; i < cnt; ++i) z[(size_t) i] = x[(size_t) i] * (0.5 - 0.5 * std::cos (2 * pi * i / cnt)); fft.forward (z.data()); for (auto& v : z) v = std::norm (v); fft.inverse (z.data());
        for (int l = (int) (0.008 * fs / dec); l < (int) (0.12 * fs / dec); ++l) best = std::max (best, std::abs (z[(size_t) l].real()) / (z[0].real() + 1e-30));
    }
    return best;
}
/** Early against late energy, dB: 50 ms for words, 80 ms for music. */
inline double clarity (const Vec& ir, double fs, double ms) { double a = 1e-20, b = 1e-20; size_t first = 0; float top = 0; for (float v : ir) top = std::max (top, std::abs (v)); while (first < ir.size() && std::abs (ir[first]) < 0.05f * top) ++first; const size_t k = first + (size_t) (ms * fs / 1000); for (size_t i = first; i < ir.size(); ++i) (i < k ? a : b) += (double) ir[i] * ir[i]; return db10 (a / b); }
/** How alike the two ears' tails are, the most over 1 ms either way: 1 mono, under 0.5 wide. */
inline double alike (const Vec& l, const Vec& r, double fs, double from) { const int m = (int) (0.001 * fs), a = (int) (from * fs) + m, n = (int) std::min (l.size(), r.size()) - m; double best = 0, el = 1e-20, er = 1e-20; for (int i = a; i < n; ++i) { el += (double) l[(size_t) i] * l[(size_t) i]; er += (double) r[(size_t) i] * r[(size_t) i]; } for (int d = -m; d <= m; ++d) { double s = 0; for (int i = a; i < n; ++i) s += (double) l[(size_t) i] * r[(size_t) (i + d)]; best = std::max (best, std::abs (s) / std::sqrt (el * er)); } return best; }
/** What is lost when the two sides are added on a phone, dB against two unrelated sides (0: nothing special, under -3: it cancels). */
inline double monoLoss (const Vec& l, const Vec& r) { double s = 1e-20, p = 1e-20; for (size_t i = 0; i < std::min (l.size(), r.size()); ++i) { s += std::pow ((double) l[i] + r[i], 2); p += (double) l[i] * l[i] + (double) r[i] * r[i]; } return db10 (s / p); }
/** How far the pitch of a steady tone wanders, cents (from the phase, 99 % of the time within). */
inline double wobbleCents (const Vec& x, double fs, double hz)
{
    Biquad lr[2], li[2]; for (int k = 0; k < 2; ++k) { lr[k].set (false, 25, k ? 1.3066 : 0.5412, fs); li[k].set (false, 25, k ? 1.3066 : 0.5412, fs); }
    std::vector<double> dev; double last = 0; const int step = (int) (fs / 200); bool on = false; for (size_t i = 0; i < x.size(); ++i) { const double w = 2 * pi * hz * (double) i / fs, re = lr[1].run (lr[0].run (x[i] * std::cos (w))), im = li[1].run (li[0].run (-x[i] * std::sin (w))); if (i % (size_t) step != 0 || i < (size_t) (0.5 * fs)) continue; const double ph = std::atan2 (im, re); if (on && re * re + im * im > 1e-12) { double d = ph - last; while (d > pi) d -= 2 * pi; while (d < -pi) d += 2 * pi; dev.push_back (std::abs (1200 * std::log2 (std::max (0.2, 1 + d / (2 * pi) * fs / step / hz)))); } last = ph; on = true; }
    if (dev.empty()) return 0; std::sort (dev.begin(), dev.end()); return dev[(size_t) (0.99 * (double) (dev.size() - 1))];
}
} // namespace percept
