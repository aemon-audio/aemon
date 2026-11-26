// Aemon Rider: rides a voice over words and phrases. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aemon::rider
{
enum class Hears { voice, anything };
enum class Gaps { hold, back };
enum class Mode { ride, write, play };

struct Params
{
    double even = 0;            // 0..1
    double target = -20;        // dB, the level the words are brought to
    double up = 6, down = 6;    // dB, the furthest moves
    double speed = 12;          // dB per second, never faster
    double words = 300;         // ms of voice that count as one level
    double below = 20;          // dB under the target: ignored
    double follow = 0;          // 0..1 of the beat's moves
    double beat = -20;          // dB, the beat level that counts as normal
    double lookMs = 150;
    Hears hears = Hears::voice;
    Gaps gaps = Gaps::hold;
    Mode mode = Mode::ride;
    double played = 0;          // dB, the written ride (Mode::play)
    bool bypass = false;
};

constexpr double pi = 3.14159265358979323846;
inline double dbOf (double power) { return 10.0 * std::log10 (power + 1e-20); }

/** Second-order section, double precision. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void set (bool high, double f, double q, double fs)
    {
        const double w = 2 * pi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        const double k = high ? 1 + c : 1 - c;
        b0 = k / 2 / a0; b1 = (high ? -k : k) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

/** Looks at the sound every 10 ms: how loud the body of it is (100 to 4000 Hz), and whether it repeats itself like a voice does. */
class Listener
{
public:
    struct Hop { double level = -200, pitch = 0; };      // dB, and 0..1
    void prepare (double rate, bool withPitch)
    {
        fs = rate; pitched = withPitch; hopLen = std::max (1, (int) std::lround (fs * 0.010)); step = std::max (1, (int) std::lround (fs / 12000.0)); fd = fs / step;
        for (int c = 0; c < 2; ++c) { hp[c].set (true, 100, 0.7071, fs); lp[c].set (false, 4000, 0.7071, fs); hi[c].set (true, 4500, 0.7071, fs); }
        aa1.set (false, 900, 0.54, fs); aa2.set (false, 900, 1.31, fs); dhp.set (true, 70, 0.7071, fs);
        ring.assign ((size_t) size, 0.0f); pos = 0; phase = 0; acc = accHi = 0; count = 0; last = lastHi = -200;
        lagLo = std::max (2, (int) (fd / 560.0)); lagHi = (int) (fd / 68.0); win = (int) (fd * 0.030);
        for (auto* q : { &hp[0], &hp[1], &lp[0], &lp[1], &hi[0], &hi[1], &aa1, &aa2, &dhp }) q->reset();
    }
    int hopLength() const { return hopLen; }
    bool push (float l, float r, Hop& out) noexcept
    {
        const double a = lp[0].run (hp[0].run (l)), b = lp[1].run (hp[1].run (r));
        acc += 0.5 * (a * a + b * b);
        if (pitched)
        {
            const double c = hi[0].run (l), d = hi[1].run (r); accHi += 0.5 * (c * c + d * d);
            const double m = dhp.run (aa2.run (aa1.run (0.5 * ((double) l + r))));
            if (++phase >= step) { phase = 0; ring[(size_t) pos] = (float) m; pos = (pos + 1) & (size - 1); }
        }
        if (++count < hopLen) return false;
        const double now = dbOf (acc / hopLen), nowHi = dbOf (accHi / hopLen); acc = accHi = 0; count = 0;
        if (! pitched) { out.level = now; out.pitch = 0; return true; }
        // the pitch window is 30 ms long: it speaks about the hop in its middle, the one before. Brighter than it is loud: an S, whatever its pitch
        out.level = last; out.pitch = last < -75 || lastHi > last - 2.0 || nowHi > now - 2.0 ? 0.0 : repeats();
        last = now; lastHi = nowHi;
        return true;
    }
private:
    double repeats() const noexcept
    {
        // normalised correlation of the last 30 ms with the 30 ms one pitch period earlier
        std::array<float, size> x; const int n = win + lagHi;
        for (int i = 0; i < n; ++i) x[(size_t) i] = ring[(size_t) ((pos - n + i + 2 * size) & (size - 1))];
        const float* a = x.data() + lagHi;
        double ea = 1e-16, eb = 1e-16, best = 0;
        for (int i = 0; i < win; ++i) { ea += (double) a[i] * a[i]; eb += (double) a[i - lagLo] * a[i - lagLo]; }
        for (int lag = lagLo; lag <= lagHi; ++lag)
        {
            const float* b = a - lag;
            double c = 0; for (int i = 0; i < win; ++i) c += (double) a[i] * b[i];
            best = std::max (best, c / std::sqrt (ea * eb));
            if (lag < lagHi) eb = std::max (1e-16, eb + (double) b[-1] * b[-1] - (double) b[win - 1] * b[win - 1]);
        }
        return std::min (1.0, best);
    }
    static constexpr int size = 1024;
    double fs = 48000, fd = 12000, acc = 0, accHi = 0, last = -200, lastHi = -200;
    int hopLen = 480, step = 4, pos = 0, phase = 0, count = 0, lagLo = 21, lagHi = 176, win = 360;
    bool pitched = true;
    Biquad hp[2], lp[2], hi[2], aa1, aa2, dhp;
    std::vector<float> ring;
};

/** Decides for every hop whether the singer is singing, and how loud the words around it are. */
class Judge
{
public:
    static constexpr int maxWindow = 256;
    void prepare() { since = 1000; n = at = 0; voiced = false; }
    bool voiced = false;         // the last hop
    /** True when there is a level to ride to: the level of the last `words` ms of voice. */
    bool step (Listener::Hop h, const Params& p, double& level) noexcept
    {
        const bool clear = p.hears == Hears::anything || h.pitch > (voiced ? 0.60 : 0.72);
        voiced = clear && h.level > std::max (-70.0, p.target - p.below);
        if (! voiced) { if (++since > 25) n = 0; return false; }      // after a pause of a quarter of a second the next word is measured alone
        since = 0;
        power[(size_t) at] = std::pow (10.0, h.level / 10.0); at = (at + 1) % maxWindow; n = std::min (n + 1, maxWindow);
        const int len = std::min (n, std::clamp ((int) std::lround (p.words / 10.0), 5, maxWindow));
        double s = 0; for (int i = 1; i <= len; ++i) s += power[(size_t) ((at - i + maxWindow) % maxWindow)];
        level = dbOf (s / len);
        return len >= 5;
    }
private:
    int since = 1000, n = 0, at = 0;
    std::array<double, maxWindow> power {};
};

/** Follows a wish no faster than `speed` dB per second. Its corners are rounded, which can only make it slower. */
class Slope
{
public:
    void prepare (double rate, int sub) { step = sub / rate; len = std::max (1, (int) std::lround (0.060 / step)); line.assign ((size_t) len, 0.0); reset (0); }
    void reset (double v) { now = v; std::fill (line.begin(), line.end(), v); sum = v * len; at = 0; still = len; }
    double go (double wish, double speed) noexcept
    {
        const double most = speed * step, next = now + std::clamp (wish - now, -most, most);
        if (next == now) { if (still < len) ++still; } else still = 0;
        now = next;
        sum += now - line[(size_t) at]; line[(size_t) at] = now; at = at + 1 == len ? 0 : at + 1;
        if (at == 0) { sum = 0; for (double v : line) sum += v; }
        const double v = still >= len ? now : sum / len;
        return std::abs (v) < 1e-9 ? 0.0 : v;      // the running sum may leave a crumb
    }
    /** Where it is going: 30 ms ahead of what go() returned. */
    double ahead() const noexcept { return now; }
private:
    double step = 0.001, now = 0, sum = 0; int len = 40, at = 0, still = 0; std::vector<double> line;
};

class Engine
{
public:
    /** What happened in the last display frame. raw: level of the sound, voiced: was it the singer, level: of the words, beat: -200 without one. */
    struct Frame { float raw = -200, level = -200, gain = 0, wish = 0, beat = -200; bool voiced = false, capped = false; };
    static constexpr int sub = 16;

    void setParams (const Params& x) noexcept { pendingLook = x.lookMs; const double keep = p.lookMs; p = x; p.lookMs = keep; }
    bool changesLatency (const Params& x) const { return samplesOf (x.lookMs) != delay; }
    void applyLatency() noexcept { p.lookMs = pendingLook; if (samplesOf (p.lookMs) != delay) { delay = samplesOf (p.lookMs); std::fill (dl.begin(), dl.end(), 0.0f); std::fill (dr.begin(), dr.end(), 0.0f); } }
    int latencySamples() const { return delay; }
    void prepare (double rate)
    {
        fs = rate; p.lookMs = pendingLook; delay = samplesOf (p.lookMs);
        listen.prepare (fs, true); beatListen.prepare (fs, false); judge.prepare(); slope.prepare (fs, sub);
        size = 1; while (size < (int) (fs * 0.2) + 64) size <<= 1;
        dl.assign ((size_t) size, 0.0f); dr = dl; w = 0; subCount = 0;
        wish = offset = gainDb = beatPower = beatFast = 0; beatLevel = -200; beatHops = gapHops = 0; g0 = g1 = 1; played = p.mode == Mode::play ? p.played : 0; slope.reset (played); frame = {};
        glide = 1 - std::exp (-sub / (0.010 * fs));
        g0 = g1 = played == 0 ? 1.0 : std::pow (10.0, played / 20.0); gainDb = played;
    }

    /** In place, stereo. The side-chain (the beat) may be null. */
    void process (float* L, float* R, int n, const float* sl = nullptr, const float* sr = nullptr) noexcept
    {
        Listener::Hop h;
        for (int i = 0; i < n; ++i)
        {
            if (sl == nullptr) { beatHops = 0; beatPower = beatFast = 0; }
            else if (beatListen.push (sl[i], sr[i], h))      // 400 ms of the beat
            {
                const double pw = h.level < -100 ? 0.0 : std::pow (10.0, h.level / 10.0);
                if (gapHops > 50 && h.level > p.beat - 15.0) { beatHops = 0; gapHops = 0; }      // the beat is back after a break: measure it afresh
                beatPower += (pw - beatPower) * std::max (0.025, 1.0 / ++beatHops); beatFast += (pw - beatFast) * std::max (0.12, 1.0 / beatHops); beatHops = std::min (beatHops, 1000); beatLevel = dbOf (beatPower);
                gapHops = std::abs (beatLevel - p.beat) >= 15.0 || dbOf (beatFast) < beatLevel - 12.0 ? gapHops + 1 : 0;
            }
            if (listen.push (L[i], R[i], h)) voiceHop (h);
            if (subCount == 0) nextGain();
            dl[(size_t) w] = L[i]; dr[(size_t) w] = R[i];
            const auto rd = (size_t) ((w - delay + size) & (size - 1)); w = (w + 1) & (size - 1);
            const float g = g0 == g1 ? (float) g1 : (float) (g0 + (g1 - g0) * (double) (subCount + 1) / sub);
            if (++subCount >= sub) subCount = 0;
            L[i] = dl[rd]; R[i] = dr[rd];
            if (g != 1.0f) { L[i] *= g; R[i] *= g; }
        }
    }
    Frame takeFrame() noexcept { auto f = frame; f.gain = (float) gainDb; f.wish = (float) wish; f.beat = beatHops > 0 ? (float) beatLevel : -200.0f; frame.raw = -200; frame.voiced = false; return f; }
    /** The ride that is heard now, dB. */
    double gainNow() const noexcept { return gainDb; }
    /** The value to write as automation: ahead by what a DAW takes to write it and to give it back. */
    double gainToWrite() const noexcept { return slope.ahead(); }

private:
    int samplesOf (double ms) const { return (int) std::lround (std::clamp (ms, 0.0, 200.0) * fs / 1000.0); }
    void voiceHop (Listener::Hop h) noexcept
    {
        double level = 0; const bool has = judge.step (h, p, level);
        frame.raw = std::max (frame.raw, (float) h.level); frame.voiced |= judge.voiced;
        if (p.follow <= 0 || beatHops == 0) offset = 0;
        else if (beatHops > 20 && gapHops == 0) offset = p.follow * (beatLevel - p.beat);      // a break is not a quiet beat: the last offset stays
        if (p.even <= 0 && p.follow <= 0) wish = 0;
        else if (has)
        {
            const double full = p.even * (p.target - level) + offset;
            wish = full; frame.level = (float) level; frame.capped = full > p.up || full < -p.down;
        }
        else if (! judge.voiced && p.gaps == Gaps::back) wish = 0;
        wish = std::clamp (wish, -p.down, p.up);
    }
    void nextGain() noexcept
    {
        double to;
        if (p.mode == Mode::play && ! p.bypass)
        {
            played += (p.played - played) * glide; if (std::abs (played - p.played) < 1e-4) played = p.played;
            slope.reset (played); to = played;
        }
        else { to = slope.go (p.bypass ? 0.0 : std::clamp (wish, -p.down, p.up), p.bypass ? 400.0 : p.speed); played = to; }
        gainDb = to; g0 = g1; g1 = to == 0 ? 1.0 : std::pow (10.0, to / 20.0);
    }

    double fs = 48000, pendingLook = 150; Params p;
    Listener listen, beatListen; Judge judge; Slope slope;
    std::vector<float> dl, dr; int size = 1, w = 0, delay = 0, subCount = 0, beatHops = 0, gapHops = 0;
    double wish = 0, offset = 0, beatLevel = -200, beatPower = 0, beatFast = 0, g0 = 1, g1 = 1, gainDb = 0, played = 0, glide = 0.1;
    Frame frame;
};

/** Looks back over a stretch of voice: the level of its words, for "Set it for me" and for the checks. */
struct Heard { bool ok = false; double target = -20, spread = 0; std::vector<double> words; };
inline Heard hear (const std::vector<float>& l, const std::vector<float>& r, double fs, Hears hears, double wordsMs = 300)
{
    Heard o; Listener li; li.prepare (fs, true); Listener::Hop h; std::vector<Listener::Hop> hops;
    for (size_t i = 0; i < l.size(); ++i) if (li.push (l[i], r[i], h)) hops.push_back (h);
    std::vector<double> v; for (auto& x : hops) if (x.level > -65 && (hears == Hears::anything || x.pitch > 0.72)) v.push_back (x.level);
    if (v.size() < 60) return o;
    std::sort (v.begin(), v.end());
    const double top = v[(size_t) (0.9 * (double) v.size())];
    // the same judgement the ride makes
    Judge j; j.prepare(); Params p; p.hears = hears; p.words = wordsMs; p.target = top; p.below = 22; double level = 0;
    for (auto& x : hops) if (j.step (x, p, level)) o.words.push_back (level);
    if (o.words.size() < 40) return o;
    auto w = o.words; std::sort (w.begin(), w.end());
    o.target = w[w.size() / 2];
    const double mean = [&] { double m = 0; for (double x : w) m += x; return m / (double) w.size(); }();
    double var = 0; for (double x : w) var += (x - mean) * (x - mean); o.spread = std::sqrt (var / (double) w.size());
    o.ok = true;
    return o;
}
} // namespace aemon::rider
