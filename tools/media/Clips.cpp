// Aemon website clips: original music generated in code, processed by the real Aemon engines.
// AGPL-3.0-or-later. Run: AemonClips <out dir>. Writes 32-bit float stereo WAV files at 48 kHz.
// Each file is one seamless loop with 0.25 s of the same loop before and after it, so a player can loop
// an exact inner region whatever padding a lossy encoder adds.
#include "ClipEngine.h"
#include "CompEngine.h"
#include "EqEngine.h"
#include "ImagerEngine.h"
#include "LimEngine.h"
#include "MbEngine.h"
#include "SatEngine.h"
#include "UtilityEngine.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr double fs = 48000.0, pi = 3.14159265358979323846;
constexpr double bpm = 88.0;
constexpr int bars = 4;
const double beat = 60.0 / bpm;
const int loopLen = (int) std::lround (bars * 4 * beat * fs);
constexpr int passes = 3;                  // the middle pass is the seamless one

struct Buf
{
    std::vector<float> l, r;
    Buf() : l ((size_t) (loopLen * passes), 0.0f), r (l) {}
    void add (int i, double a, double b) { if (i >= 0 && i < (int) l.size()) { l[(size_t) i] += (float) a; r[(size_t) i] += (float) b; } }
    void mix (const Buf& o, double g) { for (size_t i = 0; i < l.size(); ++i) { l[i] += (float) (o.l[i] * g); r[i] += (float) (o.r[i] * g); } }
};

std::mt19937 rng (20261124);
double noise() { static std::uniform_real_distribution<double> d (-1.0, 1.0); return d (rng); }
double hz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

struct OnePole { double z = 0; double lp (double x, double f) { z += (1.0 - std::exp (-2 * pi * f / fs)) * (x - z); return z; } double hp (double x, double f) { return x - lp (x, f); } };
struct Svf
{
    double a = 0, b = 0;
    void run (double x, double f, double q, double& lo, double& bp, double& hi)
    {
        const double g = std::tan (pi * std::min (f, 20000.0) / fs), k = 1 / q, a1 = 1 / (1 + g * (g + k));
        const double v1 = a1 * (a + g * (x - b)), v2 = b + g * v1;
        a = 2 * v1 - a; b = 2 * v2 - b; lo = v2; bp = v1; hi = x - k * v1 - v2;
    }
};

/** Sixteenth-note position with a light swing, in samples. */
int at (int pass, int bar, int six, double nudgeMs = 0)
{
    const double swing = (six % 2) ? 0.16 * beat / 4 : 0.0;
    return pass * loopLen + (int) std::lround (((bar * 4 + six / 4.0) * beat + swing + nudgeMs * 0.001) * fs);
}

