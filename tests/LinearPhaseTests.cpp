// Aemon EQ linear-phase tests (no JUCE). AGPL-3.0-or-later.
#include "LinearPhase.h"
#include <chrono>
#include <cstdio>
#include <string>

using namespace aemon::eq;

static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }

static void run (LinearPhaseEngine& e, std::vector<float>& l, std::vector<float>& r, int block = 512)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, l.size() - pos);
        e.process (l.data() + pos, r.data() + pos, n);
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // keep output if a test crashes
    const double fs = 48000;
    std::array<BandParams, maxBands> flat {};

    // 1. Flat: output is the input, delayed by exactly the reported latency
    {
        LinearPhaseEngine e; e.prepare (fs); e.setKernels (buildKernels (flat, 0, fs));
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i) { l[i] = (float) std::sin (i * 0.01) * 0.5f; r[i] = (float) std::cos (i * 0.013) * 0.4f; }
        auto l0 = l, r0 = r;
        run (e, l, r);
        const int lat = e.latency();
        double d = 0;
        for (size_t i = (size_t) lat; i < l.size(); ++i) d = std::max ({ d, (double) std::abs (l[i] - l0[i - (size_t) lat]), (double) std::abs (r[i] - r0[i - (size_t) lat]) });
        check (d < 1e-4, "Flat curve = clean delay of " + std::to_string (lat) + " samples (" + f2 (1000.0 * lat / fs, 1) + " ms)", "max diff " + f2 (d * 1e6, 1) + "e-6");
    }

    // 2. Magnitude follows the analogue ideal (sine sweep of single tones)
    {
        std::array<BandParams, maxBands> bands {};
        bands[0] = { true, Type::bell, 15000, 6, 1, 12, Place::stereo };
        bands[1] = { true, Type::lowCut, 60, 0, 0.707, 48, Place::stereo };
        bands[2] = { true, Type::lowShelf, 200, -4, 0.7, 12, Place::stereo };
        double worst = 0, worstF = 0;
        for (double f : { 50.0, 80.0, 120.0, 250.0, 1000.0, 4000.0, 10000.0, 15000.0, 19000.0 })
        {
            LinearPhaseEngine e; e.prepare (fs); e.setKernels (buildKernels (bands, 0, fs));
            std::vector<float> l (48000), r (48000);
            for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.25f * (float) std::sin (2 * pi * f * (double) i / fs);
            run (e, l, r);
            double si = 0, so = 0;
            for (size_t i = 24000; i < 48000; ++i) { const double x = 0.25 * std::sin (2 * pi * f * (double) i / fs); si += x * x; so += (double) l[i] * l[i]; }
            const double meas = 10 * std::log10 (so / si);
            double ideal = 0; for (auto& b : bands) if (b.on) ideal += analogDb (b, f);
            if (std::abs (meas - ideal) > worst) { worst = std::abs (meas - ideal); worstF = f; }
        }
        check (worst < 0.5, "Linear phase (High) follows the analogue curve 50 Hz - 19 kHz (48 dB/oct cut at 60 Hz, shelf, 15 kHz bell)", "worst " + f2 (worst, 3) + " dB at " + f2 (worstF, 0) + " Hz");
    }

    // 2b. Stop-band depth of a steep cut, and Medium resolution figures (informational)
    {
        std::array<BandParams, maxBands> b {};
        b[0] = { true, Type::lowCut, 60, 0, 0.707, 48, Place::stereo };
        for (auto res : { Resolution::high, Resolution::medium })
        {
            LinearPhaseEngine e; e.prepare (fs, res); e.setKernels (buildKernels (b, 0, fs, res));
            std::vector<float> l (96000), r (96000);
            for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.5f * (float) std::sin (2 * pi * 20 * (double) i / fs);
            run (e, l, r);
            double so = 0; for (size_t i = 48000; i < 96000; ++i) so += (double) l[i] * l[i];
            const double meas = 10 * std::log10 (so / 48000 / 0.125);
            const bool high = res == Resolution::high;
            check (high ? meas < -70 : meas < -60, std::string ("Steep cut depth at 20 Hz, ") + (high ? "High" : "Medium") + " resolution (" + f2 (1000.0 * e.latency() / fs, 0) + " ms delay)",
                   f2 (meas, 1) + " dB, analogue " + f2 (analogDb (b[0], 20), 1) + " dB");
        }
    }

    // 3. Phase is linear: the impulse response is symmetric around the latency point
    {
        std::array<BandParams, maxBands> bands {};
        bands[0] = { true, Type::bell, 2000, 9, 2, 12, Place::stereo };
        LinearPhaseEngine e; e.prepare (fs); e.setKernels (buildKernels (bands, 0, fs));
        std::vector<float> l (40000, 0.0f), r (40000, 0.0f);
        l[0] = r[0] = 1.0f;
        run (e, l, r);
        const int c = e.latency();
        double asym = 0, peak = 0;
        for (int k = 1; k < 3000; ++k) { asym = std::max (asym, (double) std::abs (l[(size_t) (c + k)] - l[(size_t) (c - k)])); peak = std::max (peak, (double) std::abs (l[(size_t) (c + k)])); }
        check (asym < 1e-6 && std::abs (l[(size_t) c]) > 0.5, "Impulse response symmetric (no phase shift)", "max asymmetry " + f2 (asym * 1e6, 3) + "e-6");
    }

    // 4. Mid/side: a side band leaves a pure-mid signal untouched (apart from the delay)
    {
        std::array<BandParams, maxBands> bands {};
        bands[0] = { true, Type::bell, 500, 12, 1, 12, Place::side };
        LinearPhaseEngine e; e.prepare (fs); e.setKernels (buildKernels (bands, 0, fs));
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.3f * (float) std::sin (2 * pi * 500 * (double) i / fs);
        auto l0 = l;
        run (e, l, r);
        const int lat = e.latency();
        double d = 0;
        for (size_t i = (size_t) lat + 2000; i < l.size(); ++i) d = std::max (d, (double) std::abs (l[i] - l0[i - (size_t) lat]));
        check (d < 1e-4, "Side band leaves the mid untouched", "max diff " + f2 (d * 1e6, 1) + "e-6");
    }

    // 5. Changing the curve while playing does not click
    {
        LinearPhaseEngine e; e.prepare (fs); e.setKernels (buildKernels (flat, 0, fs));
        std::vector<float> l (512 * 188), r (512 * 188);   // a whole number of 512-sample blocks
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.25f * (float) std::sin (2 * pi * 440 * (double) i / fs);
        double worst = 0; float prev = 0;
        for (size_t pos = 0; pos < l.size(); pos += 512)
        {
            if (pos % 4096 == 0)
            {
                std::array<BandParams, maxBands> b {};
                b[0] = { true, Type::bell, 440, (pos / 4096) % 2 ? 12.0 : -12.0, 1, 12, Place::stereo };
                e.setKernels (buildKernels (b, 0, fs));
            }
            e.process (l.data() + pos, r.data() + pos, 512);
            e.collectGarbage();
            for (size_t i = pos; i < pos + 512; ++i) { if (i > (size_t) e.latency()) worst = std::max (worst, (double) std::abs (l[i] - prev)); prev = l[i]; }
        }
        const double limit = 0.25 * std::pow (10, 12 / 20.0) * 2 * pi * 440 / fs * 1.3;
        check (worst < limit, "No clicks when the curve changes (+12/-12 dB every 85 ms)", "largest step " + f2 (worst, 4) + ", limit " + f2 (limit, 4));
    }

    // 6. CPU, with mid/side bands (all four kernels active)
    {
        std::array<BandParams, maxBands> bands {};
        for (int k = 0; k < maxBands; ++k) bands[(size_t) k] = { true, (Type) (k % 8), 40.0 * std::pow (2.0, k * 0.55), 3, 1, 24, (Place) (k % 5) };
        LinearPhaseEngine e; e.prepare (fs);
        const auto t0 = std::chrono::high_resolution_clock::now();
        e.setKernels (buildKernels (bands, 0, fs));
        const double build = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        std::vector<float> l (480000, 0.1f), r (480000, -0.1f);
        const auto t1 = std::chrono::high_resolution_clock::now();
        run (e, l, r);
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t1).count();
        check (s / 10.0 < 0.15, "CPU (linear phase, 16 bands incl. mid/side)", f2 (100 * s / 10.0, 2) + "% of one core; rebuilding the curve takes " + f2 (build * 1000, 1) + " ms");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
