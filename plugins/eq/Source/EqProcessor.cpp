// Aemon EQ. AGPL-3.0-or-later.
#include "EqProcessor.h"
#include "EqEditor.h"

using namespace aemon::eq;

namespace
{
    juce::String minus (juce::String s) { return s.replace ("-", juce::String::charToString (0x2212)); }
    juce::String fmtFreq (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
    float parseFreq (const juce::String& s)
    {
        auto t = s.trim().toLowerCase();
        const float v = t.retainCharacters ("0123456789.").getFloatValue();
        return t.contains ("k") ? v * 1000.0f : v;
    }
    // default positions for bands added later, spread across the spectrum
    float defaultFreq (int band)
    {
        if (band < eqp::numRoles) return eqp::roleDefaults[band].freq;
        return 60.0f * std::pow (2.0f, (float) (band - eqp::numRoles) * 0.75f);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout EqProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const int v = 1;
    const StringArray types { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass", "Tilt" };
    const StringArray slopes { "12 dB/oct", "24 dB/oct", "48 dB/oct", "96 dB/oct" };
    const StringArray places { "Stereo", "Left", "Right", "Mid", "Side" };
    for (int b = 0; b < eqp::numBands; ++b)
    {
        const String n = "Band " + String (b + 1) + " ";
        const bool role = b < eqp::numRoles;
        const int typeDefault = role ? (int) eqp::roleDefaults[b].type : 0;
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), "|");
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { eqp::id (b, "on"), v }, n + "On", false));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { eqp::id (b, "type"), v }, n + "Shape", types, typeDefault));
        NormalisableRange<float> fr (20.0f, 20000.0f, [] (float s, float e, float x) { return s * std::pow (e / s, x); },
                                     [] (float s, float e, float y) { return std::log (y / s) / std::log (e / s); },
                                     [] (float s, float e, float y) { return jlimit (s, e, std::round (y * 10.0f) / 10.0f); });
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { eqp::id (b, "freq"), v }, n + "Frequency", fr, defaultFreq (b),
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return fmtFreq (x); })
                                           .withValueFromStringFunction ([] (const String& s) { return parseFreq (s); })));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { eqp::id (b, "gain"), v }, n + "Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float x, int) { return minus ((x > 0.05f ? "+" : "") + String (x, 1)); })));
        NormalisableRange<float> qr (0.1f, 18.0f, [] (float s, float e, float x) { return s * std::pow (e / s, x); },
                                     [] (float s, float e, float y) { return std::log (y / s) / std::log (e / s); },
                                     [] (float s, float e, float y) { return jlimit (s, e, std::round (y * 100.0f) / 100.0f); });
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { eqp::id (b, "q"), v }, n + "Q", qr, role ? eqp::roleDefaults[b].q : 1.0f));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { eqp::id (b, "slope"), v }, n + "Slope", slopes, role ? eqp::roleDefaults[b].slopeIndex : 1));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { eqp::id (b, "place"), v }, n + "Placement", places, 0));
        group->addChild (std::make_unique<AudioParameterBool> (ParameterID { eqp::id (b, "dyn"), v }, n + "Dynamic", false));
        group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { eqp::id (b, "thr"), v }, n + "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
        l.add (std::move (group));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", v }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "autoGain", v }, "Auto Gain", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "phase", v }, "Phase", StringArray { "Zero latency", "Linear (medium)", "Linear (high)" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", v }, "Bypass", false));
    return l;
}

