// Aemon Clipper engine: objective tests (no JUCE). AGPL-3.0-or-later.
#include "ClipEngine.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>

using namespace aemon::clip;
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
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) { const auto wl = std::polar (1.0, -2 * PI / (double) len); for (size_t i = 0; i < n; i += len) { std::complex<double> w = 1; for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } } }
}
static double alias (Params p, int bin, bool stereoDifferent = false)
{
    const int N = 1 << 16;
    Engine e; e.setParams (p); e.prepare (48000);
    std::vector<float> l ((size_t) (2 * N)), r;
    for (int i = 0; i < 2 * N; ++i) l[(size_t) i] = (float) (0.98 * std::sin (2 * PI * bin * (double) i / N + 0.3));
    r = l; if (stereoDifferent) for (auto& v : r) v *= 0.3f;
    run (e, l, r);
    auto worst = [&] (const std::vector<float>& x) { std::vector<std::complex<double>> s ((size_t) N); for (int i = 0; i < N; ++i) s[(size_t) i] = x[(size_t) (N + i)]; fft (s); double w = 0; for (int k = 8; k < N / 2 - 1; ++k) if (k % bin != 0) w = std::max (w, std::abs (s[(size_t) k])); return db (w / std::abs (s[(size_t) bin])); };
    return stereoDifferent ? std::max (worst (l), worst (r)) : worst (l);
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. Bit-exact while nothing is clipped, at every quality, lined up with the reported latency
    for (int f : { 1, 2, 4, 8 })
    {
        Params p; p.oversampling = f; Engine e; e.setParams (p); e.prepare (fs);
        auto l = noise (24000, 0.05f, 1), r = noise (24000, 0.05f, 2), l0 = l, r0 = r; run (e, l, r);
        const size_t lat = (size_t) e.latencySamples(); bool ok = true;
        for (size_t i = lat; i < l.size(); ++i) ok &= l[i] == l0[i - lat] && r[i] == r0[i - lat];
        check (ok, "Nothing clipped, " + std::to_string (f) + "x: bit-exact", "latency " + std::to_string (lat) + " samples (" + f2 (1000.0 * (double) lat / fs) + " ms)");
    }
    // 2. The output never passes the ceiling, whatever comes in
    {
        double worst = -99; std::string where;
        for (int f : { 1, 2, 4, 8 }) for (double soft : { 0.0, 0.35, 1.0 }) for (double link : { 0.0, 1.0 }) for (double push : { 3.0, 9.0, 18.0 })
        {
            Params p; p.oversampling = f; p.softness = soft; p.link = link; p.pushDb = push; p.ceilingDb = -1; Engine e; e.setParams (p); e.prepare (fs);
            auto l = noise (12000, 0.4f, f), r = noise (12000, 0.3f, f + 9); run (e, l, r);
            for (size_t i = 0; i < l.size(); ++i) { const double v = db (std::max (std::abs (l[i]), std::abs (r[i]))); if (v > worst) { worst = v; where = std::to_string (f) + "x push " + f2 (push, 0); } }
        }
        check (worst <= -1.0 + 1e-5, "Never above the ceiling (white noise, every setting)", "highest " + f2 (worst, 5) + " dB for -1.0");
    }
    // 3. False tones, the whole plugin engine including the guard
    {
        Params p; p.pushDb = 12; p.ceilingDb = 0; p.softness = 0;
        const double a = alias (p, 6827), b = alias (p, 6827, true);
        check (a <= -90, "Hard clip, 5 kHz, +12 dB, 8x: false tones", f2 (a, 1) + " dB");
        check (b <= -90, "The same with a quieter right side, fully linked", f2 (b, 1) + " dB");
        for (int bin : { 1367, 2731, 13653 }) { p.softness = 0.35; std::printf ("INFO  Softness 35, +12 dB, %.0f Hz: false tones  (%s dB)\n", bin * 48000.0 / 65536, f2 (alias (p, bin), 1).c_str()); }
        p.oversampling = 1; p.softness = 0; std::printf ("INFO  Quality off  (%s dB)\n", f2 (alias (p, 6827), 1).c_str());
    }
    // 4. What the guard costs: level of a driven tone and of dense music-like noise against a perfect clip
    {
        Params p; p.pushDb = 9; p.ceilingDb = 0; p.softness = 0; Engine e; e.setParams (p); e.prepare (fs);
        auto l = noise (96000, 0.25f, 4); { double z = 0; for (auto& v : l) { z += 0.35 * (v - z); v = (float) (2 * z); } }
        auto r = l; auto ideal = l; for (auto& v : ideal) v = (float) std::clamp (v * std::pow (10.0, 9 / 20.0), -1.0, 1.0);
        run (e, l, r); double a = 0, b = 0; for (size_t i = 4000; i < l.size(); ++i) { a += (double) l[i] * l[i]; b += (double) ideal[i] * ideal[i]; }
        std::printf ("INFO  Dense noise 9 dB over: loudness against a naive clip  (%s dB, the price of a ceiling that holds between samples)\n", f2 (10 * std::log10 (a / b)).c_str());
        // what a clipper is for: drum hits with the tips 3 dB over
        std::vector<float> d (96000); std::mt19937 g (5); std::normal_distribution<double> nd (0, 1);
        for (size_t i = 0; i < d.size(); ++i) { const double t = std::fmod ((double) i / fs, 0.5); d[i] = (float) (0.9 * std::sin (2 * PI * (48 * t + 3 * (1 - std::exp (-t * 30)))) * std::exp (-t * 7) + 0.25 * nd (g) * std::exp (-t * 40) + 0.05 * std::sin (2 * PI * 220 * (double) i / fs)); }
        double top = 0; for (float v : d) top = std::max (top, (double) std::abs (v));
        p.pushDb = 3 - db (top); p.softness = 0.25; Engine e2; e2.setParams (p); e2.prepare (fs);
        auto x = d, y = d, id = d; run (e2, x, y); for (auto& v : id) v = (float) std::clamp (v * std::pow (10.0, p.pushDb / 20), -1.0, 1.0);
        double sa = 0, sb = 0; const size_t lat = (size_t) e2.latencySamples(); for (size_t i = 8000; i + lat < d.size(); ++i) { sa += (double) x[i + lat] * x[i + lat]; sb += (double) id[i] * id[i]; }
        const double loss = 10 * std::log10 (sa / sb);
        check (loss > -0.5, "Drum hits, tips 3 dB over: level kept", f2 (loss) + " dB against a naive clip");
    }
    // 4b. The true peak holds as well, read by the official BS.1770 filter
    {
        static const double c[4][12] = {
            { 0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000, -0.0594482421875, 0.1373291015625, 0.9721679687500, -0.1022949218750, 0.0476074218750, -0.0266113281250, 0.0148925781250, -0.0083007812500 },
            { -0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250, -0.1665039062500, 0.4650878906250, 0.7797851562500, -0.2003173828125, 0.1015625000000, -0.0582275390625, 0.0330810546875, -0.0189208984375 },
            { -0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000, -0.2003173828125, 0.7797851562500, 0.4650878906250, -0.1665039062500, 0.0891113281250, -0.0517578125000, 0.0292968750000, -0.0291748046875 },
            { -0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750, -0.1022949218750, 0.9721679687500, 0.1373291015625, -0.0594482421875, 0.0332031250000, -0.0196533203125, 0.0109863281250, 0.0017089843750 } };
        double worst = -99;
        for (double push : { 3.0, 9.0 }) for (double f0 : { 60.0, 997.0, 5000.0 })
        {
            Params p; p.pushDb = push; p.ceilingDb = -1; Engine e; e.setParams (p); e.prepare (fs);
            std::vector<float> l (48000), r; for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.8 * std::sin (2 * PI * f0 * (double) i / fs) + 0.2 * std::sin (2 * PI * 3.17 * f0 * (double) i / fs)); r = l; run (e, l, r);
            for (size_t i = 4812; i < l.size(); ++i) for (auto& ph : c) { double a = 0; for (int k = 0; k < 12; ++k) a += ph[k] * l[i - (size_t) k]; worst = std::max (worst, db (std::abs (a))); }
        }
        check (worst <= -0.5, "True peak of driven tones, BS.1770 filter", f2 (worst) + " dBTP for a -1.0 ceiling");
    }
    // 5. Removed + output = input; link keeps the image; mix 0 is the pushed input
    {
        Params p; p.pushDb = 8; p.ceilingDb = -3; Engine a, b; a.setParams (p); a.prepare (fs); auto pd = p; pd.delta = true; b.setParams (pd); b.prepare (fs);
        auto l = noise (24000, 0.2f, 7), r = noise (24000, 0.2f, 8), l1 = l, r1 = r, l2 = l, r2 = r; run (a, l1, r1); run (b, l2, r2);
        const size_t lat = (size_t) a.latencySamples(); double d = 0; for (size_t i = lat; i < l.size(); ++i) d = std::max (d, std::abs ((double) l1[i] + l2[i] - l[i - lat] * std::pow (10.0, 8 / 20.0)));
        check (d < 1e-5, "Hear removed + output = input", "max error " + f2 (d * 1e6, 2) + "e-6");
        Params m = p; m.mix = 0; Engine c; c.setParams (m); c.prepare (fs); auto l3 = l, r3 = r; run (c, l3, r3);
        double d2 = 0; for (size_t i = lat; i < l.size(); ++i) d2 = std::max (d2, std::abs ((double) l3[i] - l[i - lat] * std::pow (10.0, 8 / 20.0)));
        check (d2 < 1e-6, "Mix 0 is the untouched sound at the pushed level", "max error " + f2 (d2 * 1e6, 3) + "e-6");
        auto ratio = [&] (double link) { Params q; q.pushDb = 12; q.ceilingDb = -1; q.link = link; Engine e; e.setParams (q); e.prepare (fs); std::vector<float> x (24000), y (24000); for (size_t i = 0; i < x.size(); ++i) { x[i] = (float) (0.8 * std::sin (2 * PI * 200 * (double) i / fs)); y[i] = x[i] * 0.25f; } run (e, x, y); double sx = 0, sy = 0; for (size_t i = 4000; i < x.size(); ++i) { sx += (double) x[i] * x[i]; sy += (double) y[i] * y[i]; } return 10 * std::log10 (sy / sx); };
        const double linked = ratio (1.0), free = ratio (0.0);
        check (std::abs (linked + 12.04) < 0.1 && free > -9, "Link keeps the balance between the sides", f2 (linked) + " dB linked (12.04 in), " + f2 (free) + " dB unlinked");
    }
    // 6. No clicks; block size does not matter; stable; CPU
    {
        Params p; p.pushDb = 0; p.ceilingDb = -6; Engine e; e.setParams (p); e.prepare (fs);
        std::vector<float> l (64 * 1600), r; for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.4 * std::sin (2 * PI * 300 * (double) i / fs)); r = l;
        double worst = 0, prev = 0;
        for (size_t q = 0; q < l.size(); q += 64)
        {
            if (q == 64 * 300) { p.pushDb = 8; p.softness = 1; e.setParams (p); }
            if (q == 64 * 600) { p.ceilingDb = -2; p.softness = 0; e.setParams (p); }
            if (q == 64 * 900) { p.delta = true; e.setParams (p); }
            if (q == 64 * 1100) { p.delta = false; p.mix = 0.4; e.setParams (p); }
            if (q == 64 * 1300) { p.bypass = true; e.setParams (p); }
            e.process (l.data() + q, r.data() + q, 64);
            for (size_t i = q; i < q + 64; ++i) { if (i > 400) worst = std::max (worst, std::abs (l[i] - prev)); prev = l[i]; }
        }
        const double limit = 0.4 * std::pow (10.0, 8 / 20.0) * 2 * PI * 300 / fs * 1.5;
        check (worst < limit, "No clicks when anything changes", "largest step " + f2 (worst, 4) + ", limit " + f2 (limit, 4));
        auto render = [&] (int block) { Params q; q.pushDb = 10; Engine x; x.setParams (q); x.prepare (fs); auto a = noise (9000, 0.3f, 3), b = noise (9000, 0.3f, 4); run (x, a, b, block); return a; };
        check (render (512) == render (1) && render (512) == render (2048), "Same sound at every block size", "bit-exact");
        std::mt19937 g (9); std::uniform_real_distribution<double> u (0, 1); bool ok = true;
        for (int t = 0; t < 40; ++t) { Params q; q.pushDb = 18 * u (g); q.ceilingDb = -12 * u (g); q.softness = u (g); q.link = u (g); q.mix = 1; q.oversampling = 1 << (int) (4 * u (g)); Engine x; x.setParams (q); x.prepare (44100 + 147900 * u (g)); auto a = noise (6000, 1.0f, t), b = noise (6000, 1.0f, t + 99); run (x, a, b); for (float v : a) ok &= std::isfinite (v) && std::abs (v) <= 1.0f; }
        check (ok, "Stable with 40 random setups", "finite, under the ceiling");
        Params q; q.pushDb = 8; Engine x; x.setParams (q); x.prepare (fs); auto a = noise (480000, 0.2f, 11), b = noise (480000, 0.2f, 12);
        const auto t0 = std::chrono::high_resolution_clock::now(); run (x, a, b); const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 0.8, "CPU, stereo, 8x (target 5 %, limit 8 % for slower build machines)", f2 (10 * s) + " % of one core");
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
