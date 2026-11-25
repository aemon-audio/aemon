// Aemon Multiband. AGPL-3.0-or-later.
#include "MbProcessor.h"
#include "MbEditor.h"

using namespace aemon::mb;
using mbp::N;

juce::AudioProcessorValueTreeState::ParameterLayout MbProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ms = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto ratio = [] (float v, int) { return v >= 19.95f ? String ("inf : 1") : String (v, v < 10 ? 2 : 1) + " : 1"; };
    const auto fl = AudioParameterFloatAttributes();
    auto f = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, auto text) { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, fl.withStringFromValueFunction (text))); };
    auto b = [&] (const String& id, const String& name, bool def) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "bands", 1 }, "Bands", StringArray { "2", "3", "4", "5" }, 2));
    const float splits[] { 120, 800, 5000, 10000 };
    for (int i = 0; i < N - 1; ++i) f (mbp::id ('x', i), "Split " + String (i + 1), logRange (20, 20000, 1), splits[i], hz);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "slope", 1 }, "Slope", StringArray { "24 dB", "48 dB" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "phase", 1 }, "Phase", StringArray { "Natural", "Straight" }, 0));
    for (int i = 0; i < N; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        f (mbp::id ('t', i), n + "Threshold", { -60, 0, 0.1f }, -24, dB);
        f (mbp::id ('r', i), n + "Ratio", logRange (1, 20, 0.01f), 1, ratio);
        f (mbp::id ('a', i), n + "Attack", logRange (0.1f, 200, 0.1f), 20, ms);
        f (mbp::id ('e', i), n + "Release", logRange (5, 1000, 1), 150, ms);
        f (mbp::id ('u', i), n + "Up Below", { -70, -10, 0.1f }, -45, dB);
        f (mbp::id ('v', i), n + "Up Ratio", logRange (1, 4, 0.01f), 1, ratio);
        f (mbp::id ('g', i), n + "Gain", { -12, 12, 0.1f }, 0, dB);
        b (mbp::id ('o', i), n + "On", true);
        b (mbp::id ('s', i), n + "Solo", false);
    }
    f ("amount", "Amount", { 0, 200, 1 }, 100, pc);
    f ("mix", "Mix", { 0, 100, 1 }, 100, pc);
    f ("knee", "Knee", { 0, 18, 0.1f }, 6, dB);
    f ("look", "Lookahead", { 0, 10, 0.1f }, 0, ms);
    f ("out", "Output", { -12, 12, 0.1f }, 0, dB);
    b ("auto", "Auto Level", false);
    b ("extSc", "External Sidechain", false);
    b ("bypass", "Bypass", false);
    return l;
}