void kick (Buf& b, int s, double v)
{
    double ph = 0; OnePole hp;
    for (int i = 0; i < (int) (0.42 * fs); ++i)
    {
        const double t = i / fs, f = 47 + 125 * std::exp (-t * 42) + 22 * std::exp (-t * 9);
        ph += 2 * pi * f / fs;
        double x = std::sin (ph) * std::exp (-t * 6.2) * 1.25;
        x += hp.hp (noise(), 2500) * std::exp (-t * 260) * 0.35;
        x = std::tanh (x * 1.4) * v * std::min (1.0, (0.42 - t) / 0.02);
        b.add (s + i, x, x);
    }
}
void snare (Buf& b, Buf& send, int s, double v)
{
    Svf f; OnePole hp;
    for (int i = 0; i < (int) (0.34 * fs); ++i)
    {
        const double t = i / fs; double lo, bp, hi;
        f.run (noise(), 4200, 0.6, lo, bp, hi);
        double x = 0.55 * std::sin (2 * pi * (188 * t + 1.1 * (1 - std::exp (-t * 60)))) * std::exp (-t * 19)
                 + 0.28 * std::sin (2 * pi * 331 * t) * std::exp (-t * 26)
                 + 0.62 * bp * std::exp (-t * 15) + 0.30 * hp.hp (noise(), 5000) * std::exp (-t * 42);
        x = std::tanh (x * 1.3) * v * std::min (1.0, (0.34 - t) / 0.03);
        b.add (s + i, x * 0.98, x * 1.02); send.add (s + i, x, x);
    }
}
void hat (Buf& b, int s, double v, bool open, double pan)
{
    Svf f; const double d = open ? 11 : 62; const int n = (int) ((open ? 0.3 : 0.09) * fs);
    static const double part[6] { 4012, 5230, 6120, 7410, 8290, 9940 };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs; double lo, bp, hi, m = 0;
        for (double p : part) m += std::sin (2 * pi * p * t) > 0 ? 1 : -1;
        f.run (0.7 * noise() + 0.09 * m, 8200, 0.8, lo, bp, hi);
        const double x = hi * std::exp (-t * d) * v * std::min (1.0, (n - i) / 400.0);
        b.add (s + i, x * (1 - pan), x * (1 + pan));
    }
}
void shaker (Buf& b, int s, double v)
{
    Svf f; const int n = (int) (0.07 * fs);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs; double lo, bp, hi; f.run (noise(), 6400, 1.6, lo, bp, hi);
        const double x = bp * std::sin (pi * std::min (1.0, t / 0.07)) * v;
        b.add (s + i, x * 1.25, x * 0.75);
    }
}
void bassNote (Buf& b, int s, int note, double len, double v)
{
    Svf f; double p1 = 0, p2 = 0; const double w = hz (note);
    const int n = (int) (len * fs);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs; p1 = std::fmod (p1 + w / fs, 1.0); p2 += 2 * pi * w / fs;
        double lo, bp, hi; f.run (2 * p1 - 1, 260 + 900 * std::exp (-t * 9), 0.9, lo, bp, hi);
        const double env = std::min (1.0, t / 0.006) * (0.72 + 0.28 * std::exp (-t * 6)) * std::min (1.0, (len - t) / 0.03);
        const double x = std::tanh ((0.62 * std::sin (p2) + 0.5 * lo) * 1.3) * env * v;
        b.add (s + i, x, x);
    }
}
/** A long, almost pure sub note that drops into pitch: an 808. */
void subNote (Buf& b, int s, int note, double len, double v)
{
    double ph = 0; const int n = (int) (len * fs);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs; ph += 2 * pi * hz (note) * (1 + 0.9 * std::exp (-t * 38)) / fs;
        const double x = std::sin (ph) * std::min (1.0, t / 0.003) * std::exp (-t * 1.1) * std::min (1.0, (len - t) / 0.04) * v;
        b.add (s + i, x, x);
    }
}
void keyNote (Buf& b, Buf& send, int s, int note, double len, double v, double pan)
{
    const double w = 2 * pi * hz (note); const int n = (int) ((len + 0.5) * fs);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, rel = t > len ? std::exp (-(t - len) * 9) : 1.0;
        const double body = std::sin (w * t + (5.0 * std::exp (-t * 2.6) + 1.0) * v * std::sin (w * t + 0.2));
        const double tine = std::sin (w * t + 0.5 * std::exp (-t * 24) * std::sin (14.0 * w * t)) * std::exp (-t * 7);
        const double env = std::min (1.0, t / 0.004) * (0.35 * std::exp (-t * 4.5) + 0.65 * std::exp (-t * 0.9)) * rel;
        const double trem = 0.22 * std::sin (2 * pi * (s + i) / (double) loopLen * 48.0);   // whole cycles per loop
        const double x = (0.8 * body + 0.62 * tine) * env * v;
        b.add (s + i, x * (1 - pan + trem), x * (1 + pan - trem)); send.add (s + i, x, x);
    }
}
void padNote (Buf& b, Buf& send, int s, int note, double len, double v)
{
    double ph[6] {}; Svf fl, fr; const int n = (int) ((len + 0.5) * fs);
    for (int k = 0; k < 6; ++k) ph[k] = 0.17 * k;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs; double l = 0, r = 0, lo, bp, hi;
        for (int k = 0; k < 6; ++k)
        {
            const double det = std::pow (2.0, (k - 2.5) * 5.0 / 1200.0);
            ph[k] = std::fmod (ph[k] + hz (note) * det / fs, 1.0);
            (k % 2 ? l : r) += 2 * ph[k] - 1;
        }
        const double env = std::min (1.0, t / 0.35) * (t > len ? std::max (0.0, 1 - (t - len) / 0.5) : 1.0) * v;
        fl.run (l, 3600, 0.7, lo, bp, hi); l = lo; fr.run (r, 3600, 0.7, lo, bp, hi); r = lo;
        b.add (s + i, l * env, r * env); send.add (s + i, l * env, r * env);
    }
}
void pluck (Buf& b, Buf& send, int s, int note, double v, double pan)
{
    Svf f; double p = 0; const int n = (int) (0.6 * fs);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs; p = std::fmod (p + hz (note) / fs, 1.0); double lo, bp, hi;
        f.run ((p < 0.35 ? 1 : -1), 1600 + 8000 * std::exp (-t * 16), 1.1, lo, bp, hi);
        const double x = lo * std::exp (-t * 7.5) * std::min (1.0, t / 0.002) * v * std::min (1.0, (0.6 - t) / 0.05);
        b.add (s + i, x * (1 - pan), x * (1 + pan)); send.add (s + i, x, x);
        for (int d = 1; d <= 3; ++d)   // dotted-eighth echoes, alternating sides
        {
            const double g = 0.34 / d, sp = d % 2 ? 0.75 : -0.75;
            b.add (s + i + (int) (d * 0.75 * beat * fs), x * g * (1 - sp), x * g * (1 + sp));
        }
    }
}
/** Schroeder reverb (parallel combs into allpasses), stereo spread. */
Buf reverb (const Buf& in, double decay, double damp, double predelayMs)
{
    static const int comb[8] { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 }, ap[4] { 225, 556, 441, 341 };
    Buf out; const int pre = (int) (predelayMs * 0.001 * fs);
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<std::vector<double>> c, a; std::vector<double> lp (8, 0.0);
        for (int d : comb) c.emplace_back ((size_t) (d * fs / 44100 + ch * 23), 0.0);
        for (int d : ap) a.emplace_back ((size_t) (d * fs / 44100 + ch * 23), 0.0);
        auto& o = ch ? out.r : out.l; const auto& x = ch ? in.r : in.l; const auto& y = ch ? in.l : in.r;
        std::vector<size_t> ci (8, 0), ai (4, 0);
        for (size_t i = 0; i < o.size(); ++i)
        {
            const double s = i >= (size_t) pre ? (0.8 * x[i - (size_t) pre] + 0.2 * y[i - (size_t) pre]) * 0.03 : 0.0;
            double v = 0;
            for (size_t k = 0; k < 8; ++k)
            {
                const double z = c[k][ci[k]]; v += z;
                lp[k] = z * (1 - damp) + lp[k] * damp;
                c[k][ci[k]] = s + lp[k] * decay; ci[k] = (ci[k] + 1) % c[k].size();
            }
            for (size_t k = 0; k < 4; ++k)
            {
                const double z = a[k][ai[k]]; const double w = -v + z;
                a[k][ai[k]] = v + z * 0.5; ai[k] = (ai[k] + 1) % a[k].size(); v = w;
            }
            o[i] = (float) v;
        }
    }
    return out;
}

