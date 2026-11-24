// Aemon Utility. AGPL-3.0-or-later.
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace aemon::utility;

namespace
{
    juce::String minus (juce::String s) { return s.replace ("-", juce::String::charToString (0x2212)); }
}

juce::AudioProcessorValueTreeState::ParameterLayout UtilityProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const int v = 1;

    auto dbText = [] (float x, int) { return minus ((x > 0.05f ? "+" : "") + String (x, 1)); };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", v }, "Gain", NormalisableRange<float> (-36.0f, 36.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "pan", v }, "Pan", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int)
        { const int r = roundToInt (x); return r == 0 ? String ("C") : (r < 0 ? String (-r) + "L" : String (r) + "R"); })
        .withValueFromStringFunction ([] (const String& s)
        {
            auto t = s.trim().toUpperCase();
            if (t.startsWith ("C")) return 0.0f;
            float x = t.retainCharacters ("0123456789.").getFloatValue();
            return t.contains ("L") || t.startsWith ("-") ? -x : x;
        })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "panLaw", v }, "Pan Law", StringArray { "0 dB", "-3 dB", "-4.5 dB", "-6 dB" }, 1));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "width", v }, "Width", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "mono", v }, "Mono", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "swap", v }, "Swap L/R", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "polL", v }, "Polarity Left", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "polR", v }, "Polarity Right", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "monitor", v }, "Listen", StringArray { "Stereo", "Mid", "Side" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "matrix", v }, "M/S Matrix", StringArray { "Off", "Encode", "Decode" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bassMono", v }, "Bass Mono", false));
    NormalisableRange<float> fr (40.0f, 400.0f, 1.0f);
    fr.setSkewForCentre (126.5f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bassFreq", v }, "Bass Mono Below", fr, 120.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", v }, "Bypass", false));
    return l;
}

std::vector<aemon::Preset> UtilityProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    auto add = [&v] (const char* cat, const char* name, const char* desc, std::vector<std::pair<juce::String, float>> vals)
    {
        aemon::Preset p;
        p.name = name; p.description = desc; p.values = std::move (vals); p.category = cat;
        v.push_back (std::move (p));
    };
    add ("", "Clean start: changes nothing", "Every control at neutral. Audio passes through bit-for-bit untouched.", {});

    add ("Check your mix", "Hear it in mono (phone / club check)", "Sums left and right, the way a phone speaker or a mono club system plays your mix.", { { "mono", 1 } });
    add ("Check your mix", "Hear only the centre (Mid)", "Plays what both speakers share: usually vocal, kick, snare and bass.", { { "monitor", 1 } });
    add ("Check your mix", "Hear only the stereo sides (Side)", "Plays only the difference between left and right: reverbs, wide synths, room.", { { "monitor", 2 } });
    add ("Check your mix", "Hear the left channel only", "Mutes the right side so you can hear what is panned left.", { { "pan", -100 }, { "panLaw", 0 } });
    add ("Check your mix", "Hear the right channel only", "Mutes the left side so you can hear what is panned right.", { { "pan", 100 }, { "panLaw", 0 } });
    add ("Check your mix", "Swap left and right", "Left comes out of the right speaker and the other way round.", { { "swap", 1 } });

    add ("Fix a problem", "Fix out-of-phase stereo (flip right)", "Flips the right channel. Use when the two sides cancel each other and the sound thins out in mono.", { { "polR", 1 } });
    add ("Fix a problem", "Fix out-of-phase stereo (flip left)", "Flips the left channel. Same fix as above, from the other side.", { { "polL", 1 } });
    add ("Fix a problem", "Flip the whole track upside down", "Inverts both channels, for lining up a track against another copy of the same sound.", { { "polL", 1 }, { "polR", 1 } });
    add ("Fix a problem", "Make a fake stereo track mono", "For stereo files that are really mono: removes the slight differences between the sides.", { { "mono", 1 } });

    add ("Level", "Turn down 3 dB", "A little quieter, for balancing.", { { "gain", -3 } });
    add ("Level", "Turn down 6 dB (more headroom)", "Gives the next plugins room to work without clipping.", { { "gain", -6 } });
    add ("Level", "Turn down 12 dB (gain staging)", "Brings a hot track down to a comfortable working level.", { { "gain", -12 } });
    add ("Level", "Turn down 18 dB (very loud sample)", "For samples and loops that come in far too loud.", { { "gain", -18 } });
    add ("Level", "Turn up 3 dB", "A little louder, for balancing.", { { "gain", 3 } });
    add ("Level", "Turn up 6 dB", "Noticeably louder: for quiet recordings.", { { "gain", 6 } });

    add ("Width", "Wider pads (+40%)", "Spreads pads and chords further left and right.", { { "width", 140 } });
    add ("Width", "Very wide chorus (+60%)", "Big, wide sound for backing vocals, chorus synths and pads.", { { "width", 160 } });
    add ("Width", "Maximum width (+100%)", "As wide as it goes. Check in mono afterwards.", { { "width", 200 } });
    add ("Width", "Slightly narrower (85%)", "Pulls a sound a little towards the centre.", { { "width", 85 } });
    add ("Width", "Tighter, more focused (70%)", "Narrows a busy stereo sound so it sits better in the mix.", { { "width", 70 } });
    add ("Width", "Almost mono (40%)", "Keeps only a hint of stereo: good for bass-heavy or lead sounds.", { { "width", 40 } });

    add ("Low end", "Mono bass below 60 Hz (gentle)", "Makes only the deepest sub mono. Safe on almost anything.", { { "bassMono", 1 }, { "bassFreq", 60 } });
    add ("Low end", "Mono bass below 80 Hz", "Tightens the low end while keeping bass instruments wide above it.", { { "bassMono", 1 }, { "bassFreq", 80 } });
    add ("Low end", "Mono bass below 120 Hz (club-ready)", "Solid, centred low end that translates on club systems.", { { "bassMono", 1 }, { "bassFreq", 120 } });
    add ("Low end", "Mono bass below 150 Hz (vinyl-safe)", "Keeps the low end mono enough for vinyl cutting.", { { "bassMono", 1 }, { "bassFreq", 150 } });
    add ("Low end", "Wide top, tight mono bass", "Widens the top end while keeping everything below 120 Hz mono.", { { "bassMono", 1 }, { "bassFreq", 120 }, { "width", 130 } });

    add ("Mid / Side tools", "Split into Mid and Side (encode)", "Outputs the centre on the left channel and the sides on the right, to process them separately.", { { "matrix", 1 } });
    add ("Mid / Side tools", "Join Mid and Side back (decode)", "Turns a Mid/Side signal back into normal left and right.", { { "matrix", 2 } });

    add ("Placement", "Move it 30% left", "Places the sound a little to the left.", { { "pan", -30 } });
    add ("Placement", "Move it 30% right", "Places the sound a little to the right.", { { "pan", 30 } });
    add ("Placement", "Hard left", "All the way to the left speaker.", { { "pan", -100 } });
    add ("Placement", "Hard right", "All the way to the right speaker.", { { "pan", 100 } });
    return v;
}