//==============================================================================
std::vector<aemon::Preset> EqProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    struct B { int band; Type type; float f, g, q; int slope = 1; int place = 0; bool dyn = false; float thr = -24.0f; };
    auto add = [&v] (const char* cat, const char* name, const char* desc, std::initializer_list<B> bands, float output = 0.0f)
    {
        aemon::Preset p;
        p.name = name; p.category = cat; p.description = desc;
        for (auto& b : bands)
        {
            auto i = [&] (const char* w) { return eqp::id (b.band - 1, w); };
            p.values.insert (p.values.end(), {
                { i ("on"), 1.0f }, { i ("type"), (float) (int) b.type }, { i ("freq"), b.f }, { i ("gain"), b.g }, { i ("q"), b.q },
                { i ("slope"), (float) b.slope }, { i ("place"), (float) b.place }, { i ("dyn"), b.dyn ? 1.0f : 0.0f }, { i ("thr"), b.thr } });
        }
        if (output != 0.0f) p.values.push_back ({ "output", output });
        v.push_back (std::move (p));
    };
    using T = Type;
    add ("", "Flat: changes nothing", "All bands off. Audio passes through untouched.", {});
    add ("", "Gentle polish: a little weight, clarity and air", "Small moves for any mix: clears mud, adds a touch of presence and sparkle.", { { 1, T::lowCut, 30, 0, .71f }, { 2, T::lowShelf, 110, 1.5f, .7f }, { 3, T::bell, 320, -2, 1.2f }, { 4, T::bell, 3200, 1.5f, .9f }, { 5, T::highShelf, 11000, 2, .7f } });

    add ("Vocals", "Vocal: clearer and more upfront", "Removes rumble and boxiness, adds presence and air so the words come forward.", { { 1, T::lowCut, 90, 0, .71f }, { 3, T::bell, 280, -3, 1.4f }, { 4, T::bell, 3500, 2.5f, 1 }, { 5, T::highShelf, 12000, 3, .7f } });
    add ("Vocals", "Vocal: warmer and closer", "Adds low-mid body and softens the edge for an intimate sound.", { { 1, T::lowCut, 70, 0, .71f }, { 2, T::lowShelf, 180, 2, .7f }, { 4, T::bell, 2800, -1.5f, 1.2f }, { 5, T::highShelf, 10000, 1.5f, .7f } });
    add ("Vocals", "Vocal: more air and sparkle", "Lifts the very top for an expensive, breathy shine.", { { 1, T::lowCut, 100, 0, .71f }, { 5, T::highShelf, 14000, 4, .7f }, { 6, T::bell, 5500, 1.5f, 1.5f } });
    add ("Vocals", "Vocal: less boxy (cut 450 Hz)", "Cuts the cardboard-box sound of small rooms and cheap mics.", { { 1, T::lowCut, 80, 0, .71f }, { 3, T::bell, 450, -4, 2 } });
    add ("Vocals", "Vocal: tame harsh notes only when they jump out", "Dynamic cuts at 3 and 6.5 kHz that act only when the voice gets piercing.", { { 1, T::lowCut, 80, 0, .71f }, { 6, T::bell, 3200, -4, 3, 1, 0, true, -30 }, { 7, T::bell, 6500, -4, 4, 1, 0, true, -32 } });
    add ("Vocals", "Vocal: soften S sounds (de-ess)", "A dynamic cut around 7 kHz that only dips when an S or T gets sharp.", { { 6, T::bell, 7000, -8, 2, 1, 0, true, -30 } });
    add ("Vocals", "Backing vocals: thin and bright, out of the way", "Removes low end and body so backing vocals sit behind the lead.", { { 1, T::lowCut, 220, 0, .71f, 2 }, { 3, T::bell, 400, -3, 1 }, { 5, T::highShelf, 9000, 3, .7f } });
    add ("Vocals", "Rap vocal: punchy and upfront", "Clears the low mids and pushes the consonants for clear, hard-hitting words.", { { 1, T::lowCut, 100, 0, .71f }, { 3, T::bell, 300, -2.5f, 1.3f }, { 4, T::bell, 2500, 3, 1.1f }, { 5, T::highShelf, 10000, 2, .7f } });

    add ("Drums", "Kick: more punch and click", "More thump at 60 Hz, less mud, more beater click on top.", { { 1, T::lowCut, 30, 0, .71f, 2 }, { 2, T::bell, 60, 3, 1.4f }, { 3, T::bell, 350, -5, 1.6f }, { 4, T::bell, 3500, 3, 1.5f } });
    add ("Drums", "Kick: deeper sub", "Adds low weight and clears the boxy middle.", { { 1, T::lowCut, 25, 0, .71f, 2 }, { 2, T::lowShelf, 60, 4, .7f }, { 3, T::bell, 250, -4, 1.4f } });
    add ("Drums", "Snare: more crack", "Adds body and snap, removes the honky middle.", { { 1, T::lowCut, 90, 0, .71f }, { 2, T::bell, 200, 2, 1.4f }, { 3, T::bell, 600, -3, 1.6f }, { 4, T::bell, 5000, 3, 1.2f } });
    add ("Drums", "Snare: less ring", "A narrow cut on the ringing note around 900 Hz. Move it to your snare's ring.", { { 6, T::bell, 900, -6, 6 } });
    add ("Drums", "Hi-hats: smoother, less harsh", "Removes low spill, softens the harsh middle, keeps the shine.", { { 1, T::lowCut, 300, 0, .71f, 2 }, { 4, T::bell, 4000, -2, 1.5f }, { 5, T::highShelf, 12000, 2, .7f } });
    add ("Drums", "Overheads: more open and airy", "Less boxiness, more air on the cymbals.", { { 1, T::lowCut, 120, 0, .71f }, { 3, T::bell, 400, -2.5f, 1.2f }, { 5, T::highShelf, 10000, 2.5f, .7f } });
    add ("Drums", "Drum bus: more weight and snap", "A little low end and attack on the whole kit.", { { 2, T::lowShelf, 80, 2, .7f }, { 3, T::bell, 400, -2, 1 }, { 4, T::bell, 4500, 1.5f, 1 } });
    add ("Drums", "Room mics: less boom", "Cuts the low build-up of room recordings.", { { 1, T::lowCut, 80, 0, .71f }, { 3, T::bell, 250, -4, 1 } });

    add ("Bass & 808", "808: clean, focused sub", "Removes sub rumble below the note, adds weight, clears the middle.", { { 1, T::lowCut, 25, 0, .71f, 2 }, { 2, T::bell, 55, 2, 1.2f }, { 3, T::bell, 400, -4, 1 } });
    add ("Bass & 808", "808: audible on phone speakers", "Adds the upper harmonics small speakers can play, so the 808 is heard everywhere.", { { 1, T::lowCut, 28, 0, .71f, 2 }, { 4, T::bell, 900, 3, 1.2f }, { 6, T::bell, 2000, 1.5f, 1 } });
    add ("Bass & 808", "Bass guitar: more definition", "Less mud, more growl and finger noise so the notes are clear.", { { 1, T::lowCut, 35, 0, .71f }, { 3, T::bell, 250, -3, 1.2f }, { 4, T::bell, 800, 2, 1.2f }, { 6, T::bell, 2500, 2, 1.5f } });
    add ("Bass & 808", "Synth bass: tidier low end", "Cleans rumble and low-mid build-up, softens fizz.", { { 1, T::lowCut, 30, 0, .71f, 2 }, { 3, T::bell, 180, -2, 1.4f }, { 5, T::highCut, 9000, 0, .71f } });
    add ("Bass & 808", "Bass: make room when the kick hits", "A dynamic dip at 60 Hz that only acts on loud kick-and-bass moments.", { { 6, T::bell, 60, -4, 2, 1, 0, true, -18 } });

    add ("Guitars", "Electric guitar: sit in the mix", "Removes low clutter and fizz, adds a little bite.", { { 1, T::lowCut, 100, 0, .71f }, { 3, T::bell, 350, -2.5f, 1.2f }, { 4, T::bell, 2500, 1.5f, 1 }, { 5, T::highCut, 10000, 0, .71f } });
    add ("Guitars", "Acoustic guitar: more shimmer, less boom", "Cuts the boomy body resonance, adds sparkle.", { { 1, T::lowCut, 90, 0, .71f }, { 3, T::bell, 220, -3, 1.2f }, { 5, T::highShelf, 10000, 3, .7f } });
    add ("Guitars", "Distorted guitar: less fizz", "Rounds off harsh high fizz while keeping the crunch.", { { 1, T::lowCut, 90, 0, .71f }, { 5, T::highCut, 7000, 0, .71f, 2 }, { 6, T::bell, 3500, -2, 2 } });

    add ("Keys & pads", "Piano: clearer", "Less low-mid mud, a little extra top.", { { 1, T::lowCut, 50, 0, .71f }, { 3, T::bell, 300, -2, 1 }, { 5, T::highShelf, 8000, 1.5f, .7f } });
    add ("Keys & pads", "Pads: out of the way of vocals", "Removes the low end and dips the vocal range so pads sit behind.", { { 1, T::lowCut, 180, 0, .71f, 2 }, { 4, T::bell, 2500, -2, .8f } });
    add ("Keys & pads", "Pads: wider and airier (sides only)", "Brightens only the stereo sides, so the pad opens up without crowding the centre.", { { 1, T::lowCut, 150, 0, .71f }, { 5, T::highShelf, 8000, 3, .7f, 1, 4 } });
    add ("Keys & pads", "Strings: less harsh", "Softens the scratchy upper mids of bright strings.", { { 4, T::bell, 3000, -2.5f, 1 }, { 5, T::highShelf, 12000, 1, .7f } });

    add ("Mix bus", "Mix bus: gentle smile curve", "A touch more low end and top for a fuller, brighter mix.", { { 2, T::lowShelf, 90, 1, .7f }, { 5, T::highShelf, 12000, 1.5f, .7f } });
    add ("Mix bus", "Mix bus: clear up the middle", "A broad, gentle dip in the low mids for more clarity.", { { 3, T::bell, 300, -1.5f, .9f }, { 4, T::bell, 3000, 0.8f, .8f } });
    add ("Mix bus", "Mix bus: mono low end (cut sides below 140 Hz)", "Removes low frequencies from the stereo sides only: tight, centred bass.", { { 1, T::lowCut, 140, 0, .71f, 2, 4 } });
    add ("Mix bus", "Tilt the whole mix brighter", "Lifts the top and lowers the bottom around 1 kHz, like a tone control.", { { 6, T::tilt, 1000, 2, 1 } });
    add ("Mix bus", "Tilt the whole mix darker", "Lowers the top and lifts the bottom around 1 kHz, like a tone control.", { { 6, T::tilt, 1000, -2, 1 } });

    add ("Mastering", "Master: a little air and weight", "Subtle low and high shelves for a more finished sound.", { { 2, T::lowShelf, 80, 1, .7f }, { 5, T::highShelf, 14000, 1.5f, .7f } });
    add ("Mastering", "Master: remove inaudible sub rumble", "A steep cut at 25 Hz that frees headroom without changing what you hear.", { { 1, T::lowCut, 25, 0, .71f, 3 } });
    add ("Mastering", "Master: tame low-mid muddiness", "A gentle, wide 1 dB dip around 250 Hz.", { { 3, T::bell, 250, -1, .8f } });
    add ("Mastering", "Master: wider top, tighter bass (sides only)", "Brightens the stereo sides and removes their low end.", { { 5, T::highShelf, 6000, 1.5f, .7f, 1, 4 }, { 1, T::lowCut, 120, 0, .71f, 1, 4 } });
    add ("Mastering", "Master: calm harsh moments only", "A dynamic dip at 3 kHz that only acts when the mix gets harsh.", { { 6, T::bell, 3000, -2, 1.5f, 1, 0, true, -26 } });

    add ("Fixes", "Remove low rumble", "A low cut at 40 Hz for handling noise, traffic and air conditioning.", { { 1, T::lowCut, 40, 0, .71f, 2 } });
    add ("Fixes", "Remove mains hum (50 Hz, Europe)", "Narrow notches at 50, 100 and 150 Hz.", { { 6, T::notch, 50, 0, 8 }, { 7, T::notch, 100, 0, 8 }, { 8, T::notch, 150, 0, 8 } });
    add ("Fixes", "Remove mains hum (60 Hz, Americas)", "Narrow notches at 60, 120 and 180 Hz.", { { 6, T::notch, 60, 0, 8 }, { 7, T::notch, 120, 0, 8 }, { 8, T::notch, 180, 0, 8 } });
    add ("Fixes", "Clean up muddiness", "Cuts 3 dB around 300 Hz, where mixes get cloudy.", { { 3, T::bell, 300, -3, 1.4f } });
    add ("Fixes", "Soften sharp S sounds", "A dynamic cut around 7.5 kHz that only dips on sharp sibilants.", { { 6, T::bell, 7500, -6, 2.5f, 1, 0, true, -28 } });
    add ("Fixes", "Too bright? Soften the top", "Lowers everything above 8 kHz by 3 dB.", { { 5, T::highShelf, 8000, -3, .7f } });
    add ("Fixes", "Too dull? Open up the top", "Lifts everything above 9 kHz by 3 dB.", { { 5, T::highShelf, 9000, 3, .7f } });

    add ("Creative", "Telephone voice", "Only the middle band, like a phone call.", { { 1, T::lowCut, 400, 0, .71f, 2 }, { 5, T::highCut, 3000, 0, .71f, 2 }, { 6, T::bell, 1500, 4, 1 } });
    add ("Creative", "Old AM radio", "Narrow, boxy and mid-heavy, like an old radio.", { { 1, T::lowCut, 250, 0, .71f, 3 }, { 5, T::highCut, 4500, 0, .71f, 3 }, { 6, T::bell, 1200, 3, .8f } });
    add ("Creative", "Lo-fi, dusty sound", "Rolls off lows and highs for a soft, worn character.", { { 1, T::lowCut, 120, 0, .71f }, { 5, T::highCut, 6000, 0, .71f, 2 }, { 6, T::bell, 800, 2, .7f } });
    add ("Creative", "Underwater (heavy muffle)", "Muffles everything above 600 Hz.", { { 5, T::highCut, 600, 0, 1.2f, 2 } });
    add ("Creative", "Music through the wall (next room)", "Only the bass gets through, like a party next door.", { { 5, T::highCut, 350, 0, .71f, 3 }, { 2, T::lowShelf, 100, 3, .7f } });
    return v;
}

