// Aemon Saturation plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonSatTests [--screenshots <dir> [music.wav]]
#include "SatEditor.h"
#include "EffectTestKit.h"

using namespace kit;

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
        return screenshots<SatProcessor, SatEditor> (argc, argv, "saturation",
            [] (SatProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Drum bus" : "Mix bus: warmth"); }, {});

    nullTest<SatProcessor> ("Saturation null test", 0.8f);
    nullTest<SatProcessor> ("Saturation bypass", 0.8f, [] (SatProcessor& p) { set (p, "drive", 18); set (p, "bypass", 1); });

    // What the window says it adds is what it adds: every harmonic above -60 dB of a 1 kHz tone at -12 dB, every character, focus and tone
    for (int ch = 0; ch < 4; ++ch)
    {
        double worst = 0; int lines = 0; juce::String where;
        for (int focus = 0; focus < 4; ++focus)
            for (float drive : { 6.0f, 12.0f, 20.0f })
                for (float toneDb : { 0.0f, -4.0f })
                {
                    SatProcessor p; set (p, "drive", drive); set (p, "char", (float) ch); set (p, "focus", (float) focus); set (p, "tone", toneDb); set (p, "match", 0); prepare (p, 48000, 2);
                    auto b = tone (48000, 72000, 1000, 0.25f); run (p, b);
                    const auto said = satp::harmonics (p.readParams());
                    const double first = lineDb (b, 48000, 1000, 24000, 48000);
                    for (int h = 2; h <= 9; ++h) if (said[(size_t) h - 1] > -60.0f)
                    {
                        const double d = std::abs (lineDb (b, 48000, 1000.0 * h, 24000, 48000) - first - said[(size_t) h - 1]);
                        ++lines; if (d > worst) { worst = d; where = juce::String (h) + " at drive " + juce::String (drive, 0) + ", focus " + juce::String (focus); }
                    }
                }
        check (worst < 1.0 && lines > 50, juce::String ("The harmonics shown are the harmonics added: ") + satp::chars[ch].name, juce::String (lines) + " compared, worst difference " + juce::String (worst, 2) + " dB (harmonic " + where + ")");
    }

    // Level matched: music comes out as loud as it went in, whatever the drive
    for (float drive : { 6.0f, 15.0f, 28.0f })
        for (int ch : { 0, 3 })
        {
            const double fs = 48000; auto src = music (fs, 14.0, -10.0f);
            SatProcessor p; set (p, "drive", drive); set (p, "char", (float) ch); prepare (p, fs, 2); auto b = src; run (p, b);
            const float in = lufs (src, fs, nullptr, (int) fs * 7), out = lufs (b, fs, nullptr, (int) fs * 7);
            check (std::abs (out - in) < 0.3f, "Level matched, drive " + juce::String (drive, 0) + " dB, " + satp::chars[ch].name, juce::String (out - in, 2) + " LU from the original");
        }

    // Switching anything is silent
    {
        SatProcessor p; set (p, "drive", 12); prepare (p, 48000, 2);
        const int lat0 = p.getLatencySamples(); int lat1 = 0;
        auto b = tone (48000, 512 * 600, 300, 0.4f);
        run (p, b, 512, [&] (int k)
        {
            if (k == 100) set (p, "os", 1);
            if (k == 130) { lat1 = p.getLatencySamples(); set (p, "os", 3); }
            if (k == 260) set (p, "focus", 1);
            if (k == 300) set (p, "char", 1);
            if (k == 340) set (p, "tone", 4);
            if (k == 380) set (p, "bypass", 1);
            if (k == 420) set (p, "bypass", 0);
            if (k == 460) set (p, "mix", 30);
            if (k == 500) set (p, "drive", 0);
        });
        const float peak = b.getMagnitude (0, 100000, b.getNumSamples() - 100000), step = largestStep (b, 2000), limit = std::max (0.5f, peak) * (float) (juce::MathConstants<double>::twoPi * 300.0 / 48000.0) * 2.2f;
        check (lat0 == 134 && lat1 == 126 && p.getLatencySamples() == 134 && step < limit, "Every switch is silent, latency follows oversampling", juce::String (lat0) + ", " + juce::String (lat1) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }

    // Helpers
    {
        juce::String list; bool ok = true;
        for (int c = 0; c < 4; ++c) { aemon::sat::Params k; k.character = (aemon::sat::Character) c; const float d = satp::driveFor (k, 4.0f); k.driveDb = d; const float t = satp::thd (satp::harmonics (k)); ok &= std::abs (t - 4.0f) < 0.4f; list << satp::chars[c].name << " " << juce::String (d, 1) << " dB = " << juce::String (t, 2) << " %  "; }
        check (ok, "Set amount for me lands on 4 %", list);
        SatProcessor p; prepare (p, 48000, 2);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<SatEditor*> (base.get());
        ed->setAmountForMe(); const float before = p.real ("drive"); ed->cleaner(); const float after = p.real ("drive"); p.pluginState.history.undo (p.apvts);
        check (before > 1.0f && std::abs (before - after - std::min (3.0f, before)) < 0.06f && std::abs (p.real ("drive") - before) < 0.06f, "Cleaner backs off 3 dB, undo brings it back", juce::String (before, 1) + " to " + juce::String (after, 1));
    }

    stateTest<SatProcessor> (14, "Guitars", [] (SatProcessor& p) { set (p, "tone", -2.5f); set (p, "out", 3); set (p, "os", 2); set (p, "match", 0); });
    blockTest<SatProcessor> ([] (SatProcessor& p) { set (p, "drive", 14); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
