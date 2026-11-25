// Aemon crossovers: objective tests (no JUCE). AGPL-3.0-or-later.
#include "aemon_Crossover.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace aemon::xover;
static constexpr double PI = 3.14159265358979323846;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static std::vector<std::vector<double>> impulse (int bands, const double* f, int slope, double fs, int n)
{
    Bank b; b.set (bands, f, slope); b.prepare (fs); b.set (bands, f, slope); b.snap(); b.clear();
    std::vector<std::vector<double>> o ((size_t) bands, std::vector<double> ((size_t) n));
    double t[Bank::maxBands];
    for (int i = 0; i < n; ++i) { b.process (i == 0 ? 1.0 : 0.0, t); for (int k = 0; k < bands; ++k) o[(size_t) k][(size_t) i] = t[k]; }
    return o;
}
static std::complex<double> at (const std::vector<double>& h, double f, double fs) { std::complex<double> z = 0; for (size_t i = 0; i < h.size(); ++i) z += h[i] * std::polar (1.0, -2 * PI * f * (double) i / fs); return z; }
static double db (double g) { return 20 * std::log10 (std::max (g, 1e-15)); }

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double freqs[4] { 110, 600, 2500, 8000 };

    // 1. The bar: untouched bands add back to a flat level within 0.01 dB
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int slope : { 24, 48 })
        {
            double worst = 0; int wb = 0;
            for (int bands : { 2, 3, 4, 5 })
            {
                const int n = (int) (fs * 0.35);
                auto o = impulse (bands, freqs, slope, fs, n);
                std::vector<double> sum ((size_t) n, 0.0);
                for (auto& b : o) for (int i = 0; i < n; ++i) sum[(size_t) i] += b[(size_t) i];
                for (double f = 20; f <= 20000; f *= 1.12) { const double d = std::abs (db (std::abs (at (sum, f, fs)))); if (d > worst) { worst = d; wb = bands; } }
            }
            check (worst <= 0.01, "2 to 5 bands add back flat, " + std::to_string (slope) + " dB/oct, " + f2 (fs / 1000, 1) + " kHz", "worst " + f2 (worst, 6) + " dB (" + std::to_string (wb) + " bands)");
        }

    // 2. Each band is where it should be: -6 dB at the split, the right slope beyond
    for (int slope : { 24, 48 })
    {
        const double one[1] { 1000 };
        auto o = impulse (2, one, slope, 48000, 24000);
        const double lo = db (std::abs (at (o[0], 1000, 48000))), hi = db (std::abs (at (o[1], 1000, 48000)));
        const double loOct = db (std::abs (at (o[0], 4000, 48000))) - db (std::abs (at (o[0], 2000, 48000))), hiOct = db (std::abs (at (o[1], 125, 48000))) - db (std::abs (at (o[1], 250, 48000)));
        check (std::abs (lo + 6.02) < 0.05 && std::abs (hi + 6.02) < 0.05 && std::abs (loOct + slope) < 3.0 && std::abs (hiOct + slope) < 1.0, "Split at 1 kHz, " + std::to_string (slope) + " dB/oct: -6 dB at the split, slope",
               f2 (lo) + " and " + f2 (hi) + " dB; " + f2 (loOct, 1) + " and " + f2 (hiOct, 1) + " dB per octave");
    }

    // 3. Bands are in phase around the split (what makes compressing one band behave)
    {
        const double one[1] { 1000 };
        double worst = 0;
        for (int slope : { 24, 48 }) { auto o = impulse (2, one, slope, 48000, 24000); for (double f : { 500.0, 800.0, 1000.0, 1300.0, 2000.0 }) worst = std::max (worst, std::abs (std::arg (at (o[0], f, 48000) / at (o[1], f, 48000)))); }
        check (worst < 0.01, "Low and high band in phase around the split", f2 (worst * 180 / PI, 4) + " degrees");
    }

    // 4. Turning one band down changes only that region
    {
        for (int slope : { 24, 48 })
        {
            auto o = impulse (4, freqs, slope, 48000, 24000);
            std::vector<double> sum (24000, 0.0);
            const double g = std::pow (10.0, -6 / 20.0);
            for (size_t b = 0; b < 4; ++b) for (size_t i = 0; i < 24000; ++i) sum[i] += o[b][i] * (b == 2 ? g : 1.0);
            const double mid = db (std::abs (at (sum, std::sqrt (600.0 * 2500.0), 48000))), low = db (std::abs (at (sum, 60, 48000))), top = db (std::abs (at (sum, 12000, 48000)));
            const std::string d = f2 (mid) + " dB in the middle, " + f2 (low, 3) + " and " + f2 (top, 3) + " dB outside";
            // at 24 dB/oct the neighbours reach into a two-octave band (theory: about -5.1 dB), so the claim is tested on the steep slope
            if (slope == 24) std::printf ("INFO  One band 6 dB down, 24 dB/oct  (%s)\n", d.c_str());
            else check (std::abs (mid + 6) < 0.1 && std::abs (low) < 0.02 && std::abs (top) < 0.02, "One band 6 dB down, 48 dB/oct: only that region changes", d);
        }
    }

    // 5. Moving the splits while a tone plays: no clicks
    {
        Bank b; double f[4] { 100, 600, 2500, 8000 }; b.set (4, f, 24); b.prepare (48000);
        double prev = 0, worst = 0, t[5];
        for (int i = 0; i < 96000; ++i)
        {
            if (i == 24000) { f[0] = 300; f[1] = 1800; f[2] = 6000; b.set (4, f, 24); }
            if (i == 60000) { f[0] = 60; f[1] = 300; f[2] = 1200; b.set (4, f, 24); }
            b.process (0.5 * std::sin (2 * PI * 440 * i / 48000.0), t);
            const double y = t[0] + t[1] + t[2] + t[3];
            if (i > 2000) worst = std::max (worst, std::abs (y - prev));
            prev = y;
        }
        const double limit = 0.5 * 2 * PI * 440 / 48000 * 1.35;
        check (worst < limit, "Splits moved during playback: no clicks", "largest step " + f2 (worst, 4) + ", limit " + f2 (limit, 4));
    }

    // 6. Splits cannot cross; stable with random settings; CPU
    {
        std::mt19937 rng (5); std::uniform_real_distribution<double> u (0, 1); bool ok = true;
        for (int k = 0; k < 60; ++k)
        {
            Bank b; double f[4]; for (auto& v : f) v = 20 * std::pow (1000.0, u (rng));
            b.set (2 + (int) (3.99 * u (rng)), f, u (rng) > 0.5 ? 48 : 24); b.prepare (44100 + 147900 * u (rng));
            double t[5];
            for (int i = 0; i < 4000; ++i) { if (i % 500 == 0) { for (auto& v : f) v = 20 * std::pow (1000.0, u (rng)); b.set (b.numBands(), f, 24); } b.process (2 * u (rng) - 1, t); for (int q = 0; q < b.numBands(); ++q) ok &= std::isfinite (t[q]) && std::abs (t[q]) < 8; }
        }
        check (ok, "Stable with 60 random setups, splits moving and crossing", "all finite");
        Bank l, r; l.set (5, freqs, 48); r.set (5, freqs, 48); l.prepare (48000); r.prepare (48000);
        double t[5], acc = 0; const auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 480000; ++i) { l.process ((i % 97) * 0.01, t); acc += t[1]; r.process ((i % 89) * 0.01, t); acc += t[2]; }
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 0.5 && std::isfinite (acc), "CPU, stereo, 5 bands, 48 dB/oct", f2 (10 * s, 2) + " % of one core");
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
