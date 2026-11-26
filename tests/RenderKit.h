// Before and after files for listening: a voice or a beat made here, through every preset. AGPL-3.0-or-later.
#pragma once
#include "PerceptReport.h"

namespace renderkit
{
inline void write (const juce::File& f, const juce::AudioBuffer<float>& b) { f.deleteFile(); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000, 2, 16, {}, 0)); if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
inline juce::AudioBuffer<float> source (bool vocal)
{
    percept::Vec x; const double fs = 48000;
    if (vocal) x = voicekit::takes (fs, {}, { 0 }).x;
    else { const auto k = drumkit::loop ("kick", fs, 10, 0.5), s = drumkit::loop ("snare", fs, 10, 0.5), h = drumkit::loop ("hat", fs, 20, 0.25); x.assign (k.size(), 0.0f); for (size_t i = 0; i < k.size(); ++i) x[i] = k[i] + 0.4f * h[i] + (i >= 24000 && (i / 24000) % 2 == 1 ? 0.8f * s[i % 24000] : 0.0f); }
    const double g = std::pow (10.0, (-20 - percept::rmsDb (x)) / 20); for (auto& v : x) v = (float) (v * g);
    juce::AudioBuffer<float> b (2, (int) x.size() + 96000); b.clear(); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); return b;
}
/** Every factory preset of P; prep is called after the preset is loaded. The level of the dry file, so that nothing wins by being louder. */
template <typename P> int run (const juce::File& dir, const std::function<void (P&)>& prep)
{
    dir.createDirectory(); write (dir.getChildFile ("0 dry voice.wav"), source (true)); write (dir.getChildFile ("0 dry beat.wav"), source (false)); P probe; int n = 0;
    for (auto* pr : probe.pluginState.allPresets())
    {
        if (pr->user || pr->category.isEmpty()) continue;
        const bool vocal = pr->category.containsIgnoreCase ("vocal"); P p; p.pluginState.loadPreset (*pr); if (prep) prep (p);
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); if (p.getBusCount (true) > 1) l.inputBuses.add (juce::AudioChannelSet::disabled()); l.outputBuses.add (juce::AudioChannelSet::stereo()); p.setBusesLayout (l); p.setRateAndBufferSizeDetails (48000, 512); p.prepareToPlay (48000, 512);
        auto b = source (vocal); const auto dry = b; juce::MidiBuffer m; for (int pos = 0; pos < b.getNumSamples(); pos += 512) { juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos)); p.processBlock (v, m); }
        if (auto* s = p.apvts.getParameter ("send")) if (s->getValue() > 0.5f) for (int c = 0; c < 2; ++c) { b.applyGain (c, 0, b.getNumSamples(), 0.3f); b.addFrom (c, 0, dry, c, 0, b.getNumSamples()); }
        double a = 1e-12, o = 1e-12; float top = 0; for (int i = 0; i < b.getNumSamples(); ++i) { a += std::pow (dry.getSample (0, i), 2); o += std::pow (b.getSample (0, i), 2); top = juce::jmax (top, std::abs (b.getSample (0, i))); } float g = (float) std::sqrt (a / o); if (top * g > 0.97f) g = 0.97f / top; b.applyGain (g);
        write (dir.getChildFile (juce::File::createLegalFileName (pr->category.fromLastOccurrenceOf (":", false, false).trim() + " - " + pr->name.replaceCharacter (':', ',')) + ".wav"), b); ++n;
    }
    std::printf ("%d files in %s\n", n, dir.getFullPathName().toRawUTF8()); return 0;
}
} // namespace renderkit
