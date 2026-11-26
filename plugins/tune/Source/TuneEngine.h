// Aemon Tune: which note is meant, how it gets there, and the whole engine. Plain C++, no JUCE. AGPL-3.0-or-later.
#pragma once
#include "TuneShift.h"
#include <atomic>

namespace aemon::tune
{
/** How a note is treated. All in plain units; Simple's one line sets all of them (see `along`). */
struct Manner
{
    double pull = 1;          // 0..1: how far to the note in the end
    double landMs = 0;        // how long the way there takes
    double slideKeep = 0;     // 0..1: how much of a slide into the note is left alone
    double letGo = 0;         // 0..1: how much of the pull a long note is given back
    double letGoMs = 240;     // after how long
    double vibratoKeep = 0;   // 0..1
};
/** Simple's line: 0 leaves the voice alone, 1 puts every note on its place at once. Five stops, straight between them. */
inline Manner along (double x)
{
    static const Manner stop[5] { { 0.35, 180, 1.0, 0.6, 200, 1.0 }, { 0.78, 100, 0.8, 0.35, 260, 1.0 }, { 0.90, 55, 0.5, 0.2, 320, 0.8 }, { 1.0, 18, 0.15, 0, 400, 0.35 }, { 1.0, 0, 0, 0, 400, 0 } };
    x = std::clamp (x, 0.0, 1.0); if (x < 0.02) { auto m = stop[0]; m.pull *= x / 0.02; return m; }
    const double p = x * 4; const int i = std::min (3, (int) p); const double f = p - i; const auto mix = [&] (double Manner::* q) { return stop[i].*q + f * (stop[i + 1].*q - stop[i].*q); };
    return { mix (&Manner::pull), mix (&Manner::landMs), mix (&Manner::slideKeep), mix (&Manner::letGo), mix (&Manner::letGoMs), mix (&Manner::vibratoKeep) };
}

struct Params
{
    Manner manner;
    std::array<bool, 12> notes { true, true, true, true, true, true, true, true, true, true, true, true };      // C = 0
    std::array<double, 12> cents {};       // a note meant a little high or low
    double reference = 440;                // Hz of A4
    double throat = 1;                     // 1.12: deeper (the voice of a larger throat is 1/1.12 in formants, see Engine)
    double transpose = 0;                  // half tones, whole numbers
    int midiNote = -1;                     // a held MIDI note: the target, whatever the key
    double mix = 1;                        // 1: only the tuned voice
    bool bypass = false;
};

/** Decides, hop by hop, which note is meant and how many cents to move. */
class Brain
{
public:
    struct Out { bool voiced = false; double sung = 0, target = 0, cents = 0; bool changed = false; };      // half tones (MIDI), cents
    void prepare (double hopSeconds) { dt = hopSeconds; reset(); }
    void reset() { note = -1; since = 0; gap = 1e9; centre = 0; swing = 0; slow = 0; want = -1; wantFor = 0; done = 0; }

