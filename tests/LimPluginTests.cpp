// Aemon Limiter plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonLimTests [--screenshots <dir> [music.wav]]
#include "LimProcessor.h"
#include "PerceptReport.h"
#include "StyleRows.h"
#include "LimEditor.h"
#include "ShotAudio.h"
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& name, const juce::String& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
    if (! ok) ++failures;
}
void setParam (LimProcessor& p, const juce::String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
void prepare (LimProcessor& p, double fs, int ch, int block = 512)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    p.setBusesLayout (l);
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
}
void runBlocks (LimProcessor& p, juce::AudioBuffer<float>& b, int block = 512)
{
    juce::MidiBuffer m;
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), b.getNumChannels(), pos, juce::jmin (block, b.getNumSamples() - pos));
        p.processBlock (v, m);
    }
}
/** Beat-like music: kick, noise hits and a chord. gainDb sets the level. */
juce::AudioBuffer<float> music (double fs, double seconds, float gainDb, int channels = 2)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (channels, n);
    juce::Random rnd (11);
    const float g = juce::Decibels::decibelsToGain (gainDb);
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5), off = std::fmod (t + 0.25, 0.5);
        ph += 2 * juce::MathConstants<double>::pi * (48.0 + 110.0 * std::exp (-beat * 32)) / fs;
        double x = 0.8 * std::sin (ph) * std::exp (-beat * 8) + 0.35 * (rnd.nextDouble() * 2 - 1) * std::exp (-off * 22);
        for (double f : { 220.0, 277.2, 329.6 }) x += 0.09 * std::sin (2 * juce::MathConstants<double>::pi * f * t);
        for (int c = 0; c < channels; ++c) b.setSample (c, i, g * (float) (x + 0.02 * (c ? 1 : -1) * std::sin (2 * juce::MathConstants<double>::pi * 3000.0 * t)));
    }
    return b;
}
/** The hard case for loudness: a quiet bed with sharp, lone hits far above it. */
juce::AudioBuffer<float> spiky (double fs, double seconds)
{
    auto b = music (fs, seconds, -30.0f);
    juce::Random rnd (5);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = std::fmod (i / fs, 0.18);
        const float x = 0.8f * (float) ((rnd.nextDouble() * 2 - 1) * std::exp (-t * 160));
        for (int c = 0; c < 2; ++c) b.addSample (c, i, x);
    }
    return b;
}
float measuredLufs (const juce::AudioBuffer<float>& b, double fs, float* truePeak = nullptr)
{
    aemon::LoudnessMeter m; m.prepare (fs, b.getNumChannels());
    const float* ptr[2] { b.getReadPointer (0), b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0) };
    m.process (ptr, b.getNumChannels(), b.getNumSamples());
    if (truePeak != nullptr) *truePeak = m.getTruePeakMaxDb();
    return m.getIntegrated();
}
float largestStep (const juce::AudioBuffer<float>& b, int from)
{
    float worst = 0;
    for (int i = from + 1; i < b.getNumSamples(); ++i) worst = std::max (worst, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
    return worst;
}
bool contains (const aemon::Preset* p, const char* text) { return p->name.startsWith (text); }

int screenshots (const juce::File& dir, ShotAudio shot)
{
    dir.createDirectory();
    for (auto mode : { "simple", "pro" })
    {
        LimProcessor p;
        prepare (p, 48000, 2);
        p.pluginState.ui.setProperty ("mode", mode, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (960, 600);
        auto* le = dynamic_cast<LimEditor*> (ed.get());
        for (auto* pr : p.pluginState.allPresets()) if (contains (pr, juce::String (mode) == "pro" ? "As loud as it gets" : "Streaming-ready")) p.pluginState.loadPreset (*pr);
        auto synth = music (48000, 14.0, -14.0f);
        int pos = 0;
        auto feed = [&] (int blocks)
        {
            for (int k = 0; k < blocks; ++k)
            {
                juce::AudioBuffer<float> b (2, 768);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) b.setSample (c, i, synth.getSample (c, (pos + i) % synth.getNumSamples()));
                pos += 768;
                shot.fill (b);
                runBlocks (p, b);
                le->tickForTest();
                if (k % 60 == 0) juce::Thread::sleep (2);
            }
        };
        feed (400);
        le->makeItLoud();
        feed (520);
        juce::Thread::sleep (30);
        for (float scale : { 1.0f, 2.0f })
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (juce::String ("limiter-") + mode + (scale > 1 ? "@2x" : "") + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--styles") { style::run<LimProcessor> (rows::lim(), -16, [] (bool ok, const juce::String& n, const juce::String& d) { std::printf ("%s  %s (%s)\n", ok ? "PASS" : "FAIL", n.toRawUTF8(), d.toRawUTF8()); }, argc > 2, 0.85); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<LimProcessor> ("Limiter", argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--screenshots") return screenshots (juce::File (juce::String (argv[2])), ShotAudio::fromArgs (argc, argv));

    // Quiet audio at the defaults: bit-exact, delayed by the reported latency
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int ch : { 1, 2 })
        {
            LimProcessor p; prepare (p, fs, ch);
            juce::Random rnd (3);
            juce::AudioBuffer<float> b (ch, 512 * 60);
            for (int c = 0; c < ch; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, 0.2f * (rnd.nextFloat() * 2 - 1));
            auto ref = b;
            runBlocks (p, b);
            const int lat = p.getLatencySamples();
            float worst = 0;
            for (int c = 0; c < ch; ++c) for (int i = lat; i < b.getNumSamples(); ++i) worst = std::max (worst, std::abs (b.getSample (c, i) - ref.getSample (c, i - lat)));
            check (worst == 0.0f, "Limiter null test " + juce::String (fs / 1000, 1) + " kHz " + (ch == 1 ? "mono" : "stereo"), worst == 0.0f ? "bit-exact, latency " + juce::String (lat) : "max diff " + juce::String (worst));
        }

    // Loud music: the true peak stays at the ceiling, as the plugin's own meter and an outside one read it
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int ch : { 1, 2 })
        {
            LimProcessor p; setParam (p, "push", 12); prepare (p, fs, ch);
            auto b = music (fs, 4.0, -8.0f, ch);
            runBlocks (p, b);
            float tp = 0; const float I = measuredLufs (b, fs, &tp);
            check (tp <= -0.9f && std::abs (tp - p.loudOut.getTruePeakMaxDb()) < 0.02f && std::abs (I - p.loudOut.getIntegrated()) < 0.05f,
                   "True peak at the ceiling, +12 dB push, " + juce::String (fs / 1000, 1) + " kHz " + (ch == 1 ? "mono" : "stereo"), juce::String (tp, 2) + " dBTP for -1.0, " + juce::String (I, 1) + " LUFS");
        }

    // Latency follows the lookahead, reported after the change, without a click
    {
        LimProcessor p; prepare (p, 48000, 2);
        auto tone = [] (int n) { juce::AudioBuffer<float> b (2, n); for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0)); return b; };
        auto b = tone (512 * 120);
        juce::MidiBuffer m;
        int lat0 = p.getLatencySamples(), lat1 = 0;
        for (int k = 0; k < 120; ++k)
        {
            if (k == 40) setParam (p, "look", 5.0f);
            if (k == 70) setParam (p, "tp", 0);
            if (k == 80) setParam (p, "os", 3);
            if (k == 90) setParam (p, "tp", 1);
            juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, k * 512, 512);
            p.processBlock (v, m);
            lat1 = p.getLatencySamples();
        }
        const float step = largestStep (b, 600), limit = 0.3f * (float) (2 * juce::MathConstants<double>::pi * 440.0 / 48000.0) * 1.3f;
        check (lat0 == 95 && lat1 == 240 + 23 && step < limit, "Lookahead 1.5 to 5 ms, true peak and oversampling switched: latency reported, no click",
               juce::String (lat0) + " to " + juce::String (lat1) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }

    // Hear removed and equal loudness switch without a click
    {
        LimProcessor p; setParam (p, "push", 6); setParam (p, "ceil", -6); prepare (p, 48000, 2);
        const int n = 512 * 400;
        juce::AudioBuffer<float> b (2, n);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, 0.45f * (float) std::sin (2 * juce::MathConstants<double>::pi * 300.0 * i / 48000.0));
        juce::MidiBuffer m;
        for (int k = 0; k < 400; ++k)
        {
            if (k == 250) setParam (p, "delta", 1);
            if (k == 290) setParam (p, "delta", 0);
            if (k == 320) setParam (p, "match", 1);
            if (k == 350) setParam (p, "bypass", 1);
            if (k == 380) setParam (p, "bypass", 0);
            juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, k * 512, 512);
            p.processBlock (v, m);
        }
        const float step = largestStep (b, 2000), limit = 0.9f * (float) (2 * juce::MathConstants<double>::pi * 300.0 / 48000.0) * 1.3f;
        check (step < limit, "Hear removed, equal loudness and bypass switch without clicks", "largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }

    // Equal loudness: the limited sound is as loud as the original, and an export ignores it
    {
        auto src = music (48000, 12.0, -10.0f);
        auto render = [&] (bool match, bool offline)
        {
            LimProcessor p; setParam (p, "push", 9); setParam (p, "match", match ? 1.0f : 0.0f); prepare (p, 48000, 2);
            p.setNonRealtime (offline);
            auto b = src; runBlocks (p, b);
            juce::AudioBuffer<float> tail (2, 48000 * 6); for (int c = 0; c < 2; ++c) tail.copyFrom (c, 0, b, c, 48000 * 6, 48000 * 6);
            return measuredLufs (tail, 48000);
        };
        juce::AudioBuffer<float> t (2, 48000 * 6); for (int c = 0; c < 2; ++c) t.copyFrom (c, 0, src, c, 48000 * 6, 48000 * 6);
        const float in = measuredLufs (t, 48000), loud = render (false, false), eq = render (true, false), off = render (true, true);
        check (std::abs (eq - in) < 0.3f && loud - in > 3.0f && std::abs (off - loud) < 0.01f, "Equal loudness matches the original, export ignores it",
               "in " + juce::String (in, 2) + ", limited " + juce::String (loud, 2) + ", matched " + juce::String (eq, 2) + ", matched export " + juce::String (off, 2) + " LUFS");
    }

    // Make it loud safely: reaches the target on a quiet mix, stops short when it would squash, never adds push to a loud one
    {
        auto planFor = [] (juce::AudioBuffer<float> b, int target)
        {
            LimProcessor p; setParam (p, "target", (float) target); prepare (p, 48000, 2);
            runBlocks (p, b);
            return limp::planLoudness (p.recentInput(), 48000, p.readParams(), target);
        };
        auto plan = [&] (float gainDb, int target) { return planFor (music (48000, 6.5, gainDb), target); };
        const auto a = plan (-12.0f, 0);
        check (a.measured && std::abs (a.lufs + 14.0f) < 0.3f && a.push > 0.5f, "Make it loud: reaches -14 LUFS on a quiet mix", "push " + juce::String (a.push, 1) + " dB, " + juce::String (a.lufs, 2) + " LUFS, " + juce::String (a.avgGr, 2) + " dB average");
        {
            LimProcessor p; setParam (p, "push", a.push); prepare (p, 48000, 2);
            auto b = music (48000, 6.5, -12.0f); runBlocks (p, b);
            const float real = p.loudOut.getIntegrated();
            check (std::abs (real + 14.0f) < 0.5f, "The plugin really plays that loudness", juce::String (real, 2) + " LUFS");
        }
        const auto c = planFor (spiky (48000, 6.5), limp::loudestClean);
        check (c.measured && c.avgGr <= 2.51f && c.p95Gr <= 6.01f && c.push > 1.0f && c.push < 23.0f, "Loudest clean stays within its limits", "push " + juce::String (c.push, 1) + " dB, average " + juce::String (c.avgGr, 2) + ", 95 % " + juce::String (c.p95Gr, 2) + " dB");
        const auto d = planFor (spiky (48000, 6.5), 6);
        check (d.measured && d.shortBy > 0.5f && d.avgGr <= 4.01f && d.p95Gr <= 8.01f, "Stops short of -8 LUFS instead of squashing", juce::String (d.shortBy, 1) + " dB short, average " + juce::String (d.avgGr, 2) + ", 95 % " + juce::String (d.p95Gr, 2) + " dB");
        const auto e = plan (-6.0f, 2);
        check (e.measured && e.overTarget && e.push == 0.0f, "A mix louder than the target gets no push", juce::String (e.lufs, 1) + " LUFS against -23");
        LimProcessor q; prepare (q, 48000, 2); juce::AudioBuffer<float> z (2, 48000 * 4); z.clear(); runBlocks (q, z);
        check (! limp::planLoudness (q.recentInput(), 48000, q.readParams(), 0).measured, "Silence: asks for signal instead of guessing", "not measured");
        LimProcessor r; prepare (r, 48000, 2); auto mb = music (48000, 6.5, -12.0f); runBlocks (r, mb);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        limp::planLoudness (r.recentInput(), 48000, r.readParams(), 0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t1;
        check (ms < 1500, "The measurement is quick", juce::String (ms, 0) + " ms");
    }

    // Window: style and target follow the settings, loudness presets do what they say
    {
        LimProcessor p; prepare (p, 48000, 2);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        base->setSize (960, 600);
        auto* ed = dynamic_cast<LimEditor*> (base.get());
        auto load = [&] (const char* name) { for (auto* pr : p.pluginState.allPresets()) if (contains (pr, name)) p.pluginState.loadPreset (*pr); ed->tickForTest(); };
        load ("Loud streaming"); const int s1 = ed->highlightedStyle(), t1 = ed->highlightedTarget();
        load ("Club master"); const int s2 = ed->highlightedStyle();
        load ("Broadcast-safe"); const int s3 = ed->highlightedStyle(), t3 = ed->highlightedTarget();
        load ("Gentle: 1 to 2"); setParam (p, "rel", 333); ed->tickForTest(); const int s4 = ed->highlightedStyle();
        check (s1 == 2 && t1 == -1 && s2 == 1 && s3 == 0 && t3 == 2 && s4 == -1, "Style and target follow presets and automation",
               juce::String (s1) + " " + juce::String (t1) + " " + juce::String (s2) + " " + juce::String (s3) + " " + juce::String (t3) + " " + juce::String (s4));
        auto b = music (48000, 6.5, -12.0f); runBlocks (p, b);
        load ("Streaming-ready");
        auto b2 = music (48000, 6.5, -12.0f); runBlocks (p, b2);
        const float st = p.loudOut.getShortTerm();
        check (std::abs (st + 14.0f) < 0.7f, "Loading \"Streaming-ready\" while music plays lands on -14", juce::String (st, 2) + " LUFS, push " + juce::String (p.apvts.getRawParameterValue ("push")->load(), 1));
        const float before = p.apvts.getRawParameterValue ("push")->load();
        ed->lessSquashed();
        const float after = p.apvts.getRawParameterValue ("push")->load();
        p.pluginState.history.undo (p.apvts);
        check (std::abs (before - after - std::min (2.0f, before)) < 0.11f && std::abs (p.apvts.getRawParameterValue ("push")->load() - before) < 0.06f, "Less squashed backs off 2 dB, undo brings it back", juce::String (before, 1) + " to " + juce::String (after, 1));
    }

    // State save and recall, and preset count
    {
        LimProcessor a; prepare (a, 48000, 2);
        int count = 0, described = 0; for (auto* pr : a.pluginState.allPresets()) if (! pr->user) { ++count; described += pr->description.isNotEmpty(); }
        for (auto* pr : a.pluginState.allPresets()) if (contains (pr, "Drum bus: tame")) a.pluginState.loadPreset (*pr);
        setParam (a, "look", 2.5f); setParam (a, "os", 3); setParam (a, "dither", 1); setParam (a, "target", 5);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        LimProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same &= std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) < 1e-6f;
        check (same && count == 23 && described == 23 && contains (&b.pluginState.current, "Drum bus: tame"), "Limiter state save and recall (" + juce::String (count) + " factory presets, all described)", "all parameters restored");
    }

    // Block sizes: 1 to 2048 samples give the same sound
    {
        auto render = [] (int block) { LimProcessor p; setParam (p, "push", 8); prepare (p, 48000, 2, 2048); auto b = music (48000, 1.0, -8.0f); runBlocks (p, b, block); return b; };
        const auto ref = render (512);
        float worst = 0;
        for (int block : { 1, 37, 2048 }) { const auto x = render (block); for (int i = 0; i < x.getNumSamples(); ++i) worst = std::max (worst, std::abs (x.getSample (0, i) - ref.getSample (0, i))); }
        check (worst == 0.0f, "Same sound at every block size", worst == 0.0f ? "bit-exact" : "max diff " + juce::String (worst, 8));
    }

    style::run<LimProcessor> (rows::lim(), -16, [] (bool ok, const juce::String& n, const juce::String& d) { check (ok, n, d); }, false, 0.85);
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
