// Aemon Reverb: six spaces from one feedback network, words kept clear. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::rev
{
enum class Space { room, plate, hall, chamber, lofi, endless };
constexpr int numSpaces = 6;

struct Params
{
    Space space = Space::plate;
    double size = 0.5;              // 0..1
    double decay = 1.8;             // seconds for 60 dB, in the middle
    double lowDecay = 1.0, highDecay = 0.5;     // times decay, below 250 Hz and above 4 kHz
    double preMs = 20;
    double lowCut = 120, highCut = 12000;       // of what goes in
    double early = 0.5;             // 0 only tail, 1 only early reflections
    double mod = 0.4;               // 0..1
    double width = 1.0;             // 0..1.5
    double duck = 0, duckMs = 250;  // dB the wet steps back while the dry plays, and how long it takes to come back
    double gateMs = 0;              // 0: off. The wet is cut this long after the dry stops
    double drive = 0;               // 0..1, lo-fi grit on the tail
    double mix = 0.25;              // 0..1
    bool freeze = false, bypass = false;
};

constexpr double pi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void set (bool high, double f, double q, double fs)
    {
        const double w = 2 * pi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al, k = high ? 1 + c : 1 - c;
        b0 = k / 2 / a0; b1 = (high ? -k : k) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

/** What a space is made of: shortest and longest way (ms) at size 0.5, spread of the early reflections (ms), input diffusion, how slowly it fills, how much
    it is modulated, and its own top and bottom. */
struct Make { double lo, hi, early, diff, fill, mod, top, warm; };
inline const Make& makeOf (Space s)
{
    static const Make m[numSpaces] { { 19, 67, 34, 0.64, 0.50, 1.0, 1.0, 0.0 }, { 29, 97, 16, 0.70, 0.45, 1.0, 1.0, 0.0 }, { 47, 131, 78, 0.66, 0.80, 0.8, 0.9, 0.0 },
                                     { 37, 113, 46, 0.68, 0.60, 1.0, 0.6, 1.5 }, { 31, 97, 40, 0.64, 0.60, 1.6, 0.35, 2.0 }, { 53, 139, 60, 0.72, 0.60, 0.8, 0.9, 0.0 } };
    return m[(int) s];
}
/** How much longer all ways are, by size. */
inline double scaleOf (double size) { return 0.68 + 0.64 * size; }
inline int nextPrime (int n) { for (n = std::max (3, n | 1);; n += 2) { bool p = true; for (int k = 3; k * k <= n; k += 2) if (n % k == 0) { p = false; break; } if (p) return n; } }

/** A delay that keeps the last 2^k samples. */
struct Line
{
    std::vector<float> b; int mask = 0, w = 0;
    void size (int n) { int s = 16; while (s < n + 8) s <<= 1; b.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); }
    void put (double v) noexcept { b[(size_t) w] = (float) v; w = (w + 1) & mask; }
    /** d whole samples ago; 1 is the last one put in. */
    double at (int d) const noexcept { return b[(size_t) ((w - d) & mask)]; }
    double between (double d) const noexcept
    {
        const int i = (int) d; const double t = d - i, a = at (i - 1), c = at (i), e = at (i + 1), f = at (i + 2);
        return c + 0.5 * t * (e - a + t * (2 * a - 5 * c + 4 * e - f + t * (3 * (c - e) + f - a)));
    }
};

/** Down to 44 to 48 kHz and up again for the wet sound: a reverb has nothing to say above 20 kHz. */
class Rates
{
public:
    void prepare (double fs)
    {
        factor = fs < 60000 ? 1 : (fs < 120000 ? 2 : 4); if (factor == 1) return;
        n = 32 * factor; h.assign ((size_t) n, 0.0); double s = 0;
        for (int i = 0; i < n; ++i) { const double x = i - 0.5 * (n - 1), a = pi * x * 0.88 / factor, w = 0.42 - 0.5 * std::cos (2 * pi * (i + 0.5) / n) + 0.08 * std::cos (4 * pi * (i + 0.5) / n); h[(size_t) i] = w * (std::abs (a) < 1e-12 ? 1 : std::sin (a) / a); s += h[(size_t) i]; }
        for (auto& v : h) v /= s;
        for (auto& q : in) q.assign ((size_t) (2 * n), 0.0);
        for (auto& q : out) q.assign ((size_t) (2 * n), 0.0);
        ip = op = 0; phase = 0;
    }
    int factor = 1;
    /** Takes one input pair; true when a pair at the lower rate is ready. */
    bool down (double l, double r, double& ol, double& orr) noexcept
    {
        if (factor == 1) { ol = l; orr = r; return true; }
        ip = ip == 0 ? n - 1 : ip - 1; in[0][(size_t) ip] = in[0][(size_t) (ip + n)] = l; in[1][(size_t) ip] = in[1][(size_t) (ip + n)] = r;
        if (++phase < factor) return false;
        phase = 0; double a = 0, b = 0; for (int i = 0; i < n; ++i) { a += h[(size_t) i] * in[0][(size_t) (ip + i)]; b += h[(size_t) i] * in[1][(size_t) (ip + i)]; }
        ol = a; orr = b; return true;
    }
    /** One output pair per input pair; fresh: the pair made by this input, if there was one. */
    void up (bool fresh, double l, double r, double& ol, double& orr) noexcept
    {
        if (factor == 1) { ol = l; orr = r; return; }
        op = op == 0 ? n - 1 : op - 1; out[0][(size_t) op] = out[0][(size_t) (op + n)] = fresh ? l * factor : 0.0; out[1][(size_t) op] = out[1][(size_t) (op + n)] = fresh ? r * factor : 0.0;
        // only one of `factor` values is not zero
        double a = 0, b = 0; const int first = fresh ? 0 : since + 1; since = fresh ? 0 : since + 1;
        for (int i = first; i < n; i += factor) { a += h[(size_t) i] * out[0][(size_t) (op + i)]; b += h[(size_t) i] * out[1][(size_t) (op + i)]; }
        ol = a; orr = b;
    }
    void clear() { for (auto& q : in) std::fill (q.begin(), q.end(), 0.0); for (auto& q : out) std::fill (q.begin(), q.end(), 0.0); }
private:
    int n = 0, ip = 0, op = 0, phase = 0, since = 0; std::vector<double> h; std::array<std::vector<double>, 2> in, out;
};

class Engine
{
public:
    static constexpr int N = 16, D = 4, E = 14;
    struct Frame { float dry = -120, wet = -120, duck = 0; };

    void setParams (const Params& x) noexcept { p = x; }
    void prepare (double rate)
    {
        fs = rate; rates.prepare (fs); fr = fs / rates.factor;
        for (int i = 0; i < N; ++i) { line[(size_t) i].size ((int) (fr * 0.26)); ap[(size_t) i].size ((int) (fr * 0.06)); }
        for (int c = 0; c < 2; ++c) { for (int k = 0; k < D; ++k) diff[(size_t) c][(size_t) k].size ((int) (fr * 0.05)); pre[(size_t) c].size ((int) (fr * 0.62)); earlyLine[(size_t) c].size ((int) (fr * 0.3)); wow[(size_t) c].size ((int) (fr * 0.05)); }
        active = p; active.lowCut = std::max (10.0, p.lowCut); built = false; build(); clear(); fade = 1; fadeDir = 0; asleep = true; quiet = 0;
        mixDry = dryOf (p); mixWet = wetOf (p); widthNow = p.width; duckEnv = 0; duckGain = 1; gateGain = p.gateMs > 0 ? 0 : 1; gateHold = 0; freezeNow = p.freeze ? 1 : 0; earlyNow = p.early; preNow = p.preMs; modNow = p.mod; driveNow = p.drive; frame = {}; frame.dry = frame.wet = 0;
        for (int i = 0; i < N; ++i) { lfo[(size_t) i] = std::fmod (0.618034 * i, 1.0) * 2 * pi; lfoStep[(size_t) i] = 2 * pi * (0.13 + 0.047 * i) / fr; move[(size_t) i] = std::sin (lfo[(size_t) i]); moveStep[(size_t) i] = 0; }
        tick = 0; gliding = false;
    }
    void clear()
    {
        for (auto& l : line) l.clear();
        for (auto& l : ap) l.clear();
        for (int c = 0; c < 2; ++c) { for (auto& d : diff[(size_t) c]) d.clear(); pre[(size_t) c].clear(); earlyLine[(size_t) c].clear(); wow[(size_t) c].clear(); hpa[c].reset(); hpb[c].reset(); lpa[c].reset(); lpb[c].reset(); topOut[c].reset(); dcOut[c].reset(); warmZ[c] = 0; }
        lowZ.fill (0); highZ.fill (0); rates.clear();
    }
    /** In place. The dry sound is never delayed, and at mix 50 % and under it is never touched. */
    void process (float* L, float* R, int n) noexcept
    {
        build();
        const double g5 = 1 - std::exp (-1 / (0.005 * fs)), g30 = 1 - std::exp (-1 / (0.03 * fs)), dTo = dryOf (p), wTo = wetOf (p);
        const double dAtk = 1 - std::exp (-1 / (0.008 * fs)), dRel = 1 - std::exp (-1 / (0.001 * std::max (20.0, p.duckMs) * fs * 0.5)), duckTo = std::pow (10.0, -p.duck / 20.0);
        for (int i = 0; i < n; ++i)
        {
            const double xl = L[i], xr = R[i], top = std::max (std::abs (xl), std::abs (xr));
            frame.dry = std::max (frame.dry, (float) top);
            if (top > 1e-7) { asleep = false; quiet = 0; } else if (! asleep) ++quiet;
            double wl = 0, wr = 0;
            if (! asleep)
            {
                double dl, dr; const bool fresh = rates.down (xl, xr, dl, dr); double tl = 0, tr = 0; if (fresh) tail (dl, dr, tl, tr);
                rates.up (fresh, tl, tr, wl, wr);
                // long silence, and nothing left over: off, and exactly zero
                const double e = std::max (std::abs (wl), std::abs (wr)); held = std::max (e, held * heldFall); if (quiet > (int) (fs * 0.7) && held < 1e-7 && freezeNow < 1e-6) { asleep = true; clear(); wl = wr = 0; }
            }
            // ducked by the dry sound: the level of the last moments decides
            duckEnv += (top - duckEnv) * (top > duckEnv ? dAtk : dRel);
            const double over = p.duck > 0 ? std::clamp ((20 * std::log10 (duckEnv + 1e-9) + 50) / 20, 0.0, 1.0) : 0.0; duckGain += ((1 + (duckTo - 1) * over) - duckGain) * g5; if (std::abs (duckGain - 1) < 1e-7 && over == 0) duckGain = 1;
            if (p.gateMs > 0) { if (top > 0.003 && top > 0.25 * gateTop) gateHold = (int) (p.gateMs * 0.001 * fs); gateTop = std::max (top, gateTop * gateFall); gateGain += ((gateHold > 0 ? 1.0 : 0.0) - gateGain) * (gateHold > 0 ? g5 : g30 * 2); if (gateHold > 0) --gateHold; } else gateGain += (1 - gateGain) * g30;
            mixDry = near (mixDry, dTo, g5); mixWet = near (mixWet, wTo, g5); widthNow = near (widthNow, p.width, g30);
            if (fadeDir != 0) { fade += fadeDir * fadeStep; if (fade <= 0) { fade = 0; } if (fade >= 1) { fade = 1; fadeDir = 0; } }
            const double m = 0.5 * (wl + wr), s = 0.5 * (wl - wr) * widthNow, g = mixWet * duckGain * gateGain * fade; wl = (m + s) * g; wr = (m - s) * g;
            frame.wet = std::max (frame.wet, (float) std::max (std::abs (wl), std::abs (wr))); frame.duck = std::max (frame.duck, (float) (-20 * std::log10 (duckGain + 1e-9)));
            if (mixDry != 1) { L[i] = (float) (xl * mixDry); R[i] = (float) (xr * mixDry); }
            if (wl != 0 || wr != 0) { L[i] = (float) (L[i] + wl); R[i] = (float) (R[i] + wr); }
        }
    }
    Frame takeFrame() noexcept { auto f = frame; auto db = [] (float v) { return v > 1e-6f ? 20.0f * std::log10 (v) : -120.0f; }; f.dry = db (f.dry); f.wet = db (f.wet); frame = {}; frame.dry = frame.wet = 0; return f; }
    bool sleeping() const { return asleep; }
    /** Seconds until nothing is left. */
    static double tailSeconds (const Params& q) { return q.freeze ? 1.0e6 : q.preMs * 0.001 + 1.4 * q.decay * std::max ({ 1.0, q.lowDecay, q.highDecay }) + 0.3; }

private:
    static double dryOf (const Params& q) { return q.bypass ? 1.0 : std::min (1.0, 2 * (1 - q.mix)); }
    static double wetOf (const Params& q) { return q.bypass ? 0.0 : std::min (1.0, 2 * q.mix); }
    static double near (double a, double to, double k) { return std::abs (a - to) < 1e-6 ? to : a + (to - a) * k; }

    /** Lengths follow space and size. A change fades the wet out, changes, and fades in. */
    void build() noexcept
    {
        const bool shape = ! built || p.space != active.space || std::abs (p.size - active.size) > 1e-6;
        if (shape && built) { if (fadeDir >= 0 && fade > 0) { fadeDir = -1; fadeStep = 1 / (0.03 * fs); } if (fade > 0) { tune(); return; } }
        if (shape)
        {
            active.space = p.space; active.size = p.size; const auto& m = makeOf (active.space); const double scale = scaleOf (active.size);
            for (int i = 0; i < N; ++i)
            {
                const double t = (i + 0.5 + 0.22 * std::sin (2.4 * i + 1)) / N, ms = m.lo * std::pow (m.hi / m.lo, t) * scale; len[(size_t) i] = std::min ((int) (fr * 0.25), nextPrime ((int) (ms * 0.001 * fr)));
                const double a = (3.1 + 0.83 * ((i * 7) % N)) * (0.6 + 0.8 * m.fill) * scale * 1.25; apLen[(size_t) i] = std::min ((int) (fr * 0.055), nextPrime ((int) (a * 0.001 * fr)));
            }
            static const double dl[2][D] { { 4.77, 3.59, 12.73, 9.31 }, { 4.93, 3.71, 12.11, 9.67 } };
            for (int c = 0; c < 2; ++c) for (int k = 0; k < D; ++k) diffLen[(size_t) c][(size_t) k] = nextPrime ((int) (dl[c][k] * (0.5 + 0.8 * scale * 0.6) * 0.001 * fr));
            for (int c = 0; c < 2; ++c) for (int k = 0; k < E; ++k)
            {
                // fixed pattern: denser towards the end, different on both sides
                const double u = (k + 0.35 + 0.45 * std::sin (3.7 * k + 1.9 * c + 0.7)) / E, t = m.early * scale * std::pow (u, 0.8) + 1.5 + 1.1 * c * (k % 3); earlyAt[(size_t) c][(size_t) k] = std::max (2, (int) (t * 0.001 * fr));
                earlyG[(size_t) c][(size_t) k] = (((k * 5 + c * 3) % 7) < 4 ? 1.0 : -1.0) * std::pow (10.0, -(3.0 + 11.0 * u) / 20.0);
            }
            if (built) { fadeDir = 1; fadeStep = 1 / (0.05 * fs); clear(); inFade = 0; }
            built = true; wantTune = true;
        }
        tune();
    }
    /** Settings that can change while it sounds glide there. */
    void tune() noexcept { if (wantTune) { wantTune = false; coefficients (true); } else gliding = true; }
    void coefficients (bool now) noexcept
    {
        // towards the setting, in ratios; there after about 60 ms
        const double k = now ? 1.0 : 1 - std::exp (-32 / (0.015 * fr)); bool moving = false;
        auto glide = [&] (double& v, double to) { if (std::abs (v / to - 1) < 1e-4) { if (v != to) { v = to; moving = true; } } else { v *= std::pow (to / v, k); moving = true; } };
        glide (active.decay, p.decay); glide (active.lowDecay, p.lowDecay); glide (active.highDecay, p.highDecay); glide (active.lowCut, std::max (10.0, p.lowCut)); glide (active.highCut, p.highCut);
        gliding = moving && ! now; if (! moving && ! now) return;
        const auto& m = makeOf (active.space);
        for (int i = 0; i < N; ++i)
        {
            const double d = (len[(size_t) i] + apLen[(size_t) i]) / fr; auto g = [&] (double s) { return std::pow (10.0, -3 * d / std::max (0.05, s)); };
            gm[(size_t) i] = g (active.decay); gl[(size_t) i] = g (active.decay * active.lowDecay); gh[(size_t) i] = g (active.decay * active.highDecay);
        }
        cl = 1 - std::exp (-2 * pi * 230 / fr); ch = 1 - std::exp (-2 * pi * 3800 / fr);
        for (int c = 0; c < 2; ++c) { hpa[c].set (true, active.lowCut, 0.5412, fr); hpb[c].set (true, active.lowCut, 1.3066, fr); lpa[c].set (false, active.highCut, 0.7071, fr); lpb[c].set (false, std::min (18000.0, active.highCut * (0.6 + 0.4 * m.top) * 1.4), 0.6, fr); topOut[c].set (false, std::max (2500.0, 17000.0 * m.top), 0.6, fr); dcOut[c].set (true, 16, 0.7071, fr); }
        // the same loudness whatever the decay
        const double d = active.decay; static const double trim[numSpaces] { 1.0, 0.84, 1.0, 0.66, 1.0, 1.0 };
        level = trim[(int) active.space] * 0.62 / std::sqrt (std::max (0.2, d)) * (d > 4 ? std::sqrt (d / 4) * std::pow (4.0 / d, 0.25) : 1.0);
        heldFall = std::exp (-1 / (0.1 * fs)); gateFall = std::exp (-1 / (0.15 * fs));
    }
    /** Every 32 samples: where the lines move to, and settings that glide. */
    void control() noexcept
    {
        for (int i = 0; i < N; ++i) { auto& ph = lfo[(size_t) i]; ph += 32 * lfoStep[(size_t) i]; if (ph > 2 * pi) ph -= 2 * pi; moveStep[(size_t) i] = (std::sin (ph) - move[(size_t) i]) / 32; }
        if (gliding) coefficients (false);
    }
    void tail (double xl, double xr, double& ol, double& orr) noexcept
    {
        const auto& m = makeOf (active.space); const double g = 1 - std::exp (-1 / (0.03 * fr));
        freezeNow = near (freezeNow, p.freeze ? 1.0 : 0.0, 1 - std::exp (-1 / (0.02 * fr))); earlyNow = near (earlyNow, p.early, g); modNow = near (modNow, p.mod, g); driveNow = near (driveNow, p.drive, g);
        preNow = near (preNow, p.preMs, 1 - std::exp (-1 / (0.08 * fr)));
        // after a change the sound comes in softly: a hard start would be heard as a click a few reflections later
        if (inFade < 1) { inFade = std::min (1.0, inFade + 1 / (0.04 * fr)); const double k = 0.5 - 0.5 * std::cos (pi * inFade); xl *= k; xr *= k; }
        double in[2] { xl, xr };
        for (int c = 0; c < 2; ++c)
        {
            double v = lpb[c].run (lpa[c].run (hpb[c].run (hpa[c].run (in[c]))));
            if (m.warm > 0) { warmZ[c] += (v - warmZ[c]) * cl; v += warmZ[c] * (m.warm * 0.15); }
            pre[(size_t) c].put (v); v = pre[(size_t) c].between (2 + preNow * 0.001 * fr);
            earlyLine[(size_t) c].put (v); in[c] = v;
        }
        // early reflections: each side hears mostly its own, and some of the other
        double er[2] { 0, 0 };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < E; ++k) { const auto& q = earlyLine[(size_t) (k % 4 == 3 ? 1 - c : c)]; er[c] += earlyG[(size_t) c][(size_t) k] * q.at (earlyAt[(size_t) c][(size_t) k]); }
        // into the tail through four diffusers per side
        for (int c = 0; c < 2; ++c)
        {
            double v = in[c] * (1 - freezeNow);
            for (int k = 0; k < D; ++k) { auto& q = diff[(size_t) c][(size_t) k]; const double z = q.at (diffLen[(size_t) c][(size_t) k]), w = v - m.diff * z; q.put (w); v = z + m.diff * w; }
            in[c] = v;
        }
        if (++tick >= 32) { tick = 0; control(); }
        double y[N]; const double depth = modNow * m.mod * 0.00045 * fr * (1 - freezeNow);
        for (int i = 0; i < N; ++i)
        {
            auto& q = line[(size_t) i]; double v;
            if (depth < 1e-3) v = q.at (len[(size_t) i]);
            else { move[(size_t) i] += moveStep[(size_t) i]; v = q.between (std::max (3.0, len[(size_t) i] + depth * move[(size_t) i])); }
            // how each band decays
            lowZ[(size_t) i] += (v - lowZ[(size_t) i]) * cl; highZ[(size_t) i] += (v - highZ[(size_t) i]) * ch;
            const double f = gm[(size_t) i] * v + (gl[(size_t) i] - gm[(size_t) i]) * lowZ[(size_t) i] + (gh[(size_t) i] - gm[(size_t) i]) * (v - highZ[(size_t) i]);
            v = f + (v - f) * freezeNow;
            // a diffuser inside the loop: many more ways round
            auto& a = ap[(size_t) i]; const double z = a.at (apLen[(size_t) i]), w = v - 0.55 * z; a.put (w); y[i] = z + 0.55 * w;
        }
        double l = 0, r = 0;
        for (int i = 0; i < N; ++i) { l += sideL[i] * y[i]; r += sideR[i] * y[i]; }
        // everybody to everybody, no energy lost
        for (int h = 1; h < N; h <<= 1) for (int i = 0; i < N; i += 2 * h) for (int j = i; j < i + h; ++j) { const double a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        for (int i = 0; i < N; ++i) line[(size_t) i].put (0.25 * y[(i + 5) & (N - 1)] + ((i & 1) ? in[1] : in[0]) * (((i >> 1) & 1) ? -0.25 : 0.25));
        const double tailG = std::min (1.0, 2 * (1 - earlyNow)), earlyG2 = std::min (1.0, 2 * earlyNow) * 0.42;
        double o[2] { l * 0.5 * level * tailG + er[0] * earlyG2, r * 0.5 * level * tailG + er[1] * earlyG2 };
        for (int c = 0; c < 2; ++c)
        {
            double v = o[c];
            if (active.space == Space::lofi) { wow[(size_t) c].put (v); wowPh[c] += 2 * pi * (c ? 0.71 : 0.63) / fr; flutPh[c] += 2 * pi * (c ? 6.3 : 5.9) / fr; if (wowPh[c] > 2 * pi) wowPh[c] -= 2 * pi; if (flutPh[c] > 2 * pi) flutPh[c] -= 2 * pi; v = wow[(size_t) c].between (4 + 0.004 * fr + modNow * fr * (0.0010 * std::sin (wowPh[c]) + 0.00003 * std::sin (flutPh[c]))); }
            if (driveNow > 1e-4) { const double k = 1 + 14 * driveNow * driveNow + 2 * driveNow; v = v + (std::tanh (k * (v + 0.04 * driveNow)) / k * (1 + 1.2 * driveNow) - v) * std::min (1.0, driveNow * 3); }
            o[c] = dcOut[c].run (topOut[c].run (v));
        }
        ol = o[0]; orr = o[1];
    }

    double fs = 48000, fr = 48000; Params p, active; bool built = false, wantTune = true; Rates rates;
    std::array<Line, N> line, ap; std::array<std::array<Line, D>, 2> diff; std::array<Line, 2> pre, earlyLine, wow;
    std::array<int, N> len {}, apLen {}; std::array<std::array<int, D>, 2> diffLen {}; std::array<std::array<int, E>, 2> earlyAt {}; std::array<std::array<double, E>, 2> earlyG {};
    std::array<double, N> gm {}, gl {}, gh {}, lowZ {}, highZ {}, lfo {}, lfoStep {};
    std::array<double, N> move {}, moveStep {}; int tick = 0; bool gliding = false;
    static constexpr double sideL[N] { 1, 1, -1, -1, -1, -1, 1, 1, 1, 1, -1, -1, -1, -1, 1, 1 }, sideR[N] { 1, -1, 1, -1, 1, -1, 1, -1, -1, 1, -1, 1, -1, 1, -1, 1 };
    Biquad hpa[2], hpb[2], lpa[2], lpb[2], topOut[2], dcOut[2]; double warmZ[2] {}, wowPh[2] {}, flutPh[2] {};
    double cl = 0.03, ch = 0.4, level = 0.5, fade = 1, fadeStep = 0.001, mixDry = 1, mixWet = 0.5, widthNow = 1, duckEnv = 0, duckGain = 1, gateGain = 1, gateTop = 0, gateFall = 0.999, held = 0, heldFall = 0.999, freezeNow = 0, earlyNow = 0.5, preNow = 20, modNow = 0.4, driveNow = 0, inFade = 1;
    int fadeDir = 0, quiet = 0, gateHold = 0; bool asleep = true; Frame frame;
};
} // namespace aemon::rev
