// Aemon De-esser: detector and cut. Plain C++, no JUCE, no allocation while processing. AGPL-3.0-or-later.
//
// Two questions, both ratios, so the singer's level does not matter:
//   1. Is this an S?      S band against voice body, in the same 6 ms.
//   2. Is it too loud?    S band against the singer's own recent level.
// Whatever is not an S has a cut of exactly 0 dB, and at 0 dB the output is the input, bit for bit.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::deess
{

enum class Shape { above, justS, whole };       // shelf from the S upwards, bell on the S, the whole sound
enum class On { stereo, mid, side };

struct Params
{
    double amount = 0.0;        // 0..1, how much of what is over the line goes
    double freq = 6500.0;       // where the S sits
    double line = -10.0;        // dB an S may be over (or under) the singer
    double range = 10.0;        // deepest cut, dB
    double strictness = 0.5;    // 0 only clear S sounds, 1 also breaths, T and Z
    double releaseMs = 40.0, lookMs = 2.0;
    Shape shape = Shape::above;
    On on = On::stereo;
    bool fixedLine = false;     // line measured from -24 dBFS instead of from the singer
    bool solo = false, removed = false, bypass = false;
};

constexpr double pi = 3.14159265358979323846;
inline double powerDb (double p) { return 10.0 * std::log10 (p + 1e-14); }

struct Svf
{
    void set (double f, double q, double fs) { const double g = std::tan (pi * std::min (f, fs * 0.45) / fs); k = 1.0 / q; a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void reset() { ic1 = ic2 = 0.0; }
    inline void run (double x) { const double v3 = x - ic2; v1 = a1 * ic1 + a2 * v3; lo = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * lo - ic2; hi = x - k * v1 - lo; }
    double k = 1, a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0, v1 = 0, lo = 0, hi = 0;
};

/** Power in the S band and in the body of the voice, over a 6 ms window, once per hop (a third of a millisecond). */
class Bands
{
public:
    static constexpr int maxHalf = 12;
    void prepare (double rate) { fs = rate; hop = std::max (1, (int) std::lround (16.0 * fs / 48000.0)); half = std::clamp ((int) std::lround (0.003 * fs / hop), 1, maxHalf); curLo = curHi = 0; reset(); }
    void reset() { for (auto& c : ch) for (auto* f : { &c.h1, &c.h2, &c.l1, &c.bl, &c.bh }) f->reset(); pos = count = 0; accS = accB = 0; ringS.fill (0); ringB.fill (0); }
    void setRange (double lo, double hi)
    {
        if (lo == curLo && hi == curHi) return;
        curLo = lo; curHi = hi;
        for (auto& c : ch) { c.h1.set (lo, 0.5412, fs); c.h2.set (lo, 1.3066, fs); c.l1.set (hi, 0.7071, fs); c.bl.set (std::min (2500.0, lo * 0.6), 0.7071, fs); c.bh.set (120.0, 0.7071, fs); }
    }
    /** True once per hop. s() and b() are then the levels half a window ago. */
    inline bool push (double x0, double x1, bool two)
    {
        one (ch[0], x0);
        if (two) one (ch[1], x1);
        if (++count < hop) return false;
        count = 0;
        const double n = (two ? 2.0 : 1.0) * hop;
        ringS[(size_t) pos] = accS / n; ringB[(size_t) pos] = accB / n; accS = accB = 0;
        const int len = 2 * half + 1;
        pos = (pos + 1) % len;
        double s = 0, b = 0; for (int i = 0; i < len; ++i) { s += ringS[(size_t) i]; b += ringB[(size_t) i]; }
        sDb = powerDb (s / len); bDb = powerDb (b / len);
        return true;
    }
    int hopLength() const { return hop; }
    int halfWindow() const { return half; }
    double sDb = -140, bDb = -140;

private:
    struct Ch { Svf h1, h2, l1, bl, bh; };
    inline void one (Ch& c, double x) { c.h1.run (x); c.h2.run (c.h1.hi); c.l1.run (c.h2.hi); c.bl.run (x); c.bh.run (c.bl.lo); accS += c.l1.lo * c.l1.lo; accB += c.bh.hi * c.bh.hi; }
    std::array<Ch, 2> ch;
    std::array<double, 2 * maxHalf + 1> ringS {}, ringB {};
    double fs = 48000, curLo = 0, curHi = 0, accS = 0, accB = 0;
    int hop = 16, half = 9, pos = 0, count = 0;
};

inline double bandLow (double freq) { return freq * 0.7; }
inline double bandHigh (double freq) { return std::min (freq * 1.9, 16000.0); }

/** How much a moment is an S, how loud the singer is, and the cut that follows. One step per hop. */
class Judge
{
public:
    void prepare (double seconds) { dt = seconds; up = coef (0.060); down = coef (0.200); drop = coef (0.080); lag = std::clamp ((int) std::lround (0.05 / dt), 1, (int) hist.size()); reset(); }
    void reset() { voice = -200; quiet = 0; at = 0; hist.fill (-140.0); }
    void step (double s, double b, const Params& p)
    {
        ratio = s - b;
        const double t = threshold (p), e = std::clamp ((ratio - t) / 6.0, 0.0, 1.0);
        sib = s > -110.0 ? e * e * (3.0 - 2.0 * e) : 0.0;
        // the singer: follows the body of steady voiced sound; a dying tail, a pause or an S does not count
        const double before = hist[(size_t) at]; hist[(size_t) at] = b; at = (at + 1) % lag;
        const bool voiced = b > -100.0 && ratio < -5.0, steady = b - before > -4.0;
        if (sib > 0.5 && voice < s - 2.0) voice = s - 2.0;      // nothing heard yet, or a jump: an S is hardly ever louder than its voice
        if (voiced)
        {
            if (voice < -150.0) voice = b;
            else if (b > voice) { voice += (b - voice) * up; quiet = 0; }
            else if (steady && b > voice - 20.0) { voice += (b - voice) * down; quiet = 0; }
            else if (steady && (quiet += dt) > (b > voice - 35.0 ? 0.2 : 3.0)) voice += (b - voice) * drop;
        }
        ref = p.fixedLine ? -24.0 : voice;
        const double over = s - ref - p.line, k = 3.0, o = over <= -k ? 0.0 : (over >= k ? over : (over + k) * (over + k) / (4.0 * k));
        want = std::min (p.range, o * p.amount) * sib;
    }
    static double threshold (const Params& p) { return 4.0 - 14.0 * p.strictness; }
    double sib = 0, ratio = -60, voice = -200, ref = -200, want = 0;

private:
    double coef (double t) const { return 1.0 - std::exp (-dt / t); }
    double dt = 1.0 / 3000.0, up = 0, down = 0, drop = 0, quiet = 0;
    int lag = 150, at = 0;
    std::array<double, 400> hist {};
};

/** The cut, a filter whose depth moves. 0 dB gives the input back exactly. */
class Cut
{
public:
    void reset() { a = b = 0; }
    inline double run (double x, double gd, Shape shape, double w0)
    {
        if (shape == Shape::whole) return gd > 0 ? x * std::exp (gd * -0.11512925464970228) : x;
        const double A = gd > 0 ? std::exp (gd * -0.05756462732485114) : 1.0;       // 10^(-gd/40)
        const bool bell = shape == Shape::justS;
        const double g = bell ? w0 : w0 * std::sqrt (A), k = bell ? 1.0 / (qBell * A) : 1.0 / qShelf, a1 = 1.0 / (1.0 + g * (g + k)), v1 = a1 * (a + g * (x - b)), v2 = b + g * v1;
        a = 2 * v1 - a; b = 2 * v2 - b;
        if (! (gd > 0)) return x;
        return bell ? x + k * (A * A - 1.0) * v1 : A * A * x + k * (1.0 - A) * A * v1 + (1.0 - A * A) * v2;
    }
    static double warp (double freq, Shape shape, double fs) { return std::tan (pi * std::min (shape == Shape::justS ? freq : freq * 0.72, fs * 0.45) / fs); }
    /** Response in dB at a frequency, as the filter really has it at this rate. */
    static double responseDb (double f, double gd, Shape shape, double freq, double fs)
    {
        if (shape == Shape::whole) return -gd;
        const double A = std::pow (10.0, -gd / 40.0), x = std::tan (pi * std::min (f, fs * 0.499) / fs) / warp (freq, shape, fs);
        if (shape == Shape::justS) { const double re = 1 - x * x; return 20 * std::log10 (std::hypot (re, x * A / qBell) / std::hypot (re, x / (A * qBell))); }
        const double s = std::sqrt (A) * x / qShelf;
        return 20 * std::log10 (A * std::hypot (1 - A * x * x, s) / std::hypot (A - x * x, s));
    }
    static constexpr double qBell = 1.1, qShelf = 0.6;

private:
    double a = 0, b = 0;
};

class Engine
{
public:
    /** What happened since the last read, for the display. */
    struct Frame { float body = -140, s = -140, cut = 0, sib = 0, voice = -200, vowelCut = 0, eventCut = -1; };

    void setParams (const Params& p) { want = p; }
    void prepare (double rate)
    {
        fs = rate;
        bands.prepare (fs);
        hop = bands.hopLength(); half = bands.halfWindow(); dt = hop / fs;
        judge.prepare (dt);
        cur = want;
        look = lookHops (cur);
        delayLen = 1; while (delayLen < (half + maxLook + 2) * hop + 8) delayLen <<= 1;
        dl.assign ((size_t) delayLen, 0.0f); dr = dl;
        attack = 1.0 - std::exp (-dt / 0.0012); shut = 1.0 - std::exp (-dt / 0.0035); freqCoef = 1.0 - std::exp (-dt / 0.02);
        smoothFreq = cur.freq;
        bands.setRange (bandLow (cur.freq), bandHigh (cur.freq));
        warp0 = Cut::warp (smoothFreq, cur.shape, fs);
        ring.fill ({});
        pos = 0; hopPos = 0; write = 0; g = 0; gFrom = gTo = sFrom = sTo = 0; duck = 1; since = 1 << 20; eventPeak = -1;
        for (auto& c : cuts) c.reset();
        monitor = {}; monitor[(size_t) monitorIndex (cur)] = 1.0;
        frame = {};
    }
    int lookHops (const Params& p) const { return std::clamp ((int) std::lround (p.lookMs * 0.001 * fs / hop), 0, maxLook); }
    bool changesLatency (const Params& p) const { return lookHops (p) != look; }
    /** Takes over the lookahead of the last setParams: a restart of the delay. */
    void applyLook() { look = lookHops (want); }
    int latencySamples() const { return (half + look + 1) * hop; }

    void process (float* L, float* R, int n) noexcept
    {
        const int mask = delayLen - 1;
        for (int i = 0; i < n; ++i)
        {
            const double l = L[i], r = R[i];
            dl[(size_t) (write & mask)] = L[i]; dr[(size_t) (write & mask)] = R[i];
            const int rd = (write - latencySamples()) & mask;
            ++write;
            const float xl = dl[(size_t) rd], xr = dr[(size_t) rd];

            const double t = (double) hopPos / hop, gd = gFrom + (gTo - gFrom) * t;
            double ol, orr;
            if (cur.on == On::stereo) { ol = cuts[0].run (xl, gd, cur.shape, warp0); orr = cuts[1].run (xr, gd, cur.shape, warp0); }
            else
            {
                const double m = cur.on == On::mid ? 0.5 * ((double) xl + xr) : 0.5 * ((double) xl - xr), d = cuts[0].run (m, gd, cur.shape, warp0) - m;
                ol = xl + d; orr = cur.on == On::mid ? xr + d : xr - d;
            }
            const int target = monitorIndex (want);
            if (monitor[(size_t) target] >= 1.0) emit (target, L[i], R[i], xl, xr, ol, orr, sFrom + (sTo - sFrom) * t);
            else
            {
                const double stepSize = 1.0 / (0.008 * fs); double sumL = 0, sumR = 0;
                double rest = 0; for (int q = 0; q < 4; ++q) if (q != target) { monitor[(size_t) q] = std::max (0.0, monitor[(size_t) q] - stepSize); rest += monitor[(size_t) q]; }
                monitor[(size_t) target] = rest <= 0 ? 1.0 : std::max (0.0, 1.0 - rest);
                for (int q = 0; q < 4; ++q) if (monitor[(size_t) q] > 0) { float a, b; emit (q, a, b, xl, xr, ol, orr, sFrom + (sTo - sFrom) * t); sumL += monitor[(size_t) q] * a; sumR += monitor[(size_t) q] * b; }
                L[i] = (float) sumL; R[i] = (float) sumR;
            }

            bool full;
            if (cur.on == On::stereo) full = bands.push (l, r, true);
            else full = bands.push (cur.on == On::mid ? 0.5 * (l + r) : 0.5 * (l - r), 0.0, false);
            if (++hopPos >= hop) hopPos = 0;
            if (full) hopStep();
        }
    }

    Frame takeFrame() noexcept { auto f = frame; frame = {}; frame.voice = f.voice; return f; }
    /** The singer's level as it is followed now, dB. */
    double voiceDb() const { return judge.voice; }

private:
    static constexpr int maxLook = 16, ringLen = 64;
    struct Step { double want = 0, sib = 0, ratio = -60, body = -140, ref = -200; };
    static int monitorIndex (const Params& p) { return p.bypass ? 1 : (p.solo ? 2 : (p.removed ? 3 : 0)); }
    static inline void emit (int kind, float& a, float& b, float xl, float xr, double ol, double orr, double sib)
    {
        switch (kind)
        {
            case 1: a = xl; b = xr; break;
            case 2: a = (float) (xl * sib); b = (float) (xr * sib); break;
            case 3: a = (float) (xl - ol); b = (float) (xr - orr); break;
            default: a = (float) ol; b = (float) orr;
        }
    }

    void hopStep() noexcept
    {
        // a change of shape or side: let go for a moment, so that nothing steps
        const bool swap = want.shape != cur.shape || want.on != cur.on;
        duck = std::clamp (duck + (swap ? -dt : dt) / 0.004, 0.0, 1.0);
        const bool silent = swap && gTo <= 0.0 && gFrom <= 0.0;
        const auto keepShape = cur.shape; const auto keepOn = cur.on;
        cur = want;
        if (swap && ! (duck <= 0.0 && silent)) { cur.shape = keepShape; cur.on = keepOn; }
        else if (swap) for (auto& c : cuts) c.reset();
        bands.setRange (bandLow (cur.freq), bandHigh (cur.freq));
        if (smoothFreq != cur.freq || swap)
        {
            smoothFreq *= std::exp (std::log (cur.freq / smoothFreq) * freqCoef);
            if (std::abs (smoothFreq - cur.freq) < 0.5) smoothFreq = cur.freq;
            warp0 = Cut::warp (smoothFreq, cur.shape, fs);
        }

        judge.step (bands.sDb, bands.bDb, cur);
        ring[(size_t) pos] = { judge.want, judge.sib, judge.ratio, bands.bDb, judge.ref };
        double t = 0; for (int q = 0; q <= look; ++q) t = std::max (t, ring[(size_t) ((pos - q) & (ringLen - 1))].want);
        const auto& here = ring[(size_t) ((pos - look) & (ringLen - 1))];
        pos = (pos + 1) & (ringLen - 1);

        const bool voice = t <= g && here.ratio < Judge::threshold (cur) - 2.0;     // the voice is back: let go at once
        g += (t - g) * (t > g ? attack : (voice ? shut : 1.0 - std::exp (-dt / (cur.releaseMs * 0.001))));
        if (voice) g = std::max (t, g - 300.0 * dt);
        if (g < 0.002) g = 0;
        gFrom = gTo; gTo = duck >= 1.0 ? g : (g * duck < 0.002 ? 0.0 : g * duck);
        sFrom = sTo; sTo = here.sib;

        // display
        frame.body = std::max (frame.body, (float) bands.bDb);
        frame.s = std::max (frame.s, (float) bands.sDb);
        frame.sib = std::max (frame.sib, (float) judge.sib);
        frame.cut = std::max (frame.cut, cur.bypass ? 0.0f : (float) gTo);
        frame.voice = (float) judge.ref;
        since = here.sib > 0 ? 0 : since + 1;
        if (since * dt > 0.012 && here.body > here.ref - 20.0) frame.vowelCut = std::max (frame.vowelCut, (float) gTo);
        if (here.sib > 0.5) { eventPeak = std::max ({ eventPeak, 0.0, gTo }); ++eventLen; }
        if (here.sib <= 0.5 || eventLen * dt > 0.25) { if (eventLen * dt > 0.015) frame.eventCut = (float) eventPeak; eventPeak = -1; eventLen = 0; }
    }

    Params want, cur;
    Bands bands;
    Judge judge;
    std::array<Cut, 2> cuts;
    std::vector<float> dl, dr;
    std::array<Step, ringLen> ring {};
    std::array<double, 4> monitor { 1, 0, 0, 0 };
    Frame frame;
    double fs = 48000, dt = 1.0 / 3000, g = 0, gFrom = 0, gTo = 0, sFrom = 0, sTo = 0, attack = 0, shut = 0, freqCoef = 0, smoothFreq = 6500, warp0 = 0.5, duck = 1, eventPeak = -1;
    int hop = 16, half = 9, look = 6, pos = 0, hopPos = 0, delayLen = 1024, since = 0, eventLen = 0;
    unsigned write = 0;
};

} // namespace aemon::deess
