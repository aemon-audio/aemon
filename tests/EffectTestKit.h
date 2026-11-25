// Helpers shared by the plugin-level tests of the effects built on aemon::EffectProcessor. AGPL-3.0-or-later.
#pragma once
#include <aemon_core/aemon_core.h>
#include "ShotAudio.h"
#include <cstdio>

namespace kit
{
inline int failures = 0;
inline void check (bool ok, const juce::String& name, const juce::String& detail)
{
    std::printf ("%s  %s  (%s)\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.toRawUTF8());
    if (! ok) ++failures;
}
inline void set (aemon::EffectProcessor& p, const juce::String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
inline void prepare (aemon::EffectProcessor& p, double fs, int ch, int block = 512)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (ch == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    p.setBusesLayout (l);
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
}
inline void run (aemon::EffectProcessor& p, juce::AudioBuffer<float>& b, int block = 512, const std::function<void (int)>& each = {})
{
    juce::MidiBuffer m;
    for (int pos = 0, k = 0; pos < b.getNumSamples(); pos += block, ++k)
    {
        if (each) each (k);
        juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), b.getNumChannels(), pos, juce::jmin (block, b.getNumSamples() - pos));
        p.processBlock (v, m);
    }
}
/** Beat-like music: kick, noise hits and a chord. */
inline juce::AudioBuffer<float> music (double fs, double seconds, float gainDb, int channels = 2)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (channels, n);
    juce::Random rnd (11);
    const float g = juce::Decibels::decibelsToGain (gainDb);
    const double tau = juce::MathConstants<double>::twoPi;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, beat = std::fmod (t, 0.5), off = std::fmod (t + 0.25, 0.5);
        ph += tau * (48.0 + 110.0 * std::exp (-beat * 32)) / fs;
        double x = 0.8 * std::sin (ph) * std::exp (-beat * 8) + 0.35 * (rnd.nextDouble() * 2 - 1) * std::exp (-off * 22);
        for (double f : { 220.0, 277.2, 329.6 }) x += 0.09 * std::sin (tau * f * t);
        for (int c = 0; c < channels; ++c) b.setSample (c, i, g * (float) (x + 0.02 * (c ? 1 : -1) * std::sin (tau * 3000.0 * t)));
    }
    return b;
}
inline juce::AudioBuffer<float> tone (double fs, int n, double f, float level)
{
    juce::AudioBuffer<float> b (2, n);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * f * i / fs));
    return b;
}
inline float lufs (const juce::AudioBuffer<float>& b, double fs, float* truePeak = nullptr, int from = 0)
{
    aemon::LoudnessMeter m; m.prepare (fs, b.getNumChannels());
    const float* ptr[2] { b.getReadPointer (0, from), b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0, from) };
    m.process (ptr, b.getNumChannels(), b.getNumSamples() - from);
    if (truePeak != nullptr) *truePeak = m.getTruePeakMaxDb();
    return m.getIntegrated();
}
inline float largestStep (const juce::AudioBuffer<float>& b, int from)
{
    float worst = 0;
    for (int i = from + 1; i < b.getNumSamples(); ++i) worst = std::max (worst, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
    return worst;
}
/** Level of a tone's multiple in dB, by correlation over whole cycles. */
inline double lineDb (const juce::AudioBuffer<float>& b, double fs, double f, int from, int n)
{
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) { const double w = juce::MathConstants<double>::twoPi * f * i / fs, v = b.getSample (0, from + i); re += v * std::cos (w); im += v * std::sin (w); }
    return 20 * std::log10 (2.0 / n * std::sqrt (re * re + im * im) + 1e-12);
}
inline void load (aemon::EffectProcessor& p, const char* start) { for (auto* pr : p.pluginState.allPresets()) if (pr->name.startsWith (start)) { p.pluginState.loadPreset (*pr); return; } }

