// Aemon Low End. AGPL-3.0-or-later.
#include "LowProcessor.h"
#include "LowEditor.h"

using namespace aemon::low;

juce::AudioProcessorValueTreeState::ParameterLayout LowProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    const auto fl = AudioParameterFloatAttributes();
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "delay", 1 }, "Delay", NormalisableRange<float> (-10, 10, 0.01f), 0.0f, fl.withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " ms"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "turn", 1 }, "Phase Turn", NormalisableRange<float> (0, 150, 1), 0.0f, fl.withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " deg"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "flip", 1 }, "Flip", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "duck", 1 }, "Duck", NormalisableRange<float> (0, 24, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "below", 1 }, "Below", logRange (60, 200, 1), 120.0f, fl.withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "rel", 1 }, "Release", logRange (20, 400, 1), 120.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "clash", 1 }, "Only What Clashes", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    struct P { const char* cat; const char* name; const char* desc; float duck, below, rel; bool clash; };
    const P table[] {
        { "", "Clean start: changes nothing", "No move, no duck. Bit-exact.", 0, 120, 120, true },
        { "Style: beats", "Trap: kick over a long 808", "Up to 10 dB while the kick hits, back quickly.", 10, 120, 80, true },
        { "Style: beats", "Drill: slides stay, kick comes through", "Up to 5 dB, only what clashes.", 5, 130, 60, true },
        { "Style: beats", "West Coast: rolling bass, soft kick", "Up to 2 dB and slow. Only a little air.", 2, 120, 250, true },
        { "Style: beats", "Phonk: distorted lows step back", "Up to 14 dB under 180 Hz.", 14, 180, 160, false },
        { "Style: beats", "Boom bap: sampled bass under the kick", "Up to 4 dB under 120 Hz.", 4, 120, 120, false },
        { "Style: beats", "Afrobeats: bass dances around the kick", "Up to 7 dB, gone in a blink.", 7, 140, 35, true },

        { "808 and kick", "808: make room for the kick", "Up to 6 dB, only the lows both of them use.", 6, 120, 120, true },
        { "808 and kick", "808: just a little room", "Up to 3 dB, hardly heard, cleaner together.", 3, 110, 100, true },
        { "808 and kick", "808: kick on top", "Up to 12 dB, for a kick that has to hit.", 12, 120, 140, true },
        { "808 and kick", "808: kick first, then the note", "The lows of the 808 wait for the kick, up to 24 dB.", 24, 140, 90, false },
        { "808 and kick", "Long 808: breathes with the kick", "Up to 9 dB and a slow way back.", 9, 130, 320, false },
        { "808 and kick", "Distorted 808: lows only, grit stays", "Up to 8 dB under 90 Hz. The audible part is not touched.", 8, 90, 120, false },
        { "Bass and kick", "Bass guitar: room under the kick", "Up to 4 dB, quick, only where they meet.", 4, 140, 70, true },
        { "Bass and kick", "Synth bass: tight", "Up to 6 dB and gone fast.", 6, 150, 50, true },
        { "Bass and kick", "Sub: steady under the kick", "Up to 5 dB under 80 Hz.", 5, 80, 120, true },
        { "Bass and kick", "House bass: pump", "Up to 15 dB, the whole low end, in time with the kick.", 15, 200, 250, false },
        { "Bass and kick", "Techno rumble: under the kick", "Up to 18 dB, with a long tail.", 18, 160, 300, false },
        { "Bass and kick", "Reese: lows step aside", "Up to 7 dB under 120 Hz, the movement above stays.", 7, 120, 110, false },
        { "Other", "Pad or piano: lows out of the kick's way", "Up to 6 dB under 180 Hz.", 6, 180, 150, false },
        { "Other", "Reverb return: no mud on the kick", "Up to 12 dB under 200 Hz.", 12, 200, 180, false },
        { "Other", "Toms or loop: kick stays clear", "Up to 5 dB, only what clashes.", 5, 160, 90, true },
        { "Other", "Whole beat: room for an 808", "Put it on the beat, the 808 on the sidechain. Up to 4 dB, only what clashes.", 4, 110, 140, true },
    };
}

lowp::Intent lowp::intentOf (const juce::String& name) { for (auto& p : table) if (name == p.name) return { p.duck, p.clash }; return {}; }

std::vector<aemon::Preset> LowProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table) { aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc; o.values = { { "duck", p.duck }, { "below", p.below }, { "rel", p.rel }, { "clash", p.clash ? 1.0f : 0.0f } }; v.push_back (std::move (o)); }
    return v;
}

LowProcessor::LowProcessor() : EffectProcessor ("Low End", "AemonLowEnd", createLayout(), factoryPresets(), true) {}

Params LowProcessor::readParams() const
{
    Params p;
    p.align = { real ("delay"), real ("turn"), real ("flip") > 0.5f }; p.duck = real ("duck"); p.below = real ("below"); p.releaseMs = real ("rel"); p.clash = real ("clash") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    return p;
}

void LowProcessor::prepareEngine (double fs)
{
    engine->setParams (readParams()); engine->prepare (fs); tap.prepare (fs);
    const juce::ScopedLock s (lock); ringB.assign ((size_t) (tap.rate() * 8), 0.0f); ringK = ringB; pos = 0; filled = 0;
}

void LowProcessor::processEngine (float* L, float* R, int n)
{
    const int size = (int) ringB.size(); int w = pos.load (std::memory_order_relaxed), f = filled.load();
    for (int i = 0; i < n; ++i)
    {
        float b, k;
        if (! tap.push (0.5 * ((double) L[i] + R[i]), sideL != nullptr ? 0.5 * ((double) sideL[i] + sideR[i]) : 0.0, b, k)) continue;
        ringB[(size_t) w] = b; ringK[(size_t) w] = k; if (++w == size) w = 0; f = std::min (size, f + 1);
    }
    pos.store (w, std::memory_order_release); filled = f;
    engine->process (L, R, n, sideL, sideR);
}

Tracks LowProcessor::tracks() const
{
    const juce::ScopedLock s (lock); Tracks t; t.fs = tap.rate();
    const int size = (int) ringB.size(), n = std::min (size, filled.load()), w = pos.load (std::memory_order_acquire);
    t.bass.resize ((size_t) n); t.kick.resize ((size_t) n);
    for (int i = 0; i < n; ++i) { const auto q = (size_t) ((w - n + i + size) % size); t.bass[(size_t) i] = ringB[q]; t.kick[(size_t) i] = ringK[q]; }
    return t;
}

float LowProcessor::frameAux()
{
    const auto f = engine->takeFrame();
    fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; });
    return std::max ({ f.cut[0], f.cut[1], f.cut[2] });
}

int LowProcessor::popLow (Engine::Frame* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; });
    return n;
}

juce::AudioProcessorEditor* LowProcessor::createEditor() { return new LowEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LowProcessor(); }
