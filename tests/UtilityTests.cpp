// Aemon Utility objective tests. AGPL-3.0-or-later.
// Run: AemonTests [output-folder]. Prints a report and writes CSV files for plots.
#include "PluginProcessor.h"
#include "PerceptReport.h"
#include "StyleRows.h"
#include "PluginEditor.h"
#include <chrono>
#include "ShotAudio.h"
#include <cstdio>

namespace
{
int failures = 0;
juce::String report;

void check (bool ok, const juce::String& name, const juce::String& detail)
{
    const auto line = juce::String (ok ? "PASS  " : "FAIL  ") + name + "  (" + detail + ")";
    std::printf ("%s\n", line.toRawUTF8());
    report << line << "\n";
    if (! ok) ++failures;
}

juce::String db (double x) { return juce::String (x, 2) + " dB"; }

void setParam (UtilityProcessor& p, const juce::String& id, float real)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (real));
}

void resetParams (UtilityProcessor& p)
{
    for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (prm->getDefaultValue());
}

juce::AudioBuffer<float> noise (int ch, int n, int seed, float amp = 0.5f)
{
    juce::Random r (seed);
    juce::AudioBuffer<float> b (ch, n);
    for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * amp);
    return b;
}

/** Runs a buffer through the processor in host-sized blocks. */
juce::AudioBuffer<float> run (UtilityProcessor& p, const juce::AudioBuffer<float>& in, int block = 512)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, out.getNumSamples() - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
        p.processBlock (view, midi);
    }
    return out;
}

double maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int ca, int cb, int from = 0, float sign = 1.0f, float gain = 1.0f)
{
    double m = 0;
    for (int i = from; i < a.getNumSamples(); ++i) m = std::max (m, (double) std::abs (a.getSample (ca, i) - sign * gain * b.getSample (cb, i)));
    return m;
}

double rms (const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / n); }

void prepare (UtilityProcessor& p, double fs, int ins, int outs, int block = 512)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (ins == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (outs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    p.setBusesLayout (l);
    p.setRateAndBufferSizeDetails (fs, block);
    p.prepareToPlay (fs, block);
}
} // namespace

