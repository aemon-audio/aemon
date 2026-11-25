// Aemon Imager. AGPL-3.0-or-later.
#include "ImagerProcessor.h"
#include "ImagerEditor.h"

using namespace aemon::img;

juce::AudioProcessorValueTreeState::ParameterLayout ImagerProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto dB = [] (float v, int) { return dbText (v); };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto low = [] (float v, int) { return v <= 20.5f ? String ("off") : String (roundToInt (v)) + " Hz"; };
    const auto fl = AudioParameterFloatAttributes();
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, auto text) { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, fl.withStringFromValueFunction (text))); };
    f ("width", "Width", { 0, 200, 1 }, 100, pc);
    f ("mono", "Low End", logRange (20, 400, 1), 20, low);
    f ("make", "Make Stereo", { 0, 100, 1 }, 0, pc);
    f ("centre", "Balance", { -50, 50, 0.5f }, 0, pc);
    f ("w1", "Width Lows", { 0, 200, 1 }, 100, pc);
    f ("w2", "Width Low Mids", { 0, 200, 1 }, 100, pc);
    f ("w3", "Width High Mids", { 0, 200, 1 }, 100, pc);
    f ("w4", "Width Highs", { 0, 200, 1 }, 100, pc);
    f ("f1", "Split Low", logRange (60, 500, 1), 150, hz);
    f ("f2", "Split Mid", logRange (500, 4000, 1), 1500, hz);
    f ("f3", "Split High", logRange (3000, 14000, 10), 6000, hz);
    f ("limit", "Lose At Most", { 1, 6, 0.1f }, 3, dB);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "safe", 1 }, "Mono-Safe", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "delay", 1 }, "Quality", StringArray { "Straight", "Zero Delay" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "hearMono", 1 }, "Hear In Mono", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "hearSide", 1 }, "Hear Only The Sides", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Equal Loudness", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