const std::vector<mbp::Recipe>& mbp::recipes()
{
    using W = Want;
    static const std::vector<Recipe> v = []
    {
        std::vector<Recipe> r;
        auto add = [&r] (const char* cat, const char* name, const char* desc, int bands, std::array<float, N - 1> split, std::array<W, N> want) -> Recipe& { Recipe q; q.cat = cat; q.name = name; q.desc = desc; q.bands = bands; q.split = split; q.want = want; r.push_back (q); return r.back(); };
        const std::array<float, N - 1> s4 { 120, 800, 5000, 10000 };
        //            down ratio att  rel   up upR  gain
        add ("Goals", "Goal: tighter lows", "The low end stops swelling. About 5 dB off its loud parts, the rest is left alone.", 4, s4, { W { 5, 4, 25, 120 }, W { 1.5f, 2, 25, 150 }, W {}, W {} });
        add ("Goals", "Goal: tame harshness", "Holds the biting middle and top, only when they bite.", 4, { 150, 2000, 7000, 12000 }, { W {}, W {}, W { 4, 3, 4, 90 }, W { 3, 3, 2, 70 } }).look = 1;
        add ("Goals", "Goal: even and dense", "Loud parts come down about 4 dB in every band. Quiet parts come up as far as the music leaves room, at most 4 dB.", 4, s4, { W { 4, 3, 25, 160, 3, 1.5f }, W { 4, 3, 15, 130, 4, 1.7f }, W { 4, 3, 8, 110, 4, 1.7f }, W { 4, 3, 4, 90, 4, 1.7f } });
        add ("Goals", "Goal: glue", "A little of everything, slow and gentle.", 4, s4, { W { 2, 2, 30, 250 }, W { 1.5f, 2, 30, 250 }, W { 1.5f, 2, 25, 220 }, W { 1.5f, 2, 20, 200 } });

        add ("Mix bus", "Mix bus: glue, about 2 dB", "Holds the mix together without being heard.", 4, { 110, 700, 4500, 10000 }, { W { 2, 2, 40, 250 }, W { 2, 2, 40, 250 }, W { 1.5f, 2, 30, 220 }, W { 1.5f, 2, 20, 200 } });
        add ("Mix bus", "Mix bus: steady low end", "Kick and bass stay in one place, everything else breathes.", 3, { 130, 2500, 8000, 12000 }, { W { 4, 3, 30, 150 }, W { 1, 1.5f, 30, 250 }, W { 1, 1.5f, 20, 200 } });
        add ("Mix bus", "Mix bus: smoother top", "Cymbals and esses stop poking out.", 3, { 200, 4000, 9000, 12000 }, { W {}, W { 1, 1.5f, 20, 200 }, W { 3, 2.5f, 3, 90 } });
        add ("Mix bus", "Mix bus: more detail", "Lifts the quiet parts of the middle and top, never the peaks.", 4, s4, { W {}, W { 0, 2, 20, 150, 2.5f, 1.4f }, W { 0, 2, 10, 120, 3, 1.5f }, W { 0, 2, 5, 100, 3, 1.5f } });

        { auto& q = add ("Master", "Master: transparent control", "About 1.5 dB per band, slow, straight phase.", 4, { 100, 600, 4500, 10000 }, { W { 1.5f, 1.6f, 40, 250 }, W { 1.5f, 1.6f, 40, 250 }, W { 1.5f, 1.6f, 30, 220 }, W { 1.5f, 1.6f, 20, 200 } }); q.straight = true; q.knee = 10; }
        add ("Master", "Master: loud and dense", "Firm control in five bands, ready for the limiter.", 5, { 90, 350, 2000, 7500 }, { W { 4, 3, 25, 130 }, W { 3, 2.5f, 20, 130 }, W { 3, 2.5f, 12, 110, 2, 1.3f }, W { 3, 2.5f, 6, 90, 2, 1.3f }, W { 3, 2.5f, 3, 80, 2, 1.3f } });
        add ("Master", "Master: tight bass for clubs", "Only the bass, held firmly.", 2, { 120, 800, 5000, 10000 }, { W { 5, 4, 30, 140 }, W {} });
        { auto& q = add ("Master", "Master: calm the 3 kHz bite", "Only the harsh middle, only when it jumps out.", 3, { 1800, 5000, 9000, 12000 }, { W {}, W { 3, 3, 6, 120 }, W {} }); q.slope = 1; q.look = 2; }

        add ("Vocals", "Vocal: even from low to high", "Every register at the same level, chesty notes and bright words alike.", 4, { 180, 900, 4500, 9000 }, { W { 4, 3, 15, 140 }, W { 4, 3, 10, 120 }, W { 4, 3, 6, 100 }, W { 4, 3, 3, 80 } });
        add ("Vocals", "Vocal: no more boom", "Catches close-microphone boom and chesty notes.", 3, { 250, 3000, 8000, 12000 }, { W { 6, 4, 10, 130 }, W { 1, 1.5f, 15, 150 }, W {} });
        add ("Vocals", "Vocal: bright but not sharp", "Holds the 3 to 8 kHz range where voices hurt.", 3, { 2800, 8500, 9000, 12000 }, { W {}, W { 4, 3, 3, 80 }, W { 2, 2, 2, 70 } });
        add ("Vocals", "Rap vocal: thick and in front", "Firm in the body, lively on top, quiet syllables come up.", 4, { 160, 800, 4000, 9000 }, { W { 4, 4, 10, 110 }, W { 6, 4, 6, 100, 3, 1.5f }, W { 5, 4, 4, 80, 3, 1.5f }, W { 3, 3, 2, 70 } });
        add ("Vocals", "Backing vocals: smooth cloud", "Flattened in every band, so they sit behind the lead.", 4, { 250, 1200, 5000, 9000 }, { W { 5, 4, 15, 200 }, W { 5, 4, 12, 200 }, W { 5, 4, 8, 180 }, W { 5, 4, 4, 150 } });

        add ("Drums", "Drum bus: punch and weight", "Lets the hits through, tightens what follows.", 4, { 110, 700, 5000, 10000 }, { W { 4, 4, 30, 90 }, W { 4, 4, 25, 90 }, W { 3, 3, 15, 80 }, W { 2, 2, 8, 80 } });
        { auto& q = add ("Drums", "Drum bus: smashed, blended in", "Heavy in every band, mixed with the dry drums.", 4, s4, { W { 10, 10, 3, 90, 5, 2 }, W { 10, 10, 2, 80, 5, 2 }, W { 10, 10, 1, 70, 5, 2 }, W { 8, 8, 1, 60, 4, 2 } }); q.mix = 45; }
        add ("Drums", "Loop: kick steady, hats calm", "For a finished loop: an even kick, hats that do not jump.", 3, { 140, 4500, 9000, 12000 }, { W { 5, 4, 20, 110 }, W { 2, 2, 20, 140 }, W { 4, 3, 2, 70 } });
        add ("Drums", "Overheads: cymbals under control", "Only the top, quick and smooth.", 3, { 300, 4000, 9000, 12000 }, { W {}, W { 1, 1.5f, 15, 160 }, W { 5, 3, 2, 120 } });

        add ("808 and bass", "808: steady sub, lively grit", "The sub stays level, the harmonics keep moving.", 3, { 110, 900, 6000, 12000 }, { W { 6, 6, 25, 180 }, W { 2, 2, 15, 120 }, W {} });
        add ("808 and bass", "Bass guitar: every note equal", "Evens out lows and body, leaves the attack.", 3, { 160, 1200, 6000, 12000 }, { W { 5, 4, 25, 150 }, W { 4, 3, 20, 130 }, W { 2, 2, 10, 90 } });
        add ("808 and bass", "Bass: growl up, boom down", "Quiet harmonics come up, the boom goes.", 3, { 130, 900, 6000, 12000 }, { W { 5, 5, 20, 140 }, W { 0, 2, 10, 120, 4, 1.8f }, W { 0, 2, 5, 100, 3, 1.6f } });
        { auto& q = add ("808 and bass", "Bass ducks for the kick (sidechain)", "Send the kick to the sidechain: only the lows of the bass move away.", 2, { 150, 800, 5000, 10000 }, { W { 8, 8, 1, 110 }, W {} }); q.autoLevel = false; }

        add ("Instruments", "Acoustic guitar: boom and scratch tamed", "The body stops booming, the strings stop scratching.", 4, { 180, 900, 5000, 9000 }, { W { 5, 4, 15, 140 }, W { 1, 1.5f, 20, 150 }, W { 2, 2, 8, 110 }, W { 4, 3, 3, 90 } });
        add ("Instruments", "Electric guitars: solid wall", "Palm mutes no longer jump out of the wall.", 3, { 180, 2500, 8000, 12000 }, { W { 6, 5, 15, 110 }, W { 2, 2, 15, 130 }, W { 2, 2, 5, 90 } });
        add ("Instruments", "Piano: even over the keyboard", "Left hand and right hand at one level.", 4, { 200, 800, 3500, 9000 }, { W { 3, 2.5f, 25, 250 }, W { 3, 2.5f, 20, 250 }, W { 3, 2.5f, 12, 220 }, W { 2, 2, 8, 200 } });
        add ("Instruments", "Synth: wild filter, steady level", "For sweeps and resonances that change level all the time.", 5, { 120, 450, 1800, 6500 }, { W { 4, 4, 10, 140 }, W { 4, 4, 8, 130 }, W { 4, 4, 6, 110 }, W { 4, 4, 4, 90 }, W { 4, 4, 2, 80 } });

        add ("Creative", "Over the top: everything loud", "Both ways, hard, in every band. The sound of a festival.", 3, { 140, 2500, 8000, 12000 }, { W { 14, 12, 5, 110, 9, 3 }, W { 14, 12, 3, 90, 9, 3 }, W { 14, 12, 1, 80, 9, 3 } });
        { auto& q = add ("Creative", "Over the top, half strength", "The same, blended so that you can live with it.", 3, { 140, 2500, 8000, 12000 }, { W { 12, 10, 8, 120, 7, 2.5f }, W { 12, 10, 5, 100, 7, 2.5f }, W { 12, 10, 2, 90, 7, 2.5f } }); q.mix = 40; }
        add ("Creative", "Old radio squeeze", "Flat as a pancake in the middle, nothing else moves.", 3, { 400, 3500, 8000, 12000 }, { W { 6, 10, 5, 60 }, W { 12, 20, 1, 60, 6, 2 }, W { 6, 10, 2, 60 } });
        return r;
    }();
    return v;
}

