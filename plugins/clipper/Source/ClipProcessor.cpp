// Aemon Clipper. AGPL-3.0-or-later.
#include "ClipProcessor.h"
#include "ClipEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ClipProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    const auto fl = AudioParameterFloatAttributes();
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "push", 1 }, "Push", NormalisableRange<float> (0, 18, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceil", 1 }, "Ceiling", NormalisableRange<float> (-12, 0, 0.1f), -1.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "soft", 1 }, "Softness", NormalisableRange<float> (0, 100, 1), 35.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "link", 1 }, "Stereo Link", NormalisableRange<float> (0, 100, 1), 100.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0, 100, 1), 100.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling", StringArray { "Off", "2x", "4x", "8x" }, 3));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Equal Loudness", false, AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Hear Removed", false, AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

std::vector<aemon::Preset> ClipProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    auto add = [&v] (const char* cat, const char* name, const char* desc, float push, float soft, float ceil = -1.0f)
    {
        aemon::Preset p; p.name = name; p.category = cat; p.description = desc;
        p.values = { { "push", push }, { "soft", soft }, { "ceil", ceil } };
        v.push_back (std::move (p));
    };
    add ("", "Safety net: only stray peaks", "Does nothing until a peak passes the ceiling, then cuts it. Bit-exact otherwise.", 0, 0);
    // styles
    add ("Style: vocals", "Trap lead: shouts shaved", "Takes the very top off loud words, softly.", 3, 70);
    add ("Style: vocals", "Rage lead: clipped loud", "Pushed into a hard edge on purpose.", 10, 30, -2);
    add ("Style: vocals", "Screamed voice: peaks held", "A round top for a voice that jumps.", 5, 85);
    add ("Style: beats", "Boom bap drums: sampler hard", "A hard edge on the hits.", 7, 8);
    add ("Style: beats", "Trap drums: loud kit, snap kept", "Cuts only the first instant of every hit.", 8, 20);
    add ("Style: beats", "Trap 808: fat and loud", "All soft: the 808 grows into its harmonics.", 12, 100);
    add ("Style: beats", "Drill 808: solid slide", "A softer push, so slides stay clean.", 6, 80, -2);
    add ("Style: beats", "Memphis beat: red-lined", "Far over the top, like a tape recorded too loud.", 13, 45);
    add ("Style: beats", "House drums: solid", "A little off every kick, for a steady level.", 5, 30);
    add ("Mastering", "Master: 2 to 3 dB off the peaks, unheard", "Shaves the tallest peaks before the limiter, so the limiter works less.", 4, 25);
    add ("Mastering", "Master: loud and dense", "More push with a softer top. Follow with a gentle limiter.", 8, 60);
    add ("Drums", "Drum bus: more level, same punch", "Cuts only the first instant of each hit, so the whole bus can come up.", 6, 15);
    add ("Drums", "Kick: shorter and harder", "A hard edge on the beater. Sounds tighter and more forward.", 7, 0, -2);
    add ("Drums", "Snare: thicker crack", "A rounded top adds body to the crack.", 8, 70);
    add ("Bass & 808", "808: louder on small speakers", "Soft clipping adds harmonics phones can play.", 9, 100);
    add ("Buses", "Mix bus: trim the spikes", "A little headroom back without touching the sound.", 2, 20);
    add ("Buses", "Vocal: keep plosives and shouts in check", "Catches the loudest syllables with a soft top.", 3, 80);
    add ("Creative", "Break it (effect)", "Far too much, on purpose.", 18, 0);
    return v;
}

ClipProcessor::ClipProcessor() : EffectProcessor ("Clipper", "AemonClipper", createLayout(), factoryPresets()) {}

aemon::clip::Params ClipProcessor::readParams() const
{
    aemon::clip::Params p;
    p.pushDb = real ("push"); p.ceilingDb = real ("ceil"); p.softness = real ("soft") / 100.0; p.link = real ("link") / 100.0; p.mix = real ("mix") / 100.0;
    p.oversampling = 1 << juce::jlimit (0, 3, juce::roundToInt (real ("os")));
    p.delta = real ("delta") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    return p;
}

float ClipProcessor::frameAux()
{
    double off; int clipped;
    engine.takeStats (off, clipped);
    const float pc = samples > 0 ? 100.0f * (float) clipped / (float) (2 * samples) : 0.0f;
    clippedPercent = clippedPercent.load() + 0.004f * (pc - clippedPercent.load());
    levels.forget ((float) samples / (float) currentRate.load());
    samples = 0;
    return (float) off;
}

clipp::Suggestion clipp::suggest (const std::array<float, (size_t) LevelHistory::bins>& h, float ceiling, float softness)
{
    Suggestion s;
    double total = 0; int top = -1;
    for (int i = 0; i < LevelHistory::bins; ++i) { total += h[(size_t) i]; if (h[(size_t) i] > 0.05f) top = i; }
    if (total < 48000.0 || top < 0) return s;       // about a second of sound
    // a soft top starts to bend below the ceiling
    aemon::dist::ClipCurve curve; curve.knee = softness / 100.0;
    double x = 0.05; while (x < 1.0 && x - curve.f (x) < 1.0e-4) x += 0.002;
    const float bend = ceiling + (float) (20.0 * std::log10 (x)), topDb = LevelHistory::dbOf (top);
    auto percentAt = [&] (float push) { double over = 0; for (int i = top; i >= 0 && LevelHistory::dbOf (i) + push > bend; --i) over += h[(size_t) i]; return (float) (100.0 * over / total); };
    float push = std::floor (juce::jlimit (0.0f, 18.0f, ceiling - topDb + 3.0f) * 10.0f) / 10.0f;
    while (push > 0.0f && percentAt (push) > 1.0f) push -= 0.1f;
    s.ok = true;
    s.push = std::max (0.0f, push);
    s.off = std::max (0.0f, topDb + s.push - ceiling);
    s.percent = percentAt (s.push);
    return s;
}

juce::AudioProcessorEditor* ClipProcessor::createEditor() { return new ClipEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ClipProcessor(); }
