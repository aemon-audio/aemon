// Aemon Tune: changes the pitch of a voice and keeps the voice. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include "TunePitch.h"
#include <complex>
#include <cstdint>

namespace aemon::tune
{
/** Reads a ring at a place between samples: 24 points of a windowed sinc, from a table. */
class Between
{
public:
    static constexpr int points = 24, phases = 256;
    Between()
    {
        for (int p = 0; p <= phases; ++p)
        {
            double sum = 0, v[points]; const double f = (double) p / phases;
            for (int i = 0; i < points; ++i)
            {
                const double x = i - (points / 2 - 1) - f, w = x / (points / 2.0), k = std::abs (w) >= 1 ? 0.0 : 0.5 + 0.5 * std::cos (pi * w);
                v[i] = (std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * x) / (pi * x)) * k; sum += v[i];
            }
            for (int i = 0; i < points; ++i) t[(size_t) p][(size_t) i] = (float) (v[i] / sum);
        }
    }
    /** x: at least `points` readable around place. */
    float read (const float* x, double place) const noexcept
    {
        const double fl = std::floor (place), f = (place - fl) * phases; const int p = (int) f; const float g = (float) (f - p); const float* s = x + (int64_t) fl - (points / 2 - 1);
        const float* a = t[(size_t) p].data(); const float* b = t[(size_t) p + 1].data(); float sum[8] {};
        for (int i = 0; i < points; i += 8) for (int k = 0; k < 8; ++k) sum[k] += s[i + k] * (a[i + k] + g * (b[i + k] - a[i + k]));
        return ((sum[0] + sum[1]) + (sum[2] + sum[3])) + ((sum[4] + sum[5]) + (sum[6] + sum[7]));
    }
private:
    std::array<std::array<float, points>, phases + 1> t {};
};

/** Plain radix-2 transform, sizes up to the one prepared. */
class Fft
{
public:
    void prepare (int largest) { top = largest; tw.resize ((size_t) top / 2); for (int k = 0; k < top / 2; ++k) tw[(size_t) k] = { (float) std::cos (2 * pi * k / top), (float) -std::sin (2 * pi * k / top) }; }
    void run (std::complex<float>* x, int n, bool back) const noexcept
    {
        for (int i = 1, j = 0; i < n; ++i) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (x[i], x[j]); }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = top / len;
            for (int i = 0; i < n; i += len) for (int k = 0; k < half; ++k) { const auto w = tw[(size_t) (k * step)]; const float wr = w.real(), wi = back ? -w.imag() : w.imag(); auto& a = x[i + k]; auto& b = x[i + k + half]; const float br = b.real() * wr - b.imag() * wi, bi = b.real() * wi + b.imag() * wr; b = { a.real() - br, a.imag() - bi }; a = { a.real() + br, a.imag() + bi }; }
        }
    }
private:
    int top = 0; std::vector<std::complex<float>> tw;
};

/** Takes the voice apart into single periods, each with its own sound (throat, mouth), and puts them down again at another distance. The distance is
    the pitch, the period is the voice: so the voice stays what it was. A change of size of the period is the throat. Where there is no note
    (S, breath, silence) the pieces go back exactly where they came from. */
class Shifter
{
public:
    /** What is wanted around one moment of the input. */
    struct Wish { bool voiced = false; double period = 0, cents = 0, throat = 1, when = 0; };
    bool young = false;      // recording: the least delay, plain pieces, the marks that are known; set before prepare