const mbp::Recipe* mbp::find (const juce::String& name) { for (auto& r : recipes()) if (name == r.name) return &r; return nullptr; }

mbp::Levels mbp::assumed() { Levels l; l.loud = { -15, -19, -25, -31, -35 }; for (size_t i = 0; i < N; ++i) l.quiet[i] = l.loud[i] - 14; return l; }

mbp::Levels mbp::measure (const std::vector<float>& L, const std::vector<float>& R, double fs, const Params& p)
{
    juce::ScopedNoDenormals noDenormals;
    Levels out;
    if ((double) L.size() < fs) return out;
    std::array<aemon::xover::Bank, 2> bank; for (auto& b : bank) { b.prepare (fs); b.set (p.bands, p.split.data(), p.slope); b.snap(); b.clear(); }
    std::array<std::vector<double>, N> hist; for (auto& h : hist) h.assign (160, 0.0);
    std::array<double, N> peak {}, decay {}; std::array<int, N> hold {}, left {};
    for (int b = 0; b < p.bands; ++b) { const double ms = Engine::holdMs (b == 0 ? 35.0 : p.split[(size_t) b - 1]); hold[(size_t) b] = (int) (ms * 0.001 * fs); decay[(size_t) b] = std::exp (-1 / (ms * 0.001 * fs)); }
    const int every = juce::jmax (1, juce::roundToInt (fs * 0.016)); const int fine = juce::jmax (1, juce::roundToInt (fs * out.step)); out.step = fine / fs; std::array<double, N> top {}, part {}; double bl[N], br[N];      // the loudest moment of every 16 ms, as the window shows it
    for (size_t i = 0; i < L.size(); ++i)
    {
        bank[0].process (L[i], bl); bank[1].process (R[i], br);
        for (int b = 0; b < p.bands; ++b)
        {
            const double a = std::max (std::abs (bl[b]), std::abs (br[b])); auto& k = peak[(size_t) b];
            if (a >= k) { k = a; left[(size_t) b] = hold[(size_t) b]; } else if (left[(size_t) b] > 0) --left[(size_t) b]; else k = std::max (a, k * decay[(size_t) b]);
            auto& m = top[(size_t) b]; m = std::max (m, k);
            auto& q = part[(size_t) b]; q = std::max (q, k); if (i % (size_t) fine == (size_t) fine - 1) { out.series[(size_t) b].push_back ((float) (20 * std::log10 (q + 1e-7))); q = 0; }
            if (i % (size_t) every == (size_t) every - 1) { const double x = 20 * std::log10 (m + 1e-7); if (x > -70) hist[(size_t) b][(size_t) juce::jlimit (0, 159, (int) ((x + 80) * 2))] += 1; m = 0; }
        }
    }
    const auto def = assumed(); int heard = 0;
    for (int b = 0; b < N; ++b)
    {
        double t = 0; for (double h : hist[(size_t) b]) t += h;
        auto at = [&] (double q) { double a = 0; for (int k = 0; k < 160; ++k) { a += hist[(size_t) b][(size_t) k]; if (a >= q * t) return -80 + 0.5 * (k + 0.5); } return 0.0; };
        if (t < 30) { out.series[(size_t) b].clear(); out.loud[(size_t) b] = def.loud[(size_t) b]; out.quiet[(size_t) b] = def.quiet[(size_t) b]; continue; }
        ++heard; out.loud[(size_t) b] = at (0.9); out.quiet[(size_t) b] = at (0.3);
    }
    out.ok = heard > 0;
    return out;
}