UtilityProcessor::UtilityProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AemonUtility", createLayout()),
      pluginState (*this, apvts, "Utility", factoryPresets())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool UtilityProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return ! (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::mono());
}

Params UtilityProcessor::readParams() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    Params p;
    p.gainDb = f ("gain");
    p.pan = f ("pan");
    p.panLawIndex = (int) f ("panLaw");
    p.width = f ("width");
    p.mono = f ("mono") > 0.5f;
    p.swap = f ("swap") > 0.5f;
    p.polL = f ("polL") > 0.5f;
    p.polR = f ("polR") > 0.5f;
    p.monitor = (Monitor) (int) f ("monitor");
    p.matrix = (Matrix) (int) f ("matrix");
    p.bassMono = f ("bassMono") > 0.5f;
    p.bassFreq = f ("bassFreq");
    p.bypass = f ("bypass") > 0.5f;
    return p;
}

void UtilityProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    loudIn.prepare (sampleRate, 2);
    loudOut.prepare (sampleRate, 2);
    setLatencySamples (0);
}

void UtilityProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut == 0) return;
    if (work.getNumSamples() < n) work.setSize (2, n, false, false, true); // host sent a larger block than promised

    // Work on a stereo pair whatever the layout: mono in is copied to both sides.
    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    if (numIn >= 1) juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n); else juce::FloatVectorOperations::clear (L, n);
    juce::FloatVectorOperations::copy (R, numIn >= 2 ? buffer.getReadPointer (1) : L, n);

    inTap.push (L, R, n);
    const float* inPtrs[] = { L, R };
    loudIn.process (inPtrs, 2, n);

    engine.setParams (readParams());
    engine.process (L, R, n);

    outTap.push (L, R, n);
    const float* outPtrs[] = { L, R };
    loudOut.process (outPtrs, 2, n);
    scope.push (L, R, n, juce::jmax (1, juce::roundToInt (currentRate.load() / 48000.0)));

    if (numOut >= 2)
    {
        buffer.copyFrom (0, 0, L, n);
        buffer.copyFrom (1, 0, R, n);
    }
    else
    {
        // mono out: average of both sides (exact when they are equal, as with mono in at default settings)
        auto* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.5f * (L[i] + R[i]);
    }
    for (int c = 2; c < numOut; ++c) buffer.clear (c, 0, n);
}

void UtilityProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    pluginState.writeTo (st);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void UtilityProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (t);
            pluginState.readFrom (t);
        }
}

juce::AudioProcessorEditor* UtilityProcessor::createEditor() { return new UtilityEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UtilityProcessor(); }
