// Aemon Limiter engine objective tests (no JUCE). AGPL-3.0-or-later.
#include "LimEngine.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>

using namespace aemon::lim;
static constexpr double PI = 3.14159265358979323846;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static double db (double g) { return 20 * std::log10 (std::max (g, 1e-12)); }
static void run (Engine& e, std::vector<float>& l, std::vector<float>& r, int block = 512)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block) e.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> ((size_t) block, l.size() - pos));
}
/** Reference true peak: 16x windowed-sinc reconstruction (independent of the engine's interpolator). */
static double truePeakRef (const std::vector<float>& x, size_t from)
{
    const int K = 16, H = 64;
    double pk = 0;
    for (size_t i = from + H; i + H < x.size(); ++i)
        for (int ph = 0; ph < K; ++ph)
        {
            const double t = (double) ph / K;
            double acc = 0;
            for (int k = -H; k <= H; ++k)
            {
                const double xx = k - t;
                const double s = std::abs (xx) < 1e-12 ? 1.0 : std::sin (PI * xx) / (PI * xx);
                const double w = 0.5 + 0.5 * std::cos (PI * xx / (H + 1));
                acc += x[(size_t) ((long) i + k)] * s * w;
            }
            pk = std::max (pk, std::abs (acc));
        }
    return pk;
}
/** ITU-R BS.1770-4 true-peak meter (4x, 48-tap), as used by streaming services and loudness meters. */
static double truePeakBs1770 (const std::vector<float>& x, size_t from)
{
    static const double c[4][12] = {
        { 0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000, -0.0594482421875, 0.1373291015625, 0.9721679687500, -0.1022949218750, 0.0476074218750, -0.0266113281250, 0.0148925781250, -0.0083007812500 },
        { -0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250, -0.1665039062500, 0.4650878906250, 0.7797851562500, -0.2003173828125, 0.1015625000000, -0.0582275390625, 0.0330810546875, -0.0189208984375 },
        { -0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000, -0.2003173828125, 0.7797851562500, 0.4650878906250, -0.1665039062500, 0.0891113281250, -0.0517578125000, 0.0292968750000, -0.0291748046875 },
        { -0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750, -0.1022949218750, 0.9721679687500, 0.1373291015625, -0.0594482421875, 0.0332031250000, -0.0196533203125, 0.0109863281250, 0.0017089843750 } };
    double pk = 0;
    for (size_t i = from + 12; i < x.size(); ++i)
        for (int ph = 0; ph < 4; ++ph) { double a = 0; for (int k = 0; k < 12; ++k) a += c[ph][k] * x[i - (size_t) k]; pk = std::max (pk, std::abs (a)); }
    return pk;
}
/** Bright, music-like test material: pink-ish noise plus a high harmonic tone. */
static std::vector<float> brightMusic (int n, int seed)
{
    std::mt19937 rng ((unsigned) seed); std::normal_distribution<float> nd (0.0f, 1.0f);
    std::vector<float> v ((size_t) n);
    double b0 = 0, b1 = 0, b2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = nd (rng);
        b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        v[(size_t) i] = (float) (0.05 * (b0 + b1 + b2 + w * 0.1848) + 0.2 * std::sin (2 * PI * 9000.0 * i / 48000.0 + 0.7) + 0.1 * std::sin (2 * PI * 13500.0 * i / 48000.0));
    }
    return v;
}
static std::vector<float> noise (int n, float amp, int seed)
{
    std::mt19937 rng ((unsigned) seed); std::normal_distribution<float> nd (0.0f, amp);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = nd (rng);
    return v;
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. Below the ceiling and no push: bit-exact, just delayed by the reported latency
    {
        Params p; p.ceilingDb = -1;
        Engine e; e.setParams (p); e.prepare (fs);
        auto l = noise (48000, 0.05f, 1), r = noise (48000, 0.05f, 2), l0 = l, r0 = r;
        run (e, l, r);
        const int lat = e.latencySamples();
        double d = 0;
        for (size_t i = (size_t) lat; i < l.size(); ++i) d = std::max ({ d, (double) std::abs (l[i] - l0[i - (size_t) lat]), (double) std::abs (r[i] - r0[i - (size_t) lat]) });
        check (d == 0.0, "Quiet material passes bit-exact (delayed by " + std::to_string (lat) + " samples)", d == 0.0 ? "bit-exact" : "max diff " + std::to_string (d));
        check (lat == 72 + PeakInterpolator::delay(), "Latency = lookahead + interpolator", std::to_string (lat) + " samples (" + f2 (1000.0 * lat / fs, 2) + " ms)");
    }

    // 2. Loud music-like noise pushed 18 dB: sample peaks never exceed the ceiling
    {
        Params p; p.pushDb = 18; p.ceilingDb = -1; p.truePeak = false;
        Engine e; e.setParams (p); e.prepare (fs);
        auto l = noise (96000, 0.15f, 3), r = noise (96000, 0.15f, 4);
        run (e, l, r);
        float pk = 0; for (size_t i = 0; i < l.size(); ++i) pk = std::max ({ pk, std::abs (l[i]), std::abs (r[i]) });
        check (db (pk) <= -1.0 + 1e-6, "Sample peaks never pass the ceiling (+18 dB push, -1 dB ceiling)", "highest " + f2 (db (pk), 3) + " dBFS");
    }

    // 3. True peak: inter-sample peaks stay under the ceiling, measured with the BS.1770 meter
    for (int os : { 1, 2, 4, 8 })
    {
        Params p; p.pushDb = 12; p.ceilingDb = -1; p.truePeak = os > 1; p.oversampling = os;
        Engine e; e.setParams (p); e.prepare (fs);
        auto l = brightMusic (96000, 5), r = brightMusic (96000, 6);
        run (e, l, r);
        const double tp = std::max (truePeakBs1770 (l, 4800), truePeakBs1770 (r, 4800));
        if (os == 1) std::printf ("INFO  True peak with detection off (sample peaks only)  (%s dBTP for a -1.0 ceiling)\n", f2 (db (tp), 2).c_str());
        else check (db (tp) <= -1.0 + (os == 2 ? 0.3 : 0.1), "True peak with " + std::to_string (os) + "x detection, BS.1770 meter", f2 (db (tp), 2) + " dBTP for a -1.0 ceiling");
    }
    {   // extreme case: white noise has full energy up to Nyquist; measured with an ideal (129-tap) reconstruction
        Params p; p.pushDb = 12; p.ceilingDb = -1; p.truePeak = true; p.oversampling = 8;
        Engine e; e.setParams (p); e.prepare (fs);
        auto l = noise (48000, 0.12f, 5), r = noise (48000, 0.12f, 6);
        run (e, l, r);
        const double tp = std::max (truePeakRef (l, 4800), truePeakRef (r, 4800));
        const double bs = std::max (truePeakBs1770 (l, 4800), truePeakBs1770 (r, 4800));
        check (db (bs) <= -0.9, "White noise (worst case), 8x: BS.1770 true peak", f2 (db (bs), 2) + " dBTP; ideal reconstruction " + f2 (db (tp), 2) + " dBTP");
    }

    // 4. Exact gain: a steady sine 3 dB over the ceiling comes out exactly at the ceiling
    {
        Params p; p.ceilingDb = -6; p.truePeak = false; p.releaseMs = 300;
        Engine e; e.setParams (p); e.prepare (fs);
        std::vector<float> l (96000), r;
        for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (std::pow (10.0, -3.0 / 20) * std::sin (2 * PI * 1000 * (double) i / fs));
        r = l;
        run (e, l, r);
        float pk = 0; for (size_t i = 48000; i < l.size(); ++i) pk = std::max (pk, std::abs (l[i]));
        check (std::abs (db (pk) + 6) < 0.05, "Steady sine 3 dB over: output peak sits on the -6 dB ceiling", f2 (db (pk), 3) + " dBFS");
    }

    // 5. Release time: after a loud burst the gain recovers at the set rate (63% point)
    {
        Params p; p.ceilingDb = -12; p.truePeak = false; p.releaseMs = 100; p.autoRelease = false;
        Engine e; e.setParams (p); e.prepare (fs);
        const int n = 48000;
        std::vector<float> l ((size_t) n, 0.05f), r;
        for (int i = 4800; i < 14400; ++i) l[(size_t) i] = 0.5f;   // DC step: exact detector
        r = l;
        std::vector<double> g ((size_t) n);
        for (int i = 0; i < n; ++i) { float a = l[(size_t) i], b = r[(size_t) i]; e.process (&a, &b, 1); g[(size_t) i] = a; }
        const int lat = e.latencySamples();
        const double low = g[(size_t) (14000 + lat)] / 0.5, endG = 1.0;
        int t = 14400 + lat; while (t < n && g[(size_t) t] / 0.05 < low + 0.632 * (endG - low)) ++t;
        const double ms = (t - 14400 - lat) * 1000.0 / fs;
        check (std::abs (ms - 100) < 15, "Release time (100 ms, auto off)", f2 (ms, 1) + " ms");
    }

    // 6. Distortion: 1 kHz sine 6 dB over the ceiling (THD by DFT)
    {
        auto thd = [&] (double f, double rel)
        {
            Params p; p.ceilingDb = -6; p.truePeak = false; p.releaseMs = rel; p.autoRelease = false;
            Engine e; e.setParams (p); e.prepare (fs);
            std::vector<float> l (96000), r;
            for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (std::sin (2 * PI * f * (double) i / fs));
            r = l;
            run (e, l, r);
            auto bin = [&] (double fr) { std::complex<double> s = 0; for (size_t i = 48000; i < 96000; ++i) s += (double) l[i] * std::polar (1.0, -2 * PI * fr * (double) i / fs); return std::abs (s); };
            const double f1 = bin (f); double h = 0;
            for (int k = 2; k <= 8 && k * f < fs / 2; ++k) h += std::pow (bin (k * f), 2);
            return 100 * std::sqrt (h) / f1;
        };
        const double a = thd (1000, 100), b = thd (100, 300), c = thd (100, 100), d = thd (50, 10);
        check (a < 0.1, "Distortion, 1 kHz, 6 dB of limiting, 100 ms release", f2 (a, 4) + " % THD");
        check (b < 0.5, "Distortion, 100 Hz, 6 dB of limiting, 300 ms release", f2 (b, 3) + " % THD");
        std::printf ("INFO  Distortion, 100 Hz, 6 dB of limiting, 100 ms release  (%s %% THD)\n", f2 (c, 2).c_str());
        std::printf ("INFO  Distortion, 50 Hz, 6 dB of limiting, 10 ms release (fast release on bass is expected to distort)  (%s %% THD)\n", f2 (d, 2).c_str());
    }

    // 7. "Hear removed" + normal output = the (pushed, delayed) input
    {
        Params p; p.pushDb = 10; p.ceilingDb = -3; p.truePeak = false;
        Engine a, b; a.setParams (p); a.prepare (fs);
        Params pd = p; pd.delta = true; b.setParams (pd); b.prepare (fs);
        auto l = noise (48000, 0.1f, 7), r = noise (48000, 0.1f, 8);
        auto l1 = l, r1 = r, l2 = l, r2 = r;
        run (a, l1, r1); run (b, l2, r2);
        const int lat = a.latencySamples(); const double push = std::pow (10.0, 10 / 20.0);
        double d = 0;
        for (size_t i = (size_t) lat + 100; i < l.size(); ++i) d = std::max (d, std::abs ((double) l1[i] + l2[i] - l[i - (size_t) lat] * push));
        check (d < 1e-5, "Hear removed + output = input (nothing else changes)", "max error " + f2 (d * 1e6, 2) + "e-6");
    }

    // 8. Stereo link: a loud left side pulls the right down only when linked
    {
        auto rightChange = [&] (double link)
        {
            Params p; p.ceilingDb = -6; p.truePeak = false; p.link = link;
            Engine e; e.setParams (p); e.prepare (fs);
            std::vector<float> l (48000), r (48000);
            for (size_t i = 0; i < l.size(); ++i) { l[i] = (float) (0.9 * std::sin (2 * PI * 200 * (double) i / fs)); r[i] = (float) (0.1 * std::sin (2 * PI * 300 * (double) i / fs)); }
            auto r0 = r;
            run (e, l, r);
            double so = 0, si = 0; for (size_t i = 24000; i < r.size(); ++i) { so += (double) r[i] * r[i]; si += (double) r0[i] * r0[i]; }
            return 10 * std::log10 (so / si);
        };
        const double linked = rightChange (1.0), unlinked = rightChange (0.0);
        check (linked < -3 && std::abs (unlinked) < 0.01, "Stereo link: right follows left only when linked", f2 (linked, 2) + " dB linked, " + f2 (unlinked, 3) + " dB unlinked");
    }

    // 9. Dither: 16-bit output lands on the 16-bit grid, noise about -96 dBFS
    {
        Params p; p.ditherBits = 16; p.ceilingDb = -1;
        Engine e; e.setParams (p); e.prepare (fs);
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = (float) (0.01 * std::sin (2 * PI * 440 * (double) i / fs));
        run (e, l, r);
        bool grid = true; for (float x : l) { const double q = x * 32768.0; grid &= std::abs (q - std::round (q)) < 1e-3; }
        std::vector<float> zl (48000, 0.0f), zr (48000, 0.0f);
        Engine z; z.setParams (p); z.prepare (fs); run (z, zl, zr);
        double s = 0; for (float x : zl) s += (double) x * x;
        const double nDb = 10 * std::log10 (s / zl.size());
        check (grid && nDb > -105 && nDb < -90, "16-bit TPDF dither", std::string (grid ? "on the 16-bit grid" : "off grid") + ", noise " + f2 (nDb, 1) + " dBFS RMS");
    }

    // 10. No clicks when push, ceiling and bypass change during playback
    {
        Params p; p.ceilingDb = -6; p.truePeak = true;
        Engine e; e.setParams (p); e.prepare (fs);
        std::vector<float> l (64 * 1500), r;
        for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.4 * std::sin (2 * PI * 440 * (double) i / fs));
        r = l;
        double worst = 0; float prev = 0;
        for (size_t pos = 0; pos < l.size(); pos += 64)
        {
            if (pos == 64 * 500) { p.pushDb = 9; p.ceilingDb = -3; e.setParams (p); }
            if (pos == 64 * 1000) { p.bypass = true; e.setParams (p); }
            e.process (l.data() + pos, r.data() + pos, 64);
            for (size_t i = pos; i < pos + 64; ++i) { if (i > 300) worst = std::max (worst, (double) std::abs (l[i] - prev)); prev = l[i]; }
        }
        const double limit = 0.4 * std::pow (10.0, 9 / 20.0) * 2 * PI * 440 / fs * 1.3;
        check (worst < limit, "No clicks when push, ceiling and bypass change", "largest step " + f2 (worst, 4) + ", limit " + f2 (limit, 4));
    }

    // 11. Stability and CPU (8x true-peak detection, everything on)
    {
        std::mt19937 rng (9); std::uniform_real_distribution<double> u (0, 1);
        bool ok = true;
        for (int t = 0; t < 40 && ok; ++t)
        {
            Params p; p.pushDb = 24 * u (rng); p.ceilingDb = -6 * u (rng); p.releaseMs = 5 + 995 * u (rng); p.autoRelease = u (rng) > 0.5;
            p.lookaheadMs = 0.1 + 9.9 * u (rng); p.style = (Style) (int) (4 * u (rng)); p.link = u (rng); p.truePeak = u (rng) > 0.3;
            p.oversampling = 1 << (int) (4 * u (rng)); p.ditherBits = u (rng) > 0.7 ? 16 : 0;
            Engine e; e.setParams (p); e.prepare (fs);
            auto l = noise (8192, 0.5f, t), r = noise (8192, 0.5f, t + 100);
            run (e, l, r);
            for (float x : l) ok &= std::isfinite (x) && std::abs (x) <= 1.0f;
        }
        check (ok, "Stable with 40 random setups", ok ? "all outputs finite, never above 0 dBFS" : "problem");
        Params p; p.pushDb = 8; p.truePeak = true; p.oversampling = 8; p.ditherBits = 16;
        Engine e; e.setParams (p); e.prepare (fs);
        auto l = noise (48000 * 30, 0.2f, 11), r = noise (48000 * 30, 0.2f, 12);
        const auto t0 = std::chrono::high_resolution_clock::now();
        run (e, l, r);
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 6.0, "CPU with 8x true-peak detection and dither", f2 (100 * s / 30, 2) + "% of one core");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
