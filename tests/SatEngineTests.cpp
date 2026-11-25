// Aemon Saturation engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "SatEngine.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>

using namespace aemon::sat;
static constexpr double PI = 3.14159265358979323846;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static double db (double g) { return 20 * std::log10 (std::max (g, 1e-15)); }
static void run (Engine& e, std::vector<float>& l, std::vector<float>& r, int block = 512) { for (size_t p = 0; p < l.size(); p += (size_t) block) e.process (l.data() + p, r.data() + p, (int) std::min<size_t> ((size_t) block, l.size() - p)); }
static std::vector<float> noise (int n, float amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<float> d (0, amp); std::vector<float> v ((size_t) n); for (auto& x : v) x = d (g); return v; }
static std::vector<float> music (int n, float amp, int seed)
{
    std::mt19937 g ((unsigned) seed); std::normal_distribution<double> nd (0, 1); std::vector<float> v ((size_t) n); double z = 0;
    for (int i = 0; i < n; ++i) { const double t = i / 48000.0, b = std::fmod (t, 0.5); z += 0.2 * (nd (g) - z); v[(size_t) i] = (float) (amp * (0.7 * std::sin (2 * PI * (50 * b + 3 * (1 - std::exp (-b * 30)))) * std::exp (-b * 7) + 0.5 * z * std::exp (-std::fmod (t + 0.25, 0.5) * 18) + 0.12 * (std::sin (2 * PI * 220 * t) + std::sin (2 * PI * 277.2 * t) + std::sin (2 * PI * 329.6 * t)))); }
    return v;
}
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { const auto wl = std::polar (1.0, -2 * PI / (double) len); for (size_t i = 0; i < n; i += len) { std::complex<double> w = 1; for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } } }
}
static std::vector<std::complex<double>> spectrum (Params p, int bin, double amp)
{
    const int N = 1 << 16; Engine e; e.setParams (p); e.prepare (48000);
    std::vector<float> l ((size_t) (3 * N)), r; for (int i = 0; i < 3 * N; ++i) l[(size_t) i] = (float) (amp * std::sin (2 * PI * bin * (double) i / N)); r = l; run (e, l, r);
    std::vector<std::complex<double>> s ((size_t) N); for (int i = 0; i < N; ++i) s[(size_t) i] = l[(size_t) (2 * N + i)]; fft (s); return s;
}
static const char* names[] { "Warm", "Thick", "Bright", "Broken" };

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. Nothing added: zero drive and mix 0 are bit-exact, lined up
    for (int f : { 1, 2, 4, 8 })
    {
        bool ok = true; size_t lat = 0;
        for (int mode = 0; mode < 2; ++mode)
        {
            Params p; p.oversampling = f; if (mode) { p.driveDb = 15; p.mix = 0; } Engine e; e.setParams (p); e.prepare (fs);
            auto l = noise (20000, 0.2f, 1), r = noise (20000, 0.2f, 2), l0 = l; run (e, l, r); lat = (size_t) e.latencySamples();
            for (size_t i = lat; i < l.size(); ++i) ok &= l[i] == l0[i - lat];
        }
        check (ok, "Zero drive, and mix 0 with drive, " + std::to_string (f) + "x: bit-exact", "latency " + std::to_string (lat));
    }
    // 2. Antiderivatives are right, and the curve grows out of a straight line
    {
        double worst = 0; for (int c = 0; c < 4; ++c) for (double d : { 1.0, 7.0, 15.0, 30.0 }) for (double b : { 0.0, 0.5, 2.0 }) { Curve q; q.set ((Character) c, d, b); for (double x = -2; x <= 2; x += 0.01) { const double h = 1e-5; worst = std::max (worst, std::abs ((q.D (x + h) - q.D (x - h)) / (2 * h) - (q.f (x) - x))); } }
        check (worst < 2e-3, "Every curve's antiderivative matches", "max error " + f2 (worst, 6));
        double jump = 0; for (int c = 0; c < 4; ++c) { Curve q; q.set ((Character) c, 0.05, 0.5); for (double x = -1; x <= 1; x += 0.01) jump = std::max (jump, std::abs (q.f (x) - x)); }
        check (jump < 0.01, "A touch of drive changes a touch", "0.05 dB drive differs by at most " + f2 (jump, 5));
    }
    // 3. The bar: harmonics within 0.5 dB of the published profile (the curve itself), 1 kHz at -12 dB, no tone filters in the way
    for (int c = 0; c < 4; ++c)
    {
        Params p; p.driveDb = 9; p.character = (Character) c; p.match = false; p.focus = Focus::all;
        Curve q; q.set (p.character, p.driveDb, p.bias);
        // the published profile includes the fixed tone around the curve, so it is computed for the whole chain, harmonic by harmonic
        const int bin = 1367, N = 4096; const double a = 1.0 - std::exp (-p.driveDb / 5.0);
        auto mag = [&] (Biquad b, double f) { const auto z1 = std::polar (1.0, -2 * PI * f / fs), z2 = z1 * z1; return std::abs ((b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2)); };
        Biquad pre, post;
        if (c == 0) { pre.set (Biquad::T::high, 5000, -2.5 * a, 0.7071, fs); post.set (Biquad::T::low, 160, 1 * a, 0.7071, fs); }
        if (c == 1) { pre.set (Biquad::T::low, 180, 4 * a, 0.7071, fs); post.set (Biquad::T::low, 180, -2.5 * a, 0.7071, fs); }
        if (c == 2) { pre.set (Biquad::T::high, 3000, 6 * a, 0.7071, fs); post.set (Biquad::T::high, 3000, -4 * a, 0.7071, fs); }
        if (c == 3) { pre.set (Biquad::T::peak, 900, 3 * a, 0.6, fs); post.set (Biquad::T::high, 8000, -3 * a, 0.7071, fs); }
        const double f0 = bin * fs / 65536, amp = 0.25 * mag (pre, f0);
        auto s = spectrum (p, bin, 0.25); double worst = 0; std::string prof;
        for (int h = 1; h <= 7; ++h)
        {
            std::complex<double> z = 0; for (int i = 0; i < N; ++i) z += q.f (amp * std::sin (2 * PI * i / N)) * std::polar (1.0, -2 * PI * h * i / N);
            const double want = 2 * std::abs (z) / N * mag (post, h * f0), got = 2 * std::abs (s[(size_t) (h * bin)]) / 65536;
            if (db (want) > -90) { worst = std::max (worst, std::abs (db (got) - db (want))); prof += f2 (db (got), 0) + " "; }
        }
        check (worst < 0.5, std::string (names[c]) + ", 9 dB: harmonics 1 to 7 match the profile", "within " + f2 (worst, 3) + " dB; " + prof + "dB");
    }
    // 4. False tones
    for (int c = 0; c < 4; ++c)
    {
        double worst = -300; std::string row;
        for (double d : { 9.0, 18.0, 30.0 })
        {
            Params p; p.driveDb = d; p.character = (Character) c; p.match = false; auto s = spectrum (p, 6827, 0.5); double w = 0;
            for (int k = 8; k < 32767; ++k) if (k % 6827 != 0) w = std::max (w, std::abs (s[(size_t) k]));
            const double v = db (w / std::abs (s[6827])); row += f2 (v, 0) + " "; if (d <= 18.0) worst = std::max (worst, v);
        }
        check (worst <= -80, std::string (names[c]) + ", 5 kHz, 8x: false tones at 9, 18, 30 dB of drive", row + "dB");
    }
    // 5. Level matched: within 0.3 LU on music, every character and drive
    {
        double worst = 0; std::string where;
        for (int c = 0; c < 4; ++c) for (double d : { 6.0, 15.0, 27.0 }) for (int foc : { 0, 1, 3 })
        {
            Params p; p.driveDb = d; p.character = (Character) c; p.focus = (Focus) foc; Engine e; e.setParams (p); e.prepare (fs);
            auto l = music (48000 * 14, 0.3f, 3), r = l, l0 = l; run (e, l, r);
            // K-weighted level of the last 6 seconds (the same weighting loudness meters use)
            auto lufs = [&] (const std::vector<float>& x, size_t off) { Biquad a, b; a.set (Biquad::T::high, 1682, 4.0, 0.7071, fs); const double w = 2 * PI * 38.1 / fs, co = std::cos (w), al = std::sin (w) / 1.0006, dd = 1 + al; b.b0 = (1 + co) / 2 / dd; b.b1 = -(1 + co) / dd; b.b2 = b.b0; b.a1 = -2 * co / dd; b.a2 = (1 - al) / dd; double s = 0; size_t n = 0; for (size_t i = 48000 * 6; i < x.size(); ++i) { const double v = b.run (a.run (x[i])); if (i >= 48000 * 7 + off) { s += v * v; ++n; } } return 10 * std::log10 (s / (double) n); };
            const double diff = lufs (l, 0) - lufs (l0, 0);
            if (std::abs (diff) > std::abs (worst)) { worst = diff; where = std::string (names[c]) + " " + f2 (d, 0) + " dB"; }
        }
        check (std::abs (worst) < 0.3, "Level matched on music: loudness difference", f2 (worst, 3) + " LU, " + where);
    }
    // 6. Mix: the dry and wet line up, so half and half has no comb filtering
    {
        Params p; p.driveDb = 6; p.mix = 0.5; p.match = false; p.character = Character::bright; Engine e; e.setParams (p); e.prepare (fs);
        std::vector<float> l (16384, 0.0f), r; l[4000] = 1e-4f; r = l; run (e, l, r);
        std::vector<std::complex<double>> s (16384); for (size_t i = 0; i < 16384; ++i) s[i] = l[i] / 1e-4; fft (s);
        double lo = 99, hi = -99; for (int k = 10; k < 16384 * 19000 / 48000; ++k) { const double v = db (std::abs (s[(size_t) k])); lo = std::min (lo, v); hi = std::max (hi, v); }
        check (hi - lo < 2.0, "Mix 50 %: no comb filtering", "response between " + f2 (lo) + " and " + f2 (hi) + " dB (the character's own tone)");
    }
    // 7. Bias makes even harmonics; focus aims them
    {
        auto even = [&] (double bias) { Params p; p.driveDb = 9; p.bias = bias; p.match = false; auto s = spectrum (p, 1367, 0.25); return db (std::abs (s[2734]) / std::abs (s[1367])); };
        const double e0 = even (0), e1 = even (1.0);
        check (e0 < -100 && e1 > -30, "Bias: none is purely odd, more gives even harmonics", "2nd harmonic " + f2 (e0, 0) + " dB at 0 %, " + f2 (e1, 0) + " dB at 100 %");
        auto thd = [&] (Focus f, int bin) { Params p; p.driveDb = 9; p.focus = f; p.match = false; auto s = spectrum (p, bin, 0.2); double h = 0; for (int k = 2; k < 6 && k * bin < 32000; ++k) h += std::norm (s[(size_t) (k * bin)]); return db (std::sqrt (h) / std::abs (s[(size_t) bin])); };
        const double lowL = thd (Focus::lows, 137), lowH = thd (Focus::highs, 137), hiL = thd (Focus::lows, 8191), hiH = thd (Focus::highs, 8191);
        check (lowL > lowH + 4 && hiH > hiL + 4, "Focus: lows colour the lows, highs the highs", "100 Hz " + f2 (lowL, 0) + " vs " + f2 (lowH, 0) + " dB, 6 kHz " + f2 (hiL, 0) + " vs " + f2 (hiH, 0) + " dB");
    }
    // 8. No clicks; block size; stable; CPU
    {
        Params p; p.driveDb = 6; p.match = false; Engine e; e.setParams (p); e.prepare (fs);
        std::vector<float> l (64 * 2400), r; for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.4 * std::sin (2 * PI * 300 * (double) i / fs)); r = l; double worst = 0, prev = 0, peak = 0;
        for (size_t q = 0; q < l.size(); q += 64)
        {
            if (q == 64 * 300) { p.driveDb = 14; e.setParams (p); } if (q == 64 * 600) { p.character = Character::thick; e.setParams (p); } if (q == 64 * 900) { p.focus = Focus::lows; e.setParams (p); }
            if (q == 64 * 1200) { p.mix = 0.3; p.bias = 1.5; e.setParams (p); } if (q == 64 * 1500) { p.driveDb = 0; e.setParams (p); } if (q == 64 * 1800) { p.driveDb = 10; p.toneDb = 5; e.setParams (p); } if (q == 64 * 2100) { p.bypass = true; e.setParams (p); }
            e.process (l.data() + q, r.data() + q, 64);
            for (size_t i = q; i < q + 64; ++i) { if (i > 400) { worst = std::max (worst, std::abs (l[i] - prev)); peak = std::max (peak, (double) std::abs (l[i])); } prev = l[i]; }
        }
        const double limit = std::max (0.4, peak) * 2 * PI * 300 / fs * 4;      // a driven sine has steeper sides
        check (worst < limit, "No clicks when anything changes", "largest step " + f2 (worst, 4) + ", limit " + f2 (limit, 4));
        auto render = [&] (int block) { Params q; q.driveDb = 12; Engine x; x.setParams (q); x.prepare (fs); auto a = music (9000, 0.3f, 3), b = a; run (x, a, b, block); return a; };
        check (render (512) == render (1) && render (512) == render (2048), "Same sound at every block size", "bit-exact");
        std::mt19937 g (9); std::uniform_real_distribution<double> u (0, 1); bool ok = true; double top = 0;
        for (int t = 0; t < 60; ++t) { Params q; q.driveDb = 30 * u (g); q.character = (Character) (int) (3.99 * u (g)); q.focus = (Focus) (int) (3.99 * u (g)); q.bias = 2 * u (g); q.toneDb = 12 * u (g) - 6; q.mix = u (g); q.oversampling = 1 << (int) (4 * u (g)); Engine x; x.setParams (q); x.prepare (44100 + 147900 * u (g)); auto a = noise (6000, 0.7f, t), b = noise (6000, 0.7f, t + 99); run (x, a, b); for (float v : a) { ok &= std::isfinite (v); top = std::max (top, (double) std::abs (v)); } }
        check (ok && top < 40, "Stable with 60 random setups", "finite, highest " + f2 (db (top), 1) + " dB");
        Params q; q.driveDb = 12; Engine x; x.setParams (q); x.prepare (fs); auto a = noise (480000, 0.2f, 11), b = noise (480000, 0.2f, 12);
        const auto t0 = std::chrono::high_resolution_clock::now(); run (x, a, b); const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 0.9, "CPU, stereo, 8x (target 5 %, limit 9 % for slower build machines)", f2 (10 * s) + " % of one core");
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
