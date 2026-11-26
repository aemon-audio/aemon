// A report on every factory preset of a plugin, by calculation (PerceptKit.h). Never "listened to". AGPL-3.0-or-later.
// Run: Aemon<Name>Tests --percept <file.csv>
#pragma once
#include <aemon_core/aemon_core.h>
#include "PerceptKit.h"
#include "VoiceKit.h"
#include "DrumKit.h"
#include <cstdio>

namespace preport
{
using percept::Vec;
struct Sound { const char* name; Vec x; bool voice; };
inline std::vector<Sound> sounds (double fs)
{
    std::vector<Sound> v; const auto level = [] (Vec x, double want) { const double g = std::pow (10.0, (want - percept::rmsDb (x)) / 20); float top = 0; for (auto& s : x) { s = (float) (s * g); top = std::max (top, std::abs (s)); } if (top > 0.89f) for (auto& s : x) s *= 0.89f / top; return x; };
    v.push_back ({ "voice", level (voicekit::takes (fs, {}, { 0 }).x, -20), true });
    v.push_back ({ "low voice", level (voicekit::takes (fs, { 0.5, 0.75, 1.0, 0.92, 0.5, 7 }, { 0 }).x, -20), true });
    v.push_back ({ "high voice", level (voicekit::takes (fs, { 1.8, 1.2, 1.2, 1.15, 0.3, 11 }, { 0 }).x, -20), true });
    Vec drums; { const auto k = drumkit::loop ("kick", fs, 10, 0.5), s = drumkit::loop ("snare", fs, 10, 0.5), h = drumkit::loop ("hat", fs, 20, 0.25); drums.assign (k.size(), 0.0f); for (size_t i = 0; i < k.size(); ++i) { drums[i] = k[i] + 0.5f * h[i]; if (i >= 12000) drums[i] += 0.7f * s[i - 12000]; } }
    v.push_back ({ "drums", level (drums, -18), false });
    Vec bass (drums.size()); for (size_t i = 0; i < bass.size(); ++i) { const double t = (double) (i % 48000) / fs; bass[i] = (float) (std::tanh (1.5 * std::sin (2 * percept::pi * 46 * t)) * std::exp (-t * 1.5) * std::min (1.0, t / 0.004) * std::min (1.0, (1.0 - t) / 0.01)); } v.push_back ({ "808", level (bass, -18), false });
    Vec keys (drums.size()); for (size_t i = 0; i < keys.size(); ++i) { const double t = (double) (i % 48000) / fs; double s = 0; for (double f : { 220.0, 261.6, 329.6, 392.0 }) for (int h = 1; h < 6; ++h) s += std::sin (2 * percept::pi * f * h * t) / (h * h) * std::exp (-t * (1 + h)); keys[i] = (float) (s * std::min (1.0, t / 0.003) * std::min (1.0, (1.0 - t) / 0.02)); } v.push_back ({ "keys", level (keys, -20), false });
    Vec mix (drums.size()); for (size_t i = 0; i < mix.size(); ++i) mix[i] = 0.5f * v[3].x[i] + 0.5f * v[4].x[i] + 0.4f * v[5].x[i] + 0.4f * v[0].x[i % v[0].x.size()]; v.push_back ({ "mix", level (mix, -16), false });
    return v;
}
template <typename P> int run (const char* plugin, const char* file)
{
    const double fs = 48000; const auto list = sounds (fs); FILE* out = std::fopen (file, "w"); if (out == nullptr) return 1;
    std::fprintf (out, "plugin;preset;sound;loudness change dB;peak dB;words;2-5 kHz dB;S range dB;sharpness;roughness;click dB;phone loudness dB;flags\n"); int flags = 0, rows = 0;
    P probe; std::vector<juce::String> names; for (auto* pr : probe.pluginState.allPresets()) if (! pr->user) names.push_back (pr->name);
    for (auto& name : names)
        for (auto& s : list)
        {
            P p; for (auto* pr : p.pluginState.allPresets()) if (! pr->user && pr->name == name) { p.pluginState.loadPreset (*pr); break; }
            juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); if (p.getBusCount (true) > 1) l.inputBuses.add (juce::AudioChannelSet::disabled()); l.outputBuses.add (juce::AudioChannelSet::stereo()); p.setBusesLayout (l); p.setRateAndBufferSizeDetails (fs, 512); p.prepareToPlay (fs, 512);
            const int lat = p.getLatencySamples(), n = (int) s.x.size(); juce::AudioBuffer<float> b (2, n + lat + 4800); b.clear(); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, s.x.data(), n);
            juce::MidiBuffer m; for (int pos = 0; pos < b.getNumSamples(); pos += 512) { juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos)); p.processBlock (v, m); }
            const int from = 24000; Vec in (s.x.begin() + from, s.x.end()), y, yr; for (int i = from; i < n; ++i) { y.push_back (b.getSample (0, i + lat)); yr.push_back (b.getSample (1, i + lat)); }
            float peak = 0; for (float v : y) peak = std::max (peak, std::abs (v));
            const auto ta = percept::tone (in, fs), tb = percept::tone (y, fs); const double loud = percept::rmsDb (y) - percept::rmsDb (in), w = s.voice ? percept::words (in, y, fs) : 1, pres = percept::rangeChange (ta, tb, 2000, 5000) - loud, ess = percept::rangeChange (ta, tb, 5000, 12000) - percept::rangeChange (ta, tb, 200, 2000);
            const auto ca = percept::colourOf (in, fs), cb = percept::colourOf (y, fs); const double ra = percept::roughness (in, fs), rb = percept::roughness (y, fs), ka = percept::clickDb (in, fs), kb = percept::clickDb (y, fs);
            const double ph = percept::rmsDb (percept::phone (y, yr, fs)) - percept::rmsDb (percept::phone (in, in, fs));
            juce::String f; if (w < 0.85) f << "words "; if (s.voice && pres < -4) f << "buries2-5k "; if (s.voice && ess > 4) f << "sharperS "; if (kb > std::max (30.0, ka + 14)) f << "click "; if (peak > 1.0f) f << "over0 "; if (rb > 2 * ra + 0.25) f << "rough "; if (! std::isfinite (loud)) f << "silent ";
            flags += f.isNotEmpty(); ++rows;
            std::fprintf (out, "%s;%s;%s;%.1f;%.1f;%.3f;%.1f;%.1f;%.2f>%.2f;%.2f>%.2f;%.0f>%.0f;%.1f;%s\n", plugin, name.toRawUTF8(), s.name, loud, 20 * std::log10 (peak + 1e-9), w, pres, ess, ca.sharpness, cb.sharpness, ra, rb, ka, kb, ph, f.trim().toRawUTF8());
        }
    std::fclose (out); std::printf ("%s: %d presets, %d sounds measured, %d flagged\n", plugin, (int) names.size(), rows, flags); return 0;
}
} // namespace preport
