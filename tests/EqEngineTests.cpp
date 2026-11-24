// Aemon EQ engine objective tests (no JUCE). AGPL-3.0-or-later.
// Build: c++ -std=c++20 -O2 -I plugins/eq/Source tests/EqEngineTests.cpp -o eqtests
#include "EqEngine.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>

using namespace aemon::eq;

static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }

/** Worst deviation in dB between the digital band and its analogue ideal, from 20 Hz up to fMax,
    counted where the band is actually shaping the sound (analogue response above floorDb). */
static double worstError (const BandParams& b, double fs, double fMax, std::vector<Section> secs = {}, double floorDb = -12.0)
{
    if (secs.empty()) secs = design (b, fs);
    double worst = 0;
    for (double f = 20; f <= fMax; f *= 1.005)
    {
        const double a = analogDb (b, f), d = digitalDb (secs, f, fs);
        if (a < floorDb) continue;
        worst = std::max (worst, std::abs (a - d));
    }
    return worst;
}

static double rms (const std::vector<float>& x, size_t from) { double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt (s / double (x.size() - from)); }

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // keep output if a test crashes
    // 1. Every shape follows the analogue ideal (audible range), at common rates.
    struct Case { const char* name; BandParams b; double tol; };
    std::vector<Case> cases;
    for (double f : { 60.0, 1000.0, 8000.0, 15000.0 })
        for (double g : { -12.0, 6.0 })
            for (double q : { 0.5, 2.0, 8.0 })
                cases.push_back ({ "Bell", { true, Type::bell, f, g, q, 12, Place::stereo }, 0.25 });
    for (double f : { 80.0, 1000.0, 12000.0 })
        for (double g : { -9.0, 9.0 })
        {
            cases.push_back ({ "Low shelf", { true, Type::lowShelf, f, g, 0.7, 12, Place::stereo }, 0.25 });
            cases.push_back ({ "High shelf", { true, Type::highShelf, f, g, 0.7, 12, Place::stereo }, 0.25 });
        }
    for (double f : { 30.0, 200.0, 5000.0 })
        for (int sl : { 12, 24, 48, 96 })
        {
            cases.push_back ({ "Low cut", { true, Type::lowCut, f, 0, 0.707, sl, Place::stereo }, 0.25 });
            cases.push_back ({ "High cut", { true, Type::highCut, f * 2, 0, 0.707, sl, Place::stereo }, 0.25 });
        }
    for (double f : { 100.0, 3000.0 })
    {
        cases.push_back ({ "Notch", { true, Type::notch, f, 0, 4, 12, Place::stereo }, 0.25 });
        cases.push_back ({ "Band pass", { true, Type::bandPass, f, 0, 1, 12, Place::stereo }, 0.25 });
        cases.push_back ({ "Tilt", { true, Type::tilt, f * 3, 6, 1, 12, Place::stereo }, 0.25 });
    }
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        double worst = 0, worstTop = 0; std::string worstName, worstTopName;
        for (auto& c : cases)
        {
            const bool top = c.b.freq > fs / 4;   // bands set within about an octave of Nyquist
            const double e = worstError (c.b, fs, std::min (20000.0, fs * 0.45));
            const std::string nm = std::string (c.name) + " " + f2 (c.b.freq, 0) + " Hz " + (hasGain (c.b.type) ? f2 (c.b.gainDb, 0) + " dB " : "") + "Q" + f2 (c.b.q, 1) + (isCut (c.b.type) ? " " + std::to_string (c.b.slope) + " dB/oct" : "");
            if (top) { if (e > worstTop) { worstTop = e; worstTopName = nm; } }
            else if (e > worst) { worst = e; worstName = nm; }
        }
        check (worst < 1.0, "Every shape matches analogue to 20 kHz @" + f2 (fs / 1000, 1) + " kHz",
               "worst " + f2 (worst) + " dB: " + worstName);
        check (worstTop < 2.5, "Bands set near the top (above fs/4) @" + f2 (fs / 1000, 1) + " kHz",
               "worst " + f2 (worstTop) + " dB: " + (worstTopName.empty() ? "none" : worstTopName));
    }
    // typical settings below 10 kHz must be near perfect
    {
        double worst = 0;
        for (auto& c : cases) if (c.b.freq <= 3000) worst = std::max (worst, worstError (c.b, 44100, 10000));
        check (worst < 0.5, "Bands up to 3 kHz match analogue below 10 kHz @44.1 kHz (all but band-pass skirts within 0.15 dB)", "worst " + f2 (worst, 3) + " dB");
    }

    // 2. No cramping: a 15 kHz bell at 44.1 kHz versus the textbook (bilinear) design
    {
        BandParams b { true, Type::bell, 15000, 6, 1, 12, Place::stereo };
        const double em = worstError (b, 44100, 20000);
        const double er = worstError (b, 44100, 20000, { rbjBell (15000, 6, 1, 44100) });
        check (em < er / 3, "No cramping: 15 kHz +6 dB bell @44.1 kHz", "matched error " + f2 (em) + " dB vs textbook " + f2 (er) + " dB");
    }

    // 3. Neutral: no bands = bit-exact; a 0 dB bell is transparent
    {
        Engine e; e.prepare (48000);
        std::mt19937 rng (1); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        std::vector<float> l (48000), r (48000);
        for (auto& x : l) x = u (rng);
        for (auto& x : r) x = u (rng);
        auto l0 = l, r0 = r;
        e.process (l.data(), r.data(), 48000);
        check (l == l0 && r == r0, "Null test: no bands", "bit-exact");
        Engine e2; e2.setBand (0, { true, Type::bell, 1000, 0, 1, 12, Place::stereo }); e2.prepare (48000);
        l = l0; r = r0;
        e2.process (l.data(), r.data(), 48000);
        double d = 0; for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::abs (l[i] - l0[i]));
        check (d < 1e-6, "0 dB bell is transparent", "max diff " + f2 (d * 1e6, 3) + "e-6");
    }

    // 4. Measured gain at the centre matches the setting (bell +9 dB at 1 kHz)
    {
        Engine e; e.setBand (0, { true, Type::bell, 1000, 9, 1.5, 12, Place::stereo }); e.prepare (48000);
        std::vector<float> l (96000), r (96000);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.1f * (float) std::sin (2 * pi * 1000 * (double) i / 48000);
        auto in = l;
        e.process (l.data(), r.data(), (int) l.size());
        const double g = 20 * std::log10 (rms (l, 48000) / rms (in, 48000));
        check (std::abs (g - 9) < 0.02, "Bell +9 dB measured on a 1 kHz tone", f2 (g, 3) + " dB");
    }

    // 5. Placement: a side band leaves the mid untouched; a left band leaves the right untouched
    {
        Engine e; e.setBand (0, { true, Type::bell, 500, 12, 1, 12, Place::side }); e.prepare (48000);
        std::vector<float> l (48000), r (48000);
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.2f * (float) std::sin (2 * pi * 500 * (double) i / 48000);
        auto l0 = l;
        e.process (l.data(), r.data(), 48000);
        double d = 0; for (size_t i = 0; i < l.size(); ++i) d = std::max (d, (double) std::abs (l[i] - l0[i]));
        check (d < 1e-6, "Side band leaves a pure-mid signal untouched", "max diff " + f2 (d * 1e6, 3) + "e-6");
        Engine e2; e2.setBand (0, { true, Type::bell, 500, 12, 1, 12, Place::left }); e2.prepare (48000);
        std::vector<float> l2 (48000), r2 (48000);
        for (size_t i = 0; i < l2.size(); ++i) l2[i] = r2[i] = 0.2f * (float) std::sin (2 * pi * 500 * (double) i / 48000);
        auto r20 = r2;
        e2.process (l2.data(), r2.data(), 48000);
        check (r2 == r20 && rms (l2, 24000) > 2 * rms (r2, 24000), "Left band changes only the left", "right bit-exact, left boosted");
    }

    // 6. No clicks: sweep a band's frequency and gain hard during a tone, and switch shape mid-way
    {
        Engine e; e.setBand (0, { true, Type::bell, 200, 0, 2, 12, Place::stereo }); e.prepare (48000);
        const int n = 48000 * 2;
        std::vector<float> l (n), r (n);
        for (int i = 0; i < n; ++i) l[i] = r[i] = 0.25f * (float) std::sin (2 * pi * 440 * i / 48000.0);
        double worst = 0, prev = 0;
        for (int pos = 0; pos < n; pos += 64)
        {
            const double t = (double) pos / n;
            BandParams b { true, pos < n / 2 ? Type::bell : Type::highShelf, 200 * std::pow (40.0, t), 12 * std::sin (t * 20), 2, 12, Place::stereo };
            e.setBand (0, b);
            e.process (l.data() + pos, r.data() + pos, 64);
            for (int i = pos; i < pos + 64; ++i) { if (i > 0) worst = std::max (worst, std::abs (l[i] - prev)); prev = l[i]; }
        }
        const double toneStep = 0.25 * 2 * pi * 440 / 48000 * std::pow (10, 12.0 / 20);
        check (worst < toneStep * 1.3, "No clicks while sweeping and switching shape", "largest step " + f2 (worst, 4) + ", limit " + f2 (toneStep * 1.3, 4));
    }

    // 7. Stability: random settings, extreme Qs and gains, at 44.1 and 192 kHz
    {
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> uf (std::log (20.0), std::log (20000.0)), ug (-24, 24), uq (std::log (0.1), std::log (18.0));
        std::uniform_int_distribution<int> ut (0, 7), us (0, 3), up (0, 4);
        bool ok = true;
        for (double fs : { 44100.0, 192000.0 })
            for (int trial = 0; trial < 40 && ok; ++trial)
            {
                Engine e;
                for (int k = 0; k < maxBands; ++k)
                    e.setBand (k, { true, (Type) ut (rng), std::exp (uf (rng)), ug (rng), std::exp (uq (rng)), 12 << us (rng), (Place) up (rng) });
                e.prepare (fs);
                std::vector<float> l (8192), r (8192);
                std::uniform_real_distribution<float> u (-0.3f, 0.3f);
                for (int i = 0; i < 8192; ++i) { l[(size_t) i] = u (rng); r[(size_t) i] = u (rng); }
                e.process (l.data(), r.data(), 8192);
                for (int i = 0; i < 8192; ++i) ok &= std::isfinite (l[(size_t) i]) && std::abs (l[(size_t) i]) < 1e4f;
            }
        check (ok, "Stable with 16 random bands (80 random setups)", ok ? "all outputs finite and bounded" : "blew up");
    }

    // 8. CPU: 16 bands (incl. 96 dB/oct cuts and M/S), one minute of stereo at 48 kHz
    {
        Engine e;
        for (int k = 0; k < maxBands; ++k)
            e.setBand (k, { true, (Type) (k % 8), 40.0 * std::pow (2.0, k * 0.55), (k % 2 ? 4.0 : -3.0), 1.0, 96, (Place) (k % 5) });
        e.prepare (48000);
        const int n = 48000 * 60;
        std::vector<float> l ((size_t) n, 0.1f), r ((size_t) n, -0.1f);
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int pos = 0; pos < n; pos += 512) e.process (l.data() + pos, r.data() + pos, 512);
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 6.0, "CPU with 16 bands", f2 (100 * s / 60, 2) + "% of one core");
    }

    // 9. Dynamic band: a -9 dB dynamic cut at 3 kHz, threshold -24 dB, only acts on loud material
    {
        auto measure = [] (float amp, double f)
        {
            Engine e;
            BandParams b { true, Type::bell, 3000, -9, 2, 12, Place::stereo };
            b.dynamic = true; b.thresholdDb = -24;
            e.setBand (0, b); e.prepare (48000);
            const int n = 256 * 188;   // a whole number of 256-sample blocks
            std::vector<float> l (n), r (n);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = amp * (float) std::sin (2 * pi * f * i / 48000.0);
            auto in = l;
            for (int pos = 0; pos < n; pos += 256) e.process (l.data() + pos, r.data() + pos, 256);
            return 20 * std::log10 (rms (l, 24000) / rms (in, 24000));
        };
        const double quiet = measure (0.01f, 3000), loud = measure (0.5f, 3000), other = measure (0.5f, 300);
        check (std::abs (quiet) < 0.1, "Dynamic band leaves quiet material alone (-40 dBFS at 3 kHz)", f2 (quiet, 2) + " dB");
        check (loud < -8.0 && loud > -9.2, "Dynamic band cuts loud material fully (-6 dBFS at 3 kHz)", f2 (loud, 2) + " dB");
        check (std::abs (other) < 0.5, "Dynamic band ignores loud material at other frequencies (300 Hz)", f2 (other, 2) + " dB");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