//==============================================================================
EqProcessor::EqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Aemon EQ curve builder"),
      apvts (*this, nullptr, "AemonEQ", createLayout()),
      pluginState (*this, apvts, "EQ", factoryPresets())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) apvts.addParameterListener (r->getParameterID(), this);
    pluginState.ui.setProperty ("mode", "pro", nullptr);
    startThread (juce::Thread::Priority::low);
}

EqProcessor::~EqProcessor()
{
    stopThread (2000);
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) apvts.removeParameterListener (r->getParameterID(), this);
}

bool EqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

BandParams EqProcessor::bandParams (int b) const
{
    auto f = [this, b] (const char* w) { return apvts.getRawParameterValue (eqp::id (b, w))->load(); };
    BandParams p;
    p.on = f ("on") > 0.5f;
    p.type = (Type) juce::roundToInt (f ("type"));
    p.freq = f ("freq");
    p.gainDb = f ("gain");
    p.q = f ("q");
    p.slope = eqp::slopes[juce::jlimit (0, 3, juce::roundToInt (f ("slope")))];
    p.place = (Place) juce::roundToInt (f ("place"));
    p.dynamic = f ("dyn") > 0.5f;
    p.thresholdDb = f ("thr");
    return p;
}

std::array<BandParams, eqp::numBands> EqProcessor::allBands() const
{
    std::array<BandParams, eqp::numBands> a;
    for (int b = 0; b < eqp::numBands; ++b) a[(size_t) b] = bandParams (b);
    return a;
}

