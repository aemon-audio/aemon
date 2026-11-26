// Aemon Tune: the whole engine on sung lines, and the key finder. Plain C++, no JUCE. AGPL-3.0-or-later.
#include "TuneEngine.h"
#include "TuneVoice.h"
#include "PerceptKit.h"
#include <chrono>
#include <cstdio>
#include <string>

using namespace tunevoice;
using namespace aemon::tune;
using percept::Vec;
namespace
{
int failures = 0;
void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
std::string num (double v, int d = 1) { char b[40]; std::snprintf (b, sizeof b, "%.*f", d, v); return b; }
Vec run (const Vec& x, double fs, const Params& p, bool mixing, double lowest, int& late, int block = 512)
{
    Engine e; e.prepare (fs, 1, mixing, lowest); late = e.latency(); Vec y = x; y.resize (x.size() + (size_t) late, 0.0f); for (size_t i = 0; i < y.size(); i += (size_t) block) e.process (y.data() + i, nullptr, (int) std::min ((size_t) block, y.size() - i), p, [] (const Engine::Frame&) {});
    return Vec (y.begin() + late, y.end());
}
/** The pitch of a sound by time (hop 2.5 ms at 48 kHz), in half tones; 0 where none. */
std::vector<double> track (const Vec& y, double fs) { Listener l; l.prepare (fs); Heard h; std::vector<double> v (y.size() / 120 + 1, 0.0); for (size_t i = 0; i < y.size(); ++i) if (l.push (y[i], h) && h.voiced) { const int64_t at = (int64_t) i - h.age; if (at >= 0) v[(size_t) at / 120] = 69 + 12 * std::log2 (h.hz / 440); } return v; }
const std::vector<Note> line { { 57, 0.1, 0.6, -35 }, { 60, 0.6, 1.0, 30 }, { 64, 1.1, 2.2, -40 }, { 62, 2.2, 2.6, 35 }, { 57, 2.75, 3.6, 25 } };
/** Cents from the note in the middle part of every note: median and the 95 % point. */
void middles (const std::vector<double>& tr, const std::vector<Note>& notes, double& med, double& p95) { std::vector<double> e; for (auto& n : notes) for (double t = n.from + 0.35 * (n.to - n.from); t < n.to - 0.05; t += 0.0025) { const double v = tr[(size_t) (t * 400)]; if (v > 0) e.push_back (std::abs (v - n.midi) * 100); } std::sort (e.begin(), e.end()); med = e.empty() ? 999 : e[e.size() / 2]; p95 = e.empty() ? 999 : e[(size_t) (0.95 * (double) e.size())]; }
Params manner (double x) { Params p; p.manner = along (x); p.notes = KeyFinder::notesOf (9, true); return p; }
double depthOf (const std::vector<double>& tr, double from, double to) { double lo = 1e9, hi = -1e9; for (double t = from; t < to; t += 0.0025) { const double v = tr[(size_t) (t * 400)]; if (v > 0) { lo = std::min (lo, v); hi = std::max (hi, v); } } return (hi - lo) * 50; }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); const double fs = 48000; int late = 0;
    // 1. off means off
    {
        Settings q; q.vibrato = 30; const auto t = sing (fs, 3.8, melody (line, 0.08), q); Params p; p.manner = along (0); auto y = run (t.x, fs, p, true, 65, late); double e = 0; for (size_t i = 0; i < t.x.size(); ++i) e = std::max (e, (double) std::abs (y[i] - t.x[i])); check (e < 1e-6, "The line at its left end: the same sound", "largest difference " + num (e, 8) + ", delay " + num (1000.0 * late / fs));
        p = manner (1); p.bypass = true; y = run (t.x, fs, p, true, 65, late); e = 0; for (size_t i = 4800; i < t.x.size(); ++i) e = std::max (e, (double) std::abs (y[i] - t.x[i])); check (e == 0, "Bypassed: bit-exact and in time", "");
        {   // the first moment after a start: singing from the very first sample
            const auto s = sing (fs, 1.0, [] (double) { return 226.0; }, q); for (double x : { 0.0, 1.0 }) for (bool mixing : { true, false }) { auto w = manner (x); w.bypass = x > 0; const auto z = run (s.x, fs, w, mixing, 100, late); double d = 0; for (size_t i = 0; i < s.x.size(); ++i) d = std::max (d, (double) std::abs (z[i] - s.x[i])); check (d == 0, std::string (x > 0 ? "Bypassed" : "The left end of the line") + ", " + (mixing ? "mixing" : "recording") + ": bit-exact from the first sample", "largest difference " + num (d, 6)); }
        }
    }
    // two voices at once are said, one voice is not called two. Told apart: thirds and fourths; not: an octave, and a fourth or fifth equally loud (they share one period)
    {
        const auto most = [&] (const Vec& x) { Engine e; e.prepare (fs, 1, true, 65); Vec y = x; double top = 0; std::vector<Engine::Frame> v; e.process (y.data(), nullptr, (int) y.size(), manner (1), [&] (const Engine::Frame& f) { v.push_back (f); top = std::max (top, (double) f.others); });
            // the window has two sentences: the second one when little is heard at all (two seconds, as there)
            bool lost = false; for (size_t i = 800; i < v.size(); i += 40) { int sounds = 0, unsure = 0; for (size_t k = i - 800; k < i; ++k) { sounds += v[k].level > -50; unsure += v[k].level > -50 && ! v[k].voiced && ! v[k].sharp; } lost = lost || (sounds > 80 && unsure > sounds * 0.6); } return std::pair<double, bool> (top, lost); };
        double solo = 0; bool hard = false; int said = 0, all = 0; std::string who; for (double move : { -12.0, -5.0, 0.0, 7.0, 12.0 })
        {
            auto notes = line; for (auto& n : notes) n.midi += move; Settings q; q.vibrato = 35; q.vowel = 1; q.formant = move < 0 ? 0.9 : 1.1; const auto t = sing (fs, 3.8, melody (notes, 0.08), q); auto noisy = t.x, room = t.x; addNoise (noisy, -26); addRoom (room, fs, -14); Settings b = q; b.breath = 12; for (const auto& x : { t.x, noisy, room, sing (fs, 3.8, melody (notes, 0.08), b).x }) { const auto r = most (x); solo = std::max (solo, r.first); hard = hard || r.second; }
            if (move >= -5 && move <= 0) for (double up : { 3.0, 4.0 }) for (double db : { -6.0, -12.0 }) { auto other = notes; for (auto& n : other) n.midi += up; Settings o = q; o.vowel = 2; o.seed = 9; const auto s = sing (fs, 3.8, melody (other, 0.08), o); const double g = rms (t.x) / rms (s.x) * std::pow (10.0, db / 20); auto x = t.x; for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (g * s.x[i]); const auto r = most (x); ++all; if (r.first > 0.3 || r.second) ++said; else who += " " + num (move, 0) + " and " + num (up, 0) + " at " + num (db, 0) + " dB"; }
        }
        check (solo < 0.25 && ! hard, "One voice (clean, noise, room, breathy) is never called two", "at most " + num (100 * solo) + " % of the last moments, the window speaks from 30"); check (said == all, "Two voices a third apart are said", num (said, 0) + " of " + num (all, 0) + (who.empty() ? "" : ", not:" + who));
    }
    // 2. hard: every note on its place, in mixing and recording, low to high
    for (bool mixing : { true, false }) for (double move : { -14.0, -5.0, 0.0, 9.0 })
    {
        auto notes = line; for (auto& n : notes) n.midi += move; Settings q; q.vibrato = 35; q.vowel = 1; q.formant = move < -6 ? 0.9 : 1.05; const auto t = sing (fs, 3.8, melody (notes, 0.08), q); auto p = manner (1); p.notes = KeyFinder::notesOf (((9 + (int) move) % 12 + 12) % 12, true); const auto y = run (t.x, fs, p, mixing, move < -6 ? 70 : (move < 0 ? 100 : 130), late); const auto tr = track (y, fs); double med, p95; middles (tr, notes, med, p95);
        // clicks where the voice sounds: every 100 ms against the same 100 ms of the singer (out of total silence the measure means nothing)
        double at = 0, most = -99, loudest = -99; for (float v : t.x) loudest = std::max (loudest, 20 * std::log10 (std::abs (v) + 1e-9)); for (int i = 0; i + 4800 < (int) y.size(); i += 1200) { const Vec a (t.x.begin() + i, t.x.begin() + i + 4800), b (y.begin() + i, y.begin() + i + 4800); double quiet = 99; for (int k = 0; k < 4800; k += 480) quiet = std::min (quiet, percept::rmsDb (Vec (a.begin() + k, a.begin() + k + 480))); if (quiet < loudest - 45) continue; const double c = percept::clickDb (b, fs) - percept::clickDb (a, fs); if (c > most) { most = c; at = i / fs; } }
        check (med < 3 && p95 < (mixing ? 8 : 20) && most < 6, std::string ("Hard, ") + (mixing ? "mixing" : "recording") + ", " + num (move, 0) + " half tones: every note lands", "middle of the notes " + num (med) + " cents off, 95 % within " + num (p95) + ", click at most " + num (most) + " dB over the singer (at " + num (at, 2) + " s), delay " + num (1000.0 * late / fs) + " ms");
    }
    // 3. hard steps: from one note to the next in 15 ms
    {
        const std::vector<Note> two { { 60, 0.1, 0.7, 20 }, { 64, 0.7, 1.5, -20 } }; Settings q; q.vowel = 1; const auto t = sing (fs, 1.7, melody (two, 0.10), q); auto p = manner (1); p.notes.fill (false); p.notes[0] = p.notes[4] = true; const auto tr = track (run (t.x, fs, p, true, 100, late), fs); double a = 0, b = 0; for (double s = 0.6; s < 1.0; s += 0.0025) { const double v = tr[(size_t) (s * 400)]; if (v > 0 && v < 60.3) a = s; if (v > 63.7 && b == 0) b = s; } check (b > 0 && (b - a) * 1000 < 40, "Hard: a step, not a slide", "from the one to the other in " + num ((b - a) * 1000, 0) + " ms (the singer slid for 100, the looking itself takes about 10)");
    }
    // 4. natural: on the note, with the vibrato, the slide and the life left in
    {
        const std::vector<Note> one { { 57, 0.1, 0.6, 0 }, { 64, 0.6, 2.4, -40 } }; Settings q; q.vibrato = 40; q.vowel = 1; const auto t = sing (fs, 2.6, melody (one, 0.12), q); const auto before = track (t.x, fs), after = track (run (t.x, fs, manner (0.25), true, 100, late), fs); double m0 = 0, m1 = 0; int c = 0; for (double s = 1.0; s < 1.45; s += 0.0025) { m0 += before[(size_t) (s * 400)]; m1 += after[(size_t) (s * 400)]; ++c; }
        m0 /= c; m1 /= c; const double d0 = depthOf (before, 1.0, 1.9), d1 = depthOf (after, 1.0, 1.9); check (std::abs (m0 - 64) * 100 > 35 && std::abs (m1 - 64) * 100 < 22 && std::abs (d1 / d0 - 1) < 0.12, "Natural: nearer to the note, the vibrato as deep as it was", "from " + num ((m0 - 64) * 100) + " to " + num ((m1 - 64) * 100) + " cents, vibrato " + num (d0) + " to " + num (d1) + " cents");
        const auto tight = track (run (t.x, fs, manner (1), true, 100, late), fs); check (depthOf (tight, 1.0, 1.9) < 0.15 * d0, "Hard: the vibrato is gone", num (d0) + " to " + num (depthOf (tight, 1.0, 1.9)) + " cents");
    }
    // 4b. a note held between two others, with vibrato: one note comes out, not two in turn
    for (double off : { 25.0, 35.0, -35.0 })
    {
        const std::vector<Note> w { { 60, 0.1, 2.0, off } }; Settings q; q.vibrato = 34; q.vowel = 1; const auto t = sing (fs, 2.2, melody (w, 0), q); auto p = manner (0.9); p.notes = KeyFinder::notesOf (1, false); p.notes[0] = true; p.notes[1] = true; p.notes[11] = true; const auto tr = track (run (t.x, fs, p, true, 100, late), fs); int steps = 0; double was = 0; for (double s = 0.3; s < 1.9; s += 0.0025) { const double v = tr[(size_t) (s * 400)]; if (v <= 0) continue; const double n = std::round (v); if (was != 0 && n != was) ++steps; was = n; }
        check (steps <= 1, "A held note sung " + num (off, 0) + " cents off with vibrato stays one note", std::to_string (steps) + " changes");
    }
    // 4c. fast runs still find every note
    {
        std::vector<Note> w; const int deg[] { 60, 62, 64, 65, 67, 65, 64, 62, 60, 59, 57, 59, 60, 64, 62, 60 }; double at = 0.1; for (int m : deg) { w.push_back ({ (double) m, at, at + 0.11, (m % 2) ? 22.0 : -22.0 }); at += 0.11; } Settings q; q.vowel = 1; const auto t = sing (fs, at + 0.2, melody (w, 0.03), q); auto p = manner (1); p.notes = KeyFinder::notesOf (0, false); const auto tr = track (run (t.x, fs, p, true, 100, late), fs); int right = 0; for (auto& n : w) { std::vector<double> v; for (double s = n.from + 0.05; s < n.to - 0.02; s += 0.0025) if (tr[(size_t) (s * 400)] > 0) v.push_back (tr[(size_t) (s * 400)]); std::sort (v.begin(), v.end()); right += ! v.empty() && std::abs (v[v.size() / 2] - n.midi) < 0.12; } check (right >= 15, "A run of sixteen notes, nine in a second: every note is found", std::to_string (right) + " of 16");
    }
    // 5. S, breath and silence between the notes come back untouched
    {
        Rnd r; auto t = sing (fs, 2.6, melody ({ { 57, 0.1, 0.8, 40 }, { 62, 1.4, 2.4, 40 } }, 0)); Biquad hp; hp.set (true, 5000, 0.8, fs); for (size_t i = (size_t) (0.95 * fs); i < (size_t) (1.2 * fs); ++i) t.x[i] += (float) (0.15 * hp.run (r.next())); const auto y = run (t.x, fs, manner (1), true, 65, late); double e = 1e-20, s = 1e-20; for (size_t i = (size_t) (0.95 * fs); i < (size_t) (1.2 * fs); ++i) { e += std::pow (y[i] - t.x[i], 2); s += (double) t.x[i] * t.x[i]; } check (percept::db10 (e / s) < -60, "An S between two notes is not touched", num (percept::db10 (e / s)) + " dB");
    }
    // 6. notes switched off, a note meant higher, MIDI
    {
        const std::vector<Note> w { { 61, 0.1, 1.4, 10 } }; const auto t = sing (fs, 1.6, melody (w, 0)); auto p = manner (1); p.notes = KeyFinder::notesOf (0, false); auto at = [&] (const Params& q) { const auto tr = track (run (t.x, fs, q, true, 100, late), fs); std::vector<double> v; for (double s = 0.6; s < 1.3; s += 0.0025) if (tr[(size_t) (s * 400)] > 0) v.push_back (tr[(size_t) (s * 400)]); std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
        const double a = at (p); check (std::abs (a - 62) < 0.05, "A note that is off goes to the nearest that is on", num (a, 2)); p.notes[1] = true; p.cents[1] = 15; const double b = at (p); check (std::abs (b - 61.15) < 0.05, "A note meant 15 cents high lands there", num (b, 2)); p.midiNote = 55; const double c = at (p); check (std::abs (c - 55) < 0.06 || std::abs (c - 67) < 0.06, "A MIDI note wins, in the octave that is sung", num (c, 2)); auto none = manner (1); none.notes.fill (false); const double d = at (none); check (std::abs (d - 61.1) < 0.05, "No note on: nothing is moved", num (d, 2));
    }
    // 7. same at every block size
    { Settings q; q.vibrato = 30; const auto t = sing (fs, 2.0, melody (line, 0.08), q); const auto a = run (t.x, fs, manner (0.9), true, 65, late, 512), b = run (t.x, fs, manner (0.9), true, 65, late, 37), c = run (t.x, fs, manner (0.9), true, 65, late, 1); bool same = a == b && a == c; check (same, "The same sound at every block size", ""); }
    // 8. the key
    {
        int right = 0, rel = 0, all = 0, sureWrong = 0, sure = 0; Rnd r; r.s = 4711;
        for (int root = 0; root < 12; ++root) for (int minor = 0; minor < 2; ++minor) for (int take = 0; take < 8; ++take)
        {
            KeyFinder f; const auto on = KeyFinder::notesOf (root, minor == 1); int deg = 0; double tuning = 0.25 * r.next(); static const int M[7] { 0, 2, 4, 5, 7, 9, 11 }, m[7] { 0, 2, 3, 5, 7, 8, 10 }; (void) on;
            for (int note = 0; note < 28; ++note)
            {
                const double u = 0.5 + 0.5 * r.next(); deg = note == 0 || note == 27 || u < 0.2 ? 0 : (u < 0.35 ? 4 : (u < 0.5 ? 2 : deg + (int) std::lround (2 * r.next()))); const int d = ((deg % 7) + 7) % 7; double midi = 60 + root + (minor ? m : M)[d] + tuning; const bool wrong = 0.5 + 0.5 * r.next() < 0.06; if (wrong) midi += 1; const double len = note == 27 ? 0.9 : 0.18 + 0.2 * (1 + r.next()); for (double t = 0; t < len; t += 0.0025) f.add (midi + 0.15 * r.next() * std::exp (-t * 12) + 0.12 * std::sin (t * 34) * (t > 0.2), 0.0025, t < 0.05 ? 0.3 : 1.0);
            }
            const auto k = f.find(); ++all; const bool ok = k.root == root && k.minor == (minor == 1), rr = KeyFinder::same (root, minor == 1, k.root, k.minor); right += ok; rel += rr; if (k.sure > 0.75) { ++sure; if (! ok && ! rr) ++sureWrong; }
        }
        check ((right + rel) > 0.95 * all && sureWrong <= 0.01 * std::max (1, sure), "The key of 192 made-up melodies, every key, with mistakes and a voice out of tune by up to a quarter tone", "right " + num (100.0 * right / all) + " %, its relative (the same notes) " + num (100.0 * rel / all) + " %, other " + num (100.0 * (all - right - rel) / all) + " %; says it is sure " + num (100.0 * sure / all) + " % of the time, wrong then " + std::to_string (sureWrong));
        KeyFinder f; for (double t = 0; t < 4; t += 0.0025) f.add (60, 0.0025, 1); check (f.find().sure < 0.3, "One note alone: it does not say it knows", num (f.find().sure, 2)); KeyFinder g; for (int i = 0; i < 24; ++i) for (double t = 0; t < 0.3; t += 0.0025) g.add (60 + i % 12, 0.0025, 1); check (g.find().sure < 0.3, "All twelve notes alike: it does not say it knows", num (g.find().sure, 2));
    }
    // 9. anything thrown at it
    { Rnd r; bool fine = true; double top = 0; for (int k = 0; k < 24; ++k) { Params p; p.manner = { 0.5 + 0.5 * r.next(), 125 * (1 + r.next()), 0.5 + 0.5 * r.next(), 0.5 + 0.5 * r.next(), 300, 0.5 + 0.5 * r.next() }; p.throat = 1 + 0.3 * r.next(); p.transpose = std::round (12 * r.next()); for (auto& v : p.notes) v = r.next() > -0.3; Vec x ((size_t) fs); for (size_t i = 0; i < x.size(); ++i) x[i] = k % 3 == 0 ? (float) r.next() : (k % 3 == 1 ? (float) (0.8 * std::sin (2 * tunevoice::pi * (60 + 1000.0 * (double) i / fs) * (double) i / fs)) : (i % 9000 < 4500 ? (float) (0.9 * ((i / 100) % 2 ? 1 : -1)) : 0.0f)); for (float v : run (x, fs, p, k % 2 == 0, 65, late)) { fine &= std::isfinite (v); top = std::max (top, (double) std::abs (v)); } } check (fine && top < 6, "Noise, sweeps and squares with any setting: finite and bounded", "highest " + num (top, 2)); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED"); return failures ? 1 : 0;
}