    Out step (const Heard& h, const Params& p) noexcept
    {
        Out o; if (! h.voiced) { gap += dt; if (gap > 0.06) { note = -1; want = -1; } return o; }
        const double s = 69 + 12 * std::log2 (h.hz / p.reference); o.voiced = true; o.sung = s;
        const bool fresh = note < 0; gap = 0;
        // ---- which note
        const double n = choose (s, p);
        if (fresh) { note = n; since = 0; slow = s; speed = 0; o.changed = true; done = 0; }
        else if (n != note)
        {
            // another note has to be meant: clearly nearer, or nearer for a while. The slower the manner, the longer
            const double nearer = std::abs (s - note) - std::abs (s - n); if (n != want) { want = n; wantFor = 0; } wantFor += dt;
            // a held note swings: a small step out of it has to last longer than the swing stays out there, a clear one counts at once
            double wait = 0.010 + 0.0008 * p.manner.landMs * (nearer > 0.35 ? 0.25 : 1.0) + (since > 0.16 && nearer < 0.7 ? 0.075 : (nearer < 0.45 ? 0.02 : 0.0)); if (n == before && since < 0.30 && nearer < 0.5) wait = std::max (wait, 0.10);      // back where it just came from: a swing, until it stays there
            if ((nearer > 0.12 && wantFor > wait) || nearer > 1.1 || ! allowed (note, p)) { before = note; note = n; since = 0; o.changed = true; want = -1; }
        }
        else want = -1;
        if (! allowed (note, p) && p.midiNote < 0) { o.cents = 0; o.target = s; return o; }
        since += dt;
        // ---- the singer's own moves: where the voice rests, and what swings around it
        speed += (std::abs (s - last) / dt - speed) * (1 - std::exp (-dt / 0.03)); if (fresh) speed = 0; last = s;
        const double tau = o.changed ? 0.0 : std::min (0.11, 0.02 + since * 0.5); if (o.changed || tau <= 0) slow = s; else slow += (s - slow) * (1 - std::exp (-dt / tau));
        // over a held note the resting place is the mean of the last swing (a fifth of a second), so the swing itself is not mistaken for it
        if (o.changed || fresh) held = 0;
        line[(size_t) (held % (int) line.size())] = s; ++held; const int cycle = std::min ((int) line.size(), (int) std::lround (0.185 / dt)); double rest = slow;
        if (held >= cycle) { double m = 0; for (int i = 0; i < cycle; ++i) m += line[(size_t) ((held - 1 - i) % (int) line.size())]; rest = m / cycle; }
        const double swings = s - rest;
        // ---- how much is moved
        const double goal = note + (p.midiNote >= 0 ? 0.0 : p.cents[(size_t) (((int) std::lround (note) % 12 + 12) % 12)] / 100.0);
        const double settle = p.manner.landMs < 0.5 ? 1.0 : 1 - std::exp (-since * 1000 / (p.manner.landMs * 0.45)); double pull = p.manner.pull; if (p.manner.letGo > 0) pull *= 1 - p.manner.letGo * smooth ((since * 1000 - p.manner.letGoMs) / 220.0);
        const double keepV = p.manner.vibratoKeep * smooth ((since - 0.10) / 0.12);
        double full = goal - rest;      // what it takes to land: the swing is treated apart, and at once
        const double slide = p.manner.slideKeep * (1 - smooth ((since - 0.05) / 0.12)); full *= 1 - slide * std::min (1.0, std::abs (s - goal) / 0.3);
        full *= pull;
        if (p.manner.landMs < 0.5) done = full; else done += (full - done) * (1 - std::exp (-dt * 1000 / (p.manner.landMs * 0.45)));
        o.target = goal; o.cents = (done - (1 - keepV) * swings * pull * settle) * 100 + p.transpose * 100; return o;
    }
private:
    static double smooth (double u) noexcept { u = std::clamp (u, 0.0, 1.0); return u * u * (3 - 2 * u); }
    static bool allowed (double n, const Params& p) noexcept { if (p.midiNote >= 0) return true; return p.notes[(size_t) (((int) std::lround (n) % 12 + 12) % 12)]; }
    double choose (double s, const Params& p) const noexcept
    {
        if (p.midiNote >= 0) { const double o = std::round ((s - p.midiNote) / 12); return p.midiNote + 12 * o; }      // the held note, in the octave that is sung
        double best = std::round (s), d = 1e9; for (int k = -7; k <= 7; ++k) { const double n = std::round (s) + k; if (! allowed (n, p)) continue; const double e = std::abs (s - n - p.cents[(size_t) (((int) n % 12 + 12) % 12)] / 100.0); if (e < d) { d = e; best = n; } }
        return best;
    }
    double dt = 0.0025, before = -1, note = -1, since = 0, gap = 1e9, centre = 0, swing = 0, slow = 0, want = -1, wantFor = 0, done = 0, last = 0, speed = 0; int held = 0; std::array<double, 96> line {};
};

/** Finds the key from what was sung: how long every note was rested on, measured from the voice's own tuning, against the notes of every key and
    where its weight usually lies. */
class KeyFinder
{
public:
    struct Key { int root = -1; bool minor = false; double sure = 0; double seconds = 0; double tuning = 0; int relative = -1; };
    void reset() { weight.fill (0); fine.fill (0); heard = 0; }
    /** One hop of a voice at rest or moving; `still` 0..1: how much it rests. */
    void add (double midi, double dt, double still) noexcept { const double c = std::fmod (std::fmod (midi, 12.0) + 12.0, 12.0); fine[(size_t) std::min (119, (int) (c * 10))] += dt * still; heard += dt; }
    Key find() const noexcept
    {
        Key k; k.seconds = heard; if (heard < 1.5) return k;
        // the tuning of the voice: where within the half tone its weight lies
        double re = 0, im = 0; for (int i = 0; i < 120; ++i) { re += fine[(size_t) i] * std::cos (2 * pi * (i + 0.5) / 10); im += fine[(size_t) i] * std::sin (2 * pi * (i + 0.5) / 10); }
        k.tuning = std::atan2 (im, re) / (2 * pi); std::array<double, 12> w {}; double sum = 1e-9;
        for (int i = 0; i < 120; ++i) { const double pos = (i + 0.5) / 10 - std::clamp (k.tuning, -0.35, 0.35); const int n = ((int) std::lround (pos) % 12 + 12) % 12; const double off = std::abs (pos - std::round (pos)); w[(size_t) n] += fine[(size_t) i] * std::max (0.0, 1 - 1.6 * off); }
        for (double v : w) sum += v;
        static const double major[12] { 6.0, 0.2, 3.4, 0.2, 4.6, 3.6, 0.3, 5.0, 0.3, 3.4, 0.4, 2.4 }, minor[12] { 6.0, 0.2, 3.2, 5.0, 0.2, 3.6, 0.3, 4.8, 3.0, 0.5, 3.4, 1.0 };
        double best = -1e9, second = -1e9; for (int r = 0; r < 12; ++r) for (int m = 0; m < 2; ++m)
        {
            const double* t = m ? minor : major; double mt = 0, mw = sum / 12; for (int i = 0; i < 12; ++i) mt += t[i] / 12; double a = 0, b = 0, c = 0; for (int i = 0; i < 12; ++i) { const double x = t[i] - mt, y = w[(size_t) ((i + r) % 12)] - mw; a += x * y; b += x * x; c += y * y; }
            const double s = a / std::sqrt (b * c + 1e-12); if (s > best) { if (! (k.root >= 0 && same (k.root, k.minor, r, m == 1))) second = best; best = s; k.root = r; k.minor = m == 1; } else if (s > second && ! same (k.root, k.minor, r, m == 1)) second = s;
        }
        // the best of the others, with the relative key left out: it has the same notes
        second = -1e9; for (int r = 0; r < 12; ++r) for (int m = 0; m < 2; ++m) { if ((r == k.root && (m == 1) == k.minor) || same (k.root, k.minor, r, m == 1)) continue; const double* t = m ? minor : major; double mt = 0, mw = sum / 12; for (int i = 0; i < 12; ++i) mt += t[i] / 12; double a = 0, b = 0, c = 0; for (int i = 0; i < 12; ++i) { const double x = t[i] - mt, y = w[(size_t) ((i + r) % 12)] - mw; a += x * y; b += x * x; c += y * y; } second = std::max (second, a / std::sqrt (b * c + 1e-12)); }
        int used = 0; for (double v : w) used += v > 0.03 * sum;
        k.sure = std::clamp ((best - second) / 0.22, 0.0, 1.0) * std::clamp (heard / 6.0, 0.0, 1.0) * std::clamp ((used - 2) / 3.0, 0.0, 1.0) * std::clamp (best / 0.75, 0.0, 1.0);
        k.relative = k.minor ? (k.root + 3) % 12 : (k.root + 9) % 12; return k;
    }
    static bool same (int r1, bool m1, int r2, bool m2) noexcept { return m1 != m2 && (m1 ? (r1 + 3) % 12 == r2 : (r1 + 9) % 12 == r2); }      // relative keys
    static std::array<bool, 12> notesOf (int root, bool minor) noexcept { std::array<bool, 12> n {}; static const int M[7] { 0, 2, 4, 5, 7, 9, 11 }, m[7] { 0, 2, 3, 5, 7, 8, 10 }; for (int i = 0; i < 7; ++i) n[(size_t) ((root + (minor ? m : M)[i]) % 12)] = true; return n; }
private:
    std::array<double, 12> weight {}; std::array<double, 120> fine {}; double heard = 0;
};

/** The whole of it: listens, decides, moves. Stereo or mono, any block. */
class Engine
{
public:
    struct Frame { float sung = 0, out = 0, target = 0, level = -120; bool voiced = false, sharp = false; float sure = 0, others = 0; };      // others: how much of the last second and a half had a second voice in it

