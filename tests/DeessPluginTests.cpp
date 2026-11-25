// Aemon De-esser plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonDeessTests [--screenshots <dir> [music.wav]] | [--wav <file>]
#include "DeessEditor.h"
#include "EffectTestKit.h"
#include "VoiceKit.h"

using namespace kit;

namespace
{
juce::AudioBuffer<float> stereo (const std::vector<float>& x) { juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); return b; }
float p80 (std::vector<float> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) (0.8 * (double) v.size()))]; }
double changeDb (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int lat, int from, int to) { double a = 0, b = 0; for (int i = from; i < to && i + lat < out.getNumSamples(); ++i) { a += std::pow (in.getSample (0, i), 2); b += std::pow (out.getSample (0, i + lat), 2); } return 10 * std::log10 ((b + 1e-20) / (a + 1e-20)); }
struct Named { const char* name; voicekit::Kind kind; };
const Named voices[] { { "middle", {} }, { "low", { 0.5, 0.75, 1.0, 0.92, 0.5, 7 } }, { "high", { 1.8, 1.2, 1.2, 1.15, 0.3, 11 } }, { "soft S", { 0.9, 0.95, 0.55, 1.0, 0.6, 3 } }, { "very sharp S", { 1.2, 1.1, 2.2, 1.0, 0.4, 21 } } };
void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b) { f.deleteFile(); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--wav") { writeWav (juce::File (juce::String (argv[2])), stereo (voicekit::takes (fs, {}, { 0, -12 }).x)); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        // the singer, unless another sound is given
        const auto tmp = juce::File::createTempFile (".wav"); writeWav (tmp, stereo (voicekit::takes (fs, {}, { 0, -12 }).x));
        const auto path = tmp.getFullPathName(); std::string own = path.toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<DeessProcessor, DeessEditor> (4, args, "deesser", [] (DeessProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Pop vocal" : "Vocal: smooth"); }, [] (DeessEditor& e) { e.findMyS(); });
        tmp.deleteFile(); return rc;
    }

    nullTest<DeessProcessor> ("De-esser null test", 0.8f);
    nullTest<DeessProcessor> ("De-esser bypass", 0.8f, [] (DeessProcessor& p) { set (p, "amount", 100); set (p, "line", -24); set (p, "bypass", 1); });

    // Find my S: the right place, then about 4.5 dB on the sharper ones, for very different voices
    for (auto& v : voices)
    {
        const auto take = voicekit::takes (fs, v.kind, { 0, 0 });
        DeessProcessor p; prepare (p, fs, 2);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
        auto b = stereo (take.x); run (p, b, 512, [&] (int k) { if (k % 2 == 0) ed->tickForTest(); });
        ed->findMyS();
        const int before = p.heard.load(); auto c = stereo (take.x); run (p, c); const auto in = stereo (take.x);
        auto cuts = p.recentCuts(); const int got = std::min (p.heard.load() - before, (int) cuts.size()); cuts.erase (cuts.begin(), cuts.end() - got);
        bool exact = true; for (auto& s : take.sounds) if (s.kind == 'v') for (int i = s.from; i < s.to; ++i) exact &= c.getSample (0, i + p.getLatencySamples()) == in.getSample (0, i);
        const float f = p.real ("freq"), expect = 7000.0f * (float) v.kind.sFreq;
        check (std::abs (std::log2 (f / expect)) < 0.3f && std::abs (p80 (cuts) - 4.5f) < 0.8f && exact && got >= 12, juce::String ("Find my S, ") + v.name + " voice", juce::String (f / 1000, 2) + " kHz (about " + juce::String (expect / 1000, 2) + "), sharper ones " + juce::String (p80 (cuts), 2) + " dB, vowels " + (exact ? "bit-exact" : "changed"));
    }
    {
        DeessProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
        ed->findMyS(); const bool still = p.real ("amount") == 0.0f; auto t = tone (fs, (int) (4 * fs), 300, 0.3f); run (p, t); ed->findMyS();
        check (still && p.real ("amount") == 0.0f, "Find my S changes nothing when there is no S", "silence and a plain tone");
        auto b = stereo (voicekit::takes (fs, {}, { 0, 0 }).x); run (p, b); ed->findMyS(); const float amount = p.real ("amount"), line = p.real ("line"); p.pluginState.history.undo (p.apvts);
        check (amount == 70.0f && line != -10.0f && p.real ("amount") == 0.0f && p.real ("line") == -10.0f, "Find my S is one undo step", juce::String (amount, 0) + " % and back");
    }

    for (double r : { 44100.0, 96000.0, 192000.0 })
    {
        DeessProcessor p; prepare (p, r, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
        const auto take = voicekit::takes (r, {}, { 0, 0 }); auto b = stereo (take.x); run (p, b);
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); ed->findMyS(); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        auto c = stereo (take.x); run (p, c);
        check (std::abs (std::log2 (p.real ("freq") / 6850.0f)) < 0.1f && std::abs (p80 (p.recentCuts()) - 4.5f) < 0.5f && ms < 1500, "Find my S at " + juce::String (r / 1000, 1) + " kHz", juce::String (p.real ("freq") / 1000, 2) + " kHz, " + juce::String (p80 (p.recentCuts()), 2) + " dB, took " + juce::String (ms, 0) + " ms");
    }

    // Presets: every voice preset does what it says on every voice, once it has heard it
    {
        DeessProcessor probe; int tested = 0;
        for (auto* preset : probe.pluginState.allPresets())
        {
            const auto in = deessp::intentOf (preset->name);
            if (preset->user || ! in.aim) continue;
            ++tested; bool ok = true; juce::String list; double worstVowel = 0;
            for (auto& v : voices)
            {
                const auto take = voicekit::takes (fs, v.kind, { 0, 0 });
                DeessProcessor p; prepare (p, fs, 2);
                std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
                auto b = stereo (take.x); run (p, b); ed->tickForTest();
                load (p, preset->name.toRawUTF8()); ed->tickForTest();
                const int before = p.heard.load(); const auto src = stereo (take.x); auto c = src; run (p, c);
                auto cuts = p.recentCuts(); cuts.erase (cuts.begin(), cuts.end() - std::min (p.heard.load() - before, (int) cuts.size()));
                const float got = p80 (cuts), range = p.real ("range"); double vowel = 0; for (auto& s : take.sounds) if (s.kind == 'v') vowel = std::max (vowel, std::abs (changeDb (src, c, p.getLatencySamples(), s.from, s.to))); const bool exact = vowel < 0.1;      // the bar of the plan
                worstVowel = std::max (worstVowel, vowel);
                const bool lineAtEnd = p.real ("line") < -23.9f;      // a voice so soft that even the lowest line takes less
                ok &= exact && got <= range + 0.01f && (std::abs (got - in.downDb) < std::max (1.0f, 0.12f * in.downDb) || (lineAtEnd && got < in.downDb && got > 0.5f * in.downDb));
                list << juce::String (got, 1) << (exact ? " " : "(vowels!) ");
            }
            check (ok, "Preset on five voices: " + preset->name, juce::String (in.downDb, 1) + " dB wanted, got " + list.trim() + ", vowels " + juce::String (worstVowel, 3));
        }
        check (tested >= 18, "Voice presets tested", juce::String (tested));
    }
    // ... and is useful, never absurd, on the numbers it comes with
    {
        DeessProcessor probe; juce::String off; int n = 0;
        for (auto* preset : probe.pluginState.allPresets())
        {
            const auto in = deessp::intentOf (preset->name); if (preset->user || ! in.aim) continue;
            for (auto& v : { voices[0], voices[2] })
            {
                DeessProcessor p; load (p, preset->name.toRawUTF8()); prepare (p, fs, 2); auto b = stereo (voicekit::takes (fs, v.kind, { 0, 0 }).x); run (p, b); const float got = p80 (p.recentCuts()); ++n;
                if (got < 0.25f * in.downDb || got > p.real ("range") + 0.01f || (in.downDb < 9.0f && got > in.downDb + 5.5f)) off << preset->name << " " << v.name << " " << juce::String (got, 1) << " of " << juce::String (in.downDb, 1) << "; ";
            }
        }
        check (off.isEmpty(), "Presets on their own numbers, before anything is heard", off.isEmpty() ? juce::String (n) + " combinations sensible" : off);
    }
    // Instruments
    {
        for (auto name : { "Hi-hats", "Overheads" })
        {
            juce::AudioBuffer<float> hat (2, (int) (6 * fs)); juce::Random r (4); voicekit::Bp f; const bool cym = juce::String (name) == "Overheads";
            for (int i = 0; i < hat.getNumSamples(); ++i) { const int hit = (int) (i / (0.25 * fs)); const double t = std::fmod (i / fs, 0.25); const float s = (float) (f.run (r.nextDouble() * 2 - 1, 8500, 0.6, fs) * std::exp (-t * (cym ? 12 : 55)) * (hit % 2 == 0 ? 0.8 : 0.08)); for (int c = 0; c < 2; ++c) hat.setSample (c, i, s); }
            DeessProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
            auto b = hat; run (p, b); ed->tickForTest(); load (p, name); ed->tickForTest(); auto c = hat; run (p, c);
            double loud = 0, soft = 0; for (int h = 4; h < 20; ++h) { const int from = (int) (h * 0.25 * fs); (h % 2 == 0 ? loud : soft) += changeDb (hat, c, p.getLatencySamples(), from, from + (int) (0.05 * fs)) / 8; }
            const float want = deessp::intentOf (p.pluginState.current.name).downDb;
            check (loud < -0.4 * want && loud > -1.3 * want && soft > -0.35 * want, juce::String ("Preset: ") + p.pluginState.current.name, "loud hits " + juce::String (loud, 2) + " dB, quiet hits " + juce::String (soft, 2) + " dB");
        }
        {
            juce::AudioBuffer<float> gtr (2, (int) (8 * fs)); juce::Random r (9); voicekit::Bp f; const double tau = juce::MathConstants<double>::twoPi;
            for (int i = 0; i < gtr.getNumSamples(); ++i) { const double t = i / fs, beat = std::fmod (t, 1.0); double x = 0; for (int k = 1; k < 14; ++k) x += 0.25 / k * std::sin (tau * 147.0 * k * t) * std::exp (-beat * (1.5 + 0.7 * k)); if (beat > 0.86) x = x * 0.1 + f.run (r.nextDouble() * 2 - 1, 5200, 1.5, fs) * 0.28; for (int c = 0; c < 2; ++c) gtr.setSample (c, i, (float) x); }
            DeessProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
            auto b = gtr; run (p, b); ed->tickForTest(); load (p, "Acoustic guitar"); ed->tickForTest(); auto c = gtr; run (p, c); double sq = 0, note = 0; for (int k = 2; k < 7; ++k) { sq += changeDb (gtr, c, p.getLatencySamples(), (int) ((k + 0.88) * fs), (int) ((k + 0.99) * fs)) / 5; note = std::max (note, std::abs (changeDb (gtr, c, p.getLatencySamples(), (int) ((k + 0.1) * fs), (int) ((k + 0.8) * fs)))); }
            check (sq < -2.5 && note < 0.05, "Preset: " + p.pluginState.current.name, "squeaks " + juce::String (sq, 2) + " dB, notes " + juce::String (note, 4) + " dB");
        }
        for (auto name : { "Mix: the vocal", "Master: one S" })
        {
            auto mix = music (fs, 13.0, -16.0f); const auto v = voicekit::takes (fs, {}, { 0, 0 }); juce::Random r (2); for (int i = 0; i < mix.getNumSamples(); ++i) { const float side = 0.02f * (r.nextFloat() * 2 - 1), s = i < (int) v.x.size() ? v.x[(size_t) i] : 0; mix.addSample (0, i, s + side); mix.addSample (1, i, s - side); }
            DeessProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
            auto b = mix; run (p, b); ed->tickForTest(); load (p, name); ed->tickForTest(); auto c = mix; run (p, c); const int lat = p.getLatencySamples(); double side = 0; for (int i = (int) fs; i + lat < c.getNumSamples(); ++i) side = std::max (side, (double) std::abs ((c.getSample (0, i + lat) - c.getSample (1, i + lat)) - (mix.getSample (0, i) - mix.getSample (1, i))));
            const float got = p80 (p.recentCuts()), want = deessp::intentOf (p.pluginState.current.name).downDb, range = p.real ("range");
            check (got > 0.4f * want && got <= range + 0.01f && side < 5e-7, juce::String ("Preset: ") + p.pluginState.current.name, "S in the middle " + juce::String (got, 2) + " dB, at most " + juce::String (range, 1) + ", sides changed by " + juce::String (side * 1e9, 0) + "e-9");
        }
    }

    // Latency follows the lookahead, nothing clicks
    {
        DeessProcessor p; set (p, "amount", 60); set (p, "line", -12); prepare (p, fs, 2); const int lat0 = p.getLatencySamples(); int lat1 = 0;
        auto b = tone (fs, 512 * 400, 300, 0.4f);
        run (p, b, 512, [&] (int k) { if (k == 100) set (p, "look", 5); if (k == 140) { lat1 = p.getLatencySamples(); set (p, "look", 2); } if (k == 200) set (p, "bypass", 1); if (k == 230) set (p, "bypass", 0); if (k == 260) set (p, "shape", 1); if (k == 290) set (p, "on", 1); if (k == 320) set (p, "solo", 1); if (k == 350) set (p, "solo", 0); });
        const float step = largestStep (b, 2000), limit = 0.4f * (float) (juce::MathConstants<double>::twoPi * 300 / fs) * 2.2f;
        check (lat0 == 256 && lat1 == 400 && p.getLatencySamples() == 256 && step < limit, "Latency follows the lookahead, every switch is silent", juce::String (lat0) + ", " + juce::String (lat1) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }
    // Helpers and listening buttons
    {
        DeessProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<DeessEditor*> (base.get());
        set (p, "amount", 70); set (p, "range", 8); ed->less(); const float a = p.real ("amount"), r = p.real ("range"); for (int i = 0; i < 20; ++i) ed->less();
        check (a == 56.0f && r == 7.0f && p.real ("range") >= 2.0f, "Less backs off, and stays safe", juce::String (a, 0) + " %, " + juce::String (r, 1) + " dB, in the end " + juce::String (p.real ("range"), 1) + " dB");
    }

    {
        DeessProcessor p; set (p, "amount", 100); set (p, "line", -24); set (p, "range", 20); prepare (p, fs, 2); auto b = stereo (voicekit::takes (fs, {}, { 0 }).x); run (p, b);
        const auto deep = p.recentCuts(); set (p, "amount", 20); juce::AudioBuffer<float> gap (2, 4096); gap.clear(); run (p, gap); const bool cleared = p.recentCuts().empty(); auto c = stereo (voicekit::takes (fs, {}, { 0 }).x); run (p, c); const auto after = p.recentCuts();
        check (p80 (deep) > 10 && cleared && after.size() >= 5 && *std::max_element (after.begin(), after.end()) < 8.0f, "The check speaks about the setting as it is now", juce::String (p80 (deep), 1) + " dB before, " + juce::String (p80 (after), 1) + " after");
    }

    stateTest<DeessProcessor> (28, "Podcast", [] (DeessProcessor& p) { set (p, "on", 2); set (p, "look", 3.5f); set (p, "follow", 1); set (p, "freq", 4810); });
    blockTest<DeessProcessor> ([] (DeessProcessor& p) { set (p, "amount", 100); set (p, "line", -24); set (p, "strict", 100); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