using Bd = aemon::eq::BandParams;
using Ty = aemon::eq::Type;
Bd band (Ty t, double f, double g, double q, int slope = 12)
{
    Bd b; b.on = true; b.type = t; b.freq = f; b.gainDb = g; b.q = q; b.slope = slope; return b;
}
void eq (Buf& b, const std::vector<Bd>& bands)
{
    aemon::eq::Engine e;
    for (size_t i = 0; i < bands.size(); ++i) e.setBand ((int) i, bands[i]);
    e.prepare (fs);
    e.process (b.l.data(), b.r.data(), (int) b.l.size());
}
void compress (Buf& b, aemon::comp::Params p)
{
    aemon::comp::Engine e; e.prepare (fs, 512); e.setParams (p); e.reset();
    for (size_t i = 0; i < b.l.size(); i += 512) e.process (b.l.data() + i, b.r.data() + i, (int) std::min<size_t> (512, b.l.size() - i));
}
void limit (Buf& b, aemon::lim::Params p)
{
    aemon::lim::Engine e; e.setParams (p); e.prepare (fs);
    for (size_t i = 0; i < b.l.size(); i += 512) e.process (b.l.data() + i, b.r.data() + i, (int) std::min<size_t> (512, b.l.size() - i));
    const size_t lat = (size_t) e.latencySamples();   // line the output up with the input again
    for (auto* v : { &b.l, &b.r }) { v->erase (v->begin(), v->begin() + (long) lat); v->resize (v->size() + lat, 0.0f); }
}
/** Runs an engine over the buffer and lines the output up with the input again. */
template <typename E> void run (Buf& b, E& e)
{
    for (size_t i = 0; i < b.l.size(); i += 512) e.process (b.l.data() + i, b.r.data() + i, (int) std::min<size_t> (512, b.l.size() - i));
    const size_t lat = (size_t) e.latencySamples();
    for (auto* v : { &b.l, &b.r }) { v->erase (v->begin(), v->begin() + (long) lat); v->resize (v->size() + lat, 0.0f); }
}
void clip (Buf& b, const aemon::clip::Params& p) { aemon::clip::Engine e; e.setParams (p); e.prepare (fs); run (b, e); }
void saturate (Buf& b, const aemon::sat::Params& p) { aemon::sat::Engine e; e.setParams (p); e.prepare (fs); run (b, e); }
void image (Buf& b, const aemon::img::Params& p) { aemon::img::Engine e; e.setParams (p); e.prepare (fs); run (b, e); }
void multiband (Buf& b, const aemon::mb::Params& p) { aemon::mb::Engine e; e.setParams (p); e.prepare (fs); run (b, e); }
/** The threshold that takes `want` dB off the loudest tenth of band k. */
double aimBand (const Buf& in, aemon::mb::Params p, int k, double want)
{
    double lo = -60, hi = 0;
    for (int it = 0; it < 10; ++it)
    {
        p.band[(size_t) k].threshold = (lo + hi) / 2;
        Buf b = in; aemon::mb::Engine e; e.setParams (p); e.prepare (fs); std::vector<float> g;
        for (size_t i = 0; i < b.l.size(); i += 768) { e.process (b.l.data() + i, b.r.data() + i, (int) std::min<size_t> (768, b.l.size() - i)); g.push_back (-e.takeFrame().gain[(size_t) k]); }
        std::sort (g.begin(), g.end());
        (g[(size_t) (0.9 * (double) g.size())] > want ? lo : hi) = (lo + hi) / 2;
    }
    return std::round ((lo + hi) * 5) / 10;
}
void utility (Buf& b, const aemon::utility::Params& p)
{
    aemon::utility::Engine e; e.prepare (fs); e.setParams (p); e.snapToTargets();
    for (size_t i = 0; i < b.l.size(); i += 512) e.process (b.l.data() + i, b.r.data() + i, (int) std::min<size_t> (512, b.l.size() - i));
}
/** A rough room: boxy low mids and a dull top, the way an untreated recording sounds. */
void roughen (Buf& b, double boxDb, double dullDb)
{
    eq (b, { band (Ty::bell, 310, boxDb, 0.9), band (Ty::highShelf, 7000, dullDb, 0.6) });
}