namespace { double overFor (double wanted, double slope, double knee) { const double o = wanted / slope; return o >= knee / 2 ? o : std::sqrt (2 * knee * o) - knee / 2; } }      // the knee, backwards
float mbp::thresholdFor (double loud, float down, float ratio, float knee) { return down <= 0 || ratio <= 1 ? -24.0f : juce::jlimit (-60.0f, 0.0f, (float) (std::round ((loud - overFor (down, ratio >= 19.95f ? 1.0 : 1.0 - 1.0 / ratio, knee)) * 10) / 10)); }
float mbp::upThresholdFor (double quiet, float up, float ratio, float knee) { return up <= 0 || ratio <= 1 ? -45.0f : juce::jlimit (-70.0f, -10.0f, (float) (std::round ((quiet + overFor (up, 1.0 - 1.0 / ratio, knee)) * 10) / 10)); }

void mbp::aim (const Want& w, float knee, const std::vector<float>& x, double step, float& thr, float& up)
{
    const bool down = w.down > 0 && w.ratio > 1, lift = w.up > 0 && w.upRatio > 1;
    const double atk = 1 - std::exp (-step / (std::max (0.05, (double) w.attack) * 0.001)), rel = 1 - std::exp (-step / (std::max (1.0, (double) w.release) * 0.001));
    const int per = juce::jmax (1, juce::roundToInt (0.016 / step));
    std::vector<float> frames; frames.reserve (x.size() / (size_t) per + 1);
    auto run = [&] (float t, float u)
    {
        Band b; b.threshold = t; b.ratio = down ? w.ratio : 1; b.upThreshold = u; b.upRatio = lift ? w.upRatio : 1;
        double g = 0; float best = 0; int c = 0; frames.clear();
        for (float v : x) { const double target = gainFor (v, b, knee, 12.0); g += (target - g) * (target < g ? atk : rel); if (std::abs (g) > std::abs (best)) best = (float) g; if (++c >= per) { frames.push_back (best); best = 0; c = 0; } }
        std::sort (frames.begin(), frames.end());
    };
    auto at = [&] (double p) { return frames.empty() ? 0.0f : frames[(size_t) juce::jlimit (0, (int) frames.size() - 1, (int) (p * (double) frames.size()))]; };
    for (int round = 0; round < (lift && down ? 2 : 1); ++round)
    {
        if (down) { float lo = -60, hi = 0; for (int i = 0; i < 12; ++i) { const float m = (lo + hi) / 2; run (m, up); if (-at (0.1) > w.down) lo = m; else hi = m; } thr = std::round ((lo + hi) * 5) / 10; }
        if (lift) { float lo = -70, hi = down ? juce::jmax (-69.0f, thr - 1.0f) : -10.0f; for (int i = 0; i < 12; ++i) { const float m = (lo + hi) / 2; run (thr, m); if (at (0.9) < w.up) lo = m; else hi = m; } up = std::round ((lo + hi) * 5) / 10; }
    }
}