    void prepare (double rate, double lowestHz, int channels)
    {
        fs = rate; chans = std::clamp (channels, 1, 2); longest = fs / lowestHz; hop = std::max (1, (int) std::lround (fs * 0.0025));
        back = 1.45 * longest; delay = young ? (int) std::ceil (longest * 1.15 + fs * 0.0025) + Between::points / 2 + 2 : (int) std::ceil (std::max (longest * 2.8 + fs * 0.006, longest * 2.35 + fs * 0.0229)) + Between::points;      // the long look of the listener is about so old, and a piece reaches one period further
        if (! young) back = std::max (back, delay - 2.2 * longest);
        int s = 1024; while (s < (int) (longest * 10 + fs * 0.1)) s <<= 1;
        for (auto& r : ring) r.assign ((size_t) (2 * s), 0.0f);      // written twice, so that every stretch lies in one piece
        mono.assign ((size_t) s, 0.0f); mask = s - 1; n = 0; lastHop = known = -1;
        wishes.assign (4096, {}); marks.fill ({}); markCount = 0; grains.fill ({}); last = -1; free = fs * 0.005; lastMark = -1; corr.assign ((size_t) (2 * longest) + 8, 0.0f);
        for (int i = 0; i <= 1024; ++i) cosine[(size_t) i] = (float) std::cos (pi * i / 1024.0);
        gridShare = std::min (1.0, 52000.0 / fs); stride = std::max (1, (int) std::lround (fs / 48000)); largest = 256; while (largest < 2.2 * longest * gridShare) largest <<= 1;
        fft.prepare (4 * largest); work.assign ((size_t) (4 * largest), {}); amp.assign ((size_t) largest / 4 + 8, 0.0f); angle = amp; turns[0] = turns[1] = amp; nextCell = 0; for (auto& q : cell) { for (auto& x : q.x) x.assign ((size_t) (4 * largest + 2 * Between::points), 0.0f); q.size = 0; }
    }
    int latency() const { return delay; }
    void restart() noexcept { for (auto& r : ring) std::fill (r.begin(), r.end(), 0.0f); std::fill (mono.begin(), mono.end(), 0.0f); std::fill (wishes.begin(), wishes.end(), Wish {}); marks.fill ({}); grains.fill ({}); markCount = 0; n = 0; lastHop = known = -1; last = -1; due = 0; lastMark = -1; age = 0; }
    /** A note that has just become sure: the moments before `at` (at most `most` samples) were already this note. Only what has not gone out yet. */
    void before (int64_t at, Wish w, int most) noexcept
    {
        const int64_t out = n - delay + (int64_t) (2 * w.period), to = at / hop, from = std::max<int64_t> ({ 0, (at - most) / hop, out / hop + 1 });
        for (int64_t i = from; i < to; ++i) if (! wishes[(size_t) (i & 4095)].voiced) wishes[(size_t) (i & 4095)] = w;
    }

    /** For the hop whose sounds lie around sample `at` of the input. */
    void wish (int64_t at, Wish w) noexcept { const int64_t h = at / hop; for (int64_t i = std::max<int64_t> (lastHop + 1, h - 64); i < h; ++i) wishes[(size_t) (i & 4095)] = wishes[(size_t) (std::max<int64_t> (0, lastHop) & 4095)]; w.when = (double) at; known = h; wishes[(size_t) (h & 4095)] = w; lastHop = std::max (lastHop, h); }

