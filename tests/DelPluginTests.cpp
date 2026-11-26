// Aemon Delay plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonDelTests [--screenshots <dir> [music.wav]]
#include "DelEditor.h"
#include "PerceptReport.h"
#include "RenderKit.h"
#include "EffectTestKit.h"
#include <set>

using namespace kit;
using percept::Vec;

namespace
{
struct Playhead : juce::AudioPlayHead { double bpm = 140; juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm (bpm); p.setIsPlaying (true); return p; } };
Vec play (const juce::String& preset, const Vec& x, double fs, bool wetOnly, Vec* right = nullptr, const std::function<void (DelProcessor&)>& more = {}, double bpm = 140)
{
    Playhead ph; ph.bpm = bpm; DelProcessor p; load (p, preset.toRawUTF8()); p.bpm = bpm; if (wetOnly) set (p, "send", 1); if (more) more (p); p.setPlayHead (&ph); prepare (p, fs, 2);
    juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); run (p, b);
    if (right) right->assign (b.getReadPointer (1), b.getReadPointer (1) + b.getNumSamples());
    return Vec (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}
/** A 10 ms ping at the pitch that passes the preset's filters, and where it is loudest again in a window. */
Vec ping (double fs, double secs, double hz) { Vec v ((size_t) (secs * fs), 0.0f); for (int i = 0; i < (int) (0.012 * fs); ++i) v[(size_t) i] = (float) (0.4 * std::sin (2 * percept::pi * hz * i / fs) * std::sin (percept::pi * i / (0.012 * fs))); return v; }
double peakTime (const Vec& y, double fs, double from, double to, double* db = nullptr) { double best = 0; int at = 0; const int w = (int) (0.004 * fs); double e = 0; for (int i = (int) (from * fs); i < (int) (to * fs) && i + w < (int) y.size(); ++i) { if (i == (int) (from * fs)) { e = 0; for (int k = 0; k < w; ++k) e += std::pow (y[(size_t) (i + k)], 2); } else e += std::pow (y[(size_t) (i + w - 1)], 2) - std::pow (y[(size_t) (i - 1)], 2); if (e > best) { best = e; at = i; } } if (db) *db = 10 * std::log10 (best + 1e-20); return (at + w / 2) / fs; }
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<DelProcessor> ("Delay", argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--render") return renderkit::run<DelProcessor> (juce::File (juce::String (argv[2])), [] (DelProcessor& p) { p.bpm = 112; if (p.real ("throwOnly") > 0.5f) p.apvts.getParameter ("throw")->setValueNotifyingHost (1.0f); });
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { const auto x = voicekit::takes (fs, {}, { 0, -3 }).x; juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        const auto path = tmp.getFullPathName(); std::string own = path.toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<DelProcessor, DelEditor> (4, args, "delay", [] (DelProcessor& p, const juce::String& mode) { p.bpm = 140; load (p, mode == "pro" ? "Pop lead: quarter" : "Melodic rap lead: dotted"); }, [] (DelEditor&) {});
        tmp.deleteFile(); return rc;
    }
    nullTest<DelProcessor> ("Delay at mix 0", 0.8f);
    nullTest<DelProcessor> ("Delay bypass", 0.8f, [] (DelProcessor& p) { set (p, "mix", 100); set (p, "bypass", 1); });

    // Every preset at two tempos: echoes on the beat its text states, at the level the feedback says, one echo means one, words stay readable
    {
        DelProcessor probe; int tested = 0, vocals = 0; std::set<juce::String> seen; const auto voice = voicekit::takes (fs, {}, { 0 }).x, low = voicekit::takes (fs, { 0.5, 0.75, 1.0, 0.92, 0.5, 7 }, { 0 }).x, high = voicekit::takes (fs, { 1.8, 1.2, 1.2, 1.15, 0.3, 11 }, { 0 }).x;
        for (auto* preset : probe.pluginState.allPresets())
        {
            if (preset->user || preset->name.startsWith ("Clean start")) continue;
            ++tested; DelProcessor q; load (q, preset->name.toRawUTF8()); const bool vocal = preset->category.endsWith ("vocals"); vocals += vocal; const bool send = q.real ("send") > 0.5f, reverse = q.real ("reverse") > 0.5f, pp = q.real ("style") > 0.5f && q.real ("style") < 1.5f; bool ok = true; juce::String said; double worst = 0, lev = 0;
            auto bare = [] (DelProcessor& r) { set (r, "duck", 0); set (r, "throwOnly", 0); set (r, "wobble", 0); set (r, "width", 100); set (r, "drive", 0); };
            for (double bpm : { 92.0, 140.0 })
            {
                q.bpm = bpm; const auto p = q.readParams(); const double hz = juce::jlimit (300.0, 6000.0, std::sqrt (juce::jmax (150.0, p.lowCut) * juce::jmin (9000.0, p.character == aemon::del::Character::digital ? p.highCut : juce::jmin (p.highCut, 4000.0)))), T = p.ms * 0.001;
                if (reverse) continue;
                Vec r; const auto y = play (preset->name, ping (fs, 3.5 * T + 0.3, hz), fs, true, &r, bare, bpm); double d1, d2; const double win = std::min (0.02, T * 0.3); const double t1 = peakTime (y, fs, T - win, T + win, &d1), t0 = 0.006; worst = std::max (worst, std::abs (t1 - t0 - T) * 1000);
                peakTime (y, fs, (pp ? 3 : 2) * T - win, (pp ? 3 : 2) * T + win, &d2);
                if (p.feedback > 0.01) { const double got = d2 - d1, want = 20 * std::log10 (p.feedback); lev = std::max (lev, p.character == aemon::del::Character::digital && T > 0.05 ? std::abs (got - want) : std::max (0.0, got - want)); ok &= got < want + 1.0 && got > want - 9; } else ok &= d2 < d1 - 40;
                if (pp) { double dl, dr; peakTime (y, fs, 2 * T - win, 2 * T + win, &dl); peakTime (r, fs, 2 * T - win, 2 * T + win, &dr); ok &= dr > dl + 20; }
                if (q.real ("sync") > 0.5f) { double beats = delp::divisionBeats[juce::roundToInt (q.real ("sync"))] * 60 / bpm; while (beats > 2.5) beats *= 0.5; ok &= std::abs (T - beats) < 1e-9; }
            }
            ok &= worst < 2.5;
            const auto p = q.readParams(); const juce::String div = q.real ("sync") > 0.5f ? juce::String (delp::divisionNames[juce::roundToInt (q.real ("sync"))]) : juce::String (juce::roundToInt (q.real ("ms"))) + " ms"; ok &= preset->description.contains (div) && (p.duck < 0.5 || preset->description.contains ("ducks " + juce::String (juce::roundToInt (p.duck)) + " dB"));
            double words = 1; if (vocal) for (auto* v : { &voice, &low, &high }) { auto y = play (preset->name, *v, fs, send); if (send) for (size_t i = 0; i < y.size(); ++i) y[i] = (*v)[i] + 0.25f * y[i]; words = std::min (words, percept::words (*v, y, fs)); }
            const bool hazy = preset->name.contains ("haze") || preset->name.contains ("Whisper") || preset->name.contains ("loudspeaker") || preset->name.contains ("double") || preset->name.contains ("rock and roll");
            Vec l2, r2; const auto nz = [&] { Vec v ((size_t) (3 * fs)); juce::Random rr (4); for (auto& s : v) s = 0.1f * (rr.nextFloat() * 2 - 1); return v; }(); l2 = play (preset->name, nz, fs, true, &r2, [] (DelProcessor& d) { set (d, "throwOnly", 0); }); const double mono = percept::monoLoss (l2, r2);
            check (ok && words > (hazy ? 0.70 : 0.82) && mono > -3.5 && seen.insert (preset->name).second, "Preset: " + preset->name, div + ", first echo off by " + juce::String (worst, 2) + " ms, repeats within " + juce::String (lev, 1) + " dB" + (vocal ? ", words " + juce::String (words, 2) : juce::String()) + ", mono " + juce::String (mono, 1));
        }
        check (tested >= 35 && vocals * 2 > tested, "Presets tested", juce::String (tested) + ", " + juce::String (vocals) + " for vocals");
        const auto with = play ("Trap lead: quarter", voice, fs, false), without = play ("Trap lead: quarter", voice, fs, false, nullptr, [] (DelProcessor& p) { set (p, "duck", 0); }); const double w1 = percept::words (voice, with, fs), w0 = percept::words (voice, without, fs); check (w1 > w0, "Ducking beats the same preset without it", juce::String (w0, 3) + " to " + juce::String (w1, 3));
        auto tone = [&] (const char* name) { Vec n ((size_t) (3 * fs)); juce::Random r (3); for (auto& v : n) v = 0.1f * (r.nextFloat() * 2 - 1); return percept::tone (play (name, n, fs, true, nullptr, [] (DelProcessor& p) { set (p, "throwOnly", 0); }), fs); }; auto level = [] (const std::array<double, percept::thirds>& a, double l, double h) { double s = 0; for (int k = 0; k < percept::thirds; ++k) if (percept::thirdHz (k) >= l && percept::thirdHz (k) <= h) s += std::pow (10.0, a[(size_t) k] / 10); return 10 * std::log10 (s); };
        const auto phone = tone ("Telephone echo"), glass = tone ("Plugg lead"), dark = tone ("90s East Coast"); const double pl = level (phone, 60, 250) - level (phone, 1000, 2000), ph = level (phone, 6000, 12000) - level (phone, 1000, 2000), gl = level (glass, 6000, 12000) - level (glass, 1000, 2000), dk = level (dark, 6000, 12000) - level (dark, 1000, 2000); check (pl < -15 && ph < -15 && gl > dk + 12, "Telephone, glassy and dark are", "telephone lows " + juce::String (pl, 0) + " and top " + juce::String (ph, 0) + ", glassy " + juce::String (gl, 1) + ", dark " + juce::String (dk, 1) + " dB");
    }
    // Throw through the parameter, tail, send, helpers
    {
        DelProcessor p; load (p, "Last word: quarter throw"); prepare (p, fs, 2); auto b = tone (fs, 512 * 300, 800, 0.3f); auto dry = b; run (p, b, 512, [&] (int k) { if (k == 100) set (p, "throw", 1); if (k == 120) set (p, "throw", 0); }); double before = 0, after = 0; for (int i = 4000; i < 512 * 100; ++i) before = std::max (before, (double) std::abs (b.getSample (0, i) - dry.getSample (0, i))); for (int i = 512 * 150; i < 512 * 200; ++i) after = std::max (after, (double) std::abs (b.getSample (0, i) - dry.getSample (0, i))); check (before == 0 && after > 0.02, "Last word: nothing echoes before the throw, the throw does", juce::String (before) + ", " + juce::String (after, 3));
        DelProcessor q; set (q, "fb", 50); set (q, "sync", 9); const double t = q.getTailLengthSeconds(); check (t > 4 && t < 8, "The tail is reported to the DAW", juce::String (t, 1) + " s for half-second echoes at 50 %"); set (q, "fb", 105); check (q.getTailLengthSeconds() >= 60, "Feeding itself: a minute", "");
        DelProcessor r; set (r, "send", 1); prepare (r, fs, 2); auto c = tone (fs, 4096, 300, 0.5f); run (r, c); float peak = 0; for (int i = 0; i < 4096; ++i) peak = std::max (peak, std::abs (c.getSample (0, i))); check (peak == 0.0f, "On a send nothing dry comes out", "");
        DelProcessor s; prepare (s, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (s.createEditor()); auto* ed = dynamic_cast<DelEditor*> (base.get()); ed->clearWords(); const bool a = s.real ("duck") == 9.0f && s.real ("lowcut") == 300.0f && s.real ("highcut") == 6000.0f; s.pluginState.history.undo (s.apvts); const bool undone = s.real ("duck") == 0.0f; set (s, "fb", 108); ed->calm(); check (a && undone && s.real ("fb") == 60.0f, "The helpers do what they say, one undo step each", "");
    }
    {
        DelProcessor p; set (p, "mix", 50); prepare (p, fs, 2); auto b = tone (fs, 512 * 500, 220, 0.25f); run (p, b, 512, [&] (int k) { if (k == 100) set (p, "style", 1); if (k == 150) set (p, "char", 2); if (k == 200) set (p, "sync", 6); if (k == 240) set (p, "reverse", 1); if (k == 280) set (p, "reverse", 0); if (k == 320) set (p, "bypass", 1); if (k == 350) set (p, "bypass", 0); if (k == 380) set (p, "send", 1); if (k == 410) set (p, "send", 0); if (k == 440) { set (p, "change", 0); set (p, "sync", 9); } });
        Vec x (b.getReadPointer (0) + 20000, b.getReadPointer (0) + b.getNumSamples()); juce::String where; for (int k = 60; k + 10 < 500; k += 10) { Vec part (b.getReadPointer (0) + 512 * k, b.getReadPointer (0) + 512 * (k + 10)); const double c = percept::clickDb (part, fs); if (c > 24) where << k << ": " << juce::String (c, 0) << " "; } check (percept::clickDb (x, fs) < 26, "Every switch is silent", juce::String (percept::clickDb (x, fs), 1) + " dB " + where);
    }
    stateTest<DelProcessor> (40, "Techno", [] (DelProcessor& p) { set (p, "right", 77); set (p, "throwOnly", 1); set (p, "send", 1); set (p, "ms", 777); });
    blockTest<DelProcessor> ([] (DelProcessor& p) { set (p, "mix", 60); set (p, "duck", 9); set (p, "char", 1); set (p, "drive", 40); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
