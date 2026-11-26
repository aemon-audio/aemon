// Aemon Low End plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonLowTests [--screenshots <dir>]
#include "LowEditor.h"
#include "PerceptReport.h"
#include "EffectTestKit.h"
#include "CloseKit.h"
#include "DrumKit.h"
#include <random>

using namespace kit;

namespace
{
constexpr double pi = 3.14159265358979;
void prepareSide (LowProcessor& p, double fs, int block = 512)
{
    juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo());
    p.setBusesLayout (l); p.setRateAndBufferSizeDetails (fs, block); p.prepareToPlay (fs, block);
}
juce::AudioBuffer<float> both (const std::vector<float>& bass, const std::vector<float>& kick) { juce::AudioBuffer<float> b (4, (int) bass.size()); for (int c = 0; c < 4; ++c) b.copyFrom (c, 0, (c < 2 ? bass : kick).data(), (int) bass.size()); return b; }
std::vector<float> bass808 (double fs, int hits, double beat, double hz, double phase, double vary, double amp = 0.5, unsigned seed = 3) { std::mt19937 g (seed); std::uniform_real_distribution<double> u (-1, 1); std::vector<float> x; for (int k = 0; k < hits; ++k) { const double ph = phase + vary * u (g) * pi; const int n = (int) (beat * fs); for (int i = 0; i < n; ++i) { const double t = i / fs; x.push_back ((float) (amp * (std::sin (2 * pi * hz * t + ph) + 0.12 * std::sin (4 * pi * hz * t + ph)) * std::min (1.0, t / 0.004) * std::exp (-t * 2.0) * std::min (1.0, (beat - t) / 0.01))); } } return x; }
/** The lows together against apart, over the whole sound: independent of the plugin. */
double together (const juce::AudioBuffer<float>& b, int lat, const std::vector<float>& kick, double fs, double below, const aemon::low::Measure& m)
{
    auto lp = [&] (std::vector<float> x) { aemon::low::Steep l, a, c; l.set (false, below, fs); a.set (false, 600, fs); c.set (false, 600, fs); aemon::low::Biquad h; h.set (true, 18, 0.7071, fs); for (auto& v : x) v = (float) h.run (l.run (c.run (a.run (v)))); return x; };
    std::vector<float> o (kick.size(), 0.0f); for (size_t i = 0; i + (size_t) lat < o.size(); ++i) o[i] = b.getSample (0, (int) i + lat);
    const auto x = lp (o), k = lp (kick); double s = 0, e = 0; for (auto& q : m.hits) for (int i = (int) (q.from * fs / m.fs); i < (int) (q.to * fs / m.fs) && i < (int) x.size() - 6000; ++i) { s += std::pow ((double) x[(size_t) i] + k[(size_t) i], 2); e += std::pow (x[(size_t) i], 2) + std::pow (k[(size_t) i], 2); }
    return 10 * std::log10 (s / e);
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<LowProcessor> ("Low End", argv[2]);
    const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const juce::File dir { juce::String (argv[2]) }; dir.createDirectory();
        for (auto mode : { "simple", "pro" })
        {
            LowProcessor p; prepareSide (p, fs, 768); p.pluginState.ui.setProperty ("mode", mode, nullptr); std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); ed->setSize (960, 600); auto* e = dynamic_cast<LowEditor*> (ed.get()); load (p, "808: make room");
            auto bass = bass808 (fs, 32, 0.5, 54, 3.4, 0, 0.35); auto kick = drumkit::loop ("kick", fs, 32, 0.5); for (auto& v : kick) v *= 0.7f;
            auto feed = [&] { auto b = both (bass, kick); run (p, b, 768, [&] (int) { e->tickForTest(); }); };
            feed(); if (juce::String (mode) == "pro") e->lineUp(); feed(); e->measureNow (true); for (int i = 0; i < 30; ++i) e->tickForTest();
            shotCard (dynamic_cast<LowEditor*> (ed.get())->getShell(), *ed, dir.getChildFile (juce::String ("lowend-") + mode + "-card.png"));
        for (float scale : { 1.0f, 2.0f }) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale); auto f = dir.getChildFile (juce::String ("lowend-") + mode + (scale > 1 ? "@2x" : "") + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os); std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8()); }
        }
        return 0;
    }
    nullTest<LowProcessor> ("Low End null test", 0.8f);
    nullTest<LowProcessor> ("Low End bypass", 0.8f, [] (LowProcessor& p) { set (p, "delay", 4); set (p, "turn", 70); set (p, "flip", 1); set (p, "duck", 20); set (p, "bypass", 1); });

    // Presets: as deep as they say, on a kick and a bass that share the lows; the clashing ones leave a bass that is elsewhere alone
    {
        LowProcessor probe; int tested = 0; const auto kick = drumkit::loop ("kick", fs, 12, 0.5);
        for (auto* preset : probe.pluginState.allPresets())
        {
            const auto in = lowp::intentOf (preset->name); if (preset->user || in.duck <= 0) continue;
            ++tested; LowProcessor p; load (p, preset->name.toRawUTF8()); prepareSide (p, fs); std::vector<float> bass; for (int i = 0; i < (int) kick.size(); ++i) bass.push_back ((float) (0.3 * std::sin (2 * pi * 50 * i / fs) + 0.15 * std::sin (2 * pi * 900 * i / fs)));
            const auto src = both (bass, kick); auto b = src; aemon::low::Engine::Frame fr[256]; float deep = 0; run (p, b, 512, [&] (int) { const int n = p.popLow (fr, 256); for (int i = 0; i < n; ++i) deep = std::max ({ deep, fr[i].cut[0], fr[i].cut[1], fr[i].cut[2] }); });
            const int lat = p.getLatencySamples(); double worstHigh = 0; for (int k = 2; k < 10; ++k) { double re = 0, im = 0, r0 = 0, i0 = 0; for (int i = 0; i < 4800; ++i) { const int s = (int) (0.5 * k * fs) + i; const double w = 2 * pi * 900 * s / fs; re += b.getSample (0, s + lat) * std::cos (w); im += b.getSample (0, s + lat) * std::sin (w); r0 += bass[(size_t) s] * std::cos (w); i0 += bass[(size_t) s] * std::sin (w); } worstHigh = std::max (worstHigh, std::abs (10 * std::log10 ((re * re + im * im) / (r0 * r0 + i0 * i0)))); }
            const bool number = preset->description.containsIgnoreCase ("up to " + juce::String ((int) in.duck) + " dB"); bool line = true; for (int hz : { 80, 90, 120, 180, 200 }) if (preset->description.contains ("under " + juce::String (hz) + " Hz")) line &= std::abs (p.real ("below") - (float) hz) < 1.5f;
            check (deep <= in.duck + 0.01f && deep > in.duck - 0.35f && worstHigh < 0.1 && number && line && (p.real ("clash") > 0.5f) == in.clash, "Preset: " + preset->name, "deepest " + juce::String (deep, 2) + " of " + juce::String (in.duck, 0) + " dB, at 900 Hz " + juce::String (worstHigh, 4) + " dB");
        }
        check (tested == 22, "Presets tested", juce::String (tested));
    }
    // Line them up, through the window: the sound that comes out really is louder together, and the window's numbers are the measured ones
    {
        const auto kick = drumkit::loop ("kick", fs, 20, 0.45); juce::String list; bool ok = true; double worst = 0;
        for (double ph : { 0.6, 2.2, 3.6, 5.0 })
            for (double hz : { 46.0, 55.0 })
            {
                const auto bass = bass808 (fs, 20, 0.45, hz, ph, 0); LowProcessor p; prepareSide (p, fs); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<LowEditor*> (base.get());
                auto b = both (bass, kick); run (p, b); ed->measureNow (true); const double said = ed->reading; aemon::low::Measure m = ed->measure; auto shift = (int) (kick.size() * m.fs / fs) - (int) m.kick.size(); for (auto& h : m.hits) { h.from += shift; h.to += shift; }
                const double before = together (b, p.getLatencySamples(), kick, fs, 120, m); worst = std::max (worst, std::abs (said - before));
                const auto adv = ed->advice; ed->lineUp(); const double promised = ed->reading; auto c = both (bass, kick); run (p, c); const double after = together (c, p.getLatencySamples(), kick, fs, 120, m); worst = std::max (worst, std::abs (promised - after));
                const bool helps = adv.say == aemon::low::Advice::Say::helps; ok &= helps ? after > before + 0.6 : p.real ("delay") == 0.0f && p.real ("flip") == 0.0f; list << juce::String (before, 1) << ">" << juce::String (after, 1) << (helps ? " " : " (left) ");
            }
        check (ok, "Line them up makes the lows louder together, or leaves them alone", list.trim()); check (worst < 0.15, "The window's numbers are the measured sums", "worst " + juce::String (worst, 3) + " dB over 16 readings");
    }
    {
        const auto kick = drumkit::loop ("kick", fs, 20, 0.45); LowProcessor p; prepareSide (p, fs); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<LowEditor*> (base.get());
        ed->lineUp(); const bool a = p.real ("delay") == 0.0f; auto b = both (bass808 (fs, 20, 0.45, 52, 0, 1, 0.5, 5), kick); run (p, b); ed->lineUp(); const bool c = p.real ("delay") == 0.0f && p.real ("flip") == 0.0f && p.real ("turn") == 0.0f;
        check (a && c, "Nothing heard, or a free-running 808: nothing is moved", "");
        auto d = both (bass808 (fs, 20, 0.45, 55, 3.6, 0), kick); run (p, d); ed->lineUp(); const float del = p.real ("delay"); const float fl = p.real ("flip"), tu = p.real ("turn"); ed->tickForTest(); ed->tickForTest(); load (p, "808: kick on top"); ed->tickForTest(); const bool kept = p.real ("delay") == del && p.real ("flip") == fl && p.real ("turn") == tu && p.real ("duck") == 12.0f; ed->putBack(); check ((del != 0 || fl != 0 || tu != 0) && kept && p.real ("delay") == 0.0f && p.real ("flip") == 0.0f && p.real ("turn") == 0.0f, "Choosing a preset keeps the line-up, Put it back undoes it", juce::String (del, 2) + " ms, flip " + juce::String (fl, 0) + ", turn " + juce::String (tu, 0));
        ed->duckClash(); check (p.real ("clash") == 1.0f && p.real ("duck") >= 6.0f, "Duck what clashes", juce::String (p.real ("duck"), 0) + " dB");
    }
    {
        LowProcessor p; set (p, "duck", 12); prepareSide (p, fs); std::vector<float> tone80; for (int i = 0; i < 512 * 300; ++i) tone80.push_back ((float) (0.3 * std::sin (2 * pi * 80 * i / fs))); auto b = both (tone80, std::vector<float> (tone80.size(), 0.0f)); run (p, b, 512, [&] (int k) { if (k == 60) set (p, "delay", 6); if (k == 90) set (p, "flip", 1); if (k == 120) set (p, "turn", 90); if (k == 150) set (p, "below", 80); if (k == 180) set (p, "bypass", 1); if (k == 210) set (p, "bypass", 0); });
        const float step = largestStep (b, 6000), limit = 0.3f * (float) (2 * pi * 80 / fs) * 2.2f; check (step < limit, "Every switch is silent", juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }
    stateTest<LowProcessor> (23, "House bass", [] (LowProcessor& p) { set (p, "delay", -3.21f); set (p, "turn", 40); set (p, "flip", 1); });
    blockTest<LowProcessor> ([] (LowProcessor& p) { set (p, "delay", 2); set (p, "flip", 1); set (p, "duck", 9); });

    {
        std::atomic<int> searches { 0 };
        closekit::closeUnderLoad<LowProcessor> ("Low End and its window", 12, [&] (LowProcessor& p, int) { prepareSide (p, fs); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<LowEditor*> (base.get()); const auto kick = drumkit::loop ("kick", fs, 12, 0.45); juce::AudioBuffer<float> b (4, (int) kick.size()); for (int c = 0; c < 4; ++c) for (int k = 0; k < b.getNumSamples(); ++k) b.setSample (c, k, c < 2 ? 0.4f * (float) std::sin (k * 0.0068) : kick[(size_t) k]); run (p, b); for (int k = 0; k < 420; ++k) { ed->tickForTest(); if (ed->searching()) { ++searches; break; } } }, check);
        check (searches >= 10, "The window was closed in the middle of a search", juce::String (searches.load()) + " of 12");
    }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