void write (const std::string& path, const Buf& b)
{
    const int pre = (int) (0.25 * fs), n = loopLen + 2 * pre, first = loopLen - pre;
    std::vector<float> d; d.reserve ((size_t) n * 2);
    float pk = 0;
    for (int i = 0; i < n; ++i) { const float l = b.l[(size_t) (first + i)], r = b.r[(size_t) (first + i)]; d.push_back (l); d.push_back (r); pk = std::max ({ pk, std::abs (l), std::abs (r) }); }
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::printf ("cannot write %s\n", path.c_str()); std::exit (1); }
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const uint32_t bytes = (uint32_t) d.size() * 4;
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f); u32 (16); u16 (3); u16 (2);
    u32 ((uint32_t) fs); u32 ((uint32_t) fs * 8); u16 (8); u16 (32); std::fwrite ("data", 1, 4, f); u32 (bytes);
    std::fwrite (d.data(), 4, d.size(), f); std::fclose (f);
    std::printf ("%s  peak %.2f dBFS\n", path.c_str(), 20 * std::log10 (pk + 1e-9));
}
} // namespace

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : ".";
    Buf drums, dsend, bass, keys, ksend, pad, psend, lead, lsend;

    // F minor 9, D flat major 9, A flat major 7, C7 altered (leads back to F minor)
    const std::vector<std::vector<int>> chords { { 56, 60, 63, 67 }, { 53, 56, 60, 63 }, { 55, 60, 63, 70 }, { 52, 56, 58, 61 } };
    const int roots[4] { 29, 37, 32, 36 };
    const std::vector<std::pair<int, int>> tune[4] {          // sixteenth, note
        { { 2, 72 }, { 5, 75 }, { 8, 79 }, { 11, 77 }, { 14, 75 } }, { { 3, 72 }, { 6, 75 }, { 10, 70 }, { 13, 68 } },
        { { 2, 70 }, { 5, 72 }, { 8, 75 }, { 12, 79 } }, { { 0, 77 }, { 3, 73 }, { 6, 70 }, { 10, 68 }, { 13, 64 } } };

    for (int p = 0; p < passes; ++p)
        for (int bar = 0; bar < bars; ++bar)
        {
            for (int s : { 0, 10 }) kick (drums, at (p, bar, s), 0.72);
            if (bar % 2) { kick (drums, at (p, bar, 7), 0.55); } else kick (drums, at (p, bar, 3), 0.42);
            if (bar == 3) kick (drums, at (p, bar, 14), 0.58);
            for (int s : { 4, 12 }) snare (drums, dsend, at (p, bar, s, 4), 1.0);
            for (int s : { 7, 9 }) snare (drums, dsend, at (p, bar, s), 0.17);
            if (bar == 3) { snare (drums, dsend, at (p, bar, 15), 0.42); }
            for (int s = 0; s < 16; s += 2) hat (drums, at (p, bar, s), (s % 4 ? 0.80 : 0.55), false, 0.22);
            for (int s : { 3, 11 }) hat (drums, at (p, bar, s), 0.30, false, 0.22);
            hat (drums, at (p, bar, 14), 0.52, true, 0.22);
            for (int s = 0; s < 16; ++s) shaker (drums, at (p, bar, s, 2), s % 4 == 2 ? 0.42 : 0.18);

            const int r = roots[bar];
            bassNote (bass, at (p, bar, 0), r, 0.50 * beat * 2, 0.62);
            bassNote (bass, at (p, bar, 6), r + (bar == 3 ? 0 : 12), 0.42 * beat, 0.42);
            bassNote (bass, at (p, bar, 10), r, 0.8 * beat, 0.58);
            bassNote (bass, at (p, bar, 14), bar == 3 ? r + 4 : r + 7, 0.4 * beat, 0.45);

            int k = 0;
            for (int n : chords[(size_t) bar])
            {
                const double pan = (k - 1.5) * 0.16;
                keyNote (keys, ksend, at (p, bar, 0, 6.0 * k), n, 1.35 * beat, 0.30, pan);
                keyNote (keys, ksend, at (p, bar, 7, 4.0 * k), n, 0.45 * beat, 0.20, pan);
                keyNote (keys, ksend, at (p, bar, 10, 5.0 * k), n, 1.2 * beat, 0.26, pan);
                padNote (pad, psend, at (p, bar, 0), n + (k == 0 ? 12 : 0), 4 * beat - 0.2, 0.034);
                ++k;
            }
            for (auto& t : tune[bar]) pluck (lead, lsend, at (p, bar, t.first), t.second, 0.12, 0.12);
        }

    const Buf room = reverb (dsend, 0.72, 0.35, 8), hall = reverb (psend, 0.86, 0.3, 24), plate = reverb (ksend, 0.80, 0.4, 14), tail = reverb (lsend, 0.87, 0.3, 30);

    // Keys as recorded: boxy and dull
    Buf keysRaw; keysRaw.mix (keys, 1.0); keysRaw.mix (plate, 1.1); roughen (keysRaw, 5.0, -3.0);
    Buf bassRaw; bassRaw.mix (bass, 1.0);
    Buf drumsRaw; drumsRaw.mix (drums, 1.0); drumsRaw.mix (room, 1.0);
    Buf padRaw; padRaw.mix (pad, 1.0); padRaw.mix (hall, 1.6);
    Buf leadRaw; leadRaw.mix (lead, 1.0); leadRaw.mix (tail, 1.2);

    // 1. EQ on the keys
    {
        Buf a; a.mix (keysRaw, 0.26); a.mix (padRaw, 0.5); a.mix (leadRaw, 0.5); a.mix (bassRaw, 0.5); roughen (a, 4.0, -5.0); write (out + "/eq-before.wav", a);
        eq (a, { band (Ty::lowCut, 30, 0, 0.71), band (Ty::bell, 310, -6.5, 0.9), band (Ty::bell, 2800, 2.0, 0.9), band (Ty::highShelf, 7000, 6.5, 0.6) });
        write (out + "/eq-after.wav", a);
    }
    // 2. Compressor on the drum bus: preset "Drum bus: more punch" settings (4:1, 30 ms attack, 90 Hz side-chain high-pass)
    {
        Buf a; a.mix (drumsRaw, 0.46); write (out + "/comp-before.wav", a);
        aemon::comp::Params p; p.thresholdDb = -24; p.ratio = 4; p.kneeDb = 4; p.attackMs = 30; p.releaseMs = 120; p.autoRelease = false; p.scHighPassHz = 90; p.mix = 1.0;
        compress (a, p); write (out + "/comp-after.wav", a);
    }
    // 3. Utility: a pad with one side upside down, heard in mono (as on a phone), then fixed
    {
        Buf bed; bed.mix (bassRaw, 0.62); bed.mix (drumsRaw, 0.34);
        Buf bad; { Buf k = keys; k.mix (plate, 0.6); bad.mix (k, 0.42); } bad.mix (lead, 0.9);
        for (auto& v : bad.r) v = -v;
        Buf fixed = bad;
        aemon::utility::Params fix; fix.polR = true; utility (fixed, fix);
        aemon::utility::Params mono; mono.mono = true;
        bad.mix (bed, 1.0); fixed.mix (bed, 1.0);
        utility (bad, mono); utility (fixed, mono);
        write (out + "/utility-before.wav", bad); write (out + "/utility-after.wav", fixed);
    }
    // The rough mix
    Buf mixRaw; mixRaw.mix (drumsRaw, 0.32); mixRaw.mix (bassRaw, 0.58); mixRaw.mix (keysRaw, 0.20); mixRaw.mix (padRaw, 0.50); mixRaw.mix (leadRaw, 0.55); roughen (mixRaw, 1.5, -3.0);
    // 4. Limiter on a clean mix: 9 dB louder
    {
        Buf a; a.mix (drumsRaw, 0.34); a.mix (bassRaw, 0.58); a.mix (padRaw, 0.50); a.mix (leadRaw, 0.60);
        Buf k; k.mix (keysRaw, 0.20); eq (k, { band (Ty::lowCut, 90, 0, 0.71), band (Ty::bell, 310, -5.0, 0.9), band (Ty::highShelf, 8000, 4.0, 0.65) }); a.mix (k, 1.0);
        write (out + "/limiter-before.wav", a);
        aemon::lim::Params p; p.pushDb = 9; p.ceilingDb = -1; p.releaseMs = 80; p.autoRelease = true; p.lookaheadMs = 1.5; p.oversampling = 8;
        limit (a, p); write (out + "/limiter-after.wav", a);
    }
    // 5. Clipper on the drum bus: preset "Drum bus: more level, same punch" (6 dB push, 15 % softness)
    {
        Buf a; a.mix (drumsRaw, 0.46); write (out + "/clipper-before.wav", a);
        aemon::clip::Params p; p.pushDb = 6; p.softness = 0.15; p.ceilingDb = -1; clip (a, p); write (out + "/clipper-after.wav", a);
    }
    // 6. Saturation on an 808, both heard as on a phone: preset "808: heard on phones" (thick, lows, 13 dB)
    {
        Buf d; d.mix (drumsRaw, 0.16); d.mix (leadRaw, 0.30); Buf a;
        const int low[4] { 29, 25, 32, 24 };
        for (int q = 0; q < passes; ++q) for (int bar = 0; bar < bars; ++bar) { subNote (a, at (q, bar, 0), low[bar] + 12, 1.4 * beat, 0.6); subNote (a, at (q, bar, 6), low[bar] + 12, 0.9 * beat, 0.5); subNote (a, at (q, bar, 10), low[bar] + 12, 1.4 * beat, 0.6); }
        Buf b = a;
        aemon::sat::Params p; p.driveDb = 13; p.character = aemon::sat::Character::thick; p.focus = aemon::sat::Focus::lows; saturate (b, p);
        for (auto* x : { &a, &b }) { x->mix (d, 1.0); eq (*x, { band (Ty::lowCut, 250, 0, 0.71, 24), band (Ty::highCut, 9000, 0, 0.71, 12) }); }
        write (out + "/saturation-before.wav", a); write (out + "/saturation-after.wav", b);
    }
    // 7. Imager: keys, pad and lead that were printed in mono, opened up. The bass and drums are not processed.
    {
        Buf music; music.mix (keysRaw, 0.22); music.mix (padRaw, 0.5); music.mix (leadRaw, 0.6);
        aemon::utility::Params mono; mono.mono = true; utility (music, mono);
        Buf bed; bed.mix (bassRaw, 0.5); bed.mix (drumsRaw, 0.26);
        Buf a = music, b = music; aemon::img::Params p; p.make = 0.60; p.width = 1.3; p.monoHz = 200; p.safe = true; p.limitDb = 5; image (b, p);
        a.mix (bed, 1.0); b.mix (bed, 1.0); write (out + "/imager-before.wav", a); write (out + "/imager-after.wav", b);
        for (auto* x : { &a, &b }) utility (*x, mono);
        double worst = 0; for (size_t i = (size_t) loopLen; i < 2 * (size_t) loopLen; ++i) worst = std::max (worst, (double) std::abs (a.l[i] - b.l[i]));
        std::printf ("imager: the mono sums differ by %.1f dBFS at most\n", 20 * std::log10 (worst + 1e-12));
    }
    // 8. Multiband on a boomy mix: goal "tighter lows" (about 5 dB off the loud parts of the lows)
    {
        Buf a; a.mix (drumsRaw, 0.36); a.mix (bassRaw, 0.74); a.mix (keysRaw, 0.16); a.mix (padRaw, 0.5); a.mix (leadRaw, 0.55); eq (a, { band (Ty::bell, 75, 3.5, 0.8) });
        write (out + "/multiband-before.wav", a);
        aemon::mb::Params p; p.bands = 4; p.split = { 120, 800, 5000, 10000 };
        p.band[0].ratio = 4; p.band[0].attackMs = 25; p.band[0].releaseMs = 120; p.band[1].ratio = 2; p.band[1].attackMs = 25; p.band[1].releaseMs = 150;
        p.band[0].threshold = aimBand (a, p, 0, 5.0); p.band[1].threshold = aimBand (a, p, 1, 1.5);
        std::printf ("multiband: thresholds %.1f and %.1f dB\n", p.band[0].threshold, p.band[1].threshold);
        multiband (a, p); write (out + "/multiband-after.wav", a);
    }
    // 9. Seven of the nine on the rough mix, in a row
    {
        Buf a = mixRaw; write (out + "/suite-before.wav", a);
        eq (a, { band (Ty::lowCut, 28, 0, 0.71, 24), band (Ty::bell, 300, -4.0, 0.85), band (Ty::lowShelf, 75, 1.5, 0.7), band (Ty::bell, 3200, 1.5, 0.8), band (Ty::highShelf, 9000, 4.5, 0.65) });
        aemon::comp::Params c; c.thresholdDb = -22; c.ratio = 2; c.kneeDb = 8; c.attackMs = 25; c.releaseMs = 150; c.autoRelease = true; c.scHighPassHz = 80; c.outputDb = 2.5;
        compress (a, c);
        aemon::sat::Params s; s.driveDb = 5; saturate (a, s);                                                       // preset "Mix bus: warmth you miss when it is gone"
        aemon::img::Params w; w.band = { 0.5, 1.0, 1.12, 1.35 }; w.monoHz = 100; image (a, w);                       // preset "Mix bus: open the top"
        aemon::clip::Params k; k.pushDb = 4; k.softness = 0.25; k.ceilingDb = -1; clip (a, k);                       // preset "Master: 2 to 3 dB off the peaks, unheard"
        aemon::lim::Params p; p.pushDb = 4; p.ceilingDb = -1; p.oversampling = 8;
        limit (a, p); write (out + "/suite-after.wav", a);
    }
    if (argc > 2)   // solo stems at mix level, for checking the balance
    {
        for (auto& s : { std::pair<const char*, std::pair<const Buf*, double>> { "drums", { &drumsRaw, 0.32 } }, { "bass", { &bassRaw, 0.58 } }, { "keys", { &keysRaw, 0.20 } }, { "pad", { &padRaw, 0.46 } }, { "lead", { &leadRaw, 0.50 } } })
        { Buf b; b.mix (*s.second.first, s.second.second); write (out + "/stem-" + s.first + ".wav", b); }
    }
    std::printf ("loop %d samples = %.6f s\n", loopLen, loopLen / fs);
    return 0;
}