    /** One sample in, the sound `latency` samples earlier out. */
    void run (const float* in, float* out) noexcept
    {
        float m = 0; for (int c = 0; c < chans; ++c) { ring[(size_t) c][(size_t) (n & mask)] = ring[(size_t) c][(size_t) ((n & mask) + mask + 1)] = in[c]; m += in[c]; }
        mono[(size_t) (n & mask)] = m / (float) chans; ++n;
        if ((n & 15) == 0) findMarks();
        const double t = (double) (n - 1 - delay);
        if (last < 0) { if (t < 0) { for (int c = 0; c < chans; ++c) out[c] = 0; return; } last = t; }
        if (t >= due) start (t);
        double w = 0, y[2] {};
        for (auto& g : grains)
        {
            if (! g.on) continue;
            if (g.cell >= 0)
            {
                // a period made anew: its own shape fades it, the weight is that of its distance to its neighbours
                const double d = t - g.at; if (d >= std::max (g.half, g.reach)) { g.on = false; continue; }
                const auto& q = cell[(size_t) g.cell]; if (std::abs (d) < g.half) for (int c = 0; c < chans; ++c) y[c] += g.gain * between.read (q.x[(size_t) c].data() + Between::points, q.size / 2 + d * g.size * q.rate);
                if (std::abs (d) < g.reach) w += 0.5 + 0.5 * std::cos (pi * d / g.reach);
                continue;
            }
            const double u = (t - g.at) / g.half; if (u >= 1) { g.on = false; continue; }
            if (u <= -1) continue;
            const double k = 0.5 + 0.5 * std::cos (pi * u), place = g.from + (t - g.at) * g.size; w += k;
            for (int c = 0; c < chans; ++c) y[c] += k * fromRing (c, place);
        }
        const double fill = std::max (0.0, 0.30 - w), g = 1.0 / (w + fill);      // a place no piece covers is filled with the sound itself
        for (int c = 0; c < chans; ++c) out[c] = (float) ((y[c] + fill * ring[(size_t) c][(size_t) ((n - 1 - delay) & mask)]) * g);
    }

private:
    struct Grain { bool on = false; double at = 0, from = 0, half = 1, size = 1; int cell = -1; double gain = 1, reach = 1; };
    struct Mark { double at = -1; double period = 0; int cell = -1; };
    static constexpr int cells = 48;
    struct Cell { std::array<std::vector<float>, 2> x; int size = 0; double rate = 1; };      // rate: cell samples per sample

    Wish at (double t) const noexcept { return wishes[(size_t) (std::clamp<int64_t> ((int64_t) std::floor (t / hop), std::max<int64_t> (0, lastHop - 4000), std::max<int64_t> (0, lastHop)) & 4095)]; }

    /** The cents wanted at one moment, straight between the two looks around it. */
    double centsAt (double t) const noexcept
    {
        const int64_t h = std::clamp<int64_t> ((int64_t) std::floor (t / hop), 1, std::max<int64_t> (1, lastHop - 1)); const Wish* a = &wishes[(size_t) (h & 4095)]; const Wish* b = a;
        if (a->when > t && h > 0) a = &wishes[(size_t) ((h - 1) & 4095)]; else b = &wishes[(size_t) ((h + 1) & 4095)];
        if (! a->voiced) return b->cents; if (! b->voiced) return a->cents;
        const double f = b->when > a->when ? std::clamp ((t - a->when) / (b->when - a->when), 0.0, 1.0) : 0.0; return std::abs (b->cents - a->cents) > 40 ? (f < 0.5 ? a : b)->cents : a->cents + f * (b->cents - a->cents);
    }

