// Aemon Delay: echoes that stay off the words. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "aemon_Distortion.h"

namespace aemon::del
{
enum class Style { stereo, pingpong, mono };
enum class Character { digital, tape, lofi };
enum class Change { glide, fade };

struct Params
{
    double ms = 375;                // left, and right times `right`
    double right = 1.0;             // 0.25..1.5
    Style style = Style::stereo;
    Character character = Character::digital;
    double feedback = 0.3;          // 0..1.1
    double lowCut = 150, highCut = 9000;        // of the echoes, again at every repeat
    double drive = 0, wobble = 0;   // 0..1
    double duck = 0, duckMs = 250;
    double width = 1;
    Change change = Change::fade;
    bool throwOnly = false, throwNow = false;   // with throwOnly nothing goes in but what is thrown
    bool reverse = false, bypass = false;
    double mix = 0.2;
};

constexpr double pi = 3.14159265358979323846;
constexpr double maxMs = 2500, safe = 0.7;      // an echo that feeds itself never gets louder than this

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

class Engine
{
public:
    struct Frame { float dry = -120, wet = -120, duck = 0; bool thrown = false; };
    void setParams (const Params& x) noexcept { p = x; }
    void prepare (double rate)
    {
        fs = rate; size = 1; while (size < (int) (fs * (2 * maxMs + 60) / 1000.0) + 64) size <<= 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        for (auto& c : clip) c.prepare (fs < 60000 ? 4 : (fs < 120000 ? 2 : 1));
        loop = clip[0].latency(); w = 0; curve.knee = 1.0;
        for (int c = 0; c < 2; ++c) { now[c] = to[c] = target (c); pos[c] = now[c]; fading[c] = 0; revPh[c] = 0; revLen[c] = std::max (64.0, now[c]); }
        fadeLen = (int) (fs * 0.04); mixDry = dryOf(); mixWet = wetOf(); widthNow = p.width; duckEnv = 0; duckGain = 1; inGain = p.throwOnly ? (p.throwNow ? 1 : 0) : 1; fbNow = p.feedback; driveNow = p.drive; wobbleNow = p.wobble; lowNow = p.lowCut; highNow = p.highCut; styleNow = p.style; charNow = p.character; revNow = p.reverse; swap = 1; swapDir = 0; asleep = true; quiet = sinceLoud = 0; gritTick = -1; wowPh = flutPh = 0;
        filters(); clear(); frame = {}; frame.dry = frame.wet = 0;
    }
    void clear() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); for (auto& c : clip) c.clear(); for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); lp2[c].reset(); hp2[c].reset(); } }
    /** In place. The dry sound is never delayed, and at mix 50 % and under never touched. */
    void process (float* L, float* R, int n) noexcept
    {
        const double g5 = 1 - std::exp (-1 / (0.005 * fs)), g20 = 1 - std::exp (-1 / (0.02 * fs)), dTo = dryOf(), wTo = wetOf(), inTo = p.throwOnly ? (p.throwNow ? 1.0 : 0.0) : 1.0, dAtk = 1 - std::exp (-1 / (0.008 * fs)), dRel = 1 - std::exp (-1 / (0.001 * std::max (20.0, p.duckMs) * fs * 0.5)), duckTo = std::pow (10.0, -p.duck / 20.0);
        // what cannot glide changes while the echoes are silent for a moment
        if (swapDir == 0 && (p.style != styleNow || p.character != charNow || p.reverse != revNow)) swapDir = -1;
        if (std::abs (lowNow / p.lowCut - 1) > 1e-4 || std::abs (highNow / p.highCut - 1) > 1e-4) glideFilters = true;
        for (int i = 0; i < n; ++i)
        {
            const double xl = L[i], xr = R[i], top = std::max (std::abs (xl), std::abs (xr)); frame.dry = std::max (frame.dry, (float) top);
            if (top > 1e-7) { asleep = false; quiet = 0; } else if (! asleep) ++quiet;
            fbNow = near (fbNow, p.feedback, g20); driveNow = near (driveNow, p.drive, g20); wobbleNow = near (wobbleNow, p.wobble, g20); inGain = near (inGain, inTo, g5);
            if (glideFilters && (++tick & 15) == 0) { const double k = 1 - std::exp (-16 / (0.02 * fs)); lowNow *= std::pow (p.lowCut / lowNow, k); highNow *= std::pow (p.highCut / highNow, k); if (std::abs (lowNow / p.lowCut - 1) < 1e-4 && std::abs (highNow / p.highCut - 1) < 1e-4) { lowNow = p.lowCut; highNow = p.highCut; glideFilters = false; } filters(); }
            double wl = 0, wr = 0;
            if (! asleep)
            {
                double y[2]; for (int c = 0; c < 2; ++c) y[c] = revNow ? backwards (c) : read (c);
                // round the loop: filters, grit, and a limit that only the loop meets
                if ((++gritTick & 15) == 0 || gritTick < 0) { gritK = std::pow (10.0, driveNow); gritBack = std::pow (gritK, -0.75); gritOn = std::min (1.0, driveNow * 6); }
                const double k = gritK, back = gritBack; double f[2], loopF[2];
                // a switch is soft in the loop as well, or it would come back as a click one echo later
                const double soft = swap < 1 ? 0.5 - 0.5 * std::cos (pi * swap) : 1.0;
                for (int c = 0; c < 2; ++c)
                {
                    double v = y[c]; { const double a = hp[c].run (v); if (lowNow > 20.5) v = a; const double b = lp[c].run (v); if (highNow < 19500) v = b; }      // at their ends the filters are not there
                    { const double a = lp2[c].run (v); if (charNow != Character::digital) v = a; const double b = hp2[c].run (v); if (charNow == Character::lofi) v = b; }      // they always run, so a switch finds them warm
                    // without grit the echo that is heard is clean: the limit only sits in the loop
                    v *= soft;
                    double dry; const double hot = clip[c].process (v * k / safe, curve, dry) * safe * back, clean = dry * safe / k; loopF[c] = hot; f[c] = gritOn <= 0 ? clean : clean + (hot - clean) * gritOn;
                }
                const double in[2] { xl * inGain, xr * inGain }; double put[2];
                if (styleNow == Style::pingpong) { put[0] = 0.5 * (in[0] + in[1]) + fbNow * loopF[1]; put[1] = gritOn > 0 ? loopF[0] : std::clamp (f[0], -1.5, 1.5); }
                else if (styleNow == Style::mono) { const double m = 0.5 * (in[0] + in[1]) + fbNow * 0.5 * (loopF[0] + loopF[1]); put[0] = put[1] = m; }
                else { put[0] = in[0] + fbNow * loopF[0]; put[1] = in[1] + fbNow * loopF[1]; }
                for (int c = 0; c < 2; ++c) buf[(size_t) c][(size_t) w] = (float) put[c];
                w = (w + 1) & (size - 1); sinceLoud = std::max (std::abs (put[0]), std::abs (put[1])) > 1e-7 ? 0 : sinceLoud + 1;
                wl = f[0]; wr = f[1];
                if (swapDir != 0) { swap += swapDir / (0.015 * fs); if (swap <= 0) { swap = 0; if (swapDir < 0) { styleNow = p.style; if (charNow != p.character) { charNow = p.character; filters(); } revNow = p.reverse; for (int c = 0; c < 2; ++c) revPh[c] = 0; swapDir = 1; } } else if (swap >= 1) { swap = 1; swapDir = 0; } }
                if (sinceLoud > (int) (fs * 2.2 * maxMs / 1000.0)) { asleep = true; clear(); wl = wr = 0; }
            }
            else if (swapDir != 0) { styleNow = p.style; charNow = p.character; revNow = p.reverse; filters(); swapDir = 0; swap = 1; }
            duckEnv += (top - duckEnv) * (top > duckEnv ? dAtk : dRel);
            const double over = p.duck > 0 ? std::clamp ((20 * std::log10 (duckEnv + 1e-9) + 50) / 20, 0.0, 1.0) : 0.0; duckGain += ((1 + (duckTo - 1) * over) - duckGain) * g5; if (over == 0 && std::abs (duckGain - 1) < 1e-7) duckGain = 1;
            mixDry = near (mixDry, dTo, g5); mixWet = near (mixWet, wTo, g5); widthNow = near (widthNow, p.width, g20);
            const double m = 0.5 * (wl + wr), s = 0.5 * (wl - wr) * widthNow, g = mixWet * duckGain; wl = (m + s) * g; wr = (m - s) * g;
            frame.wet = std::max (frame.wet, (float) std::max (std::abs (wl), std::abs (wr))); frame.duck = std::max (frame.duck, (float) (-20 * std::log10 (duckGain + 1e-9))); frame.thrown |= inGain > 0.5 && p.throwOnly;
            if (mixDry != 1) { L[i] = (float) (xl * mixDry); R[i] = (float) (xr * mixDry); }
            if (wl != 0 || wr != 0) { L[i] = (float) (L[i] + wl); R[i] = (float) (R[i] + wr); }
        }
    }
    Frame takeFrame() noexcept { auto f = frame; auto db = [] (float v) { return v > 1e-6f ? 20.0f * std::log10 (v) : -120.0f; }; f.dry = db (f.dry); f.wet = db (f.wet); frame = {}; frame.dry = frame.wet = 0; return f; }
    bool sleeping() const { return asleep; }
    static double tailSeconds (const Params& q) { const double t = q.ms * 0.001 * std::max (1.0, q.right) * (q.style == Style::pingpong ? 2 : 1); if (q.feedback >= 0.995) return 1.0e6; return t * (1 + (q.feedback > 0.001 ? std::min (200.0, -60 / (20 * std::log10 (q.feedback))) : 0.0)) + 0.2; }

