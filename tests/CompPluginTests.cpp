// Aemon Compressor plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonCompTests [--screenshots <dir>]
#include "CompProcessor.h"
#include "CompEditor.h"
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
void setParam (CompProcessor& p, const juce::String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
void prepare (CompProcessor& p, double fs, int ch, bool sidechain = false)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.inputBuses.add (sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled());
    l.outputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    p.setBusesLayout (l);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);
}
void runBlocks (CompProcessor& p, juce::AudioBuffer<float>& b)
{
    juce::MidiBuffer m;
    for (int pos = 0; pos < b.getNumSamples(); pos += 512)
    {
        juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), b.getNumChannels(), pos, juce::jmin (512, b.getNumSamples() - pos));
        p.processBlock (v, m);
    }
}

int screenshots (const juce::File& dir, ShotAudio shot)
{
    dir.createDirectory();
    for (auto mode : { "simple", "pro" })
    {
        CompProcessor p;
        prepare (p, 48000, 2);
        p.pluginState.ui.setProperty ("mode", mode, nullptr);
        if (shot.loaded()) { for (auto* pr : p.pluginState.allPresets()) if (pr->name == juce::String (juce::String (mode) == "pro" ? "Drum bus: more punch" : "Gentle glue: hold it together")) p.pluginState.loadPreset (*pr); }
        else { setParam (p, "thr", -24); setParam (p, "ratio", 4); setParam (p, "atk", 15); setParam (p, "rel", 120); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (960, 600);
        juce::Random rnd (7);
        double ph = 0, t = 0;
        for (int k = 0; k < 60; ++k)
        {
            juce::AudioBuffer<float> b (2, 4608);
            for (int i = 0; i < 4608; ++i)
            {
                t += 1.0 / 48000.0;
                const double beat = std::fmod (t, 60.0 / 112.0);
                ph += 2 * juce::MathConstants<double>::pi * (50.0 + 100.0 * std::exp (-beat * 30)) / 48000.0;
                const float kick = (float) (0.8 * std::sin (ph) * std::exp (-beat * 9));
                const float pad = (float) (0.12 * std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t)) + 0.03f * (rnd.nextFloat() * 2 - 1);
                b.setSample (0, i, kick + pad); b.setSample (1, i, kick + pad * 0.9f);
            }
            shot.fill (b);
            runBlocks (p, b);
            juce::Thread::sleep (16);
            if (auto* e = dynamic_cast<CompEditor*> (ed.get())) e->tickForTest();
        }
        for (float scale : { 1.0f, 2.0f })
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (juce::String ("compressor-") + mode + (scale > 1 ? "@2x" : "") + ".png");
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
    if (argc > 2 && juce::String (argv[1]) == "--screenshots") return screenshots (juce::File (juce::String (argv[2])), ShotAudio::fromArgs (argc, argv));

    // Null test with ratio 1:1, no auto level, every rate, mono and stereo
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int ch : { 1, 2 })
        {
            CompProcessor p; setParam (p, "ratio", 1); setParam (p, "autoLevel", 0);
            prepare (p, fs, ch);
            juce::Random r (2);
            juce::AudioBuffer<float> in (ch, 512 * 100);
            for (int c = 0; c < ch; ++c) for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (c, i, (r.nextFloat() * 2 - 1) * 0.8f);
            auto out = in;
            runBlocks (p, out);
            double d = 0;
            for (int c = 0; c < ch; ++c) for (int i = 0; i < in.getNumSamples(); ++i) d = std::max (d, (double) std::abs (out.getSample (c, i) - in.getSample (c, i)));
            check (d == 0.0, "Compressor null test " + juce::String (fs / 1000, 1) + " kHz " + (ch == 1 ? "mono" : "stereo"), d == 0.0 ? "bit-exact" : juce::String (d));
        }

    // Lookahead is reported as latency
    {
        CompProcessor p; setParam (p, "look", 5); prepare (p, 48000, 2);
        check (p.getLatencySamples() == 240, "Lookahead 5 ms reported as 240 samples latency", juce::String (p.getLatencySamples()));
    }

    // External sidechain bus: a loud key ducks quiet music
    {
        CompProcessor p; setParam (p, "extSc", 1); setParam (p, "thr", -30); setParam (p, "ratio", 8); setParam (p, "autoLevel", 0); setParam (p, "atk", 1);
        prepare (p, 48000, 2, true);
        const int n = 512 * 94;
        juce::AudioBuffer<float> b (4, n);   // channels 0-1 main, 2-3 sidechain
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.1f * (float) std::sin (2 * juce::MathConstants<double>::pi * 300.0 * i / 48000.0);
            const float k = 0.7f * (float) std::sin (2 * juce::MathConstants<double>::pi * 60.0 * i / 48000.0);
            b.setSample (0, i, m); b.setSample (1, i, m); b.setSample (2, i, k); b.setSample (3, i, k);
        }
        runBlocks (p, b);
        double so = 0; for (int i = n / 2; i < n; ++i) so += (double) b.getSample (0, i) * b.getSample (0, i);
        const double db = 10 * std::log10 (so / (n / 2)) - 10 * std::log10 (0.01 * 0.5);
        check (db < -10 && p.sidechainConnected(), "External sidechain input ducks the music", juce::String (db, 1) + " dB");
    }

    // Simple mode: the highlighted style follows the settings, however they were changed
    {
        CompProcessor p; prepare (p, 48000, 2);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        base->setSize (960, 600);
        auto* ed = dynamic_cast<CompEditor*> (base.get());
        auto load = [&] (const char* name) { for (auto* pr : p.pluginState.allPresets()) if (pr->name == name) p.pluginState.loadPreset (*pr); ed->tickForTest(); return ed->highlightedStyle(); };
        const int glue = load ("Gentle glue: hold it together"), punchy = load ("Drum bus: more punch"), smash = load ("Drum bus: big and smashed (parallel)");
        const int vocal = load ("Vocal: even and upfront"), gentle = load ("Vocal: gentle levelling"), custom = load ("Kick: more click and attack");
        check (glue == 2 && punchy == 1 && smash == 4 && vocal == 3 && gentle == 0 && custom == -1, "Simple style follows presets",
               juce::String (glue) + " " + juce::String (punchy) + " " + juce::String (smash) + " " + juce::String (vocal) + " " + juce::String (gentle) + " " + juce::String (custom));
        load ("Drum bus: more punch");
        ed->applyStyle (2); ed->tickForTest();
        const int clicked = ed->highlightedStyle();
        p.pluginState.history.undo (p.apvts); ed->tickForTest();
        const int undone = ed->highlightedStyle();
        setParam (p, "ratio", 7.0f); ed->tickForTest();
        check (clicked == 2 && undone == 1 && ed->highlightedStyle() == -1, "Simple style follows clicks, undo and automation", juce::String (clicked) + " " + juce::String (undone) + " " + juce::String (ed->highlightedStyle()));
    }

    // State save and recall, and preset count
    {
        CompProcessor a; prepare (a, 48000, 2);
        int count = 0; for (auto* pr : a.pluginState.allPresets()) if (! pr->user) ++count;
        for (auto* pr : a.pluginState.allPresets()) if (pr->name == "Vocal: radio-ready, very consistent") a.pluginState.loadPreset (*pr);
        setParam (a, "look", 2.5f); setParam (a, "det", 1); setParam (a, "hpf", 120);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        CompProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same &= std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) < 1e-6f;
        check (same && b.pluginState.current.name == "Vocal: radio-ready, very consistent", "Compressor state save and recall (" + juce::String (count) + " factory presets)", "all parameters restored");
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