    /** Follows the periods of the input, one mark per period, each where the sound looks most like it did at the mark before. */
    float fromRing (int c, double place) const noexcept { const double fl = std::floor (place); const int64_t i = ((int64_t) fl - Between::points) & mask; return between.read (ring[(size_t) c].data() + i + Between::points, place - fl); }
    void findMarks() noexcept
    {
        for (int guard = 0; guard < 8; ++guard)
        {
            const double from = lastMark < 0 ? (double) n - back - longest * 0.05 : lastMark;
            // mixing has the time to wait until the Listener has spoken about this moment, as long as no piece needs the mark yet
            if (! young && from + (lastMark < 0 ? 0.0 : at (from).period) > (double) (known * hop) && (double) (n - delay) < from - 2.6 * longest) return;
            const Wish w = at (from);
            if (! w.voiced || w.period <= 0)
            {
                if (lastMark >= 0) { lastMark = -1; return; }
                return;
            }
            if (lastMark < 0)
            {
                // a new note: the mark goes to the strongest moment of its first period
                const double a = std::max (0.0, (double) n - back); double best = -1, place = a; const int len = (int) w.period; if (a + len + 4 >= (double) n) return;
                double e = 0; const int sm = std::max (2, len / 12); for (int i = -sm; i < len + sm; ++i) { const float x = mono[(size_t) (((int64_t) a + i) & mask)], o = mono[(size_t) (((int64_t) a + i - 2 * sm) & mask)]; e += (double) x * x - (double) o * o; if (i >= sm && e > best) { best = e; place = a + i - sm; } }
                lastMark = place; age = 0; for (auto& q : turns) std::fill (q.begin(), q.end(), 0.0f); add (place, w.period); continue;
            }
            const double T = w.period, guess = lastMark + T; const int reach = std::max (2, (int) (T * 0.18)), half = (int) (T * 0.5);
            if (guess + reach + half + 4 >= (double) n) return;
            const int64_t a0 = (int64_t) std::floor (lastMark); const double frac = lastMark - (double) a0; const int64_t g0 = (int64_t) std::floor (guess - frac);
            double best = -1e30; int bk = 0; double ea = 1e-12; for (int i = -half; i <= half; i += stride) { const double x = mono[(size_t) ((a0 + i) & mask)]; ea += x * x; }
            const auto look = [&] (int k) { double s = 0, eb = 1e-12; for (int i = -half; i <= half; i += stride) { const double y = mono[(size_t) ((g0 + k + i) & mask)]; s += (double) mono[(size_t) ((a0 + i) & mask)] * y; eb += y * y; } const double far = (double) k / (T * 0.18); return s / std::sqrt (ea * eb) - 0.12 * far * far; };
            for (int k = -reach; k <= reach; k += stride) { const double v = look (k); corr[(size_t) (k + reach)] = (float) v; if (v > best) { best = v; bk = k; } }
            if (stride > 1) { const int mid = bk; for (int k = std::max (-reach, mid - stride - 1); k <= std::min (reach, mid + stride + 1); ++k) { const double v = look (k); corr[(size_t) (k + reach)] = (float) v; if (v > best) { best = v; bk = k; } } }
            double q = 0; if (bk > -reach && bk < reach) { const double l = corr[(size_t) (bk + reach - 1)], r = corr[(size_t) (bk + reach + 1)]; q = std::clamp (0.5 * (l - r) / (l - 2 * best + r - 1e-12), -0.5, 0.5); }
            double to = (double) (g0 + bk) + frac + q; if (best < 0.2) to = guess;
            // on a short period the Listener knows its length better than one look, on a long one the look is the better
            if (std::abs (to - guess) < 0.04 * T) to = guess + std::clamp (T / (0.007 * fs), 0.25, 0.9) * (to - guess);
            // the piece should sit on the strong part of its period, where neighbouring partials agree: go there slowly, faster while the note is young
            {
                const double part = centre (to, T); const double most = (age < 6 ? 0.03 : 0.0025) * T; if (std::abs (part) > 0.04 * T) to += std::clamp (0.25 * part, -most, most); ++age;
            }
            marks[(size_t) ((markCount - 1) % (int64_t) marks.size())].period = to - lastMark; add (to, to - lastMark); lastMark = to;
        }
    }
    /** How far the middle of the sound's power within one period is from `place` (on the circle of the period). */
    double centre (double place, double T) const noexcept
    {
        const int len = std::max (8, (int) T); double re = 0, im = 0; const int64_t a = (int64_t) std::lround (place) - len / 2;
        const double cw = std::cos (2 * pi / len), sw = std::sin (2 * pi / len); double c = -1, s = 0;      // angle -pi at the first sample
        for (int i = 0; i < len; ++i) { const double x = mono[(size_t) ((a + i) & mask)], p = x * x; re += p * c; im += p * s; const double c2 = c * cw - s * sw; s = s * cw + c * sw; c = c2; }
        return std::atan2 (im, re) / (2 * pi) * T;
    }
    void add (double place, double T) noexcept { marks[(size_t) (markCount % (int64_t) marks.size())] = { place, T, -1 }; ++markCount; }

