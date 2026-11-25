// Aemon Clipper plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonClipTests [--screenshots <dir> [music.wav]]
#include "ClipEditor.h"
#include "EffectTestKit.h"

using namespace kit;

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
        return screenshots<ClipProcessor, ClipEditor> (argc, argv, "clipper",
            [] (ClipProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Drum bus" : "Master: 2 to 3 dB"); },
            [] (ClipEditor& e) { e.shavePeaks(); });

    // default softness (35 %) bends from -5 dB, and white noise peaks higher between its samples: 0.2 stays below the bend
    nullTest<ClipProcessor> ("Clipper null test", 0.2f);
    nullTest<ClipProcessor> ("Clipper null test, hard", 0.35f, [] (ClipProcessor& p) { set (p, "soft", 0); });
    nullTest<ClipProcessor> ("Clipper bypass", 0.9f, [] (ClipProcessor& p) { set (p, "push", 12); set (p, "bypass", 1); });

    // Loud music: never above the ceiling, between the samples too
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int ch : { 1, 2 })
            for (float soft : { 0.0f, 100.0f })
            {
                ClipProcessor p; set (p, "push", 12); set (p, "soft", soft); prepare (p, fs, ch);
                auto b = music (fs, 4.0, -6.0f, ch);
                run (p, b);
                float tp = 0, sp = b.getMagnitude (0, b.getNumSamples()); lufs (b, fs, &tp);
                sp = juce::Decibels::gainToDecibels (sp);
                check (sp <= -0.999f && tp <= -0.8f, "Ceiling holds, +12 dB push, softness " + juce::String (soft, 0) + ", " + juce::String (fs / 1000, 1) + " kHz " + (ch == 1 ? "mono" : "stereo"),
                       "samples " + juce::String (sp, 2) + ", true peak " + juce::String (tp, 2) + " dB for -1.0");
            }

    // Oversampling, hear removed, equal loudness and bypass: latency reported, no click
    {
        ClipProcessor p; set (p, "push", 6); set (p, "ceil", -6); prepare (p, 48000, 2);
        const int lat0 = p.getLatencySamples(); int lat1 = 0;
        auto b = tone (48000, 512 * 500, 300, 0.45f);
        run (p, b, 512, [&] (int k)
        {
            if (k == 100) set (p, "os", 1);
            if (k == 130) { lat1 = p.getLatencySamples(); set (p, "os", 3); }
            if (k == 250) set (p, "delta", 1);
            if (k == 290) set (p, "delta", 0);
            if (k == 320) set (p, "match", 1);
            if (k == 350) set (p, "bypass", 1);
            if (k == 380) set (p, "bypass", 0);
        });
        const float step = largestStep (b, 2000), limit = 0.9f * (float) (juce::MathConstants<double>::twoPi * 300.0 / 48000.0) * 1.3f;
        check (lat0 == 158 && lat1 == 150 && p.getLatencySamples() == 158 && step < limit, "Every switch is silent, latency follows oversampling", juce::String (lat0) + ", " + juce::String (lat1) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }

    // Equal loudness: as loud as the original, and an export ignores it
    {
        const double fs = 48000; auto src = music (fs, 14.0, -8.0f);
        auto render = [&] (bool match, bool offline) { ClipProcessor p; set (p, "push", 9); set (p, "match", match ? 1.0f : 0.0f); prepare (p, fs, 2); p.setNonRealtime (offline); auto b = src; run (p, b); return lufs (b, fs, nullptr, (int) fs * 7); };
        const float in = lufs (src, fs, nullptr, (int) fs * 7), loud = render (false, false), eq = render (true, false), off = render (true, true);
        check (std::abs (eq - in) < 0.3f && loud - in > 3.0f && std::abs (off - loud) < 0.01f, "Equal loudness matches the original, export ignores it",
               "in " + juce::String (in, 2) + ", clipped " + juce::String (loud, 2) + ", matched " + juce::String (eq, 2) + ", matched export " + juce::String (off, 2) + " LUFS");
    }

    // Shave the peaks for me: takes what it says, and no more than 3 dB (or the body of the sound)
    {
        ClipProcessor p; prepare (p, 48000, 2);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<ClipEditor*> (base.get());
        check (! ed->shavePeaks() && p.real ("push") == 0.0f, "Silence: asks for signal instead of guessing", "push stays 0");
        auto feed = [&] (int n) { for (int k = 0; k < n; ++k) { juce::AudioBuffer<float> b (2, 768); static auto src = music (48000, 8.0, -10.0f); static int pos = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) b.setSample (c, i, src.getSample (c, (pos + i) % src.getNumSamples())); pos += 768; run (p, b); ed->tickForTest(); } };
        feed (420);
        const bool ok = ed->shavePeaks(); const float push = p.real ("push");
        feed (420);
        const float off = ed->offPeaks(), pc = p.clippedPercent.load();
        check (ok && push > 1.0f && off < 3.3f && pc < 1.5f, "Shave the peaks: at most 3 dB off, for an instant", "push " + juce::String (push, 1) + ", measured " + juce::String (off, 2) + " dB off, " + juce::String (pc, 2) + " % of the time");
        ed->less(); const float after = p.real ("push"); p.pluginState.history.undo (p.apvts);
        check (std::abs (push - after - std::min (2.0f, push)) < 0.11f && std::abs (p.real ("push") - push) < 0.06f, "Less backs off 2 dB, undo brings it back", juce::String (push, 1) + " to " + juce::String (after, 1));
        load (p, "808"); ed->tickForTest(); const int s1 = ed->highlightedShape();
        load (p, "Kick"); ed->tickForTest(); const int s2 = ed->highlightedShape();
        set (p, "soft", 52); ed->tickForTest(); const int s3 = ed->highlightedShape();
        check (s1 == 2 && s2 == 0 && s3 == -1, "The shape buttons follow presets and automation", juce::String (s1) + " " + juce::String (s2) + " " + juce::String (s3));
        load (p, "Master: 2 to 3 dB"); ed->tickForTest(); feed (10);
        check (std::abs (p.real ("push") - push) < 1.0f, "The mastering preset fits itself to the music that plays", "push " + juce::String (p.real ("push"), 1));
    }

    stateTest<ClipProcessor> (10, "Drum bus", [] (ClipProcessor& p) { set (p, "link", 40); set (p, "mix", 70); set (p, "os", 1); set (p, "match", 1); });
    blockTest<ClipProcessor> ([] (ClipProcessor& p) { set (p, "push", 8); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