float EqProcessor::autoGainFor (const std::array<BandParams, eqp::numBands>& bands)
{
    double s = 0, w = 0;
    for (double f = 40; f < 16000; f *= 1.12)
    {
        double c = 0;
        for (auto& b : bands) if (b.on && b.place != Place::side) c += analogDb (b, f) * (b.place == Place::stereo || b.place == Place::mid ? 1.0 : 0.5);
        const double wt = 1.0 / (1.0 + (f / 2500.0) * (f / 2500.0)) + 0.3;   // more weight where music has most energy
        s += c * wt; w += wt;
    }
    return (float) juce::jlimit (-24.0, 24.0, -s / w);
}

void EqProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = false;
    const juce::ScopedLock sl (buildLock);
    currentRate = sampleRate;
    for (int b = 0; b < eqp::numBands; ++b) zeroLatency.setBand (b, bandParams (b));
    zeroLatency.setOutputGainDb (apvts.getRawParameterValue ("output")->load());
    zeroLatency.prepare (sampleRate);
    linearMedium.prepare (sampleRate, Resolution::medium);
    linearHigh.prepare (sampleRate, Resolution::high);
    const auto bands = allBands();
    const float out = apvts.getRawParameterValue ("output")->load() + (apvts.getRawParameterValue ("autoGain")->load() > 0.5f ? autoGainFor (bands) : 0.0f);
    linearMedium.setKernels (buildKernels (bands, out, sampleRate, Resolution::medium));
    linearHigh.setKernels (buildKernels (bands, out, sampleRate, Resolution::high));
    builtVersion = paramVersion.load();

    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    dryDelay.setSize (2, linearHigh.latency() + juce::jmax (1, samplesPerBlock) + 1, false, true, true);
    dryDelay.clear();
    dryWrite = 0;
    activePhase = phase();
    activeLatency = activePhase == eqp::Phase::zeroLatency ? 0 : (activePhase == eqp::Phase::linearMedium ? linearMedium.latency() : linearHigh.latency());
    setLatencySamples (activeLatency);
    wetMix.reset (sampleRate, 0.02);
    wetMix.setCurrentAndTargetValue (bypassParam->get() ? 0.0f : 1.0f);
    prepared = true;
}