    /** Starts every piece whose window opens now. Where the next one lies is decided as late as it can be, when the period after this one is known. */
    void start (double t) noexcept
    {
        for (int guard = 0; guard < 6; ++guard)
        {
            const Wish w = at (last); const Mark* m = w.voiced && w.period > 0 ? nearest (last) : nullptr;
            const double T = m ? m->period : free, cents = m ? centsAt (last + 0.5 * T) : 0.0, way = m ? std::pow (2.0, cents / 1200.0) : 1.0;      // the distance to the next piece is the pitch halfway there
            // the window of the next piece opens before its place: earlier the shorter the step and the longer the window
            if (t < last + T / way - (m && ! young ? 1.5 : 1.0) * T / (m ? std::clamp (w.throat, 0.5, 1.0) : 1.0) - 2) { due = t + 4; return; }
            const double place = m && std::abs (cents) < 0.01 ? follow (m, last) : last + T / way;
            const Wish v = at (place); const Mark* k = v.voiced && v.period > 0 ? nearest (place) : nullptr; const double size = k ? std::clamp (v.throat, 0.5, 2.0) : 1.0; double where = place;
            if (k && std::abs (v.cents) < 0.01 && std::abs (k->at - place) < 0.5 * k->period) where = k->at;      // nothing to change: exactly where it was
            if (k && ! m && std::abs (k->at - place) < 0.5 * k->period && k->at > last + 1) where = k->at;      // the first piece of a note lies where it came from: nothing jumps
            if (where <= last + 1) where = last + (m ? T : free);
            Grain* g = nullptr; for (auto& q : grains) if (! q.on) { g = &q; break; }
            if (g == nullptr) { due = t + 4; return; }
            const bool same = ! k || (std::abs (v.cents) < 0.01 && std::abs (size - 1) < 1e-4);
            *g = { true, where, same ? where : k->at, (k ? k->period : free) / size, size };
            // the first and the last piece of a note stay plain: a made one is longer than its own period
            const bool inside = k && at (place - 1.3 * k->period).voiced && at (place + 1.3 * k->period).voiced;
            if (! young && ! same && inside && 2 * k->period * gridShare <= largest)
            {
                auto& mk = const_cast<Mark&> (*k); if (mk.cell < 0) { mk.cell = nextCell; nextCell = (nextCell + 1) % cells; for (auto& o : marks) if (&o != &mk && o.cell == mk.cell) o.cell = -1; make (mk); }
                const double apart = k->period / std::pow (2.0, v.cents / 1200.0); g->cell = mk.cell; g->gain = apart / k->period * size; g->reach = apart; g->half = k->period * 1.5 / size;
            }
            last = where;
        }
    }
    /** One period made anew from its partials: between two of them the mouth is guessed (straight in dB, phase along the shorter way), where a plain
        piece would let them cancel each other. */
    void make (const Mark& m) noexcept
    {
        const double P = m.period; int M = 64; while (M < 2 * P * gridShare) M <<= 1;      // at high rates the grid stops where hearing does
        M = std::min (M, largest); const int N = 2 * M, K = std::min ({ M / 4 - 2, (int) (P * 0.48), (int) (P * 21500.0 / fs), (int) amp.size() - 4 }); auto& q = cell[(size_t) m.cell]; q.size = N; q.rate = N / (4 * P);
        for (int c = 0; c < chans; ++c)
        {
            // two periods with a smooth window, on a grid that fits them: partial k lies on line 2k, its neighbours on the zeros of the window
            for (int i = 0; i < M; ++i) { const double u = 2.0 * i / M - 1; work[(size_t) i] = { (float) ((0.5 + 0.5 * std::cos (pi * u)) * fromRing (c, m.at + u * P)), 0.0f }; }
            fft.run (work.data(), M, false);
            double last2 = 0; for (int k = 1; k <= K; ++k)
            {
                const auto v = work[(size_t) (2 * k)]; amp[(size_t) k] = (float) (20 * std::log10 (4.0 * std::abs (v) / M + 1e-9));
                double ph = std::arg (v); if (k > 1) { double turn = std::remainder (ph - last2, 2 * pi); auto& was = turns[(size_t) c][(size_t) k]; if (std::abs (turn) > 1.9 && std::abs (turn - was) > pi) turn += turn < was ? 2 * pi : -2 * pi; was = (float) turn; ph = last2 + turn; } angle[(size_t) k] = (float) ph; last2 = ph;      // the grid begins two half turns before the middle: every partial starts at a whole turn
            }
            amp[0] = amp[1] - 6; angle[0] = 2 * angle[1] - angle[2]; amp[(size_t) K + 1] = amp[(size_t) K]; angle[(size_t) K + 1] = angle[(size_t) K];
            // four lines for every partial: a shape that lives for four periods and has died away after one and a half
            std::fill (work.begin(), work.begin() + N, std::complex<float>()); auto& out = q.x[(size_t) c];
            for (int l = 2; l <= 4 * K; ++l)
            {
                const double x = l / 4.0; const int i = (int) x; const double f = x - i, g = std::exp (0.11512925465 * (amp[(size_t) i] + f * (amp[(size_t) i + 1] - amp[(size_t) i]))) / 8, th = angle[(size_t) i] + f * (angle[(size_t) i + 1] - angle[(size_t) i]);
                work[(size_t) l] = { (float) (g * std::cos (th)), (float) (g * std::sin (th)) }; work[(size_t) (N - l)] = std::conj (work[(size_t) l]);
            }
            fft.run (work.data(), N, true);
            std::fill (out.begin() + N + Between::points, out.begin() + N + 2 * Between::points, 0.0f);
            // faded at its ends, and nothing left at 0 Hz: a piece with a mean would move the whole sound up or down
            const int L = std::min (N / 2 - 1, (int) (1.5 * P * q.rate) + 1), flat = (int) (0.9 * P * q.rate); double mean = 0, weight = 1e-9; std::fill (out.begin() + Between::points, out.begin() + N + Between::points, 0.0f);
            const auto edge = [&] (int at) { const int n = std::abs (at); return n < flat ? 1.0f : (float) (0.5 + 0.5 * cosine[(size_t) std::min (1024, (int) (1024.0 * (n - flat) / std::max (1, L - flat)))]); };
            float* mid = out.data() + N / 2 + Between::points;
            for (int at = -L; at <= L; ++at) { const float e = edge (at), v = work[(size_t) (at < 0 ? at + N : at)].real() * e; mid[at] = v; mean += v; weight += e; }
            mean /= weight; for (int at = -L; at <= L; ++at) mid[at] -= (float) mean * edge (at);
        }
    }
    double follow (const Mark* m, double from) const noexcept { return m->at + m->period > from + 1 ? m->at + m->period : from + m->period; }
    const Mark* nearest (double t) const noexcept
    {
        const Mark* best = nullptr; double d = 1e9; const int64_t to = markCount, from = std::max<int64_t> (0, to - (int64_t) marks.size());
        for (int64_t i = to - 1; i >= from; --i) { const auto& m = marks[(size_t) (i % (int64_t) marks.size())]; const double e = std::abs (m.at - t); if (e < d) { d = e; best = &m; } else if (m.at < t) break; }
        return best != nullptr && d < best->period * 1.2 ? best : nullptr;
    }

    double fs = 48000, longest = 800, last = -1, due = 0, free = 240, lastMark = -1; int chans = 1, age = 0, delay = 0, mask = 0, hop = 120; int64_t n = 0, lastHop = -1, known = -1, markCount = 0; double back = 0;
    std::array<std::vector<float>, 2> ring; std::vector<float> mono, corr; std::vector<Wish> wishes; std::array<Mark, 256> marks {}; std::array<Grain, 24> grains {}; std::vector<Cell> cell = std::vector<Cell> (cells); int nextCell = 0, largest = 1024, stride = 1; double gridShare = 1; std::array<float, 1025> cosine {}; std::vector<float> amp, angle; std::array<std::vector<float>, 2> turns; std::vector<std::complex<float>> work; Fft fft; Between between;
};
} // namespace aemon::tune
