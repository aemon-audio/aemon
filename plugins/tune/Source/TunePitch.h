// Aemon Tune: hears which note a voice is on. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aemon::tune
{
constexpr double pi = 3.14159265358979323846;

/** Second-order section, double precision. */
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

/** What was heard, every `hop` samples. */
struct Heard
{
    bool voiced = false;
    double hz = 0;          // 0 when not voiced
    double sure = 0;        // 0..1, how well one period repeats the one before
    double level = -200;    // dB
    int age = 0;            // samples: how long ago the middle of what `hz` speaks about was
    bool sharp = false;     // brighter than it is loud: an S or a breath
};

/** A voice repeats itself. This finds after how long, in two steps: candidates on a coarse copy of the sound (normalised correlation), chosen
    by how well they repeat and by where the voice was a moment ago, then the exact period at full rate over one or two periods only, so the
    answer is not older than it has to be. */
class Listener
{
public:
    double lowest = 60, highest = 1100;      // Hz
    double patience = 0;                     // 0: the youngest answer (recording), 1: the surest (mixing). Set before prepare.

    void prepare (double rate)
    {
        fs = rate; hop = std::max (1, (int) std::lround (fs * 0.0025)); fine = std::max (1, (int) std::lround (fs / 48000.0)); ff = fs / fine; step = std::max (1, (int) std::lround (ff / 11025.0)); fd = ff / step;
        int s = 1024; while (s < (int) (0.14 * ff)) s <<= 1; line.assign ((size_t) s, 0.0f); finePhase = 0;
        full.assign ((size_t) s, 0.0f); mask = s - 1; w = 0;
        s = 256; while (s < (int) (0.14 * fd)) s <<= 1;
        low.assign ((size_t) s, 0.0f); lowMask = s - 1; lw = 0; phase = 0; count = 0;
        hp.set (true, 45, 0.7071, fs); lp1.set (false, 1700, 0.54, fs); lp2.set (false, 1700, 1.31, fs); hi.set (true, 4500, 0.7071, fs); body.set (false, 4000, 0.7071, fs);
        for (auto* b : { &hp, &lp1, &lp2, &hi, &body }) b->reset();
        acc = accHi = 0; levels.fill (-200); levelsHi.fill (-200); lv = 0; voiced = false; lastHz = 0; loud = -200; since = 1000; track = 0; wasCount = 0;
        beats = 0; lowCount = 0; behind = 0.416 / 1700.0 * fs;      // what the low-pass holds back
        lagLo = std::max (3, (int) (fd / highest)); lagHi = (int) std::ceil (fd / lowest); nc.assign ((size_t) lagHi + 2, 0.0); rest.assign ((size_t) (0.08 * fd) + 8, 0.0f);
    }
    int hopLength() const { return hop; }
    /** Somebody else in there? Takes the voice that is heard away (it repeats itself after one period) and looks whether what is left repeats itself
        too, after another time. 0..1. An octave above cannot be told from the voice itself. */
    double company (double hz, double& left) noexcept
    {
        left = 0; const double per = fd / hz; const int win = (int) (0.040 * fd), reachTo = std::min (lagHi, (int) (fd / 75)), need = win + reachTo + 2; const int p = (int) per; const double f = per - p;
        if (need + p + 8 > (int) line.size() || need > (int) rest.size()) return 0;
        const float* x = newest (low, lowMask, lw, need + p + 6); double whole = 1e-12, what = 1e-12;
        // a smooth place between the samples, or the upper partials would stay
        const double a = -f * (f - 1) * (f - 2) / 6, b = (f + 1) * (f - 1) * (f - 2) / 2, c = -(f + 1) * f * (f - 2) / 2, d = (f + 1) * f * (f - 1) / 6;
        for (int i = 0; i < need; ++i) { const double once = a * x[i + p - 1] + b * x[i + p] + c * x[i + p + 1] + d * x[i + p + 2], v = x[i] - once; rest[(size_t) i] = (float) v; if (i < win) { whole += (double) x[i] * x[i] + once * once; what += v * v; } }
        left = what / whole; double best = 0, eb = 1e-12; for (int i = 0; i < win; ++i) eb += (double) rest[(size_t) (i + lagLo - 1)] * rest[(size_t) (i + lagLo - 1)];
        double before = -1, now = -1;
        for (int lag = lagLo; lag < reachTo; ++lag)
        {
            const double out = rest[(size_t) (lag - 1)], in = rest[(size_t) (lag - 1 + win)]; eb = std::max (1e-12, eb + in * in - out * out);
            const double r = dot (rest.data(), rest.data() + lag, win) / std::sqrt (what * eb);
            if (now > before && now >= r) { const double q = (lag - 1) / per, m = q >= 1 ? q : 1 / q; if (std::abs (m - std::round (m)) > 0.06 * m && now > best) best = now; }
            before = now; now = r;
        }
        return best;
    }
    void restart() noexcept { std::fill (full.begin(), full.end(), 0.0f); std::fill (low.begin(), low.end(), 0.0f); for (auto* b : { &hp, &lp1, &lp2, &hi, &body }) b->reset(); acc = accHi = 0; levels.fill (0); levelsHi.fill (0); lv = 0; count = phase = finePhase = 0; voiced = false; lastHz = 0; loud = -200; since = 1000; track = 0; wasCount = 0; kept = 0; beats = 0; }

    bool push (float x, Heard& out) noexcept
    {
        const double y = hp.run (x), m = lp2.run (lp1.run (y)), b = body.run (y), h = hi.run (y);
        acc += b * b; accHi += h * h;
        if (++finePhase >= fine)
        {
            finePhase = 0; full[(size_t) w] = (float) m; w = (w + 1) & mask;
            if (++phase >= step) { phase = 0; low[(size_t) lw] = (float) m; lw = (lw + 1) & lowMask; ++lowCount; }
        }
        if (++count < hop) return false;
        count = 0; levels[(size_t) lv] = acc / hop; levelsHi[(size_t) lv] = accHi / hop; lv = (lv + 1) % (int) levels.size(); acc = accHi = 0;
        out = listen(); return true;
    }

private:
    Heard listen() noexcept
    {
        Heard o; double e = 0, eh = 0; for (size_t i = 0; i < levels.size(); ++i) { e += levels[i]; eh += levelsHi[i]; }
        o.level = 10 * std::log10 (e / (double) levels.size() + 1e-20); o.sharp = eh > e * 0.8;
        // the last breath of a note that is dying away (and the room after it) is not a new note
        loud = std::max (o.level, loud - 60.0 * hop / fs);
        if (o.level < -72 || o.level < loud - 34) { lose(); return o; }
        struct Cand { double lag, r, score; }; std::array<Cand, 12> c; int n = 0;
        // a clear voice is heard over a short time; a breathy one needs a longer look
        double top = correlate (std::max (lagHi, (int) (0.0155 * fd))); if (top < 0.88) top = correlate ((int) (0.030 * fd));
        if (top > 0.35)
            for (int i = lagLo + 1; i < lagHi && n < (int) c.size(); ++i)
                if (nc[(size_t) i] > 0.35 && nc[(size_t) i] >= nc[(size_t) i - 1] && nc[(size_t) i] > nc[(size_t) i + 1] && nc[(size_t) i] > 0.75 * top)
                {
                    const double a = nc[(size_t) i - 1], b = nc[(size_t) i], d = nc[(size_t) i + 1], q = 0.5 * (a - d) / (a - 2 * b + d + 1e-18);
                    c[(size_t) n++] = { i + q, b - 0.25 * (a - d) * q, 0 };
                }
        if (n == 0) { lose(); return o; }
        int best = 0;
        for (int i = 0; i < n; ++i)
        {
            auto& k = c[(size_t) i];
            // every multiple of the period repeats as well: the shorter wins unless the longer is clearly better
            k.score = k.r - (0.055 + 0.28 * (1 - top)) * std::log2 (k.lag / c[0].lag);
            // the evidence of the last tens of milliseconds: every candidate goes on from the one before it fits best, a leap costs
            double from = 0; for (int j = 0; j < wasCount; ++j) from = std::max (from, was[(size_t) j].sum * 0.93 - 1.5 * std::min (1.0, std::abs (std::log2 (k.lag / was[(size_t) j].lag)) / 0.35));
            k.score += from; if (k.score > c[(size_t) best].score) best = i;
        }
        for (int i = 0; i < n; ++i) was[(size_t) i] = { c[(size_t) i].lag, c[(size_t) i].score };
        wasCount = n;
        double sure = 0; const double guess = c[(size_t) best].lag * step, shortest = guess * (guess < ff * 0.004 ? 2.0 : 1.3); double period = refine (guess, shortest, sure, o.age);
        // noise moves the answer of a short look: the less sure, the longer, and over several periods, which divides what is left of it
        if (period > 0 && sure < 0.992)
        {
            const double need = std::clamp ((0.992 - sure) / 0.05, 0.0, 1.0), most = std::max ((0.017 + 0.017 * patience) * ff, 3.0 * guess);
            const double window = shortest + need * std::max (0.0, most - shortest); double s2 = 0; int a2 = 0; const double p2 = refine (period, window, s2, a2);
            if (p2 > 0)
            {
                period = p2; sure = s2; o.age = a2;
                for (int k = 2; k <= 8; k *= 2)
                {
                    if (k * period > need * (0.013 + 0.009 * patience) * ff) break;
                    double s3 = 0; int a3 = 0; const double p3 = refine (k * period, window, s3, a3, 0.4 * period);
                    if (p3 <= 0 || s3 < sure - 0.12 || std::abs (p3 / k / period - 1) > 0.02) break;
                    period = p3 / k; o.age = a3;
                }
            }
        }
        o.sure = std::clamp (sure, 0.0, 1.0);
        const bool clear = o.sure > (voiced ? 0.78 : 0.85) && ! (o.sharp && o.sure < 0.93) && period > 0 && ! (o.sure < 0.93 && octaveBelow (period / step));
        if (! clear) { lose(); return o; }
        const double hz = ff / period;
        track = lastHz > 0 && since < 24 && std::abs (std::log2 (hz / lastHz)) < 0.12 ? track + 1 : 1;
        lastHz = hz; since = 0; voiced = true; o.hz = hz;
        o.age += (int) std::lround (behind);
        if (patience > 0) follow (hz); else beats = 0;
        if (! glide (o)) steady (o);
        o.voiced = track >= (o.sure > 0.93 ? 2 : (o.sure > 0.85 ? 4 : 8));      // one look alone can be an accident, more so in noise
        return o;
    }
    /** In noise one look jitters: the mean of the last looks, at the middle of their times, so a glide is not bent. The clearer, the fewer. */
    void steady (Heard& o) noexcept
    {
        if (track <= 1) kept = 0;
        for (int i = std::min (kept, (int) past.size() - 1); i > 0; --i) past[(size_t) i] = past[(size_t) i - 1];
        past[0] = { std::log2 (o.hz), 1.0 / (0.004 + (1 - o.sure)), (double) o.age }; kept = std::min (kept + 1, (int) past.size());
        const int use = std::min (kept, o.sure > 0.985 ? 1 : 1 + (int) std::lround (std::min (1.0, (0.985 - o.sure) / 0.10) * (6 + patience * 21)));
        double s = 0, g = 0, a = 0; for (int i = 0; i < use; ++i) { const auto& q = past[(size_t) i]; s += q.w * q.l; g += q.w; a += q.w * (q.age + i * hop); }
        double l = s / g; const double mid = a / g;
        if (use >= 9)
        {
            // weighted parabola through the looks, time in steps from the middle
            double m[3][4] {}; for (int i = 0; i < use; ++i) { const auto& q = past[(size_t) i]; const double x = (q.age + i * hop - mid) / (8.0 * hop), p[3] { 1, x, x * x }; for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) m[r][c] += q.w * p[r] * p[c]; m[r][3] += q.w * p[r] * q.l; } }
            for (int c = 0; c < 3; ++c) { const double d = m[c][c]; if (std::abs (d) < 1e-12) break; for (int k = c; k < 4; ++k) m[c][k] /= d; for (int r = 0; r < 3; ++r) if (r != c) { const double f = m[r][c]; for (int k = c; k < 4; ++k) m[r][k] -= f * m[c][k]; } }
            if (std::isfinite (m[0][3]) && std::abs (m[0][3] - l) < 0.05) l = m[0][3];
        }
        o.hz = std::pow (2.0, l); o.age = (int) std::lround (mid);
    }
    void lose() noexcept { voiced = false; if (since > 2) beats = 0; if (++since > 24) { lastHz = 0; track = 0; wasCount = 0; } }

    /** The long look (mixing): where the first partials stand in their turn, at one moment, over two and a half periods. */
    void follow (double hz) noexcept
    {
        const double per = fd / hz; const int half = std::min ((int) std::lround (looks * 0.5 * per), (int) line.size() / 2 - 2); const float* x = newest (low, lowMask, lw, 2 * half + 1);
        Beat b; b.hz = hz; b.at = (double) (lowCount - 1 - half); b.count = std::clamp ((int) (ceiling / hz), 1, (int) b.mag.size());
        for (int h = 1; h <= b.count; ++h)
        {
            // the samples run backwards from the newest: x[half] is the middle
            const double turn = 2 * pi * h / per, cw = std::cos (turn), sw = std::sin (turn); double cr = 1, ci = 0, re = x[half], im = 0;
            for (int i = 1; i < half; ++i)
            {
                const double c2 = cr * cw - ci * sw; ci = cr * sw + ci * cw; cr = c2; const double k = 0.5 + 0.5 * std::cos (pi * i / half), up = x[half - i], down = x[half + i];
                re += k * (up + down) * cr; im -= k * (up - down) * ci;
            }
            b.mag[(size_t) (h - 1)] = re * re + im * im; b.arg[(size_t) (h - 1)] = std::atan2 (im, re);
        }
        if (beats > 0 && (std::abs (std::log2 (hz / beat[0].hz)) > 0.12 || b.at <= beat[0].at)) beats = 0;
        for (int i = std::min (beats, stretch - 1); i > 0; --i) beat[(size_t) i] = beat[(size_t) i - 1];
        beat[0] = b; beats = std::min (beats + 1, stretch);
    }
    /** One glide through all of them: the turns of every partial, unwound, divided by its number, are the same curve. */
    bool glide (Heard& o) noexcept
    {
        const int m = beats; if (m < 5) return false;
        int partials = 99; for (int i = 0; i < m; ++i) partials = std::min (partials, beat[(size_t) i].count);
        const double mid = 0.5 * (beat[0].at + beat[(size_t) m - 1].at), span = std::max (1.0, 0.5 * (beat[0].at - beat[(size_t) m - 1].at));
        std::array<double, stretch> base {}; for (int i = m - 2; i >= 0; --i) base[(size_t) i] = base[(size_t) i + 1] + pi * (beat[(size_t) i].hz + beat[(size_t) i + 1].hz) / fd * (beat[(size_t) i].at - beat[(size_t) i + 1].at);
        std::array<std::array<double, stretch>, 16> u {}; std::array<double, 16> strength {}; strength.fill (1.0);
        for (int h = 0; h < partials; ++h) for (int i = m - 2; i >= 0; --i) { const double d = beat[(size_t) i].arg[(size_t) h] - beat[(size_t) i + 1].arg[(size_t) h] - (h + 1) * (base[(size_t) i] - base[(size_t) i + 1]); u[(size_t) h][(size_t) i] = u[(size_t) h][(size_t) i + 1] + d - 2 * pi * std::round (d / (2 * pi)); }
        // what was unwound goes with the straight reference it was unwound from, so only the whole turn counts
        double ref = 0; for (int i = 0; i < m; ++i) ref += beat[(size_t) i].hz / m;
        std::array<double, stretch> bend {}; for (int i = 0; i < m; ++i) bend[(size_t) i] = base[(size_t) i] - 2 * pi * ref / fd * (beat[(size_t) i].at - mid);
        const int order = m >= 10 ? 3 : 2; double c[3] {};
        // of every partial once: how its turns go with the first, second and third power of the time, all from its own middle
        struct Sums { double g = 0, c[3][3] {}, b[3] {}, yy = 0; }; std::array<Sums, 16> sums {};
        for (int h = 0; h < partials; ++h)
        {
            double p[7] {}, q[4] {}; auto& z = sums[(size_t) h];
            for (int i = 0; i < m; ++i)
            {
                const double s = (beat[(size_t) i].at - mid) / span, y = u[(size_t) h][(size_t) i] / (h + 1) + bend[(size_t) i]; double k = beat[(size_t) i].mag[(size_t) h];
                z.yy += k * y * y; for (int r = 0; r < 7; ++r) { p[r] += k; if (r < 4) q[r] += k * y; k *= s; }
            }
            if (p[0] <= 0) continue;
            z.g = p[0]; z.yy -= q[0] * q[0] / p[0]; for (int r = 0; r < 3; ++r) { z.b[r] = q[r + 1] - p[r + 1] * q[0] / p[0]; for (int k = 0; k < 3; ++k) z.c[r][k] = p[r + k + 2] - p[r + 1] * p[k + 1] / p[0]; }
        }
        for (int pass = 0; pass < passes; ++pass)
        {
            double a[3][4] {};
            for (int h = 0; h < partials; ++h) { const auto& z = sums[(size_t) h]; if (z.g <= 0) continue; const double k = strength[(size_t) h] * (h + 1.0) * (h + 1.0); for (int r = 0; r < order; ++r) { for (int q = 0; q < order; ++q) a[r][q] += k * z.c[r][q]; a[r][3] += k * z.b[r]; } }
            for (int r = 0; r < order; ++r) { const double d = a[r][r]; if (! (std::abs (d) > 1e-300)) return false; for (int k = r; k < 4; ++k) a[r][k] /= d; for (int q = 0; q < order; ++q) if (q != r) { const double x = a[q][r]; for (int k = r; k < 4; ++k) a[q][k] -= x * a[r][k]; } }
            for (int r = 0; r < 3; ++r) c[r] = r < order ? a[r][3] : 0.0;
            if (pass + 1 < passes)      // how far each partial strays from the common curve: what it says counts accordingly
                for (int h = 0; h < partials; ++h) { const auto& z = sums[(size_t) h]; if (z.g <= 0) continue; double v = z.yy; for (int r = 0; r < order; ++r) { v -= 2 * c[r] * z.b[r]; for (int q = 0; q < order; ++q) v += c[r] * c[q] * z.c[r][q]; } strength[(size_t) h] = (double) m / z.g / (std::max (0.0, v) * (h + 1.0) * (h + 1.0) / z.g + trust); }
        }
        const double hz = ref + c[0] / span * fd / (2 * pi);
        if (! (hz > 0) || std::abs (std::log2 (hz / ref)) > 0.08) return false;
        o.hz = hz; o.age = (int) std::lround (((double) (lowCount - 1) - mid) * step * fine + phase * fine + finePhase + behind); return true;
    }
    /** The newest samples in one piece, the newest first. */
    const float* newest (const std::vector<float>& ring, int m, int at, int n) noexcept { n = std::min (n, (int) line.size()); for (int i = 0; i < n; ++i) line[(size_t) i] = ring[(size_t) ((at - 1 - i) & m)]; return line.data(); }
    static double dot (const float* a, const float* b, int n) noexcept { float s[8] {}; int i = 0; for (; i + 8 <= n; i += 8) for (int k = 0; k < 8; ++k) s[k] += a[i + k] * b[i + k]; double t = 0; for (float v : s) t += v; for (; i < n; ++i) t += (double) a[i] * b[i]; return t; }

    /** Normalised correlation on the coarse copy, window and shift centred on the newest sound. Returns the highest peak. */
    double correlate (int win) noexcept
    {
        double top = 0; int from = lagLo - 1, to = lagHi + 1;
        // a window of 1.0 to 1.6 periods of what is looked for: short for high notes, so that they are heard early
        const float* x = newest (low, lowMask, lw, win + to + 2);
        const auto at = [x] (int back) { return (double) x[back]; };
        double ea = 1e-12; for (int i = 0; i < win; ++i) { const double a = at (i); ea += a * a; }
        double eb = 1e-12; for (int i = 0; i < win; ++i) { const double b = at (i + from); eb += b * b; }
        for (int lag = from; lag < to; ++lag)
        {
            const double s = dot (x, x + lag, win);
            const double r = s / std::sqrt (ea * eb); if (lag >= lagLo) { nc[(size_t) std::min (lag, lagHi + 1)] = r; if (lag > lagLo + 1) top = std::max (top, r); } else nc[(size_t) lag] = r;
            const double out = at (lag), in = at (lag + win); eb = std::max (1e-12, eb + in * in - out * out);
        }
        // only what comes after the first dip counts: at very short shifts everything repeats itself
        int i = lagLo; double down = nc[(size_t) i]; while (i < lagHi && nc[(size_t) i + 1] < down) down = nc[(size_t) ++i];
        for (int k = lagLo - 1; k < i; ++k) nc[(size_t) k] = -1;
        top = 0; for (int k = i + 1; k < lagHi; ++k) if (nc[(size_t) k] >= nc[(size_t) k - 1] && nc[(size_t) k] > nc[(size_t) k + 1]) top = std::max (top, nc[(size_t) k]);
        return top;
    }

    /** Over a longer look: is half of it as good? Then it is the octave below of what is sung, or it cannot be told. */
    bool octaveBelow (double lag) noexcept
    {
        const int win = (int) (0.045 * fd), half = (int) std::lround (lag * 0.5), whole = (int) std::lround (lag); if (half - 1 < lagLo) return false;
        const float* x = newest (low, lowMask, lw, win + whole + 4); double ea = 1e-12; for (int i = 0; i < win; ++i) ea += (double) x[i] * x[i];
        const auto at = [&] (int l) { double eb = 1e-12; for (int i = 0; i < win; ++i) eb += (double) x[i + l] * x[i + l]; return dot (x, x + l, win) / std::sqrt (ea * eb); };
        double h = -1, f = -1; for (int k = -1; k <= 1; ++k) { h = std::max (h, at (half + k)); f = std::max (f, at (whole + k)); }
        return h > f - 0.14;
    }
    /** The exact period around `guess` samples, over a window of one to two periods. */
    double refine (double guess, double window, double& sure, int& age, double most = 1e9) noexcept
    {
        const int reach = (int) std::min (most, std::max (2.0, std::ceil (guess * 0.035) + step)), mid = (int) std::lround (guess);
        const int win = std::min ((int) std::lround (window), (mask >> 1) - reach);
        if (mid - reach < 2 || win + mid + reach + 2 > mask) { sure = 0; return 0; }
        const float* x = newest (full, mask, w, win + mid + reach + 2); const auto at = [x] (int back) { return (double) x[back]; };
        // the two windows lie evenly on both sides of one moment, so a glide does not pull the answer
        double best = -2, rm = 0, rp = 0; int bl = mid; const int m = std::min (2 * reach + 1, (int) around.size());
        double ea = 1e-12; for (int i = 0; i < win; ++i) { const double a = at (i); ea += a * a; }
        double eb = 1e-12; for (int i = 0; i < win; ++i) { const double b = at (i + mid - reach); eb += b * b; }
        for (int k = 0; k < m; ++k)
        {
            const int lag = mid - reach + k; const double s = dot (x, x + lag, win);
            around[(size_t) k] = s / std::sqrt (ea * eb); if (around[(size_t) k] > best) { best = around[(size_t) k]; bl = k; }
            const double out = at (lag), in = at (lag + win); eb = std::max (1e-12, eb + in * in - out * out);
        }
        if (bl <= 0 || bl >= m - 1) { sure = 0; return 0; }
        rm = around[(size_t) bl - 1]; rp = around[(size_t) bl + 1]; const double q = 0.5 * (rm - rp) / (rm - 2 * best + rp - 1e-18);
        sure = best - 0.25 * (rm - rp) * q; const double period = mid - reach + bl + std::clamp (q, -0.5, 0.5);
        age = (int) std::lround (0.5 * (win + period)) * fine; return period;
    }

    double fs = 48000, ff = 48000, fd = 12000, acc = 0, accHi = 0, lastHz = 0, loud = -200; int hop = 120, fine = 1, finePhase = 0, step = 4, mask = 0, w = 0, lowMask = 0, lw = 0, phase = 0, count = 0, lv = 0, lagLo = 10, lagHi = 200, since = 1000, track = 0; bool voiced = false;
    Biquad hp, lp1, lp2, hi, body;
    std::vector<float> full, low, line, rest; std::array<double, 1024> around {}; std::vector<double> nc; std::array<double, 4> levels {}, levelsHi {};
    struct Path { double lag = 1, sum = 0; }; std::array<Path, 12> was {}; int wasCount = 0;
    static constexpr double looks = 2.6, ceiling = 2600, trust = 0.01; static constexpr int stretch = 18, passes = 4; struct Beat { double at = 0, hz = 0; int count = 0; std::array<double, 16> mag {}, arg {}; }; std::array<Beat, stretch> beat {}; int beats = 0; int64_t lowCount = 0; double behind = 0;
    struct Look { double l = 0, w = 0, age = 0; }; std::array<Look, 28> past {}; int kept = 0;
};
} // namespace aemon::tune
