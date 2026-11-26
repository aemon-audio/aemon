// Checks of the perceptual kit itself: known values, and sounds made to be bad that it must catch. AGPL-3.0-or-later.
#include "PerceptKit.h"
#include "VoiceKit.h"
#include "DrumKit.h"
#include <cstdio>
#include <random>

using namespace percept;
static int failures = 0;
static void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
static std::string f2 (double x, int d = 2) { char b[64]; std::snprintf (b, sizeof b, "%.*f", d, x); return b; }
static Vec noise (int n, double amp, int seed) { std::mt19937 g ((unsigned) seed); std::normal_distribution<double> d (0, amp); Vec v ((size_t) n); for (auto& x : v) x = (float) d (g); return v; }
static Vec sine (int n, double hz, double amp, double fs) { Vec v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * pi * hz * i / fs)); return v; }
static Vec plus (Vec a, const Vec& b, double g = 1) { for (size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += (float) (g * b[i]); return a; }
static Vec band (const Vec& x, double lo, double hi, double fs) { return filtered (filtered (x, true, lo, fs), false, hi, fs); }
static Vec level (Vec x, double want) { const double g = std::pow (10.0, (want - rmsDb (x)) / 20); for (auto& v : x) v = (float) (v * g); return x; }
static Vec tail (double t60, double secs, int seed, double fs) { auto v = noise ((int) (secs * fs), 1, seed); for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (v[i] * 0.2 * std::pow (10.0, -3 * (double) i / fs / t60)); return v; }

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const double fs = 48000; const int n = 96000;

    // hearing: known values
    check (std::abs (quietDb (1000) - 3.4) < 1 && std::abs (quietDb (100) - 23) < 2 && quietDb (3300) < -3 && quietDb (12000) > 15, "Threshold of hearing at 100, 1000, 3300, 12000 Hz", f2 (quietDb (100), 1) + ", " + f2 (quietDb (1000), 1) + ", " + f2 (quietDb (3300), 1) + ", " + f2 (quietDb (12000), 1) + " dB");
    {
        const Ear ear (fs, 90); const auto f = ear.frame (sine (n, 1000, 1, fs), 20000); double t = 0; for (double v : f.level) t += std::pow (10.0, v / 10); check (std::abs (db10 (t) - 90) < 1, "A full-scale sine reads 90 dB at 90", f2 (db10 (t), 2));
    }
    // masking: a tone in noise disappears around 4 to 6 dB under it, noise in a tone only 20 to 30 dB under it
    {
        Vec many ((size_t) n, 0.0f); std::mt19937 g (3); for (int k = 0; k < 160; ++k) { const double hz = 920 + k, ph = (double) (g() % 6283) / 1000; for (int i = 0; i < n; ++i) many[(size_t) i] += (float) std::sin (2 * pi * hz * i / fs + ph); }      // noise without skirts
        const auto nb = level (many, -30); const auto tn = sine (n, 1000, 1, fs);
        const double at3 = audible (nb, level (tn, -30 + 3), fs).worst, at15 = audible (nb, level (tn, -30 - 20), fs).worst;
        const auto tone = level (tn, -30); const double n15 = audible (tone, level (nb, -30 - 12), fs).worst, n32 = audible (tone, level (nb, -30 - 34), fs).worst;
        check (at3 > 0 && at15 < 0, "A tone in noise: heard 3 dB over it, not 20 dB under it (ears: about 5 dB under; the kit errs on the strict side)", f2 (at3, 1) + ", " + f2 (at15, 1) + " dB"); check (n15 > 0 && n32 < 0, "Noise in a tone: heard 12 dB under it, not 34 dB under it", f2 (n15, 1) + ", " + f2 (n32, 1) + " dB");
        const auto up = audible (level (tn, -25), level (sine (n, 1400, 1, fs), -50), fs).worst, down = audible (level (tn, -25), level (sine (n, 700, 1, fs), -50), fs).worst; check (up < down, "Masking spreads upwards more than downwards", f2 (up, 1) + " over, " + f2 (down, 1) + " under");
        const auto quiet = audible (Vec ((size_t) n, 0.0f), level (tn, -95), fs, 90).worst, louder = audible (Vec ((size_t) n, 0.0f), level (tn, -60), fs, 90).worst; check (quiet < 0 && louder > 10, "Alone: -95 dB is under the threshold of hearing, -60 is not", f2 (quiet, 1) + ", " + f2 (louder, 1));
    }
    {
        const auto sh = colourOf (level (band (noise (n, 1, 4), 920, 1080, fs), -30), fs, 90).sharpness, hi = colourOf (level (band (noise (n, 1, 5), 7000, 8000, fs), -30), fs, 90).sharpness, wh = colourOf (level (noise (n, 1, 6), -30), fs, 90).sharpness;
        check (std::abs (sh - 1) < 0.2 && hi > 2.2 && wh > sh && wh < hi, "Sharpness: about 1 acum at 1 kHz, more for 7 kHz, between for white noise", f2 (sh) + ", " + f2 (hi) + ", " + f2 (wh));
        const double l1 = colourOf (level (sine (n, 1000, 1, fs), -30), fs, 90).loudness, l2 = colourOf (level (sine (n, 1000, 1, fs), -20), fs, 90).loudness; check (l2 / l1 > 1.6 && l2 / l1 < 2.4, "10 dB more is about twice as loud", f2 (l2 / l1));
        auto am = sine (n, 1000, 0.1, fs), slow = am, pure = am; for (int i = 0; i < n; ++i) { am[(size_t) i] *= (float) (1 + std::sin (2 * pi * 70 * i / fs)); slow[(size_t) i] *= (float) (1 + std::sin (2 * pi * 4 * i / fs)); } check (std::abs (roughness (am, fs) - 1) < 0.25 && roughness (slow, fs) < 0.1 && roughness (pure, fs) < 0.02, "Roughness: 1 at 70 Hz modulation, nothing at 4 Hz or steady", f2 (roughness (am, fs)) + ", " + f2 (roughness (slow, fs)) + ", " + f2 (roughness (pure, fs)));
    }
    // words
    {
        const auto v = voicekit::takes (fs, {}, { 0 }).x; const auto nz = noise ((int) v.size(), 1, 7); const double r = rmsDb (v); std::string list; double last = 2; bool falls = true, ok = true; for (double snr : { 30.0, 10.0, 0.0, -10.0, -20.0 }) { const double w = words (v, plus (v, level (nz, r - snr)), fs); list += f2 (w) + " "; falls &= w < last; last = w; if (snr == 30) ok &= w > 0.95; if (snr == -20) ok &= w < 0.45; }
        const double same = words (v, v, fs), louder = words (v, level (v, r + 12), fs); check (same > 0.999 && louder > 0.999 && falls && ok, "Words: 1 untouched and louder, less and less in noise", "same " + f2 (same, 3) + ", noise 30 to -20 dB: " + list);
        // a drowning reverb
        const auto ir = tail (4, 2, 8, fs); Vec wet (v.size(), 0.0f); for (size_t k = 0; k < ir.size(); k += 37) for (size_t i = k; i < v.size(); ++i) wet[i] += v[i - k] * ir[k]; const double in = words (v, plus (v, level (wet, r + 6)), fs), little = words (v, plus (v, level (wet, r - 20)), fs); check (in < 0.6 && little > 0.88, "Bad example, a voice drowned in reverb: caught", f2 (in) + " drowned, " + f2 (little) + " with a little");
        const auto muff = filtered (v, false, 700, fs); check (words (v, plus (muff, noise ((int) v.size(), 0.003, 3)), fs) < 0.93, "Bad example, a voice with its consonants gone", f2 (words (v, plus (muff, noise ((int) v.size(), 0.003, 3)), fs)));
    }
    // tone
    {
        const auto x = noise (n * 2, 0.1, 9); auto y = filtered (x, false, 3000, fs); const auto a = tone (x, fs), b = tone (y, fs); check (rangeChange (a, b, 6000, 12000) < -20 && std::abs (rangeChange (a, b, 200, 1000)) < 0.5, "Tone: a low-pass is measured as one", f2 (rangeChange (a, b, 6000, 12000), 1) + " dB up high, " + f2 (rangeChange (a, b, 200, 1000)) + " below");
    }
    // things that must be caught
    {
        auto x = sine (n, 110, 0.3, fs); auto z = x; for (int i = n / 2 + 100; i < n; ++i) z[(size_t) i] *= 0.9f; const double clean = clickDb (x, fs); check (clickDb (z, fs) > clean + 20 && clean < 12, "Bad example, a bypass that clicks: caught", f2 (clean, 1) + " dB clean, " + f2 (clickDb (z, fs), 1) + " clicking"); const auto vv = voicekit::sing (fs).x; check (clickDb (vv, fs) < 32, "... and a voice has no clicks", f2 (clickDb (vv, fs), 1));
        // an aliasing clipper: the same clip at 1x against 16x
        auto clip = [&] (int os) { Vec o ((size_t) n); if (os == 1) { for (int i = 0; i < n; ++i) o[(size_t) i] = (float) std::clamp (1.5 * std::sin (2 * pi * 5100 * i / fs), -0.4, 0.4); return o; } Vec up ((size_t) (n * os)); for (int i = 0; i < n * os; ++i) up[(size_t) i] = (float) std::clamp (1.5 * std::sin (2 * pi * 5100 * i / (fs * os)), -0.4, 0.4); return resample (up, fs * os, fs); };
        const auto good = clip (8), bad = clip (1); Vec diff = plus (bad, good, -1); Vec mid (diff.begin() + 4800, diff.end() - 4800), ref (good.begin() + 4800, good.end() - 4800); const auto a = audible (ref, mid, fs); check (a.worst > 10, "Bad example, an aliasing clipper: heard", f2 (a.worst, 1) + " dB over what the tone hides, at " + f2 (a.atHz, 0) + " Hz"); Vec tiny = mid; for (auto& q : tiny) q *= 0.0003f; check (audible (ref, tiny, fs).worst < 0, "... the same 70 dB lower is not", f2 (audible (ref, tiny, fs).worst, 1));
        // a lisp: S turned into something dull
        const auto voice = voicekit::sing (fs); auto lisp = voice.x; { const auto dull = filtered (voice.x, false, 4000, fs); for (auto& s : voice.sounds) if (s.kind == 's' || s.kind == 'S') for (int i = s.from; i < s.to; ++i) lisp[(size_t) i] = dull[(size_t) i]; } const auto ta = tone (voice.x, fs), tb = tone (lisp, fs); check (rangeChange (ta, tb, 5000, 12000) < -8 && std::abs (rangeChange (ta, tb, 200, 2000)) < 0.5, "Bad example, a lispy de-esser: caught", f2 (rangeChange (ta, tb, 5000, 12000), 1) + " dB in the S range");
    }
    // reverb: known values
    for (double t : { 0.4, 1.5, 5.0 }) { const auto d = decayOf (tail (t, t * 1.3, 11, fs), fs); check (std::abs (d.t30 / t - 1) < 0.05 && std::abs (d.edt / t - 1) < 0.1, "Decay of " + f2 (t, 1) + " s", "T30 " + f2 (d.t30) + ", early " + f2 (d.edt)); }
    {
        const auto good = tail (1.5, 2, 12, fs); Vec sparse ((size_t) (2 * fs), 0.0f); std::mt19937 g (4); for (int k = 0; k < 240; ++k) { const int p = (int) (g() % (unsigned) sparse.size()); sparse[(size_t) p] = (float) ((k % 2 ? 1 : -1) * 0.2 * std::pow (10.0, -3 * p / fs / 1.5)); }
        check (std::abs (echoDensity (good, fs, 0.2) - 1) < 0.15 && echoDensity (sparse, fs, 0.2) < 0.3 && diffuseFrom (good, fs, 1) < 0.03 && diffuseFrom (sparse, fs, 1) > 0.9, "Echo density: 1 for a diffuse tail, low for single echoes", f2 (echoDensity (good, fs, 0.2)) + " and " + f2 (echoDensity (sparse, fs, 0.2)));
        // a metallic comb reverb: four feedback combs
        Vec metal ((size_t) (2 * fs), 0.0f); for (int d : { 1117, 1321, 1481, 1617 }) { Vec line ((size_t) d, 0.0f); int p = 0; const float fb = (float) std::pow (10.0, -3 * d / fs / 1.5); for (size_t i = 0; i < metal.size(); ++i) { const float o = line[(size_t) p]; line[(size_t) p] = (i == 0 ? 1.0f : 0.0f) + fb * o; p = (p + 1) % d; metal[i] += o * 0.25f; } }
        Vec oneMode = good; for (size_t i = 0; i < oneMode.size(); ++i) oneMode[i] += (float) (0.06 * std::sin (2 * pi * 1234 * (double) i / fs) * std::pow (10.0, -3 * (double) i / fs / 1.5)); double hz = 0; const double rg = ringing (good, fs, 0.1, 1.2), rm = ringing (metal, fs, 0.1, 1.2), ro = ringing (oneMode, fs, 0.1, 1.2, &hz);
        check (rg < 13 && rm > rg + 4 && ro > rg + 6 && std::abs (hz - 1234) < 3, "Bad example, a metallic comb reverb and a tail with one ringing mode: caught", f2 (rg, 1) + " dB diffuse, " + f2 (rm, 1) + " comb, " + f2 (ro, 1) + " at " + f2 (hz, 0) + " Hz");
        auto flut = good; for (size_t i = 1440; i < flut.size(); ++i) flut[i] += 0.8f * flut[i - 1440]; const double fg = flutter (good, fs, 0.1, 1), ff = flutter (flut, fs, 0.1, 1); check (fg < 0.15 && ff > 0.35, "Bad example, flutter every 30 ms: caught", f2 (fg) + " clean, " + f2 (ff));
        const auto r = tail (1.5, 2, 13, fs); Vec anti = good; for (auto& v : anti) v = -v; check (alike (good, r, fs, 0.05) < 0.2 && alike (good, good, fs, 0.05) > 0.99 && std::abs (monoLoss (good, r)) < 0.5 && monoLoss (good, anti) < -40 && std::abs (monoLoss (good, good) - 3.01) < 0.05, "Width and what mono loses: unrelated, the same, and out of phase", f2 (alike (good, r, fs, 0.05)) + ", " + f2 (monoLoss (good, r)) + ", " + f2 (monoLoss (good, anti), 0) + " dB");
        check (std::abs (clarity (good, fs, 50) - db10 (std::pow (10.0, 6 * 0.05 / 1.5) - 1)) < 0.5, "Clarity of an even decay is the textbook value", f2 (clarity (good, fs, 50)) + " and " + f2 (db10 (std::pow (10.0, 6 * 0.05 / 1.5) - 1)) + " dB");
        Vec w ((size_t) (4 * fs)); double ph = 0; for (size_t i = 0; i < w.size(); ++i) { ph += 2 * pi * 500 * std::pow (2.0, 20 * std::sin (2 * pi * 0.8 * (double) i / fs) / 1200) / fs; w[i] = (float) (0.3 * std::sin (ph)); } check (std::abs (wobbleCents (w, fs, 500) - 20) < 2 && wobbleCents (sine (4 * 48000, 500, 0.3, fs), fs, 500) < 0.1, "Pitch wobble of 20 cents", f2 (wobbleCents (w, fs, 500), 1));
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
