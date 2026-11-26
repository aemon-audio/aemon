// Aemon Tune engine tests. Plain C++, no JUCE. AGPL-3.0-or-later.
#include "TunePitch.h"
#include "TuneVoice.h"
#include <chrono>
#include <cstdio>
#include <string>

using namespace tunevoice;
namespace
{
int failures = 0;
void check (bool ok, const std::string& name, const std::string& detail) { std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str()); if (! ok) ++failures; }
std::string num (double v, int d = 1) { char b[40]; std::snprintf (b, sizeof b, "%.*f", d, v); return b; }

struct Score { double cents = 5, within = 0, octave = 0, wrong = 0, heard = 0, onset = 0, p95 = 0; int n = 0; };
/** Compares what is heard with what was sung, at the moment it speaks about. The first 40 ms of every note count for `heard`, not for `within`. */
Score score (const Take& t, double fs, double cents = 5, double patience = 0)
{
    aemon::tune::Listener l; l.patience = patience; l.prepare (fs); aemon::tune::Heard h; Score s; int sung = 0, good = 0, oct = 0, far = 0, seen = 0, judged = 0; double on = -1, worstOn = 0; bool waiting = false; std::vector<double> errs;
    for (size_t i = 0; i < t.x.size(); ++i)
    {
        if (i > 0 && t.hz[i] > 0 && t.hz[i - 1] == 0) { on = (double) i / fs; waiting = true; }
        if (! l.push (t.x[i], h)) continue;
        const int at = (int) i - h.age; if (at < 0) continue;
        const double truth = t.hz[(size_t) at]; const bool steady = truth > 0 && at > (int) (0.04 * fs) && t.hz[(size_t) (at - (int) (0.04 * fs))] > 0 && t.hz[i] > 0;
        if (h.voiced && waiting) { worstOn = std::max (worstOn, (double) i / fs - on); waiting = false; }
        if (! steady) continue;
        ++sung; if (! h.voiced) continue;
        ++seen; const double e = 1200 * std::log2 (h.hz / truth); ++judged; errs.push_back (std::abs (e)); if (std::abs (e) <= cents) ++good; if (std::abs (e) > 400) ++oct; else if (std::abs (e) > 60) ++far;
    }
    std::sort (errs.begin(), errs.end()); s.cents = cents; s.n = sung; s.heard = sung ? (double) seen / sung : 0; s.within = judged ? (double) good / judged : 0; s.octave = judged ? (double) oct / judged : 1; s.wrong = judged ? (double) far / judged : 1; s.onset = worstOn * 1000; s.p95 = errs.empty() ? 999 : errs[(size_t) (0.95 * (double) errs.size())]; return s;
}
std::string say (const Score& s) { return "within " + num (s.cents, 0) + " cents " + num (100 * s.within) + " %, 95 % within " + num (s.p95) + " cents, wrong octave " + num (100 * s.octave, 2) + " %, heard " + num (100 * s.heard) + " %, first heard after " + num (s.onset, 0) + " ms"; }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); const double fs = 48000;
    // 1. steady notes, from a deep bass to a high whistle-like voice, five vowels
    for (double hz : { 70.0, 82.4, 98.0, 130.8, 164.8, 220.0, 311.1, 440.0, 587.3, 784.0, 1000.0 })
    {
        Score all; all.within = all.heard = 1; for (int v = 0; v < 5; ++v) { Settings q; q.vowel = v; q.formant = hz < 150 ? 0.9 : (hz > 300 ? 1.2 : 1); q.seed = (unsigned) (5 + v); const auto s = score (sing (fs, 1.2, [hz] (double t) { return t > 0.1 && t < 1.1 ? hz : 0.0; }, q), fs); all.within = std::min (all.within, s.within); all.heard = std::min (all.heard, s.heard); all.octave = std::max (all.octave, s.octave); all.onset = std::max (all.onset, s.onset); all.p95 = std::max (all.p95, s.p95); }
        check (all.within > 0.95 && all.octave < 0.005 && all.heard > 0.97, "Steady note, five vowels, " + num (hz, 0) + " Hz", say (all));
    }
    // 2. a melody, as it is really sung: vibrato, slides, off notes, and then made harder
    const std::vector<Note> tune { { 57, 0.1, 0.5, -30 }, { 60, 0.5, 0.85, 25 }, { 62, 0.95, 1.6, 10 }, { 60, 1.6, 1.8, -20 }, { 64, 1.95, 2.7, -35 }, { 65, 2.7, 2.95, 20 }, { 69, 3.1, 4.0, -15 }, { 57, 4.15, 4.6, 30 } };
    for (double move : { -12.0, -5.0, 0.0, 7.0, 12.0 })
    {
        auto notes = tune; for (auto& n : notes) n.midi += move; Settings q; q.vibrato = 35; q.vowel = 1; q.formant = move < 0 ? 0.9 : 1.1; const auto base = sing (fs, 4.8, melody (notes, 0.08), q);
        const std::string who = "Melody with vibrato and slides, " + num (move, 0) + " half tones";
        { const auto s = score (base, fs); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.95, who + ", clean", say (s)); }
        for (double patience : { 1.0, 0.0 })
        {
            // the plan's bar is for mixing; for recording (the youngest answer) twice as wide, and both are printed
            const double cents = patience > 0 ? 5 : 10; const std::string as = who + (patience > 0 ? ", mixing, " : ", recording, ");
            { auto t = base; addNoise (t.x, -26); const auto s = score (t, fs, cents, patience); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.93, as + "noise 26 dB down", say (s)); }
            { auto t = base; addRoom (t.x, fs, -14); const auto s = score (t, fs, cents, patience); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.90, as + "in a room", say (s)); }
            { auto t = base; addBeat (t.x, fs, -24); const auto s = score (t, fs, cents, patience); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.90, as + "beat 24 dB down", say (s)); }
            { Settings b = q; b.breath = 12; const auto s = score (sing (fs, 4.8, melody (notes, 0.08), b), fs, cents, patience); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.85, as + "breathy (12 dB)", say (s)); }
            { Settings b = q; b.breath = 3; const auto s = score (sing (fs, 4.8, melody (notes, 0.08), b), fs, cents, patience); check (s.octave * s.heard < 0.01, as + "almost whispered (3 dB): silent rather than wrong", say (s)); }
        }
        { auto t = base; for (auto& v : t.x) v *= 0.01f; const auto s = score (t, fs); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.95, who + ", 40 dB quieter", say (s)); }
    }
    // 3. what is not a note
    {
        auto share = [&] (const std::vector<float>& x) { aemon::tune::Listener l; l.prepare (fs); aemon::tune::Heard h; int n = 0, v = 0; for (float s : x) if (l.push (s, h)) { ++n; v += h.voiced; } return 100.0 * v / n; };
        Rnd r; std::vector<float> white, ess, breath, sil (96000, 0.0f); aemon::tune::Biquad hp, bp1, bp2; hp.set (true, 5000, 0.8, fs); bp1.set (true, 500, 0.7, fs); bp2.set (false, 2500, 0.7, fs);
        for (int i = 0; i < 96000; ++i) { const double v = r.next(); white.push_back ((float) (0.1 * v)); ess.push_back ((float) (0.2 * hp.run (v))); breath.push_back ((float) (0.05 * bp2.run (bp1.run (v)))); }
        check (share (white) < 0.5, "Noise is not a note", num (share (white), 2) + " % of the time"); check (share (ess) < 0.5, "An S is not a note", num (share (ess), 2) + " %"); check (share (breath) < 1.0, "A breath is not a note", num (share (breath), 2) + " %"); check (share (sil) == 0, "Silence is not a note", "");
        auto two = sing (fs, 2, [] (double) { return 220.0; }); Settings o; o.vowel = 2; o.seed = 9; const auto second = sing (fs, 2, [] (double) { return 293.7; }, o); for (size_t i = 0; i < two.x.size(); ++i) two.x[i] += second.x[i]; std::printf ("INFO  two voices at once: says it hears one note %.0f %% of the time\n", share (two.x));
    }
    // 4. rates, and the cost
    for (double r : { 44100.0, 88200.0, 96000.0, 192000.0 }) { Settings q; q.vibrato = 35; q.vowel = 1; const auto s = score (sing (r, 4.8, melody (tune, 0.08), q), r); check (s.within > 0.95 && s.octave < 0.005 && s.heard > 0.95, "The same at " + num (r / 1000) + " kHz", say (s)); }
    for (double r : { 48000.0, 192000.0 }) { const auto t = sing (r, 4.8, melody (tune, 0.08)); aemon::tune::Listener l; l.prepare (r); aemon::tune::Heard h; const auto a = std::chrono::steady_clock::now(); for (float v : t.x) l.push (v, h); const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - a).count(); check (ms / 4800 < 0.02, "Cost of listening at " + num (r / 1000, 0) + " kHz", num (100 * ms / 4800, 2) + " % of one core"); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED"); return failures ? 1 : 0;
}
