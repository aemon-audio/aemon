// Aemon Transient plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonTransTests [--screenshots <dir> [music.wav]]
#include "TransEditor.h"
#include "PerceptReport.h"
#include "EffectTestKit.h"
#include "DrumKit.h"

using namespace kit;

namespace
{
juce::AudioBuffer<float> stereo (const std::vector<float>& x) { juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); return b; }
double changeDb (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int lat, double from, double to, double fs) { double a = 1e-20, b = 1e-20; for (int i = (int) (from * fs); i < (int) (to * fs) && i + lat < out.getNumSamples(); ++i) { a += std::pow (in.getSample (0, i), 2); b += std::pow (out.getSample (0, i + lat), 2); } return 10 * std::log10 (b / a); }
/** The same, of the part between two frequencies. */
juce::AudioBuffer<float> part (const juce::AudioBuffer<float>& b, int focus, double fs)
{
    if (focus == 0) return b;
    auto o = b; aemon::trans::Biquad h, l; h.set (true, focus == 3 ? 3000 : 250, 0.7071, fs); l.set (false, focus == 1 ? 250 : 2500, 0.7071, fs);
    for (int i = 0; i < b.getNumSamples(); ++i) { double v = b.getSample (0, i); if (focus >= 2) v = h.run (v); if (focus <= 2) v = l.run (v); o.setSample (0, i, (float) v); }
    return o;
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<TransProcessor> ("Transient", argv[2]);
    const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { auto x = drumkit::loop ("snare", fs, 8, 0.6); for (auto& v : x) v *= 0.7f; const auto b = stereo (x); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        const auto path = tmp.getFullPathName(); std::string own = path.toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<TransProcessor, TransEditor> (4, args, "transient", [] (TransProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Snare: fatter" : "Snare: crack and dry"); }, [] (TransEditor&) {});
        tmp.deleteFile(); return rc;
    }
    nullTest<TransProcessor> ("Transient null test", 0.8f);
    nullTest<TransProcessor> ("Transient bypass", 0.8f, [] (TransProcessor& p) { set (p, "attack", 12); set (p, "body", 12); set (p, "out", 5); set (p, "bypass", 1); });

    // Presets: each on the drum it is for. What it says it does, nothing it does not say, and the number in its text is the most it does
    {
        TransProcessor probe; int tested = 0;
        for (auto* preset : probe.pluginState.allPresets())
        {
            const auto in = transp::intentOf (preset->name); if (preset->user || in.drum == nullptr) continue;
            ++tested; const auto src = stereo (drumkit::loop (in.drum, fs, 9, 0.5)); TransProcessor p; load (p, preset->name.toRawUTF8()); prepare (p, fs, 2); auto out = src; run (p, out);
            const int lat = p.getLatencySamples(); const auto a = part (src, in.focus, fs), b = part (out, in.focus, fs); double hit = 0, body = 0, most = -99, least = 99;
            for (int k = 2; k < 8; ++k) { hit += changeDb (a, b, lat, 0.5 * k, 0.5 * k + (juce::String (in.drum) == "kick" && in.focus == 1 ? 0.015 : 0.004), fs) / 6; body += changeDb (a, b, lat, 0.5 * k + 0.11, 0.5 * k + 0.28, fs) / 6; for (int w = 0; w < 40; ++w) { const double c = changeDb (src, out, lat, 0.5 * k + 0.01 * w, 0.5 * k + 0.01 * w + 0.01, fs); most = std::max (most, c); least = std::min (least, c); } }
            const bool hitOk = in.attack == 0 ? std::abs (hit) < 0.6 : (in.focus == 0 ? std::abs (hit - in.attack) < 1.2 : hit > 0.4 * in.attack && hit < in.attack + 0.5);
            const bool bodyOk = in.body == 0 ? std::abs (body) < 0.6 : body / in.body > 0.4 && body / in.body < 1.01;
            const double top = std::max ({ 0.0f, in.attack, in.body }), low = std::min ({ 0.0f, in.attack, in.body }); bool number = false; const int biggest = (int) std::max (std::abs (in.attack), std::abs (in.body)); number = preset->description.contains ("p to " + juce::String (biggest) + " dB");
            check (hitOk && bodyOk && number && most < top + 0.35 && least > low - 0.35, "Preset on a " + juce::String (in.drum) + ": " + preset->name, "hit " + juce::String (hit, 2) + " (says " + juce::String (in.attack, 0) + "), body " + juce::String (body, 2) + " (says " + juce::String (in.body, 0) + "), between " + juce::String (least, 2) + " and " + juce::String (most, 2));
        }
        check (tested == 31, "Presets tested", juce::String (tested));
        // the low end under "click for small speakers"
        TransProcessor p; load (p, "Kick: click"); prepare (p, fs, 2); const auto src = stereo (drumkit::loop ("kick", fs, 6, 0.5)); auto out = src; run (p, out); const auto a = part (src, 1, fs), b = part (out, 1, fs); double w = 0; for (int k = 1; k < 5; ++k) w = std::max (w, std::abs (changeDb (a, b, p.getLatencySamples(), 0.5 * k + 0.004, 0.5 * k + 0.3, fs))); check (w < 0.25, "Kick: click leaves the low end alone", juce::String (w, 3) + " dB under 250 Hz");
        // cymbals under "kick forward, cymbals untouched"
        TransProcessor q; load (q, "Loop: kick forward"); prepare (q, fs, 2); auto k1 = drumkit::loop ("kick", fs, 6, 0.5); const auto hats = drumkit::loop ("hat", fs, 12, 0.25); for (size_t i = 0; i < k1.size(); ++i) k1[i] += hats[i]; const auto s2 = stereo (k1); auto o2 = s2; run (q, o2); const auto c = part (s2, 3, fs), d = part (o2, 3, fs); double w2 = 0; for (int k = 1; k < 5; ++k) w2 = std::max (w2, std::abs (changeDb (c, d, q.getLatencySamples(), 0.5 * k, 0.5 * k + 0.4, fs))); check (w2 < 0.25, "Loop: kick forward leaves the cymbals alone", juce::String (w2, 3) + " dB over 3 kHz");
    }
    // The window's picture measures what happens
    {
        TransProcessor p; set (p, "attack", 6); set (p, "body", -8); prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<TransEditor*> (base.get());
        auto src = stereo (drumkit::loop ("snare", fs, 16, 0.5)); src.applyGain (0.4f); auto b = src; run (p, b, 512, [&] (int) { ed->tickForTest(); }); for (int i = 0; i < 5; ++i) ed->tickForTest();
        double hit = 0, body = 0; for (int k = 8; k < 15; ++k) { body += changeDb (src, b, p.getLatencySamples(), 0.5 * k + 0.1, 0.5 * k + 0.25, fs) / 7; float pi = 0, po = 0; for (int i = 0; i < 576; ++i) { pi = std::max (pi, std::abs (src.getSample (0, (int) (0.5 * k * fs) + i))); po = std::max (po, std::abs (b.getSample (0, (int) (0.5 * k * fs) + i + p.getLatencySamples()))); } hit += 20 * std::log10 (po / pi) / 7; }
        check (ed->hits >= 12 && std::abs (ed->hitChange - hit) < 0.5 && std::abs (ed->bodyChange - body) < 0.8, "The window says what is measured", juce::String (ed->hits) + " hits, hit " + juce::String (ed->hitChange, 2) + " (measured " + juce::String (hit, 2) + "), body " + juce::String (ed->bodyChange, 2) + " (" + juce::String (body, 2) + ")");
        ed->samePeaks(); const float out = p.real ("out"); auto c = src; run (p, c); float pi = 0, po = 0; for (int i = 48000; i < c.getNumSamples(); ++i) { pi = std::max (pi, std::abs (src.getSample (0, i))); po = std::max (po, std::abs (c.getSample (0, i))); }
        check (std::abs (20 * std::log10 (po / pi)) < 0.6 && out < -3, "Same peaks puts the peaks back", "output " + juce::String (out, 1) + " dB, peaks " + juce::String (20 * std::log10 (po / pi), 2) + " dB apart");
        ed->punch(); ed->tighter(); check (p.real ("attack") == 8.0f && p.real ("body") == -10.0f, "More punch and Tighter are 2 dB steps", ""); for (int i = 0; i < 9; ++i) { ed->punch(); ed->tighter(); } check (p.real ("attack") == 12.0f && p.real ("body") == -12.0f, "... and stop at the ends", "");
    }
    // Equal loudness
    {
        TransProcessor p; set (p, "body", 10); set (p, "match", 1); prepare (p, fs, 2); auto src = music (fs, 16, -14); auto b = src; run (p, b); const float a = lufs (src, fs, nullptr, 48000 * 8), o = lufs (b, fs, nullptr, 48000 * 8); check (std::abs (a - o) < 0.5f, "Equal loudness", juce::String (a, 2) + " and " + juce::String (o, 2) + " LUFS");
        p.setNonRealtime (true); prepare (p, fs, 2); auto c = src; run (p, c); check (lufs (c, fs, nullptr, 48000 * 8) > a + 1, "Equal loudness is never in an export", juce::String (lufs (c, fs, nullptr, 48000 * 8) - a, 2) + " LU louder");
    }
    {
        TransProcessor p; set (p, "attack", 12); set (p, "body", 12); prepare (p, fs, 2); auto b = tone (fs, 512 * 300, 100, 0.2f); run (p, b, 512, [&] (int k) { if (k == 60) set (p, "focus", 1); if (k == 90) set (p, "focus", 3); if (k == 120) set (p, "delta", 1); if (k == 150) set (p, "delta", 0); if (k == 180) set (p, "bypass", 1); if (k == 210) set (p, "bypass", 0); if (k == 240) set (p, "out", 6); });
        const float step = largestStep (b, 3000), limit = 0.4f * (float) (juce::MathConstants<double>::twoPi * 100 / fs) * 1.3f; check (step < limit, "Every switch is silent", juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }
    stateTest<TransProcessor> (32, "Toms: no", [] (TransProcessor& p) { set (p, "focus", 2); set (p, "out", -3.5f); set (p, "alen", 33); set (p, "blen", 415); });
    blockTest<TransProcessor> ([] (TransProcessor& p) { set (p, "attack", 8); set (p, "body", -8); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