aemon::Preset mbp::presetOf (const Recipe& r, const Levels& l)
{
    aemon::Preset p; p.name = r.name; p.category = r.cat; p.description = r.desc;
    p.values = { { "bands", (float) (r.bands - 2) }, { "slope", (float) r.slope }, { "phase", r.straight ? 1.0f : 0.0f }, { "amount", r.amount }, { "mix", r.mix }, { "knee", r.knee }, { "look", r.look }, { "auto", r.autoLevel ? 1.0f : 0.0f }, { "extSc", juce::String (r.name).contains ("(sidechain)") ? 1.0f : 0.0f } };
    for (int i = 0; i < N - 1; ++i) p.values.emplace_back (id ('x', i), r.split[(size_t) i]);
    for (int i = 0; i < r.bands; ++i)
    {
        const auto& w = r.want[(size_t) i]; const bool d = w.down > 0, u = w.up > 0;
        float thr = thresholdFor (l.loud[(size_t) i], w.down, w.ratio, r.knee), upThr = upThresholdFor (l.quiet[(size_t) i], w.up, w.upRatio, r.knee);
        if (! l.series[(size_t) i].empty()) aim (w, r.knee, l.series[(size_t) i], l.step, thr, upThr);
        p.values.emplace_back (id ('t', i), thr); p.values.emplace_back (id ('r', i), d ? w.ratio : 1.0f);
        p.values.emplace_back (id ('a', i), w.attack); p.values.emplace_back (id ('e', i), w.release);
        p.values.emplace_back (id ('u', i), upThr); p.values.emplace_back (id ('v', i), u ? w.upRatio : 1.0f); p.values.emplace_back (id ('g', i), w.gain);
    }
    return p;
}