private:
    double dryOf() const { return p.bypass ? 1.0 : std::min (1.0, 2 * (1 - p.mix)); }
    double wetOf() const { return p.bypass ? 0.0 : std::min (1.0, 2 * p.mix); }
    static double near (double a, double b, double k) { return std::abs (a - b) < 1e-6 ? b : a + (b - a) * k; }
    /** Length of one way round for a side, in samples, without what the loop itself takes. */
    double target (int c) const { double ms = std::clamp (p.ms, 5.0, maxMs); if (c == 1 && p.style == Style::stereo) ms = std::clamp (ms * p.right, 5.0, maxMs); const double s = ms * 0.001 * fs; return std::max (8.0, std::round (s) - loop); }
    void filters() noexcept
    {
        // digital as drawn. Tape loses its top, more at every repeat. Lo-fi is a small band
        const double top = charNow == Character::digital ? highNow : (charNow == Character::tape ? std::min (highNow, 7500.0) : std::min (highNow, 4200.0));
        for (int c = 0; c < 2; ++c) { hp[c].set (true, std::max (15.0, lowNow), 0.7071, fs); lp[c].set (false, std::min (highNow, fs * 0.42), 0.7071, fs); lp2[c].set (false, std::min (top, fs * 0.42), 0.5, fs); hp2[c].set (true, 350, 0.7071, fs); }
    }
    double at (int c, double d) const noexcept
    {
        const int i = (int) d; const double t = d - i; const auto& b = buf[(size_t) c]; auto q = [&] (int k) { return (double) b[(size_t) ((w - i - k) & (size - 1))]; };
        if (t == 0) return q (0);
        const double a = q (-1), e = q (0), g = q (1), h = q (2); return e + 0.5 * t * (g - a + t * (2 * a - 5 * e + 4 * g - h + t * (3 * (e - g) + h - a)));
    }
    double read (int c) noexcept
    {
        const double want = target (c);
        if (p.change == Change::glide) { if (fading[c] > 0) { pos[c] = to[c]; now[c] = to[c]; fading[c] = 0; } const double most = 0.45, d = want - pos[c]; pos[c] += std::clamp (d * (1 / (0.06 * fs)), -most, most); if (std::abs (want - pos[c]) < 0.01) pos[c] = want; now[c] = pos[c]; to[c] = want; }
        else { if (fading[c] == 0 && want != now[c]) { to[c] = want; fading[c] = fadeLen; } pos[c] = now[c]; }
        double wob = 0;
        if (charNow != Character::digital && wobbleNow > 0) { if (c == 0) { wowPh += 2 * pi * 0.55 / fs; flutPh += 2 * pi * 6.1 / fs; if (wowPh > 2 * pi) wowPh -= 2 * pi; if (flutPh > 2 * pi) flutPh -= 2 * pi; } wob = wobbleNow * fs * (0.0014 * std::sin (wowPh + c * 0.7) + 0.00006 * std::sin (flutPh + c)); }
        double v = at (c, std::max (4.0, now[c] + wob));
        if (fading[c] > 0) { const double t = 0.5 + 0.5 * std::cos (pi * (double) fading[c] / fadeLen); v += (at (c, std::max (4.0, to[c] + wob)) - v) * t; if (--fading[c] == 0) { now[c] = to[c]; pos[c] = now[c]; } }
        return v;
    }
    /** Every piece of one delay time is played from its end to its start. Two players, half a piece apart, one fading in as the other fades out. */
    double backwards (int c) noexcept
    {
        const double len = std::clamp (target (c), 256.0, size * 0.48); if (revPh[c] == 0) revLen[c] = len;
        double y = 0; const double T = revLen[c];
        for (int k = 0; k < 2; ++k) { double u = revPh[c] + k * 0.5 * T; if (u >= T) u -= T; const double win = std::sin (pi * u / T); y += win * win * at (c, std::floor (4 + 2 * u)); }
        revPh[c] += 1; if (revPh[c] >= T) revPh[c] = 0;
        return y;
    }

    double fs = 48000; Params p; std::array<std::vector<float>, 2> buf; int size = 1, w = 0, loop = 0, fadeLen = 1920, quiet = 0, tick = 0, sinceLoud = 0, gritTick = -1;
    std::array<dist::DeltaChannel<dist::ClipCurve>, 2> clip; dist::ClipCurve curve; Biquad hp[2], lp[2], lp2[2], hp2[2];
    double now[2] {}, to[2] {}, pos[2] {}, revPh[2] {}, revLen[2] {}; int fading[2] {};
    double mixDry = 1, mixWet = 0.4, widthNow = 1, duckEnv = 0, duckGain = 1, inGain = 1, fbNow = 0.3, driveNow = 0, wobbleNow = 0, lowNow = 150, highNow = 9000, swap = 1, gritK = 1, gritBack = 1, gritOn = 0, wowPh = 0, flutPh = 0; int swapDir = 0; bool asleep = true, glideFilters = false, revNow = false;
    Style styleNow = Style::stereo; Character charNow = Character::digital; Frame frame;
};
} // namespace aemon::del
