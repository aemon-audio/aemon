// Aemon EQ plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonEqTests [--screenshots <dir>]
#include "EqProcessor.h"
#include "EqEditor.h"
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
void setParam (EqProcessor& p, const juce::String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
void prepare (EqProcessor& p, double fs, int ch)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    p.setBusesLayout (l);
    p.setRateAndBufferSizeDetails (fs, 512);
    p.prepareToPlay (fs, 512);
}
juce::AudioBuffer<float> run (EqProcessor& p, juce::AudioBuffer<float> b)
{
    juce::MidiBuffer m;
    for (int pos = 0; pos < b.getNumSamples(); pos += 512)
    {
        juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), b.getNumChannels(), pos, juce::jmin (512, b.getNumSamples() - pos));
        p.processBlock (v, m);
    }
    return b;
}
juce::AudioBuffer<float> noise (int ch, int n)
{
    juce::Random r (3);
    juce::AudioBuffer<float> b (ch, n);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, (r.nextFloat() * 2 - 1) * 0.5f);
    return b;
}

int screenshots (const juce::File& dir, ShotAudio shot)
{
    dir.createDirectory();
    for (auto mode : { "simple", "pro" })
    {
        EqProcessor p;
        prepare (p, 48000, 2);
        p.pluginState.ui.setProperty ("mode", mode, nullptr);
        for (auto* pr : p.pluginState.allPresets()) if (pr->name == "Gentle polish: a little weight, clarity and air") p.pluginState.loadPreset (*pr);
        setParam (p, "b6on", 1); setParam (p, "b6type", 0); setParam (p, "b6freq", 6500); setParam (p, "b6gain", -4); setParam (p, "b6q", 4); setParam (p, "b6dyn", 1);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (960, 600);
        juce::Random rnd (7);
        double ph[3] {};
        for (int k = 0; k < 60; ++k)
        {
            juce::AudioBuffer<float> b (2, 4800);
            for (int i = 0; i < 4800; ++i)
            {
                ph[0] += 2 * juce::MathConstants<double>::pi * 55.0 / 48000.0;
                ph[1] += 2 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
                ph[2] += 2 * juce::MathConstants<double>::pi * 3300.0 / 48000.0;
                const float x = 0.3f * (float) std::sin (ph[0]) + 0.15f * (float) std::sin (ph[1]) + 0.04f * (float) std::sin (ph[2]) + 0.04f * (rnd.nextFloat() * 2 - 1);
                b.setSample (0, i, x); b.setSample (1, i, x * 0.9f);
            }
            shot.fill (b);
            run (p, b);
            juce::Thread::sleep (16);
            if (auto* e = dynamic_cast<EqEditor*> (ed.get())) e->tickForTest();
        }
        for (float scale : { 1.0f, 2.0f })
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (juce::String ("eq-") + mode + (scale > 1 ? "@2x" : "") + ".png");
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
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots") return screenshots (juce::File (juce::String (argv[2])), ShotAudio::fromArgs (argc, argv));

    // Null test: all bands off, every rate, mono and stereo
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int ch : { 1, 2 })
        {
            EqProcessor p; prepare (p, fs, ch);
            auto in = noise (ch, (int) fs);
            auto out = run (p, in);
            double d = 0;
            for (int c = 0; c < ch; ++c) for (int i = 0; i < in.getNumSamples(); ++i) d = std::max (d, (double) std::abs (out.getSample (c, i) - in.getSample (c, i)));
            check (d == 0.0, "EQ null test " + juce::String (fs / 1000, 1) + " kHz " + (ch == 1 ? "mono" : "stereo"), d == 0.0 ? "bit-exact" : juce::String (d));
        }

    // Latency is reported per phase mode
    {
        EqProcessor p; prepare (p, 48000, 2);
        const int z = p.getLatencySamples();
        setParam (p, "phase", 2); prepare (p, 48000, 2);
        const int h = p.getLatencySamples();
        setParam (p, "phase", 1); prepare (p, 48000, 2);
        const int m = p.getLatencySamples();
        check (z == 0 && m == 4352 && h == 8448, "Latency reported: zero / linear medium / linear high", juce::String (z) + " / " + juce::String (m) + " / " + juce::String (h) + " samples");
    }

    // A preset loads, and settings survive save and recall
    {
        EqProcessor a; prepare (a, 48000, 2);
        int count = 0;
        for (auto* pr : a.pluginState.allPresets()) if (! pr->user) ++count;
        for (auto* pr : a.pluginState.allPresets()) if (pr->name == "Vocal: clearer and more upfront") a.pluginState.loadPreset (*pr);
        setParam (a, "b9on", 1); setParam (a, "b9freq", 1234); setParam (a, "b9place", 4); setParam (a, "phase", 2);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        EqProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same &= std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) < 1e-6f;
        check (same && b.pluginState.current.name == "Vocal: clearer and more upfront", "EQ state save and recall (" + juce::String (count) + " factory presets)", "all " + juce::String (a.getParameters().size()) + " parameters restored");
    }

    // Bypass in linear-phase mode stays time-aligned and click-free
    {
        EqProcessor p;
        setParam (p, "phase", 1); setParam (p, "b6on", 1); setParam (p, "b6gain", 6);
        prepare (p, 48000, 2);
        const int total = 512 * 188;   // a whole number of blocks
        juce::AudioBuffer<float> b (2, total);
        for (int i = 0; i < total; ++i) { const float x = 0.2f * (float) std::sin (2 * juce::MathConstants<double>::pi * 300.0 * i / 48000.0); b.setSample (0, i, x); b.setSample (1, i, x); }
        juce::MidiBuffer m;
        double worst = 0; float prev = 0;
        for (int pos = 0; pos < total; pos += 512)
        {
            if (pos == 48128) setParam (p, "bypass", 1);
            juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, 512);
            p.processBlock (v, m);
            for (int i = pos; i < pos + 512; ++i) { if (i > 9000) worst = std::max (worst, (double) std::abs (b.getSample (0, i) - prev)); prev = b.getSample (0, i); }
        }
        const double limit = 0.2 * 2.0 * 2 * juce::MathConstants<double>::pi * 300.0 / 48000.0 * 1.3;
        check (worst < limit, "Bypass in linear phase is click-free", "largest step " + juce::String (worst, 4) + ", limit " + juce::String (limit, 4));
    }

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