void EqProcessor::rebuild()
{
    const juce::ScopedLock sl (buildLock);
    if (! prepared) return;
    const int ver = paramVersion.load();
    if (ver == builtVersion) return;
    const auto bands = allBands();
    const float ag = autoGainFor (bands);
    autoGainDb = ag;
    const float out = apvts.getRawParameterValue ("output")->load() + (apvts.getRawParameterValue ("autoGain")->load() > 0.5f ? ag : 0.0f);
    const auto ph = phase();
    if (ph == eqp::Phase::linearMedium) linearMedium.setKernels (buildKernels (bands, out, currentRate, Resolution::medium));
    if (ph == eqp::Phase::linearHigh) linearHigh.setKernels (buildKernels (bands, out, currentRate, Resolution::high));
    builtVersion = ver;
}

void EqProcessor::run()
{
    while (! threadShouldExit())
    {
        wait (40);
        linearMedium.collectGarbage();
        linearHigh.collectGarbage();
        rebuild();
    }
}

void EqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), numCh = getTotalNumOutputChannels();
    if (n == 0 || numCh == 0) return;
    if (work.getNumSamples() < n) work.setSize (2, n, false, false, true);
    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (R, numCh >= 2 ? buffer.getReadPointer (1) : L, n);
    preRing.push (L, R, n);

    // phase mode change: new latency reported to the host
    const auto ph = phase();
    if (ph != activePhase)
    {
        activePhase = ph;
        activeLatency = ph == eqp::Phase::zeroLatency ? 0 : (ph == eqp::Phase::linearMedium ? linearMedium.latency() : linearHigh.latency());
        setLatencySamples (activeLatency);
        ++paramVersion;
        notify();
    }

    // keep the dry signal, delayed to line up with the processed one (for click-free bypass)
    const int dsz = dryDelay.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        dryDelay.setSample (0, (dryWrite + i) % dsz, L[i]);
        dryDelay.setSample (1, (dryWrite + i) % dsz, R[i]);
    }

    const bool autoGain = apvts.getRawParameterValue ("autoGain")->load() > 0.5f;
    if (ph == eqp::Phase::zeroLatency)
    {
        for (int b = 0; b < eqp::numBands; ++b) zeroLatency.setBand (b, bandParams (b));
        zeroLatency.setOutputGainDb (apvts.getRawParameterValue ("output")->load() + (autoGain ? autoGainDb.load() : 0.0f));
        zeroLatency.process (L, R, n);
    }
    else (ph == eqp::Phase::linearMedium ? linearMedium : linearHigh).process (L, R, n);

    wetMix.setTargetValue (bypassParam->get() ? 0.0f : 1.0f);
    if (wetMix.isSmoothing() || wetMix.getCurrentValue() < 1.0f)
        for (int i = 0; i < n; ++i)
        {
            const float w = wetMix.getNextValue();
            const int rd = ((dryWrite + i - activeLatency) % dsz + dsz) % dsz;
            L[i] = w * L[i] + (1.0f - w) * dryDelay.getSample (0, rd);
            R[i] = w * R[i] + (1.0f - w) * dryDelay.getSample (1, rd);
        }
    dryWrite = (dryWrite + n) % dsz;

    postRing.push (L, R, n);
    outTap.push (L, R, n);
    buffer.copyFrom (0, 0, L, n);
    if (numCh >= 2) buffer.copyFrom (1, 0, R, n);
}

void EqProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    pluginState.writeTo (st);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void EqProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (t);
            pluginState.readFrom (t);
            ++paramVersion;
            notify();
        }
}

juce::AudioProcessorEditor* EqProcessor::createEditor() { return new EqEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EqProcessor(); }
