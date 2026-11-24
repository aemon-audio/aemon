// Aemon Compressor engine objective tests (no JUCE). AGPL-3.0-or-later.
#include "CompEngine.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <string>

using namespace aemon::comp;
static constexpr double PI = 3.14159265358979323846;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    if (! ok) ++failures;
}
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static double rmsDb (const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return 10 * std::log10 (s / double (b - a) + 1e-30); }
static void run (Engine& e, std::vector<float>& l, std::vector<float>& r, int block = 256, const std::vector<float>* sc = nullptr)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, l.size() - pos);
        e.process (l.data() + pos, r.data() + pos, n, sc ? sc->data() + pos : nullptr, sc ? sc->data() + pos : nullptr);
    }
}
static std::vector<float> sine (double f, double ampDb, int n, double fs = 48000)
{
    std::vector<float> v ((size_t) n);
    const double a = std::pow (10.0, ampDb / 20.0);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (a * std::sin (2 * PI * f * i / fs));
    return v;
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000;

    // 1. Static curve: ratio 4, threshold -20, hard knee. Sine peak level in -> peak level out.
    {
        double worst = 0; std::string at;
        for (double in : { -40.0, -24.0, -20.0, -16.0, -10.0, -4.0 })
        {
            Params p; p.thresholdDb = -20; p.ratio = 4; p.kneeDb = 0; p.attackMs = 5; p.releaseMs = 300;
            Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
            auto l = sine (1000, in, 96000), r = l;
            run (e, l, r);
            const double outPeakDb = rmsDb (l, 48000, 96000) + 3.0103;   // sine: peak = rms + 3 dB
            const double ideal = in + gainComputer (in, -20, 4, 0);
            if (std::abs (outPeakDb - ideal) > worst) { worst = std::abs (outPeakDb - ideal); at = f2 (in, 0) + " dB in"; }
        }
        check (worst < 0.2, "Static curve: ratio 4:1 at -20 dB matches the ideal", "worst " + f2 (worst) + " dB at " + at);
    }
    // 2. Infinite ratio holds the output at the threshold
    {
        Params p; p.thresholdDb = -12; p.ratio = 20; p.kneeDb = 0; p.attackMs = 1; p.releaseMs = 300;
        Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
        auto l = sine (1000, -2, 96000), r = l;
        run (e, l, r);
        const double out = rmsDb (l, 48000, 96000) + 3.0103;
        check (std::abs (out + 12) < 0.4, "Ratio infinity holds peaks at the threshold (-12 dB)", f2 (out) + " dB");
    }
    // 3. Attack and release times: step -40 -> -10 dBFS and back, threshold -30, ratio 10
    {
        Params p; p.thresholdDb = -30; p.ratio = 10; p.kneeDb = 0; p.attackMs = 10; p.releaseMs = 100;
        Engine e; e.setParams (p); e.prepare (fs, 64); e.setParams (p);
        const int n = 48000;
        std::vector<float> l ((size_t) n, 0.01f), r;   // DC steps make the detector exact
        for (int i = 9600; i < 28800; ++i) l[(size_t) i] = (float) std::pow (10.0, -10.0 / 20.0);
        for (int i = 0; i < 9600; ++i) l[(size_t) i] = (float) std::pow (10.0, -40.0 / 20.0);
        for (int i = 28800; i < n; ++i) l[(size_t) i] = (float) std::pow (10.0, -40.0 / 20.0);
        r = l;
        std::vector<double> gr ((size_t) n);
        for (int i = 0; i < n; ++i) { e.process (&l[(size_t) i], &r[(size_t) i], 1); gr[(size_t) i] = e.currentGainReductionDb(); }
        const double finalGr = gr[28799];
        int ta = 9600; while (ta < 28800 && gr[(size_t) ta] > 0.632 * finalGr) ++ta;
        int tr = 28800; while (tr < n && gr[(size_t) tr] < 0.368 * finalGr) ++tr;
        const double atkMs = (ta - 9600) * 1000.0 / fs, relMs = (tr - 28800) * 1000.0 / fs;
        check (std::abs (atkMs - 10) < 1.5, "Attack time (10 ms setting, 63% point)", f2 (atkMs) + " ms");
        check (std::abs (relMs - 105) < 10, "Release time (100 ms setting + 5 ms peak hold, 63% point)", f2 (relMs) + " ms");
    }
    // 4. Neutral settings are bit-exact
    {
        Params p; p.ratio = 1.0;
        Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
        std::mt19937 rng (1); std::uniform_real_distribution<float> u (-0.9f, 0.9f);
        std::vector<float> l (48000), r (48000);
        for (auto& x : l) x = u (rng);
        for (auto& x : r) x = u (rng);
        auto l0 = l, r0 = r;
        run (e, l, r);
        check (l == l0 && r == r0, "Null test: ratio 1:1", "bit-exact");
    }
    // 5. Mix 0% gives the dry signal (and delay-aligned with lookahead)
    {
        Params p; p.thresholdDb = -40; p.ratio = 8; p.mix = 0.0; p.lookaheadMs = 5;
        Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
        auto l = sine (440, -6, 48000), r = l, l0 = l;
        run (e, l, r);
        const int lat = e.latencySamples();
        double d = 0; for (size_t i = (size_t) lat; i < l.size(); ++i) d = std::max (d, (double) std::abs (l[i] - l0[i - (size_t) lat]));
        check (d < 1e-6 && lat == 240, "Mix 0% = dry, delayed by the reported lookahead (5 ms = 240 samples)", "max diff " + f2 (d * 1e6, 3) + "e-6, latency " + std::to_string (lat));
    }
    // 6. Lookahead catches a sudden peak that a zero-lookahead compressor lets through
    {
        auto overshoot = [&] (double la)
        {
            Params p; p.thresholdDb = -20; p.ratio = 20; p.kneeDb = 0; p.attackMs = 1; p.releaseMs = 200; p.lookaheadMs = la;
            Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
            std::vector<float> l (24000, 0.0f);
            for (int i = 12000; i < 24000; ++i) l[(size_t) i] = (float) (0.9 * std::sin (2 * PI * 1000 * i / fs));
            auto r = l;
            run (e, l, r);
            float pk = 0; for (int i = 12000; i < 24000; ++i) pk = std::max (pk, std::abs (l[(size_t) i]));
            return 20 * std::log10 (pk) - (-20.0);
        };
        const double o0 = overshoot (0), o5 = overshoot (5);
        check (o5 < o0 - 3, "Lookahead 5 ms catches the first peak", "overshoot " + f2 (o0, 1) + " dB without, " + f2 (o5, 1) + " dB with");
    }
    // 7. Side-chain high-pass: a loud 50 Hz tone stops driving the compressor
    {
        auto grWith = [&] (double hpf)
        {
            Params p; p.thresholdDb = -30; p.ratio = 4; p.scHighPassHz = hpf;
            Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
            auto l = sine (50, -6, 96000), r = l, l0 = l;
            run (e, l, r);
            return rmsDb (l, 48000, 96000) - rmsDb (l0, 48000, 96000);
        };
        const double off = grWith (20), on = grWith (200);
        check (on > off + 10, "Side-chain high-pass at 200 Hz ignores a 50 Hz tone", f2 (off, 1) + " dB reduction off, " + f2 (on, 1) + " dB with it on");
    }
    // 8. External side-chain: a silent key does nothing, a loud key ducks the music
    {
        Params p; p.thresholdDb = -30; p.ratio = 8; p.externalSidechain = true; p.attackMs = 1; p.releaseMs = 100;
        Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
        auto music = sine (220, -6, 96000), r = music, m0 = music;
        std::vector<float> key (96000, 0.0f);
        for (int i = 48000; i < 96000; ++i) key[(size_t) i] = (float) (0.5 * std::sin (2 * PI * 60 * i / fs));
        run (e, music, r, 256, &key);
        const double quiet = rmsDb (music, 24000, 48000) - rmsDb (m0, 24000, 48000), keyed = rmsDb (music, 72000, 96000) - rmsDb (m0, 72000, 96000);
        check (std::abs (quiet) < 0.01 && keyed < -15, "External side-chain ducks only when the key plays", f2 (quiet, 2) + " dB without key, " + f2 (keyed, 1) + " dB with key");
    }
    // 9. Auto level matches output loudness to the input
    {
        Params p; p.thresholdDb = -30; p.ratio = 4; p.autoLevel = true; p.attackMs = 10; p.releaseMs = 150;
        Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
        std::vector<float> l (48000 * 20), r;
        std::mt19937 rng (3); std::normal_distribution<float> nd (0.0f, 0.12f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = nd (rng) * (float) (0.6 + 0.4 * std::sin (2 * PI * 2 * (double) i / fs));
        r = l; auto l0 = l;
        run (e, l, r);
        const double d = rmsDb (l, 48000 * 15, l.size()) - rmsDb (l0, 48000 * 15, l.size());
        check (std::abs (d) < 1.0, "Auto level keeps the loudness the same (after settling)", f2 (d, 2) + " dB difference, compressing about " + f2 (-e.currentGainReductionDb(), 1) + " dB");
    }
    // 10. Distortion: 1 kHz sine, 6 dB above threshold, typical and fast settings (THD via DFT)
    {
        auto thd = [&] (double atkMs, double relMs, double f)
        {
            Params p; p.thresholdDb = -20; p.ratio = 4; p.kneeDb = 0; p.attackMs = atkMs; p.releaseMs = relMs;
            Engine e; e.setParams (p); e.prepare (fs, 256); e.setParams (p);
            auto l = sine (f, -14, 96000), r = l;
            run (e, l, r);
            auto bin = [&] (double fr) { std::complex<double> s = 0; for (int i = 48000; i < 96000; ++i) s += (double) l[(size_t) i] * std::polar (1.0, -2 * PI * fr * i / fs); return std::abs (s); };
            const double f1 = bin (f);
            double h = 0; for (int k = 2; k <= 8 && k * f < fs / 2; ++k) h += std::pow (bin (k * f), 2);
            return 100.0 * std::sqrt (h) / f1;
        };
        const double t1 = thd (10, 150, 1000), t2 = thd (1, 50, 1000), t3 = thd (1, 20, 100);
        check (t1 < 0.05, "Distortion, 1 kHz, 10 ms / 150 ms (typical)", f2 (t1, 4) + " % THD");
        check (t2 < 0.5, "Distortion, 1 kHz, 1 ms / 50 ms (fast)", f2 (t2, 3) + " % THD");
        std::printf ("INFO  Distortion, 100 Hz, 1 ms / 20 ms (very fast, expected to colour bass)  (%s %% THD)\n", f2 (t3, 2).c_str());
    }
    // 11. No clicks when changing threshold, ratio and bypass while playing
    {
        Params p; p.thresholdDb = -10; p.ratio = 2; p.attackMs = 5; p.releaseMs = 100;
        Engine e; e.setParams (p); e.prepare (fs, 64); e.setParams (p);
        auto l = sine (440, -6, 64 * 1500), r = l;
        double worst = 0; float prev = 0;
        for (size_t pos = 0; pos < l.size(); pos += 64)
        {
            if (pos == 64 * 500) { p.thresholdDb = -40; p.ratio = 20; e.setParams (p); }
            if (pos == 64 * 1000) { p.bypass = true; e.setParams (p); }
            e.process (l.data() + pos, r.data() + pos, 64);
            for (size_t i = pos; i < pos + 64; ++i) { if (i) worst = std::max (worst, (double) std::abs (l[i] - prev)); prev = l[i]; }
        }
        const double limit = std::pow (10.0, -6 / 20.0) * 2 * PI * 440 / fs * 1.3;
        check (worst < limit, "No clicks when threshold, ratio and bypass change", "largest step " + f2 (worst, 4) + ", limit " + f2 (limit, 4));
    }
    // 12. Stability with random settings, and CPU
    {
        std::mt19937 rng (9);
        std::uniform_real_distribution<double> u (0, 1);
        bool ok = true;
        for (int t = 0; t < 60 && ok; ++t)
        {
            Params p; p.thresholdDb = -60 * u (rng); p.ratio = 1 + 19 * u (rng); p.kneeDb = 24 * u (rng); p.attackMs = 0.05 + 100 * u (rng);
            p.releaseMs = 10 + 990 * u (rng); p.autoRelease = u (rng) > 0.5; p.lookaheadMs = 10 * u (rng); p.rmsDetector = u (rng) > 0.5;
            p.scHighPassHz = 20 + 480 * u (rng); p.mix = u (rng); p.outputDb = -24 + 48 * u (rng); p.autoLevel = u (rng) > 0.5;
            Engine e; e.setParams (p); e.prepare (fs, 512); e.setParams (p);
            std::vector<float> l (8192), r (8192);
            for (int i = 0; i < 8192; ++i) { l[(size_t) i] = (float) (u (rng) * 2 - 1); r[(size_t) i] = (float) (u (rng) * 2 - 1); }
            run (e, l, r, 512);
            for (float x : l) ok &= std::isfinite (x) && std::abs (x) < 100.0f;
        }
        check (ok, "Stable with 60 random setups", ok ? "all outputs finite" : "blew up");
        Params p; p.thresholdDb = -24; p.ratio = 4; p.autoRelease = true; p.lookaheadMs = 3; p.rmsDetector = true; p.scHighPassHz = 100; p.mix = 0.7; p.autoLevel = true;
        Engine e; e.setParams (p); e.prepare (fs, 512); e.setParams (p);
        std::vector<float> l (48000 * 60), r;
        for (size_t i = 0; i < l.size(); ++i) l[i] = (float) (0.3 * std::sin ((double) i * 0.05));
        r = l;
        const auto t0 = std::chrono::high_resolution_clock::now();
        run (e, l, r, 512);
        const double s = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (s < 6.0, "CPU (all features on)", f2 (100 * s / 60, 2) + "% of one core");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
