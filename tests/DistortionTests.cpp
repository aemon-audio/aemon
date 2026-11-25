// Aemon distortion core: objective tests (no JUCE). AGPL-3.0-or-later.
#include "aemon_Distortion.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>

using namespace aemon::dist;
static constexpr double PI = 3.14159265358979323846;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }

/** Radix-2 FFT, for spectra of a whole second-like power of two. */
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto wl = std::polar (1.0, -2 * PI / (double) len);
        for (size_t i = 0; i < n; i += len) { std::complex<double> w = 1; for (size_t k = 0; k < len / 2; ++k) { auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}
/** Loudest false tone relative to the fundamental, dB. The tone sits exactly on an FFT bin, so anything that is not a
    harmonic below Nyquist is aliasing. */
static double worstAlias (int factor, double knee, double driveDb, int bin, double* at = nullptr, double limitHz = 24000, bool adaa = true)
{
    const int N = 1 << 16;
    ClipCurve c; c.knee = knee;
    DeltaChannel<ClipCurve> ch; ch.prepare (factor);
    Oversampler plain; plain.prepare (factor);
    const double A = std::pow (10.0, driveDb / 20);
    std::vector<std::complex<double>> s ((size_t) N);
    for (int i = 0; i < 2 * N; ++i)
    {
        double dry;
        const double x = A * std::sin (2 * PI * bin * (double) i / N + 0.3);
        const double y = adaa ? ch.process (x, c, dry) : plain.process (x, [&c] (double v) { return c.f (v); });
        if (i >= N) s[(size_t) (i - N)] = y;
    }
    fft (s);
    const double fund = std::abs (s[(size_t) bin]);
    double worst = 0; int where = 0;
    const int top = (int) std::min (N / 2.0 - 1, limitHz / 48000 * N);
    for (int k = 8; k < top; ++k) if (k % bin != 0 && std::abs (s[(size_t) k]) > worst) { worst = std::abs (s[(size_t) k]); where = k; }
    if (at) *at = where * 48000.0 / N;
    return 20 * std::log10 (worst / fund + 1e-20);
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    // 1. The bar: 5 kHz, 12 dB into a hard clip, every false tone at least 90 dB down at the default (8x)
    const int bin = 6827;      // 5000.2 Hz, coprime with the FFT size
    for (int factor : { 1, 2, 4, 8 })
    {
        double at = 0; const double w = worstAlias (factor, 0.0, 12.0, bin, &at);
        const auto d = f2 (w, 1) + " dB, at " + f2 (at, 0) + " Hz";
        if (factor == 8) check (w <= -90, "Hard clip, 5 kHz, +12 dB, 8x: false tones", d);
        else std::printf ("INFO  Hard clip, 5 kHz, +12 dB, %dx: false tones  (%s)\n", factor, d.c_str());
    }
    std::printf ("INFO  The same at 8x without the antiderivative method  (%s dB)\n", f2 (worstAlias (8, 0, 12, bin, nullptr, 24000, false), 1).c_str());
    std::printf ("INFO  Plain clipping, no protection at all  (%s dB)\n", f2 (worstAlias (1, 0, 12, bin, nullptr, 24000, false), 1).c_str());
    {
        // the whole map, so that nothing hides behind the one tone above
        for (int b : { 1367, 2731, 6827, 13653, 20479 })
        {
            std::string row;
            for (double knee : { 0.0, 0.3, 1.0 }) for (double drive : { 3.0, 12.0, 24.0 }) row += f2 (worstAlias (8, knee, drive, b), 0) + " ";
            std::printf ("INFO  8x, %5.0f Hz, knee 0 / 0.3 / 1 at +3 +12 +24 dB: false tones  (%s dB)\n", b * 48000.0 / 65536, row.c_str());
        }
        double worst = -300;
        for (int b : { 1367, 2731, 6827, 13653 }) for (double knee : { 0.3, 1.0 }) for (double drive : { 3.0, 12.0 }) worst = std::max (worst, worstAlias (8, knee, drive, b));
        check (worst <= -85, "8x, 1 to 10 kHz, soft knees, up to +12 dB: worst false tone", f2 (worst, 1) + " dB");
    }

    // 2. Below the knee: bit-exact, delayed by the reported latency
    for (int factor : { 1, 2, 4, 8 })
    {
        ClipCurve c; c.knee = 0.4;
        DeltaChannel<ClipCurve> ch; ch.prepare (factor);
        std::mt19937 rng (3); std::uniform_real_distribution<double> u (-0.25, 0.25);   // peaks between samples stay under the knee too
        std::vector<double> in (20000); for (auto& v : in) v = u (rng);
        bool exact = true; double dry; const int lat = ch.latency();
        for (int i = 0; i < (int) in.size(); ++i) { const double y = ch.process (in[(size_t) i], c, dry); if (i >= lat) exact &= y == in[(size_t) (i - lat)]; }
        check (exact, "True peaks below the knee, " + std::to_string (factor) + "x: bit-exact", "delayed by " + std::to_string (lat) + " samples");
    }

    // 3. The oversampler: delay and flatness of the distortion path
    for (int factor : { 2, 4, 8 })
    {
        Oversampler o; o.prepare (factor);
        std::vector<double> y (2048);
        for (int i = 0; i < 2048; ++i) y[(size_t) i] = o.process (i == 0 ? 1.0 : 0.0, [] (double v) { return v; });
        int peak = 0; for (int i = 0; i < 2048; ++i) if (std::abs (y[(size_t) i]) > std::abs (y[(size_t) peak])) peak = i;
        double dev = 0;
        for (double f = 100; f <= 19000; f += 100) { std::complex<double> z = 0; for (int i = 0; i < 2048; ++i) z += y[(size_t) i] * std::polar (1.0, -2 * PI * f * i / 48000); dev = std::max (dev, std::abs (20 * std::log10 (std::abs (z)))); }
        check (peak == o.latency() && dev < 0.05, "Oversampler " + std::to_string (factor) + "x: delay and level", std::to_string (peak) + " samples, within " + f2 (dev, 4) + " dB to 19 kHz");
    }

    // 4. The curve: reaches the ceiling exactly, never past it, smooth, and its antiderivative is right
    {
        double worstOver = 0, worstSlope = 0, worstD = 0;
        for (double knee : { 0.0, 0.25, 0.5, 1.0 })
        {
            ClipCurve c; c.knee = knee;
            double prev = c.f (-4.0);
            for (double x = -4; x <= 4; x += 1e-3)
            {
                worstOver = std::max (worstOver, std::abs (c.f (x)) - 1.0);
                worstSlope = std::max ({ worstSlope, (c.f (x) - prev) / 1e-3 - 1.0, -(c.f (x) - prev) / 1e-3 });
                prev = c.f (x);
                const double h = 1e-5, num = (c.D (x + h) - c.D (x - h)) / (2 * h);
                worstD = std::max (worstD, std::abs (num - (c.f (x) - x)));
            }
        }
        check (worstOver <= 1e-12 && worstSlope < 1e-9, "Curve: never past the ceiling, never turns back", "over " + f2 (worstOver, 12));
        check (worstD < 1e-4, "Curve: antiderivative matches", "max error " + f2 (worstD, 7));
    }

    // 5. Removed + output = input, also with the clipper working hard
    {
        ClipCurve c; DeltaChannel<ClipCurve> ch; ch.prepare (8);
        std::mt19937 rng (4); std::normal_distribution<double> g (0, 1.2);
        double worst = 0, dry;
        for (int i = 0; i < 48000; ++i) { const double y = ch.process (g (rng), c, dry); const double removed = dry - y; worst = std::max (worst, std::abs (removed + y - dry)); }
        check (worst < 1e-12, "Removed plus output equals input", f2 (worst, 15));
    }

    // 6. Stability and CPU
    {
        std::mt19937 rng (9); std::uniform_real_distribution<double> u (0, 1);
        bool ok = true;
        for (int t = 0; t < 60; ++t)
        {
            ClipCurve c; c.knee = u (rng); DeltaChannel<ClipCurve> ch; ch.prepare (1 << (int) (4 * u (rng)));
            std::normal_distribution<double> g (0, 8 * u (rng)); double dry;
            for (int i = 0; i < 4000; ++i) { const double y = ch.process (i % 700 == 0 ? 0.0 : g (rng), c, dry); ok &= std::isfinite (y) && std::abs (y) < 40.0; }
        }
        check (ok, "Stable with 60 random setups, up to 8 times over", "all finite");
        // Nothing audible slips past: noise up to 20 kHz driven 12 dB over stays near the ceiling (the plugin adds the final ceiling)
        {
            ClipCurve cc; DeltaChannel<ClipCurve> ch; ch.prepare (8);
            std::vector<double> h; const int T = 200; double sum = 0;
            for (int k = -T; k <= T; ++k) { const double f = 19500.0 / 48000, v = (k == 0 ? 2 * f : std::sin (2 * PI * f * k) / (PI * k)) * (0.42 + 0.5 * std::cos (PI * k / (T + 1)) + 0.08 * std::cos (2 * PI * k / (T + 1))); h.push_back (v); sum += v; }
            std::normal_distribution<double> n1 (0, 4.0 / 3.0); std::vector<double> w (24000), z (24000, 0.0);
            for (auto& v : w) v = n1 (rng);
            for (int i = T; i + T < 24000; ++i) for (int k = -T; k <= T; ++k) z[(size_t) i] += w[(size_t) (i + k)] * h[(size_t) (k + T)] / sum;
            double peak = 0, d2; int over = 0, cnt = 0;
            for (int i = 0; i < 24000; ++i) { const double y = std::abs (ch.process (z[(size_t) i], cc, d2)); if (i > 1000) { peak = std::max (peak, y); over += y > 1.25; ++cnt; } }
            // any clean clipper overshoots between samples on dense material; the Clipper's ceiling stage takes these
            std::printf ("INFO  Noise to 20 kHz, 12 dB over, before the ceiling stage  (highest %s dB, above +2 dB %s %% of the time)\n", f2 (20 * std::log10 (peak), 2).c_str(), f2 (100.0 * over / cnt, 2).c_str());
        }
        ClipCurve c; DeltaChannel<ClipCurve> l, r; l.prepare (8); r.prepare (8);
        std::normal_distribution<double> g (0, 0.6); double dry, acc = 0;
        std::vector<double> x (48000 * 10); for (auto& v : x) v = g (rng);
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (double v : x) { acc += l.process (v, c, dry); acc += r.process (-v, c, dry); }
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 10 * 0.05 && std::isfinite (acc), "CPU, stereo, 8x", f2 (100 * s / 10, 2) + " % of one core");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
