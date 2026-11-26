// Aemon Imager plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonImagerTests [--screenshots <dir> [music.wav]]
#include "ImagerEditor.h"
#include "PerceptReport.h"
#include "StyleRows.h"
#include "EffectTestKit.h"
#include "VoiceKit.h"

using namespace kit;
using Buf = juce::AudioBuffer<float>;

namespace
{
const double fs = 48000;
/** A whole mix in stereo: centred kick and lead, wide pads and hats, optionally a low end that is too wide, optionally leaning left. */
Buf mix (double seconds, float gainDb = -12, bool wideLow = false, float lean = 0)
{
    auto b = music (fs, seconds, gainDb); juce::Random r (5); const float g = juce::Decibels::decibelsToGain (gainDb); const double tau = juce::MathConstants<double>::twoPi; float z = 0, z2 = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / fs, h = std::fmod (t + 0.125, 0.25); const float nz = r.nextFloat() * 2 - 1; z += 0.3f * (nz - z);
        float s = g * (float) (0.06 * std::sin (tau * 523.3 * t) + 0.05 * std::sin (tau * 880 * t + 1) + 0.10 * (nz - z) * std::exp (-h * 30) + 0.012 * std::sin (tau * 2637 * t));
        const float n2 = r.nextFloat() * 2 - 1; z2 += 0.3f * (n2 - z2); const float hat = g * 0.22f * (n2 - z2) * (float) std::exp (-h * 30);      // hats live on both, as in a real mix
        if (wideLow) s += g * 0.25f * (float) std::sin (tau * 65 * t);
        b.addSample (0, i, hat + s); b.addSample (1, i, hat - s);
    }
    if (lean != 0) { b.applyGain (0, 0, b.getNumSamples(), juce::Decibels::decibelsToGain (lean / 2)); b.applyGain (1, 0, b.getNumSamples(), juce::Decibels::decibelsToGain (-lean / 2)); }
    return b;
}
Buf voice() { const auto v = voicekit::sing (fs); Buf b (2, (int) v.x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, v.x.data(), (int) v.x.size()); return b; }
Buf pad() { Buf b (2, (int) (5 * fs)); const double tau = juce::MathConstants<double>::twoPi; for (int i = 0; i < b.getNumSamples(); ++i) { const double t = i / fs; double l = 0, r = 0; for (double f : { 98.0, 196.0, 246.9, 293.7, 587.3, 1174.7, 2349.3, 4698.6, 9397.0 }) { const double a = 0.11 / std::sqrt (f / 98); l += a * std::sin (tau * f * 1.0017 * t); r += a * std::sin (tau * f * 0.9981 * t + 1.1); } b.setSample (0, i, (float) l); b.setSample (1, i, (float) r); } return b; }
double monoErrorDb (const Buf& in, const Buf& out, int lat, int from = 0) { double w = 0; for (int i = from; i + lat < out.getNumSamples(); ++i) w = std::max (w, std::abs (((double) out.getSample (0, i + lat) + out.getSample (1, i + lat)) - ((double) in.getSample (0, i) + in.getSample (1, i)))); return 20 * std::log10 (w + 1e-15); }
imgp::Cells cellsOf (const Buf& b, int from = 24000)
{
    imgp::Analyser a; a.prepare (fs); imgp::Cells sum; const int N = a.size();
    for (int at = from; at + N < b.getNumSamples(); at += N / 2) { for (int i = 0; i < N; ++i) { a.L[(size_t) i] = b.getSample (0, at + i); a.R[(size_t) i] = b.getSample (1, at + i); } const auto c = a.run(); for (size_t i = 0; i < imgp::cells; ++i) { sum.m[i] += c.m[i]; sum.s[i] += c.s[i]; sum.l[i] += c.l[i]; sum.r[i] += c.r[i]; } }
    return sum;
}
double widthDb (const imgp::Cells& c, double lo, double hi) { return 10 * std::log10 ((c.side (lo, hi) + 1e-14) / (c.middle (lo, hi) + 1e-14)); }
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--styles") { style::run<ImagerProcessor> (rows::imager(), -18, [] (bool ok, const juce::String& n, const juce::String& d) { std::printf ("%s  %s (%s)\n", ok ? "PASS" : "FAIL", n.toRawUTF8(), d.toRawUTF8()); }, argc > 2, 0.75, true); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<ImagerProcessor> ("Imager", argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { auto b = mix (12, -8, false, 0); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 24, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        std::string own = tmp.getFullPathName().toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<ImagerProcessor, ImagerEditor> (4, args, "imager", [] (ImagerProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Mix bus: open" : "Master: wider"); }, {});
        tmp.deleteFile(); return rc;
    }

    nullTest<ImagerProcessor> ("Imager null test", 0.8f);
    nullTest<ImagerProcessor> ("Imager null test, zero delay", 0.8f, [] (ImagerProcessor& p) { set (p, "delay", 1); });
    nullTest<ImagerProcessor> ("Imager bypass", 0.8f, [] (ImagerProcessor& p) { set (p, "width", 200); set (p, "make", 80); set (p, "mono", 200); set (p, "bypass", 1); });
    {
        ImagerProcessor p; set (p, "width", 170); set (p, "make", 80); set (p, "hearSide", 1); prepare (p, fs, 1); Buf b (1, 48000); juce::Random r (1); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, r.nextFloat() - 0.5f); auto in = b; run (p, b); bool same = true; for (int i = p.getLatencySamples(); i < b.getNumSamples(); ++i) same &= b.getSample (0, i) == in.getSample (0, i - p.getLatencySamples());
        check (same, "A mono track passes through bit-exact, whatever is set", same ? "identical" : "changed");
    }

    // Every preset: the promise, and that it does what it says
    {
        const Buf sounds[] { mix (5, -12), mix (5, -12, true), voice(), pad() }; const char* names[] { "mix", "wide low", "mono voice", "pad" };
        ImagerProcessor probe; int n = 0; juce::String bad; double worst = -400;
        for (auto* preset : probe.pluginState.allPresets())
        {
            if (preset->user) continue;
            ++n; juce::String problem, info;
            for (int k = 0; k < 4; ++k)
            {
                ImagerProcessor p; load (p, preset->name.toRawUTF8()); prepare (p, fs, 2); auto b = sounds[k]; run (p, b);
                const double err = monoErrorDb (sounds[k], b, p.getLatencySamples()); worst = std::max (worst, err); if (err > -138) problem << names[k] << " mono " << juce::String (err, 1) << "; ";
                const auto was = cellsOf (sounds[k]), now = cellsOf (b, 24000 + p.getLatencySamples());
                if (p.real ("safe") > 0.5f) { const double loss = imgp::lossDb (now.middle(), now.side()); if (loss > p.real ("limit") + 0.3) problem << names[k] << " loses " << juce::String (loss, 2) << "; "; }
                if (p.real ("mono") > 79) { const double low = widthDb (now, 0, 0.45 * p.real ("mono")); if (low > -25 && now.middle (0, 0.45 * p.real ("mono")) > 1e-4 * now.total())      // only where there is sound at all
                    problem << names[k] << " low end " << juce::String (low, 1) << "; "; }
                // balance: no preset makes a centred sound lean
                if (std::abs (now.balanceDb()) > std::abs (was.balanceDb()) + 1.0) problem << names[k] << " leans " << juce::String (now.balanceDb(), 1) << "; ";
                const double top = widthDb (now, 2000, 18000) - widthDb (was, 2000, 18000), all = widthDb (now, 200, 18000) - widthDb (was, 200, 18000);
                info << names[k] << " " << juce::String (all, 1) << "/" << juce::String (top, 1) << "  ";
                const auto nm = preset->name; auto has = [&nm] (const char* w) { return nm.containsIgnoreCase (w); };
                if (k == 0 && (has ("wide cym") || has ("open the top") || has ("wider, still") || has ("hats out")) && top < 0.5) problem << "top not wider; ";
                if (k != 2 && (has ("bring it in") || has ("pull it together") || has ("in front") || has ("one place")) && all > -0.2) problem << names[k] << " not narrower; ";
                if (k == 0 && (has ("around the lead") || has ("to the sides") || has ("bigger") || has ("wider wall") || has ("Reverb") || has ("Delay")) && all < 1.0) problem << "not wider; ";
                if (k == 2 && (has ("make it stereo") || has ("gentle space") || has ("spread it") || has ("wide but solid")) && (widthDb (now, 300, 18000) < -32 || imgp::lossDb (now.middle(), now.side()) > 4.2)) problem << "mono voice " << juce::String (widthDb (now, 300, 18000), 1) << "; ";
            }
            if (problem.isNotEmpty()) bad << preset->name << ": " << problem << "[" << info << "] ";
            std::printf ("      %-42s  width change all/top: %s\n", preset->name.toRawUTF8(), info.toRawUTF8());
        }
        check (bad.isEmpty() && n == 41, "All presets on four sounds: mono unchanged, safe limit kept, no lean, as described", bad.isEmpty() ? juce::String (n) + " presets, worst mono difference " + juce::String (worst, 1) + " dB" : bad);
    }

    // One-click fixes
    {
        ImagerProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<ImagerEditor*> (base.get());
        ed->centreIt(); const bool still = p.real ("centre") == 0.0f;
        const auto src = mix (8, -12, false, 4.0f); auto b = src; run (p, b); ed->centreIt(); auto c = src; run (p, c); const auto was = cellsOf (src), now = cellsOf (c, 48000);
        check (still && std::abs (was.balanceDb() - 4) < 1.0 && std::abs (now.balanceDb()) < 0.4 && monoErrorDb (src, c, p.getLatencySamples()) < -138, "Centre it", "from " + juce::String (was.balanceDb(), 2) + " to " + juce::String (now.balanceDb(), 2) + " dB, mono sum unchanged");
        p.pluginState.history.undo (p.apvts); check (p.real ("centre") == 0.0f, "Centre it is one undo step", "back to as is");
    }
    {
        ImagerProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<ImagerEditor*> (base.get());
        ed->tighten(); const auto src = mix (5, -12, true); auto b = src; run (p, b); const auto was = cellsOf (src), now = cellsOf (b, 24000 + p.getLatencySamples()); const double l0 = imgp::lossDb (was.middle (0, 130), was.side (0, 130)), l1 = imgp::lossDb (now.middle (0, 130), now.side (0, 130));
        check (l0 > 0.35 && l1 < 0.1, "Tighten it", "bass loses " + juce::String (l0, 2) + " dB in mono, then " + juce::String (l1, 3));
        ImagerProcessor q; prepare (q, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base2 (q.createEditor()); auto* ed2 = dynamic_cast<ImagerEditor*> (base2.get()); ed2->makeItStereo(); const auto v = voice(); auto d = v; run (q, d); const auto c = cellsOf (d, 24000 + q.getLatencySamples()); const double wd = widthDb (c, 300, 18000);
        check (wd > -14 && wd < -3 && std::abs (c.balanceDb()) < 0.8 && monoErrorDb (v, d, q.getLatencySamples()) < -138, "Make it stereo", "sides " + juce::String (wd, 1) + " dB, balance " + juce::String (c.balanceDb(), 2) + " dB, mono is the dry voice");
    }
    // The map and the numbers agree with the sound
    {
        imgp::Analyser a; a.prepare (fs); juce::Random r (3); bool ok = true; juce::String list;
        for (double s : { 0.0, 0.3, 1.0, 2.0 }) { for (int i = 0; i < a.size(); ++i) { const float m = r.nextFloat() - 0.5f, q = (float) s * (r.nextFloat() - 0.5f); a.L[(size_t) i] = m + q; a.R[(size_t) i] = m - q; } const auto c = a.run(); const double got = imgp::lossDb (c.middle(), c.side()), want = 10 * std::log10 (1 + s * s); ok &= std::abs (got - want) < 0.5; list << juce::String (want, 1) << " = " << juce::String (got, 1) << "  "; }
        check (ok, "Lost in mono is what mono loses", list + "dB");
        ImagerProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<ImagerEditor*> (base.get()); auto b = mix (6, -12); const auto truth = cellsOf (b); run (p, b, 800, [&] (int) { ed->tickForTest(); }); const double said = imgp::lossDb (ed->outSlow.middle(), ed->outSlow.side()), is = imgp::lossDb (truth.middle(), truth.side());
        check (std::abs (said - is) < 0.35, "The window says it too", juce::String (said, 2) + " against " + juce::String (is, 2) + " dB");
    }
    // Equal loudness only while listening, never above the original
    {
        const auto src = mix (8, -14); ImagerProcessor p; set (p, "width", 200); set (p, "match", 1); prepare (p, fs, 2); auto b = src; run (p, b); const float in = lufs (src, fs, nullptr, (int) fs * 4), out = lufs (b, fs, nullptr, (int) fs * 4);
        ImagerProcessor q; set (q, "width", 200); set (q, "match", 1); q.setNonRealtime (true); prepare (q, fs, 2); auto c = src; run (q, c); const double err = monoErrorDb (src, c, q.getLatencySamples());
        check (std::abs (out - in) < 0.4f && err < -138, "Equal loudness while listening, not in an export", juce::String (out - in, 2) + " LU from the original; export mono sum " + juce::String (err, 1) + " dB");
    }
    // Latency and clicks
    {
        ImagerProcessor p; set (p, "width", 140); prepare (p, fs, 2); const int lat0 = p.getLatencySamples(); int lat1 = -1; auto b = tone (fs, 512 * 420, 300, 0.4f); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (1, i, 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 300 * i / fs + 0.7));
        run (p, b, 512, [&] (int k) { if (k == 100) set (p, "delay", 1); if (k == 150) { lat1 = p.getLatencySamples(); set (p, "delay", 0); } if (k == 220) set (p, "safe", 1); if (k == 250) set (p, "bypass", 1); if (k == 280) set (p, "bypass", 0); if (k == 310) set (p, "f2", 600); if (k == 340) set (p, "mono", 400); if (k == 370) set (p, "hearMono", 1); });
        const float step = largestStep (b, 6000), limit = 0.5f * (float) (juce::MathConstants<double>::twoPi * 300 / fs) * 2.2f;
        check (lat0 == 2304 && lat1 == 0 && p.getLatencySamples() == 2304 && step < limit, "Latency follows the quality, every switch is silent", juce::String (lat0) + ", " + juce::String (lat1) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }

    stateTest<ImagerProcessor> (41, "Overheads", [] (ImagerProcessor& p) { set (p, "centre", -12.5f); set (p, "delay", 1); set (p, "f2", 810); set (p, "limit", 2.2f); });
    blockTest<ImagerProcessor> ([] (ImagerProcessor& p) { set (p, "width", 160); set (p, "make", 30); set (p, "mono", 120); set (p, "safe", 1); });

    style::run<ImagerProcessor> (rows::imager(), -18, [] (bool ok, const juce::String& n, const juce::String& d) { check (ok, n, d); }, false, 0.75, true);
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
