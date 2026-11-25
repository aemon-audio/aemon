// Aemon Saturation. AGPL-3.0-or-later.
#include "SatProcessor.h"
#include "SatEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout SatProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    const auto fl = AudioParameterFloatAttributes();
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive", NormalisableRange<float> (0, 30, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "char", 1 }, "Character", StringArray { "Warm", "Thick", "Bright", "Broken" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "focus", 1 }, "Focus", StringArray { "All", "Lows", "Mids", "Highs" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bias", 1 }, "Bias", NormalisableRange<float> (0, 200, 1), 50.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone", NormalisableRange<float> (-6, 6, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0, 100, 1), 100.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output", NormalisableRange<float> (-12, 12, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling", StringArray { "Off", "2x", "4x", "8x" }, 3));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Level Matched", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

std::vector<aemon::Preset> SatProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    auto add = [&v] (const char* cat, const char* name, const char* desc, float drive, int ch, int focus = 0, float mix = 100, float bias = 50)
    {
        aemon::Preset p; p.name = name; p.category = cat; p.description = desc;
        p.values = { { "drive", drive }, { "char", (float) ch }, { "focus", (float) focus }, { "mix", mix }, { "bias", bias } };
        v.push_back (std::move (p));
    };
    enum { warm, thick, bright, broken }; enum { all, lows, mids, highs };
    add ("", "Clean start: changes nothing", "No drive. Bit-exact.", 0, warm);
    add ("Mix bus", "Mix bus: warmth you miss when it is gone", "A little even harmonic weight on the whole mix.", 5, warm);
    add ("Mix bus", "Mix bus: denser and closer", "Thickens quiet detail without changing the balance.", 8, thick, all, 60);
    add ("Vocals", "Vocal: warmer, closer", "Even harmonics in the body of the voice.", 8, warm, mids);
    add ("Vocals", "Vocal: presence without EQ", "Harmonics on top bring the words forward.", 10, bright, highs, 55);
    add ("Vocals", "Rap vocal: gritty edge", "Grit blended under the clean voice.", 14, broken, all, 25);
    add ("Drums", "Drum bus: weight and attitude", "Low-driven saturation, level matched.", 10, thick, lows);
    add ("Drums", "Snare: more crack", "Drives the upper mids.", 11, bright, mids);
    add ("Drums", "Room mics: crush it", "Heavy and dirty, blend to taste.", 22, broken, all, 45);
    add ("Bass & 808", "808: heard on phones", "Adds the harmonics small speakers can play.", 13, thick, lows);
    add ("Bass & 808", "Bass guitar: growl", "Mid harmonics, low end kept clean.", 12, warm, mids, 70);
    add ("Instruments", "Synths: alive", "A touch of movement and air.", 7, bright, all, 70);
    add ("Instruments", "Guitars: thicker", "More body without more volume.", 9, thick, mids);
    add ("Creative", "Fuzz (effect)", "Folds and breaks the sound on purpose.", 30, broken, all, 100, 120);
    return v;
}

SatProcessor::SatProcessor() : EffectProcessor ("Saturation", "AemonSaturation", createLayout(), factoryPresets()) {}

aemon::sat::Params SatProcessor::readParams() const
{
    aemon::sat::Params p;
    p.driveDb = real ("drive"); p.character = (aemon::sat::Character) juce::jlimit (0, 3, juce::roundToInt (real ("char"))); p.focus = (aemon::sat::Focus) juce::jlimit (0, 3, juce::roundToInt (real ("focus")));
    p.bias = real ("bias") / 100.0; p.toneDb = real ("tone"); p.mix = real ("mix") / 100.0; p.outputDb = real ("out");
    p.oversampling = 1 << juce::jlimit (0, 3, juce::roundToInt (real ("os")));
    p.match = real ("match") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    return p;
}

std::array<float, 9> satp::harmonics (const aemon::sat::Params& p)
{
    constexpr double f0 = 1000.0, fs = 192000.0;
    aemon::sat::Curve c; c.set (p.character, p.driveDb, p.bias);
    aemon::sat::Shaping s; s.set (p.character, p.focus, p.driveDb, p.toneDb, fs);
    const double amp = 0.25 * s.pre.gainAt (f0, fs);
    constexpr int N = 256;
    std::array<float, 9> out {};
    double y[N]; for (int i = 0; i < N; ++i) y[i] = c.f (amp * std::sin (juce::MathConstants<double>::twoPi * i / N));
    double first = 1e-12;
    for (int h = 1; h <= 9; ++h)
    {
        double re = 0, im = 0;
        for (int i = 0; i < N; ++i) { const double w = juce::MathConstants<double>::twoPi * h * i / N; re += y[i] * std::cos (w); im += y[i] * std::sin (w); }
        const double m = std::sqrt (re * re + im * im) * s.post.gainAt (f0 * h, fs) * s.tone.gainAt (f0 * h, fs);
        if (h == 1) first = std::max (1e-12, m);
        out[(size_t) (h - 1)] = (float) (20.0 * std::log10 (m / first + 1e-9));
    }
    return out;
}

float satp::thd (const std::array<float, 9>& h) { double s = 0; for (size_t i = 1; i < 9; ++i) s += std::pow (10.0, h[i] / 10.0); return (float) (100.0 * std::sqrt (s)); }

float satp::driveFor (aemon::sat::Params p, float percent)
{
    float lo = 0, hi = 30;
    for (int i = 0; i < 24; ++i) { const float m = 0.5f * (lo + hi); p.driveDb = m; if (thd (harmonics (p)) < percent) lo = m; else hi = m; }
    return std::round (lo * 10.0f) / 10.0f;
}

juce::AudioProcessorEditor* SatProcessor::createEditor() { return new SatEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SatProcessor(); }