/** Renders the real plugin window (Simple and Pro, 1x and 2x) to PNG after feeding it audio. */
int screenshots (const juce::File& dir, ShotAudio shot)
{
    dir.createDirectory();
    for (auto mode : { "simple", "pro" })
    {
        UtilityProcessor p;
        prepare (p, 48000, 2, 2);
        p.pluginState.ui.setProperty ("mode", mode, nullptr);
        if (shot.loaded()) setParam (p, "width", 115);
        else { setParam (p, "width", 135); setParam (p, "pan", -12); setParam (p, "gain", -2.5f); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (960, 600);
        juce::Random rnd (7);
        double ph[4] {};
        for (int k = 0; k < 60; ++k)          // ~3 s of audio, letting the UI timers run in between
        {
            juce::AudioBuffer<float> b (2, 4800);
            for (int i = 0; i < 4800; ++i)
            {
                ph[0] += 2 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
                ph[1] += 2 * juce::MathConstants<double>::pi * 221.3 / 48000.0;
                ph[2] += 2 * juce::MathConstants<double>::pi * 55.0 / 48000.0;
                const float bass = 0.3f * (float) std::sin (ph[2]);
                const float n = 0.05f * (rnd.nextFloat() * 2 - 1);
                b.setSample (0, i, bass + 0.18f * (float) std::sin (ph[0]) + n);
                b.setSample (1, i, bass + 0.18f * (float) std::sin (ph[1]) + 0.6f * n);
            }
            shot.fill (b);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < 4800; pos += 480)
            {
                juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, 480);
                p.processBlock (v, midi);
            }
            juce::Thread::sleep (16);
            if (auto* ue = dynamic_cast<UtilityEditor*> (ed.get())) ue->tickForTest();
        }
        for (float scale : { 1.0f, 2.0f })
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
            auto f = dir.getChildFile (juce::String ("utility-") + mode + (scale > 1 ? "@2x" : "") + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--styles") { style::run<UtilityProcessor> (rows::utility(), -18, [] (bool ok, const juce::String& n, const juce::String& d) { std::printf ("%s  %s (%s)\n", ok ? "PASS" : "FAIL", n.toRawUTF8(), d.toRawUTF8()); }, argc > 2, 0.95, true); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<UtilityProcessor> ("Utility", argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--screenshots") return screenshots (juce::File (juce::String (argv[2])), ShotAudio::fromArgs (argc, argv));
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory().getChildFile ("test-results");
    outDir.createDirectory();

    // 1. Null test at neutral settings: every rate, mono and stereo, bit-exact.
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (auto [ins, outs] : { std::pair { 2, 2 }, std::pair { 1, 1 }, std::pair { 1, 2 } })
        {
            UtilityProcessor p;
            prepare (p, fs, ins, outs);
            auto in = noise (std::max (ins, outs), (int) fs, 1);
            if (ins == 1 && outs == 2) in.copyFrom (1, 0, in, 0, 0, in.getNumSamples());
            auto out = run (p, in);
            double d = 0;
            for (int c = 0; c < outs; ++c) d = std::max (d, maxAbsDiff (out, in, c, ins == 1 ? 0 : c));
            check (d == 0.0, "Null test " + juce::String (fs / 1000.0, 1) + " kHz " + juce::String (ins) + "in/" + juce::String (outs) + "out",
                   d == 0.0 ? "bit-exact" : "max diff " + juce::String (d));
        }

    // 2. Gain accuracy
    {
        UtilityProcessor p; prepare (p, 48000, 2, 2);
        for (float g : { -36.0f, -6.0f, 0.1f, 6.0f, 36.0f })
        {
            resetParams (p); setParam (p, "gain", g);
            auto in = noise (2, 48000, 2, 0.01f);
            auto out = run (p, in);
            const double measured = 20.0 * std::log10 (rms (out.getReadPointer (0) + 4800, 43200) / rms (in.getReadPointer (0) + 4800, 43200));
            check (std::abs (measured - g) < 0.001, "Gain " + juce::String (g, 1) + " dB", "measured " + db (measured));
        }
    }

    // 3. Polarity, swap, mono, width, mid/side listening
    {
        UtilityProcessor p; prepare (p, 48000, 2, 2);
        auto in = noise (2, 48000, 3);
        resetParams (p); setParam (p, "polL", 1);
        auto o = run (p, in);
        check (maxAbsDiff (o, in, 0, 0, 2400, -1.0f) < 1e-7 && maxAbsDiff (o, in, 1, 1, 2400) < 1e-7, "Polarity left", "L inverted, R untouched");
        resetParams (p); p.prepareToPlay (48000, 512); setParam (p, "swap", 1);
        o = run (p, in);
        check (maxAbsDiff (o, in, 0, 1, 2400) < 1e-7 && maxAbsDiff (o, in, 1, 0, 2400) < 1e-7, "Swap", "L and R exchanged");
        resetParams (p); p.prepareToPlay (48000, 512); setParam (p, "mono", 1);
        o = run (p, in);
        check (maxAbsDiff (o, o, 0, 1, 2400) < 1e-7, "Mono", "L equals R after settling");
        resetParams (p); p.prepareToPlay (48000, 512); setParam (p, "width", 200);
        o = run (p, in);
        double e = 0;
        for (int i = 2400; i < 48000; ++i)
        {
            const float l = in.getSample (0, i), r = in.getSample (1, i);
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * 2.0f;
            e = std::max (e, (double) std::abs (o.getSample (0, i) - (m + s)));
        }
        check (e < 1e-6, "Width 200%", "side doubled, mid kept, max error " + juce::String (e));
        resetParams (p); p.prepareToPlay (48000, 512); setParam (p, "monitor", 2);
        o = run (p, in);
        e = 0;
        for (int i = 2400; i < 48000; ++i) e = std::max (e, (double) std::abs (o.getSample (0, i) - 0.5f * (in.getSample (0, i) - in.getSample (1, i))));
        check (e < 1e-6, "Listen: Side", "both outputs carry (L-R)/2");
    }

    // 4. M/S encode then decode returns the original
    {
        UtilityProcessor enc, dec; prepare (enc, 48000, 2, 2); prepare (dec, 48000, 2, 2);
        setParam (enc, "matrix", 1); setParam (dec, "matrix", 2);
        enc.prepareToPlay (48000, 512); dec.prepareToPlay (48000, 512);
        auto in = noise (2, 48000, 4);
        auto o = run (dec, run (enc, in));
        const double d = std::max (maxAbsDiff (o, in, 0, 0), maxAbsDiff (o, in, 1, 1));
        check (d < 1e-6, "M/S encode + decode round trip", "max diff " + juce::String (d));
    }

    // 5. Pan law: hard left at -3 dB law raises left by 3 dB and mutes right
    {
        UtilityProcessor p; prepare (p, 48000, 2, 2);
        setParam (p, "pan", -100); p.prepareToPlay (48000, 512);
        auto in = noise (2, 48000, 5, 0.1f);
        auto o = run (p, in);
        const double gl = 20 * std::log10 (rms (o.getReadPointer (0) + 2400, 45600) / rms (in.getReadPointer (0) + 2400, 45600));
        const double rr = rms (o.getReadPointer (1) + 2400, 45600);
        check (std::abs (gl - 3.0103) < 0.01 && rr < 1e-6, "Pan hard left, -3 dB law", "left " + db (gl) + ", right silent");
    }

    // 6. Bass mono: side response (written to CSV) and the mid untouched
    {
        UtilityProcessor p; prepare (p, 48000, 2, 2);
        setParam (p, "bassMono", 1); setParam (p, "bassFreq", 120); p.prepareToPlay (48000, 512);
        juce::String csv ("freq_hz,side_db,mid_db\n");
        double at30 = 0, at1k = 0, midWorst = 0;
        for (double f = 20; f <= 20000; f *= std::pow (2.0, 1.0 / 6.0))
        {
            p.prepareToPlay (48000, 512);
            juce::AudioBuffer<float> s (2, 48000), m (2, 48000);
            for (int i = 0; i < 48000; ++i)
            {
                const float x = 0.25f * (float) std::sin (2 * juce::MathConstants<double>::pi * f * i / 48000.0);
                s.setSample (0, i, x); s.setSample (1, i, -x);   // pure side
                m.setSample (0, i, x); m.setSample (1, i, x);    // pure mid
            }
            auto os = run (p, s);
            p.prepareToPlay (48000, 512);
            auto om = run (p, m);
            const double sd = 20 * std::log10 (std::max (1e-9, rms (os.getReadPointer (0) + 24000, 24000) / rms (s.getReadPointer (0) + 24000, 24000)));
            const double md = 20 * std::log10 (rms (om.getReadPointer (0) + 24000, 24000) / rms (m.getReadPointer (0) + 24000, 24000));
            midWorst = std::max (midWorst, std::abs (md));
            if (std::abs (f - 30) < 3) at30 = sd;
            if (std::abs (f - 1016) < 60) at1k = sd;
            csv << juce::String (f, 2) << "," << juce::String (sd, 3) << "," << juce::String (md, 4) << "\n";
        }
        outDir.getChildFile ("bass-mono-response.csv").replaceWithText (csv);
        check (at30 < -20.0, "Bass mono 120 Hz: side at ~30 Hz", db (at30));
        check (std::abs (at1k) < 0.1, "Bass mono 120 Hz: side at ~1 kHz", db (at1k));
        check (midWorst < 0.001, "Bass mono leaves the mid untouched", "worst " + db (midWorst));
    }

    // 7. Click test: flip polarity during a 1 kHz tone; the step must stay smooth
    {
        UtilityProcessor p; prepare (p, 48000, 2, 2, 64);
        juce::AudioBuffer<float> in (2, 9600);
        for (int i = 0; i < 9600; ++i) { const float x = 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0); in.setSample (0, i, x); in.setSample (1, i, x); }
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < 9600; pos += 64)
        {
            if (pos == 4800) { setParam (p, "polL", 1); setParam (p, "gain", -24); setParam (p, "bypass", 1); setParam (p, "bypass", 0); }
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, 64);
            p.processBlock (v, midi);
        }
        const double maxSlopeTone = 0.5 * 2 * juce::MathConstants<double>::pi * 1000.0 / 48000.0;
        double worst = 0;
        for (int i = 1; i < 9600; ++i) worst = std::max (worst, (double) std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        check (worst <= maxSlopeTone * 1.05, "No clicks when switching polarity, gain and bypass", "largest step " + juce::String (worst, 4) + " vs tone slope " + juce::String (maxSlopeTone, 4));
    }

    // 8. Loudness meter against EBU Tech 3341 style references
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            aemon::LoudnessMeter m; m.prepare (fs, 2);
            const int n = (int) (fs * 20);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            const float a = juce::Decibels::decibelsToGain (-23.0f);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = a * (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / fs);
            const float* ch[] = { l.data(), r.data() };
            m.process (ch, 2, n);
            check (std::abs (m.getIntegrated() + 23.0f) < 0.1f && std::abs (m.getShortTerm() + 23.0f) < 0.1f,
                   "Loudness: 1 kHz at -23 dBFS stereo = -23 LUFS @" + juce::String (fs / 1000.0, 1) + " kHz",
                   "I " + juce::String (m.getIntegrated(), 2) + ", S " + juce::String (m.getShortTerm(), 2));
        }
        aemon::LoudnessMeter m; m.prepare (48000, 2);
        std::vector<float> l (48000 * 2), r (48000 * 2, 0.0f);
        for (size_t i = 0; i < l.size(); ++i) l[i] = (float) std::sin (2 * juce::MathConstants<double>::pi * 12000.0 * (double) i / 48000.0 + 0.785398);
        const float* ch[] = { l.data(), r.data() };
        m.process (ch, 2, (int) l.size());
        float sp = 0; for (float x : l) sp = std::max (sp, std::abs (x));
        check (m.getTruePeakMaxDb() > juce::Decibels::gainToDecibels (sp) + 2.0f && std::abs (m.getTruePeakMaxDb()) < 0.6f,
               "True peak catches inter-sample peaks (fs/4 sine at 45 degrees)",
               "sample peak " + db (juce::Decibels::gainToDecibels (sp)) + ", true peak " + db (m.getTruePeakMaxDb()));
    }

    // 9. State save and recall
    {
        UtilityProcessor a; prepare (a, 48000, 2, 2);
        setParam (a, "gain", -4.5f); setParam (a, "width", 133); setParam (a, "polR", 1); setParam (a, "monitor", 1); setParam (a, "bassFreq", 95);
        a.pluginState.ui.setProperty ("mode", "pro", nullptr);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        UtilityProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (auto* prm : a.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                same &= std::abs (r->getValue() - b.apvts.getParameter (r->getParameterID())->getValue()) < 1e-6f;
        check (same && b.pluginState.ui.getProperty ("mode").toString() == "pro", "State save and recall", "all parameters and UI mode restored");
    }

    // 10. CPU: one minute of stereo at 48 kHz, busiest settings
    {
        UtilityProcessor p; prepare (p, 48000, 2, 2);
        setParam (p, "width", 140); setParam (p, "bassMono", 1); setParam (p, "pan", 20); setParam (p, "gain", -3);
        p.prepareToPlay (48000, 512);
        auto in = noise (2, 48000 * 60, 9);
        const auto t0 = std::chrono::high_resolution_clock::now();
        run (p, in);
        const double secs = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
        check (secs < 3.0, "CPU", juce::String (secs * 1000.0 / 60.0, 3) + " ms per second of audio (" + juce::String (100.0 * secs / 60.0, 3) + "% of one core)");
    }

    report << "\n" << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILED") << "\n";
    outDir.getChildFile ("report.txt").replaceWithText (report);
    style::run<UtilityProcessor> (rows::utility(), -18, [] (bool ok, const juce::String& n, const juce::String& d) { check (ok, n, d); }, false, 0.95, true);
    std::printf ("\n%s\n", failures == 0 ? "ALL PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