    /** mixing: the surest answer and the best sound; recording: the least delay. lowest: the lowest voice in Hz. */
    void prepare (double rate, int channels, bool mixing, double lowestHz)
    {
        fs = rate; chans = channels; listener.patience = mixing ? 1 : 0; listener.lowest = lowestHz; listener.prepare (fs); hop = listener.hopLength();
        shifter.young = ! mixing; shifter.prepare (fs, lowestHz, channels); brain.prepare ((double) hop / fs); finder.reset(); n = 0; gapHops = 1000; others = 0; last = {};
        int s = 64; while (s < shifter.latency() + 8) s <<= 1;
        for (auto& d : dry) d.assign ((size_t) s, 0.0f);
        dryMask = s - 1; mix = -1;
    }
    int latency() const { return shifter.latency(); }
    /** Forgets what was heard; nothing is allocated. */
    void restart() noexcept { shifter.restart(); listener.restart(); brain.reset(); n = 0; gapHops = 1000; others = 0; last = {}; for (auto& d : dry) std::fill (d.begin(), d.end(), 0.0f); }
    KeyFinder finder;

    /** In place. Frames: one every hop, the caller's ring. */
    template <typename F> void process (float* L, float* R, int count, const Params& p, F&& frame) noexcept
    {
        const float k = (float) (1 - std::exp (-1 / (0.006 * fs)));
        for (int i = 0; i < count; ++i)
        {
            const float in[2] { L[i], R != nullptr ? R[i] : L[i] }; Heard h; float o[2] {};
            for (int c = 0; c < chans; ++c) dry[(size_t) c][(size_t) (n & dryMask)] = in[c];
            if (listener.push (chans > 1 ? 0.5f * (in[0] + in[1]) : in[0], h)) decide (h, p, frame);
            shifter.run (in, o);
            const float mixTo = p.bypass || (p.manner.pull == 0 && p.throat == 1 && p.transpose == 0) ? 0.0f : (float) p.mix;      // nothing wanted: the sound itself
            if (mix < 0) mix = mixTo;
            mix += k * (mixTo - mix); if (std::abs (mix - mixTo) < 1e-4f) mix = mixTo;
            const int64_t at = n - shifter.latency(); for (int c = 0; c < chans; ++c) { const float d = at < 0 ? 0.0f : dry[(size_t) c][(size_t) (at & dryMask)]; const float v = mix == 1.0f ? o[c] : (mix == 0.0f ? d : d + mix * (o[c] - d)); if (c == 0) L[i] = v; else if (R != nullptr) R[i] = v; }
            ++n;
        }
    }
private:
    template <typename F> void decide (Heard h, const Params& p, F&& frame) noexcept
    {
        const int64_t at = std::max<int64_t> (0, n - h.age);
        const auto b = brain.step (h, p); const double throat = 1.0 / std::clamp (p.throat, 0.5, 2.0);
        Shifter::Wish w { h.voiced, h.voiced ? fs / h.hz : 0, b.cents, throat };
        if (b.voiced)
        {
            // the first moments of a note were not sure yet: they are the same note
            if (gapHops > 2) shifter.before (at, w, (int) (0.03 * fs));
            gapHops = 0; last = w; shifter.wish (at, w); finder.add (b.sung, (double) hop / fs, std::exp (-std::pow ((b.sung - lastSung) / (0.06), 2)));
            lastSung = b.sung;
        }
        else
        {
            ++gapHops; const int64_t now = std::max<int64_t> (0, n - (int64_t) (last.period > 0 ? last.period * 1.2 : 0.01 * fs));
            shifter.wish (now, gapHops * hop < (int) (0.012 * fs) && last.period > 0 ? last : Shifter::Wish {});
        }
        if (h.voiced && (++turn & 3) == 0) { double left = 0; const double c = listener.company (h.hz, left); others += ((c > 0.7 && left > 0.01 ? 1.0 : 0.0) - others) * (1 - std::exp (-4.0 * hop / (1.5 * fs))); }
        Frame f; f.others = (float) others; f.voiced = b.voiced; f.sung = (float) b.sung; f.out = (float) (b.sung + b.cents / 100); f.target = (float) b.target; f.level = (float) h.level; f.sharp = h.sharp; f.sure = (float) h.sure; frame (f);
    }
    double fs = 48000, lastSung = 0, others = 0; int turn = 0, chans = 2, hop = 120, dryMask = 63, gapHops = 1000; int64_t n = 0; float mix = 1; Shifter::Wish last; Listener listener; Brain brain; Shifter shifter; std::array<std::vector<float>, 2> dry;
};
} // namespace aemon::tune