std::vector<aemon::Preset> MbProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    aemon::Preset clean; clean.name = "Clean start: changes nothing"; clean.description = "Every band at rest. Bit-exact."; v.push_back (clean);
    for (auto& r : mbp::recipes()) v.push_back (mbp::presetOf (r, mbp::assumed()));
    return v;
}

MbProcessor::MbProcessor() : EffectProcessor ("Multiband", "AemonMultiband", createLayout(), factoryPresets(), true) {}

Params MbProcessor::readParams() const
{
    Params p;
    p.bands = bandCount(); p.slope = real ("slope") > 0.5f ? 48 : 24; p.straight = real ("phase") > 0.5f;
    for (int i = 0; i < N - 1; ++i) p.split[(size_t) i] = get (mbp::id ('x', i));
    for (int i = 0; i < N; ++i)
    {
        auto& b = p.band[(size_t) i];
        b.threshold = get (mbp::id ('t', i)); b.ratio = get (mbp::id ('r', i)); if (b.ratio < 1.005) b.ratio = 1; b.attackMs = get (mbp::id ('a', i)); b.releaseMs = get (mbp::id ('e', i));
        b.upThreshold = get (mbp::id ('u', i)); b.upRatio = get (mbp::id ('v', i)); if (b.upRatio < 1.005) b.upRatio = 1; b.gainDb = get (mbp::id ('g', i)); b.on = get (mbp::id ('o', i)) > 0.5f; b.solo = get (mbp::id ('s', i)) > 0.5f;
    }
    p.kneeDb = real ("knee"); p.lookaheadMs = real ("look"); p.amount = real ("amount") / 100.0; p.mix = real ("mix") / 100.0; p.outputDb = real ("out");
    p.autoLevel = real ("auto") > 0.5f; p.external = real ("extSc") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    return p;
}

int MbProcessor::matchingGoal() const
{
    for (int g = 0; g < mbp::numGoals; ++g)
    {
        const auto* r = mbp::find (mbp::goals[g].recipe);
        if (r == nullptr || bandCount() != r->bands) continue;
        bool same = true; auto close = [] (float a, float b) { return std::abs (a - b) <= 0.02f * std::max (1.0f, std::abs (b)); };
        for (int i = 0; i < r->bands; ++i) { const auto& w = r->want[(size_t) i]; same = same && close (get (mbp::id ('r', i)), w.down > 0 ? w.ratio : 1.0f) && close (get (mbp::id ('v', i)), w.up > 0 ? w.upRatio : 1.0f) && (w.down <= 0 || (close (get (mbp::id ('a', i)), w.attack) && close (get (mbp::id ('e', i)), w.release))); }
        if (same) return g;
    }
    return -1;
}

float MbProcessor::frameAux()
{
    const auto f = engine->takeFrame();
    fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; });
    matchDb = (float) engine->matchDb();
    return 0.0f;
}

int MbProcessor::popBands (Engine::Frame* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; });
    return n;
}

juce::AudioProcessorEditor* MbProcessor::createEditor() { return new MbEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MbProcessor(); }