/** Bit-exact at the given settings, delayed by the reported latency, at every rate, mono and stereo. */
template <typename P> void nullTest (const char* name, float level, const std::function<void (P&)>& setup = {})
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int ch : { 1, 2 })
        {
            P p; if (setup) setup (p);
            prepare (p, fs, ch);
            juce::Random rnd (3);
            juce::AudioBuffer<float> b (ch, 512 * 60);
            for (int c = 0; c < ch; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, level * (rnd.nextFloat() * 2 - 1));
            auto ref = b;
            run (p, b);
            const int lat = p.getLatencySamples();
            float worst = 0;
            for (int c = 0; c < ch; ++c) for (int i = lat; i < b.getNumSamples(); ++i) worst = std::max (worst, std::abs (b.getSample (c, i) - ref.getSample (c, i - lat)));
            check (worst == 0.0f, juce::String (name) + " " + juce::String (fs / 1000, 1) + " kHz " + (ch == 1 ? "mono" : "stereo"), worst == 0.0f ? "bit-exact, latency " + juce::String (lat) : "max diff " + juce::String (worst));
        }
}

/** Everything in the state comes back, every factory preset has a description. `prep` changes settings first. */
template <typename P> void stateTest (int presets, const char* preset, const std::function<void (P&)>& prep)
{
    P a; prepare (a, 48000, 2);
    int count = 0, described = 0; for (auto* pr : a.pluginState.allPresets()) if (! pr->user) { ++count; described += pr->description.isNotEmpty(); }
    load (a, preset);
    prep (a);
    juce::MemoryBlock mb; a.getStateInformation (mb);
    P b; b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true;
    for (auto* prm : a.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            same &= std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) < 1e-6f;
    check (same && count == presets && described == presets && b.pluginState.current.name.startsWith (preset), "State save and recall (" + juce::String (count) + " factory presets, all described)", same ? "all parameters restored" : "a parameter differs");
}

template <typename P> void blockTest (const std::function<void (P&)>& setup)
{
    auto render = [&] (int block) { P p; setup (p); prepare (p, 48000, 2, 2048); auto b = music (48000, 1.0, -8.0f); run (p, b, block); return b; };
    const auto ref = render (512);
    float worst = 0;
    for (int block : { 1, 37, 2048 }) { const auto x = render (block); for (int i = 0; i < x.getNumSamples(); ++i) worst = std::max (worst, std::abs (x.getSample (0, i) - ref.getSample (0, i))); }
    check (worst == 0.0f, "Same sound at every block size", worst == 0.0f ? "bit-exact" : "max diff " + juce::String (worst, 8));
}

/** Real windows as pictures. prep(processor, mode) before the window opens; act(editor) after some seconds of music. */
template <typename P, typename E> int screenshots (int argc, char** argv, const char* file, const std::function<void (P&, const juce::String&)>& prep, const std::function<void (E&)>& act, float synthDb = -9.0f)
{
    const juce::File dir { juce::String (argv[2]) };
    dir.createDirectory();
    auto shot = ShotAudio::fromArgs (argc, argv);
    for (auto mode : { "simple", "pro" })
    {
        P p; prepare (p, 48000, 2);
        p.pluginState.ui.setProperty ("mode", mode, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (960, 600);
        auto* e = dynamic_cast<E*> (ed.get());
        prep (p, mode);
        auto synth = music (48000, 14.0, synthDb);
        int pos = 0;
        auto feed = [&] (int blocks)
        {
            for (int k = 0; k < blocks; ++k)
            {
                juce::AudioBuffer<float> b (2, 768);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 768; ++i) b.setSample (c, i, synth.getSample (c, (pos + i) % synth.getNumSamples()));
                pos += 768;
                shot.fill (b);
                run (p, b);
                e->tickForTest();
                if (k % 60 == 0) juce::Thread::sleep (2);
            }
        };
        feed (1100);
        if (act) act (*e);
        feed (520);
        juce::Thread::sleep (30);
        for (float scale : { 1.0f, 2.0f })
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (juce::String (file) + "-" + mode + (scale > 1 ? "@2x" : "") + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }
    return 0;
}
} // namespace kit
