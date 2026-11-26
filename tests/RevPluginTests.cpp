// Aemon Reverb plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonRevTests [--screenshots <dir> [music.wav]]
#include "RevEditor.h"
#include "PerceptReport.h"
#include "RenderKit.h"
#include "EffectTestKit.h"
#include <set>

using namespace kit;
using percept::Vec;

namespace
{
/** One preset through a whole processor; the dry part can be taken out to look at the space alone. */
Vec play (const juce::String& preset, const Vec& x, double fs, bool wetOnly, Vec* right = nullptr, const std::function<void (RevProcessor&)>& more = {})
{
    RevProcessor p; load (p, preset.toRawUTF8()); if (wetOnly) set (p, "send", 1); if (more) more (p); prepare (p, fs, 2);
    juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); run (p, b);
    if (right) right->assign (b.getReadPointer (1), b.getReadPointer (1) + b.getNumSamples());
    return Vec (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<RevProcessor> ("Reverb", argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--render") return renderkit::run<RevProcessor> (juce::File (juce::String (argv[2])), {});
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { const auto x = voicekit::takes (fs, {}, { 0, -3 }).x; juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        const auto path = tmp.getFullPathName(); std::string own = path.toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<RevProcessor, RevEditor> (4, args, "reverb", [] (RevProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Melodic rap lead: long" : "Trap lead: tight"); }, [] (RevEditor&) {});
        tmp.deleteFile(); return rc;
    }
    nullTest<RevProcessor> ("Reverb at mix 0", 0.8f);
    nullTest<RevProcessor> ("Reverb bypass", 0.8f, [] (RevProcessor& p) { set (p, "mix", 100); set (p, "bypass", 1); });

    // Every preset: the length and the gap its text states, the lows it keeps out, dense, not ringing, whole in mono, and the words stay readable
    {
        RevProcessor probe; int tested = 0, vocals = 0; std::set<juce::String> seen; const auto voice = voicekit::takes (fs, {}, { 0 }).x, low = voicekit::takes (fs, { 0.5, 0.75, 1.0, 0.92, 0.5, 7 }, { 0 }).x, high = voicekit::takes (fs, { 1.8, 1.2, 1.2, 1.15, 0.3, 11 }, { 0 }).x;
        for (auto* preset : probe.pluginState.allPresets())
        {
            if (preset->user || preset->name.startsWith ("Clean start")) continue;
            ++tested; RevProcessor q; load (q, preset->name.toRawUTF8()); const auto p = q.readParams(); const bool vocal = preset->category.endsWith ("vocals"); vocals += vocal; const bool send = q.real ("send") > 0.5f;
            // the space alone, straight (no duck, gate, grit or early part): its length
            const double secs = juce::jlimit (1.5, 14.0, p.decay * 1.4 + 0.6); Vec imp ((size_t) (secs * fs), 0.0f); imp[0] = 0.5f; Vec ir_r;
            auto bare = [] (RevProcessor& r) { set (r, "duck", 0); set (r, "gate", 0); set (r, "drive", 0); set (r, "early", 0); };
            const auto ir = play (preset->name, imp, fs, true, &ir_r, bare); const double hi = std::min (2000.0, p.highCut * 0.7), lo = std::max (500.0, p.lowCut * 1.3); const auto d = percept::decayOf (ir, fs, lo, hi);
            const double expect = std::pow (revp::decayAt (p, lo) * revp::decayAt (p, hi), 0.5); const bool length = std::abs (d.t30 / expect - 1) < 0.2 && std::abs (expect / p.decay - 1) < 0.35;
            size_t first = 0; float top = 0; for (float v : ir) top = std::max (top, std::abs (v)); while (first < ir.size() && std::abs (ir[first]) < 0.02f * top) ++first; const bool gap = (double) first / fs * 1000 >= p.preMs - 1;
            const double from = p.preMs * 0.001 + std::min (0.12, 0.03 + 0.2 * p.decay), part = std::min (2.0, 0.6 * p.decay);      // the part of the tail that is heard: the first 40 dB
            const double ring = percept::ringing (ir, fs, from, part); double base = 0; { Vec nz (ir.size()); juce::Random rr (5); for (size_t i = 0; i < nz.size(); ++i) nz[i] = (float) (0.1 * (rr.nextFloat() * 2 - 1) * std::pow (10.0, -3 * (double) i / fs / p.decay)); nz = percept::filtered (percept::filtered (nz, true, p.lowCut, fs), false, std::min (18000.0, p.highCut), fs); base = percept::ringing (nz, fs, from, part); } const double mono = percept::monoLoss (ir, ir_r);
            // the lows it keeps out
            const auto kick = drumkit::loop ("kick", fs, 4, 0.5); const auto kw = play (preset->name, kick, fs, true); const double sub = percept::rmsDb (percept::filtered (kw, false, 70, fs)) - percept::rmsDb (percept::filtered (kick, false, 70, fs));
            // the words, as used: an insert with its own mix, a send 10 dB under the voice
            double words = 1; if (vocal) for (auto* v : { &voice, &low, &high }) { auto y = play (preset->name, *v, fs, send); if (send) for (size_t i = 0; i < y.size(); ++i) y[i] = (*v)[i] + 0.316f * y[i]; words = std::min (words, percept::words (*v, y, fs)); }
            const auto facts = juce::String (p.decay, p.decay < 10 ? 1 : 0).replace (".0", "") + " s"; const bool said = preset->description.contains (facts) && (p.duck < 0.5 || preset->description.contains ("ducks " + juce::String (juce::roundToInt (p.duck)) + " dB"));
            const bool hazy = preset->name.contains ("haze") || preset->name.contains ("wash") || preset->name.contains ("pushed back") || preset->name.contains ("Telephone") || preset->name.contains ("Whisper");      // meant to blur
            const double wordsBar = hazy ? 0.70 : 0.82; const bool lows = ! preset->name.contains ("808-safe") || sub < -25;
            check (length && gap && ring < base + 6 && mono > -3.2 && (p.lowCut < 120 || sub < -8) && lows && words > wordsBar && said && seen.insert (preset->name).second, "Preset: " + preset->name,
                   "decay " + juce::String (d.t30, 2) + " s (says " + juce::String (p.decay, 2) + "), gap " + juce::String ((double) first / fs * 1000, 0) + " ms (says " + juce::String (p.preMs, 0) + "), mode " + juce::String (ring, 1) + " dB (even noise: " + juce::String (base, 1) + "), mono " + juce::String (mono, 1) + ", under 70 Hz " + juce::String (sub, 0) + (vocal ? " dB, words " + juce::String (words, 2) : juce::String (" dB")));
        }
        check (tested >= 35 && vocals * 2 > tested, "Presets tested", juce::String (tested) + ", " + juce::String (vocals) + " for vocals");
        // promises in names
        auto tone = [&] (const char* name) { Vec n ((size_t) (3 * fs)); juce::Random r (3); for (auto& v : n) v = 0.1f * (r.nextFloat() * 2 - 1); return percept::tone (play (name, n, fs, true), fs); }; auto level = [] (const std::array<double, percept::thirds>& a, double l, double h) { double s = 0; for (int k = 0; k < percept::thirds; ++k) if (percept::thirdHz (k) >= l && percept::thirdHz (k) <= h) s += std::pow (10.0, a[(size_t) k] / 10); return 10 * std::log10 (s); };
        const auto bright = tone ("Trap lead: tight"), dark = tone ("90s East Coast lead"), glass = tone ("Rage lead"), room = tone ("Drill lead"); const double b = level (bright, 6000, 12000) - level (bright, 800, 2000), dk = level (dark, 6000, 12000) - level (dark, 800, 2000), gl = level (glass, 6000, 12000) - level (glass, 800, 2000), rm = level (room, 6000, 12000) - level (room, 800, 2000);
        check (b > dk + 5 && gl > dk + 6 && rm < b - 5, "Bright, glassy and dark are", "top against middle: bright " + juce::String (b, 1) + ", glassy " + juce::String (gl, 1) + ", dark " + juce::String (dk, 1) + ", dark room " + juce::String (rm, 1) + " dB");
        Vec snare = drumkit::hit ("snare", fs, 2); const auto gated = play ("80s drums", snare, fs, true); check (percept::rmsDb (gated, 60000, 90000) < -90 && percept::rmsDb (gated, 4800, 9600) > -50, "Gated drums: big, then gone", juce::String (percept::rmsDb (gated, 4800, 9600), 0) + " dB, then " + juce::String (percept::rmsDb (gated, 60000, 90000), 0));
        const auto with = play ("Trap lead: tight", voice, fs, false), without = play ("Trap lead: tight", voice, fs, false, nullptr, [] (RevProcessor& p) { set (p, "duck", 0); }); const double w1 = percept::words (voice, with, fs), w0 = percept::words (voice, without, fs); check (w1 > w0, "Ducking beats the same preset without it", juce::String (w0, 3) + " to " + juce::String (w1, 3));
    }
    // Pre-delay on the beat, tail length, send, state, blocks
    {
        struct Playhead : juce::AudioPlayHead { juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm (140.0); p.setIsPlaying (true); return p; } } ph; RevProcessor p; set (p, "send", 1); set (p, "sync", 3); p.setPlayHead (&ph); prepare (p, fs, 2); juce::AudioBuffer<float> b (2, 96000); b.clear(); run (p, b); check (std::abs (p.readParams().preMs - 60000.0 / 140 / 4) < 0.01, "Pre-delay of a 16th at 140 BPM", juce::String (p.readParams().preMs, 2) + " ms");
        RevProcessor q; set (q, "decay", 3); const double t = q.getTailLengthSeconds(); check (t > 3 && t < 6, "The tail is reported to the DAW", juce::String (t, 1) + " s for 3 s of decay");
        RevProcessor r; set (r, "send", 1); set (r, "pre", 100); prepare (r, fs, 2); auto c = tone (fs, 4096, 300, 0.5f); run (r, c); float peak = 0; for (int i = 0; i < 4000; ++i) peak = std::max (peak, std::abs (c.getSample (0, i))); check (peak == 0.0f, "On a send nothing dry comes out", juce::String (peak));
    }
    {
        RevProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RevEditor*> (base.get()); set (p, "mix", 40); ed->clearWords(); const bool a = p.real ("duck") == 8.0f && p.real ("pre") == 30.0f && p.real ("lowcut") == 180.0f; p.pluginState.history.undo (p.apvts); const bool undone = p.real ("duck") == 0.0f; ed->cleanLows(); ed->shorter(); check (a && undone && p.real ("lowcut") == 250.0f && std::abs (p.real ("decay") - 1.35f) < 0.011f, "The helpers do what they say, one undo step each", "");
    }
    {
        RevProcessor p; set (p, "mix", 50); prepare (p, fs, 2); auto b = tone (fs, 512 * 500, 220, 0.25f); run (p, b, 512, [&] (int k) { if (k == 100) set (p, "space", 2); if (k == 150) set (p, "space", 4); if (k == 200) set (p, "size", 90); if (k == 250) set (p, "decay", 6); if (k == 300) set (p, "bypass", 1); if (k == 330) set (p, "bypass", 0); if (k == 360) set (p, "send", 1); if (k == 400) set (p, "send", 0); if (k == 440) set (p, "freeze", 1); if (k == 470) set (p, "freeze", 0); });
        Vec x (b.getReadPointer (0) + 20000, b.getReadPointer (0) + b.getNumSamples()); check (percept::clickDb (x, fs) < 26, "Every switch is silent", juce::String (percept::clickDb (x, fs), 1) + " dB");
    }
    stateTest<RevProcessor> (41, "Dub", [] (RevProcessor& p) { set (p, "sync", 4); set (p, "width", 33); set (p, "send", 1); });
    blockTest<RevProcessor> ([] (RevProcessor& p) { set (p, "mix", 60); set (p, "duck", 9); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