std::vector<aemon::Preset> ImagerProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    using V = std::vector<std::pair<const char*, double>>;
    auto add = [&v] (const char* cat, const char* name, const char* desc, V values) { aemon::Preset p; p.name = name; p.category = cat; p.description = desc; for (auto& [id, x] : values) p.values.emplace_back (id, (float) x); v.push_back (std::move (p)); };
    add ("", "Clean start: changes nothing", "Everything at 100 %. Bit-exact.", {});

    add ("Vocals", "Lead vocal: wide but solid", "A little width on top. The body stays in the middle, mono loses at most 1.5 dB.", { { "make", 25 }, { "mono", 250 }, { "w2", 70 }, { "w4", 140 }, { "safe", 1 }, { "limit", 1.5 } });
    add ("Vocals", "Backing vocals: around the lead", "Moves the stack outwards and leaves the middle to the lead.", { { "width", 160 }, { "make", 15 }, { "mono", 200 }, { "safe", 1 }, { "limit", 4 } });
    add ("Vocals", "Doubles and ad-libs: to the sides", "Wide and airy. In mono they fold back to the dry voice.", { { "make", 60 }, { "width", 130 }, { "mono", 250 }, { "safe", 1 }, { "limit", 5 } });
    add ("Vocals", "Mono vocal: gentle space", "Just enough width to stop sounding like a dot.", { { "make", 18 }, { "mono", 300 } });

    add ("808 and bass", "808: solid in the middle", "Everything below 150 Hz in mono, the rest as it was.", { { "mono", 150 } });
    add ("808 and bass", "808: mono sub, wide grit", "The sub stays in one place, the harmonics open up.", { { "mono", 140 }, { "w1", 0 }, { "w2", 110 }, { "w3", 150 }, { "w4", 160 }, { "make", 20 }, { "f1", 140 }, { "f2", 900 }, { "safe", 1 }, { "limit", 3 } });
    add ("808 and bass", "Bass guitar: tighten the lows", "Less width down low, for a bass with stereo effects on it.", { { "mono", 100 }, { "w1", 40 } });
    add ("808 and bass", "Synth bass: all in one place", "Narrow everywhere, mono below 200 Hz.", { { "mono", 200 }, { "width", 50 } });

    add ("Drums", "Drum bus: punch in the middle", "Firm lows, a little air at the top.", { { "w1", 20 }, { "w2", 80 }, { "w3", 108 }, { "w4", 120 }, { "mono", 120 } });
    add ("Drums", "Overheads: wide cymbals, centred kick", "Opens the top, pulls the kick to the middle.", { { "w1", 20 }, { "w2", 90 }, { "w3", 125 }, { "w4", 150 }, { "mono", 120 } });
    add ("Drums", "Room: bigger than it was", "A wider room that is still there in mono.", { { "width", 150 }, { "w1", 40 }, { "mono", 150 }, { "safe", 1 }, { "limit", 4 } });
    add ("Drums", "Loop: kick in, hats out", "For a finished loop: centred low end, wide hats.", { { "mono", 130 }, { "w1", 0 }, { "w3", 110 }, { "w4", 165 }, { "safe", 1 }, { "limit", 3.5 } });

    add ("Synths and keys", "Mono synth: make it stereo", "For a synth played or recorded in mono.", { { "make", 55 }, { "mono", 150 }, { "safe", 1 }, { "limit", 4 } });
    add ("Synths and keys", "Wide pad: huge, still there in mono", "As wide as it goes before mono starts to suffer.", { { "width", 170 }, { "w1", 30 }, { "mono", 150 }, { "safe", 1 }, { "limit", 3 } });
    add ("Synths and keys", "Too wide: bring it in", "For a sound that pulls the ears apart.", { { "width", 65 }, { "w4", 90 } });
    add ("Synths and keys", "Piano: in front, not around", "Left hand and right hand closer together.", { { "width", 75 }, { "w1", 50 }, { "mono", 90 } });

    add ("Guitars", "Acoustic guitar: spread it", "One microphone, two sides. The body stays centred.", { { "make", 35 }, { "mono", 180 }, { "w4", 125 } });
    add ("Guitars", "Doubled guitars: wider wall", "Further out, lows out of the way of the bass.", { { "width", 130 }, { "mono", 160 }, { "safe", 1 }, { "limit", 5 } });

    add ("Returns", "Reverb return: wide and clean below", "A wider tail, nothing muddy in the low end.", { { "width", 140 }, { "mono", 250 }, { "w4", 115 } });
    add ("Returns", "Delay return: off the lead", "Echoes move to the sides, the words stay clear.", { { "width", 155 }, { "mono", 250 }, { "make", 20 } });

    add ("Mix bus", "Mix bus: open the top", "Wider highs, firm lows, the middle untouched.", { { "w1", 50 }, { "w3", 112 }, { "w4", 135 }, { "mono", 100 } });
    add ("Mix bus", "Mix bus: pull it together", "A touch narrower where it counts.", { { "w1", 50 }, { "w2", 92 }, { "w3", 95 }, { "mono", 80 } });

    add ("Master", "Master: mono bass, nothing else", "Everything below 100 Hz in the middle.", { { "mono", 100 } });
    add ("Master", "Master: club safe", "Mono bass, and no band loses more than 2.5 dB on a mono system.", { { "mono", 120 }, { "safe", 1 }, { "limit", 2.5 } });
    add ("Master", "Master: wider, still safe", "A little more width on top, watched by the safe limit.", { { "w3", 112 }, { "w4", 130 }, { "mono", 100 }, { "safe", 1 }, { "limit", 4 } });
    add ("Master", "Vinyl: ready to cut", "Mono lows and calm highs, the way cutting engineers like it.", { { "mono", 180 }, { "w4", 85 }, { "safe", 1 }, { "limit", 3 } });

    add ("Creative", "Almost only sides (effect)", "As wide as it can go. It will vanish in mono, on purpose.", { { "width", 200 }, { "make", 100 } });
    add ("Creative", "Mono (effect)", "Everything in the middle.", { { "width", 0 } });
    return v;
}

ImagerProcessor::ImagerProcessor() : EffectProcessor ("Imager", "AemonImager", createLayout(), factoryPresets()) {}

Params ImagerProcessor::readParams() const
{
    Params p;
    p.zeroDelay = real ("delay") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    if (monoTrack()) return p;
    p.width = real ("width") / 100.0; p.monoHz = real ("mono") <= 20.5f ? monoOff : (double) real ("mono"); p.make = real ("make") / 100.0; p.balance = real ("centre") / 100.0;
    for (int b = 0; b < bands; ++b) { const char* ids[] = { "w1", "w2", "w3", "w4" }; p.band[(size_t) b] = real (ids[b]) / 100.0; }
    p.split[0] = real ("f1"); p.split[1] = std::max ((double) real ("f2"), 1.5 * p.split[0]); p.split[2] = std::max ((double) real ("f3"), 1.5 * p.split[1]);
    p.safe = real ("safe") > 0.5f; p.limitDb = real ("limit"); p.hearMono = real ("hearMono") > 0.5f; p.hearSide = real ("hearSide") > 0.5f;
    return p;
}

void ImagerProcessor::processEngine (float* L, float* R, int n)
{
    ringInput.push (L, R, n);
    double c = cov.load(), v = var.load();
    for (int i = 0; i < n; ++i) { const double m = 0.5 * ((double) L[i] + R[i]), s = 0.5 * ((double) L[i] - R[i]); c += statCoef * (m * s - c); v += statCoef * (m * m - v); }
    cov = c; var = v;
    engine.process (L, R, n);
    ringOutput.push (L, R, n);
}

juce::AudioProcessorEditor* ImagerProcessor::createEditor() { return new ImagerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ImagerProcessor(); }
