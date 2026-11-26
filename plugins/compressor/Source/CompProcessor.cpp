// Aemon Compressor. AGPL-3.0-or-later.
#include "CompProcessor.h"
#include "CompEditor.h"

namespace
{
    juce::String minus (juce::String s) { return s.replace ("-", juce::String::charToString (0x2212)); }
    juce::String fmtMs (float v) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }
    juce::NormalisableRange<float> logRange (float lo, float hi, float step)
    {
        return { lo, hi, [] (float s, float e, float x) { return s * std::pow (e / s, x); },
                 [] (float s, float e, float y) { return std::log (y / s) / std::log (e / s); },
                 [step] (float s, float e, float y) { return juce::jlimit (s, e, std::round (y / step) * step); } };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout CompProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const int v = 1;
    auto dB = [] (float x, int) { return minus (String (x, 1)) + " dB"; };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "thr", v }, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ratio", v }, "Ratio", logRange (1.0f, 20.0f, 0.01f), 2.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return x >= 19.95f ? String ("inf : 1") : String (x, x < 10 ? 1 : 0) + " : 1"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "knee", v }, "Knee", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 8.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "atk", v }, "Attack", logRange (0.1f, 100.0f, 0.1f), 10.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return fmtMs (x); })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "rel", v }, "Release", logRange (10.0f, 1000.0f, 1.0f), 300.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return fmtMs (x); })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "autoRel", v }, "Auto Release", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "look", v }, "Lookahead", NormalisableRange<float> (0.0f, 10.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return fmtMs (x); })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "det", v }, "Detector", StringArray { "Peak", "RMS" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "hpf", v }, "Sidechain High-pass", logRange (20.0f, 500.0f, 1.0f), 20.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return x <= 20.5f ? String ("Off") : String (roundToInt (x)) + " Hz"; })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "extSc", v }, "External Sidechain", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", v }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", v }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "autoLevel", v }, "Auto Level", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", v }, "Bypass", false));
    return l;
}

std::vector<aemon::Preset> CompProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    using P = std::pair<juce::String, float>;
    auto add = [&v] (const char* cat, const char* name, const char* desc, std::vector<P> vals)
    {
        aemon::Preset p; p.name = name; p.category = cat; p.description = desc; p.values = std::move (vals); v.push_back (std::move (p));
    };
    auto style = [] (int i, float thr, std::vector<P> extra = {})
    {
        const auto& s = compp::styles[i];
        std::vector<P> r { { "thr", thr }, { "ratio", s.ratio }, { "atk", s.attack }, { "rel", s.release }, { "knee", s.knee },
                           { "autoRel", s.autoRelease ? 1.0f : 0.0f }, { "mix", s.mix } };
        r.insert (r.end(), extra.begin(), extra.end());
        return r;
    };
    add ("", "Gentle glue: hold it together", "2:1 with a soft knee and auto release. Evens things out without being heard.", style (2, -18));
    // styles
    add ("Style: vocals", "Boom bap lead: steady, still breathing", "Medium attack, so the punch of every word stays.", { { "thr", -24 }, { "ratio", 3.5f }, { "atk", 10 }, { "rel", 90 }, { "knee", 4 }, { "autoRel", 0 }, { "hpf", 90 }, { "mix", 100 } });
    add ("Style: vocals", "90s East Coast lead: firm and raw", "Hard and quick, the hold is meant to be heard.", { { "thr", -26 }, { "ratio", 6 }, { "atk", 4 }, { "rel", 120 }, { "knee", 2 }, { "autoRel", 0 }, { "hpf", 90 }, { "mix", 100 } });
    add ("Style: vocals", "West Coast lead: smooth and even", "Slow hands and a wide knee. Level without grip.", { { "thr", -24 }, { "ratio", 3 }, { "atk", 18 }, { "rel", 180 }, { "knee", 8 }, { "autoRel", 1 }, { "hpf", 90 }, { "mix", 100 } });
    add ("Style: vocals", "Trap lead: pinned in front", "Fast and firm: every syllable at the same height.", { { "thr", -29 }, { "ratio", 6 }, { "atk", 1.5f }, { "rel", 60 }, { "knee", 4 }, { "autoRel", 0 }, { "hpf", 110 }, { "mix", 100 } });
    add ("Style: vocals", "Trap ad-libs: flat and loud", "As flat as a shout gets. For ad-libs that answer the lead.", { { "thr", -33 }, { "ratio", 10 }, { "atk", 0.5f }, { "rel", 50 }, { "knee", 2 }, { "autoRel", 0 }, { "hpf", 150 }, { "mix", 100 } });
    add ("Style: vocals", "Drill lead: tight, words bite", "Lets the first 10 ms of a word through, then closes fast.", { { "thr", -27 }, { "ratio", 5 }, { "atk", 10 }, { "rel", 60 }, { "knee", 3 }, { "autoRel", 0 }, { "hpf", 100 }, { "mix", 100 } });
    add ("Style: vocals", "Melodic rap lead: floating, never jumps", "Soft knee and a long way back, for long notes.", { { "thr", -27 }, { "ratio", 4 }, { "atk", 3 }, { "rel", 140 }, { "knee", 10 }, { "autoRel", 1 }, { "hpf", 100 }, { "mix", 100 } });
    add ("Style: vocals", "Pop lead: polished and close", "Quick, medium firm, a little of the dry voice left in.", { { "thr", -27 }, { "ratio", 4 }, { "atk", 5 }, { "rel", 110 }, { "knee", 8 }, { "autoRel", 1 }, { "hpf", 100 }, { "mix", 90 } });
    add ("Style: vocals", "R&B lead: soft hands", "Two and a half to one, slow, barely there.", { { "thr", -23 }, { "ratio", 2.5f }, { "atk", 15 }, { "rel", 220 }, { "knee", 12 }, { "autoRel", 1 }, { "hpf", 80 }, { "mix", 100 } });
    add ("Style: vocals", "Rage lead: crushed and loud", "Twelve to one and fast, a little of the dry voice left in.", { { "thr", -40 }, { "ratio", 12 }, { "atk", 0.3f }, { "rel", 40 }, { "knee", 0 }, { "autoRel", 0 }, { "hpf", 120 }, { "mix", 96 } });
    add ("Style: vocals", "Gospel hook stack: one voice out of many", "Pulls a stack of takes together.", { { "thr", -28 }, { "ratio", 4 }, { "atk", 8 }, { "rel", 220 }, { "knee", 8 }, { "autoRel", 1 }, { "hpf", 120 }, { "mix", 100 } });
    add ("Style: vocals", "Whisper layer: every breath up", "Lifts the quietest sounds next to the loud ones.", { { "thr", -40 }, { "ratio", 5 }, { "atk", 2 }, { "rel", 120 }, { "knee", 8 }, { "autoRel", 0 }, { "hpf", 200 }, { "mix", 100 } });
    add ("Style: vocals", "Spoken intro: late-night radio", "Firm and slow to let go, for a voice that fills the room.", { { "thr", -29 }, { "ratio", 5 }, { "atk", 5 }, { "rel", 160 }, { "knee", 6 }, { "autoRel", 0 }, { "hpf", 60 }, { "mix", 100 } });
    add ("Style: vocals", "Already tuned and compressed: barely touch it", "For a take that arrives squeezed. Catches only what jumps.", { { "thr", -18 }, { "ratio", 1.25f }, { "atk", 15 }, { "rel", 200 }, { "knee", 10 }, { "autoRel", 1 }, { "hpf", 90 }, { "mix", 100 } });
    add ("Style: beats", "Boom bap drums: chopped and thick", "The hit goes through, the rest closes behind it.", { { "thr", -23 }, { "ratio", 4 }, { "atk", 15 }, { "rel", 80 }, { "knee", 3 }, { "autoRel", 0 }, { "hpf", 60 }, { "mix", 100 } });
    add ("Style: beats", "West Coast beat: slow glue", "Under two to one and slow. Bass and drums lean on each other.", { { "thr", -20 }, { "ratio", 1.7f }, { "atk", 35 }, { "rel", 250 }, { "knee", 8 }, { "autoRel", 1 }, { "hpf", 60 }, { "mix", 100 } });
    add ("Style: beats", "Trap drums: keep the snap", "Slow attack and a high filter, so the 808 does not pull it down.", { { "thr", -20 }, { "ratio", 3 }, { "atk", 35 }, { "rel", 60 }, { "knee", 4 }, { "autoRel", 0 }, { "hpf", 120 }, { "mix", 100 } });
    add ("Style: beats", "House drums: breathes with the kick", "Dips on the kick and comes back before the next.", { { "thr", -26 }, { "ratio", 4 }, { "atk", 20 }, { "rel", 120 }, { "knee", 3 }, { "autoRel", 0 }, { "hpf", 20 }, { "mix", 100 } });
    add ("Style: beats", "Lo-fi beat: tired pump", "Fast in, slow out, blended. Sounds like an old recorder.", { { "thr", -31 }, { "ratio", 6 }, { "atk", 2 }, { "rel", 260 }, { "knee", 4 }, { "autoRel", 0 }, { "hpf", 20 }, { "mix", 85 } });
    add ("Style: beats", "70s soul loop: old glue", "Gentle and slow to let go, for a sampled record.", { { "thr", -23 }, { "ratio", 2 }, { "atk", 5 }, { "rel", 300 }, { "knee", 8 }, { "autoRel", 1 }, { "hpf", 60 }, { "mix", 75 } });
    add ("Style: beats", "80s drums: big squeeze", "Heavy and slow, with the dry drums under it.", { { "thr", -30 }, { "ratio", 8 }, { "atk", 8 }, { "rel", 180 }, { "knee", 2 }, { "autoRel", 0 }, { "hpf", 60 }, { "mix", 60 } });
    add ("Drums", "Drum bus: more punch", "Lets each hit through, then tightens the body. Low end does not drive it.", style (1, -20, { { "hpf", 90 } }));
    add ("Drums", "Drum bus: big and smashed (parallel)", "Heavy, fast compression blended with the dry drums for size and energy.", style (4, -30));
    add ("Drums", "Kick: tighter, shorter boom", "Lets the attack through and controls the tail.", { { "thr", -16 }, { "ratio", 4 }, { "atk", 30 }, { "rel", 60 }, { "knee", 3 }, { "autoRel", 0 } });
    add ("Drums", "Kick: more click and attack", "Slow-ish attack accentuates the beater.", { { "thr", -20 }, { "ratio", 6 }, { "atk", 15 }, { "rel", 50 }, { "knee", 2 }, { "autoRel", 0 } });
    add ("Drums", "Snare: more crack", "Brings out the initial snap of the snare.", { { "thr", -20 }, { "ratio", 5 }, { "atk", 20 }, { "rel", 70 }, { "knee", 2 }, { "autoRel", 0 } });
    add ("Drums", "Snare: fatter body", "Fast attack holds down the hit and brings up the ring and body.", { { "thr", -24 }, { "ratio", 4 }, { "atk", 3 }, { "rel", 120 }, { "knee", 4 }, { "autoRel", 0 } });
    add ("Drums", "Room mics: bring up the room", "Crushes the room sound and blends it in for size.", { { "thr", -30 }, { "ratio", 8 }, { "atk", 1 }, { "rel", 120 }, { "knee", 2 }, { "mix", 60 }, { "autoRel", 0 } });
    add ("Drums", "Hi-hats: even out the level", "Calms uneven hi-hat hits.", { { "thr", -24 }, { "ratio", 3 }, { "atk", 2 }, { "rel", 80 }, { "knee", 6 }, { "autoRel", 0 } });
    add ("Vocals", "Vocal: even and upfront", "3:1, fast enough to catch words: the voice stays at one level and in front.", style (3, -22));
    add ("Vocals", "Vocal: gentle levelling", "Slow and soft. Smooths the level without changing the character.", style (0, -20));
    add ("Vocals", "Vocal: radio-ready, very consistent", "Firm control for a polished, broadcast-style vocal.", { { "thr", -26 }, { "ratio", 4 }, { "atk", 3 }, { "rel", 100 }, { "knee", 6 }, { "autoRel", 1 } });
    add ("Vocals", "Rap vocal: in your face", "Fast and firm so every word hits at the same level.", { { "thr", -24 }, { "ratio", 6 }, { "atk", 2 }, { "rel", 80 }, { "knee", 3 }, { "autoRel", 0 } });
    add ("Vocals", "Backing vocals: sit them back", "Flattens backing vocals so they support without competing.", { { "thr", -28 }, { "ratio", 4 }, { "atk", 5 }, { "rel", 200 }, { "knee", 8 }, { "autoRel", 1 } });
    add ("Vocals", "Vocal: thicker (parallel)", "Heavily compressed copy blended in for body, keeping the natural dynamics.", { { "thr", -34 }, { "ratio", 8 }, { "atk", 1 }, { "rel", 90 }, { "knee", 2 }, { "mix", 40 }, { "autoRel", 0 } });
    add ("Bass & 808", "Bass: steady level on every note", "Evens out the notes so the bass sits at one level.", { { "thr", -20 }, { "ratio", 4 }, { "atk", 15 }, { "rel", 120 }, { "knee", 6 }, { "autoRel", 1 } });
    add ("Bass & 808", "808: controlled tail", "Keeps the hit, tames the long tail.", { { "thr", -18 }, { "ratio", 3 }, { "atk", 30 }, { "rel", 200 }, { "knee", 6 }, { "autoRel", 0 } });
    add ("Bass & 808", "Bass ducks when the kick hits (sidechain)", "Route the kick into the sidechain input: the bass dips on every kick so both are heard.", { { "thr", -30 }, { "ratio", 6 }, { "atk", 1 }, { "rel", 90 }, { "knee", 4 }, { "extSc", 1 }, { "autoRel", 0 }, { "autoLevel", 0 } });
    add ("Instruments", "Acoustic guitar: even strums", "Smooths the strumming so it sits steady.", style (0, -22));
    add ("Instruments", "Electric guitar: longer sustain", "Holds notes up as they fade for more sustain.", { { "thr", -28 }, { "ratio", 4 }, { "atk", 10 }, { "rel", 250 }, { "knee", 6 }, { "autoRel", 1 } });
    add ("Instruments", "Piano: gentle control", "Slow and soft: tames the loudest chords without killing dynamics.", { { "thr", -20 }, { "ratio", 2 }, { "atk", 20 }, { "rel", 300 }, { "knee", 10 }, { "autoRel", 1 } });
    add ("Instruments", "Synth lead: steady level", "Keeps a lead line at a consistent level.", style (3, -20));
    add ("Mix bus", "Mix bus: glue, about 2 dB", "The classic gentle bus glue. Low end does not drive it.", style (2, -14, { { "hpf", 60 } }));
    add ("Mix bus", "Mix bus: punchy glue", "4:1 with slow attack: glues while keeping the drums hitting.", { { "thr", -14 }, { "ratio", 4 }, { "atk", 30 }, { "rel", 100 }, { "knee", 6 }, { "hpf", 80 }, { "autoRel", 1 } });
    add ("Mix bus", "Music ducks under a voice (sidechain)", "Route a voice-over or vocal into the sidechain: the music dips while it talks.", { { "thr", -30 }, { "ratio", 3 }, { "atk", 10 }, { "rel", 300 }, { "knee", 8 }, { "extSc", 1 }, { "autoRel", 1 }, { "autoLevel", 0 } });
    add ("Mastering", "Master: transparent control", "1.5:1 with a wide knee. Barely there, just more even.", { { "thr", -12 }, { "ratio", 1.5f }, { "atk", 30 }, { "rel", 300 }, { "knee", 12 }, { "autoRel", 1 }, { "hpf", 40 } });
    add ("Mastering", "Master: denser and louder", "Firmer control for a modern, dense master before the limiter.", { { "thr", -16 }, { "ratio", 2.5f }, { "atk", 10 }, { "rel", 150 }, { "knee", 8 }, { "autoRel", 1 } });
    add ("Mastering", "Master: catch peaks cleanly (lookahead)", "Uses lookahead to catch fast peaks without distortion.", { { "thr", -8 }, { "ratio", 4 }, { "atk", 1 }, { "rel", 150 }, { "knee", 4 }, { "look", 3 }, { "autoRel", 1 } });
    add ("Creative", "EDM pumping (sidechain to the kick)", "Route the kick into the sidechain: everything breathes with the beat.", { { "thr", -30 }, { "ratio", 10 }, { "atk", 1 }, { "rel", 140 }, { "knee", 0 }, { "extSc", 1 }, { "autoRel", 0 }, { "autoLevel", 0 } });
    add ("Creative", "Lo-fi squash", "Extreme, fast compression for a crushed, gritty character.", { { "thr", -36 }, { "ratio", 20 }, { "atk", 0.1f }, { "rel", 40 }, { "knee", 0 }, { "mix", 70 }, { "autoRel", 0 } });
    add ("Creative", "Breathing room effect", "Slow release makes the room and reverb swell between hits.", { { "thr", -40 }, { "ratio", 10 }, { "atk", 5 }, { "rel", 400 }, { "knee", 2 }, { "autoRel", 0 } });
    return v;
}

CompProcessor::CompProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "AemonCompressor", createLayout()),
      pluginState (*this, apvts, "Compressor", factoryPresets())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool CompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out || (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

aemon::comp::Params CompProcessor::readParams() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    aemon::comp::Params p;
    p.thresholdDb = f ("thr"); p.ratio = f ("ratio"); p.kneeDb = f ("knee");
    p.attackMs = f ("atk"); p.releaseMs = f ("rel"); p.autoRelease = f ("autoRel") > 0.5f;
    p.lookaheadMs = f ("look"); p.rmsDetector = f ("det") > 0.5f; p.scHighPassHz = f ("hpf");
    p.externalSidechain = f ("extSc") > 0.5f;
    p.mix = f ("mix") / 100.0; p.outputDb = f ("out"); p.autoLevel = f ("autoLevel") > 0.5f;
    p.bypass = f ("bypass") > 0.5f;
    return p;
}

void CompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    engine.setParams (readParams());
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    side.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    lastLatency = engine.latencySamples();
    setLatencySamples (lastLatency);
    frameLen = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));
    frameCount = 0; accIn = accOut = 0; accGr = 0; accInSq = accOutSq = 0;
}

void CompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples(), ch = mainIn.getNumChannels();
    if (n == 0 || ch == 0) return;
    if (work.getNumSamples() < n) { work.setSize (2, n, false, false, true); side.setSize (2, n, false, false, true); }
    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    juce::FloatVectorOperations::copy (L, mainIn.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (R, ch >= 2 ? mainIn.getReadPointer (1) : L, n);
    inTap.push (L, R, n);

    const float* sL = nullptr; const float* sR = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            juce::FloatVectorOperations::copy (side.getWritePointer (0), sc.getReadPointer (0), n);
            juce::FloatVectorOperations::copy (side.getWritePointer (1), sc.getNumChannels() > 1 ? sc.getReadPointer (1) : sc.getReadPointer (0), n);
            sL = side.getReadPointer (0); sR = side.getReadPointer (1);
        }
    }
    scConnected = sL != nullptr;

    // metering of the input before processing (peak per 5 ms frame)
    float framePeakIn[2048];
    const int fn = juce::jmin (n, 2048);
    for (int i = 0; i < fn; ++i) framePeakIn[i] = std::max (std::abs (L[i]), std::abs (R[i]));

    engine.setParams (readParams());
    const double deepest = engine.process (L, R, n, sL, sR);
    if (engine.latencySamples() != lastLatency) { lastLatency = engine.latencySamples(); setLatencySamples (lastLatency); }
    grNow = (float) deepest;
    makeupNow = (float) engine.currentMakeupDb();

    for (int i = 0; i < n; ++i)
    {
        const float pi = i < fn ? framePeakIn[i] : 0.0f, po = std::max (std::abs (L[i]), std::abs (R[i]));
        accIn = std::max (accIn, pi); accOut = std::max (accOut, po);
        accInSq += (double) pi * pi; accOutSq += (double) po * po;
        if (++frameCount >= frameLen)
        {
            compp::Frame f { juce::Decibels::gainToDecibels (accIn, -120.0f), juce::Decibels::gainToDecibels (accOut, -120.0f),
                             (float) engine.currentGainReductionDb(),
                             (float) (10.0 * std::log10 (accInSq / frameCount + 1e-12)), (float) (10.0 * std::log10 (accOutSq / frameCount + 1e-12)) };
            frameFifo.write (1).forEach ([&] (int idx) { frameBuf[(size_t) idx] = f; });
            frameCount = 0; accIn = accOut = 0; accInSq = accOutSq = 0;
        }
    }
    outTap.push (L, R, n);

    auto mainOut = getBusBuffer (buffer, false, 0);
    mainOut.copyFrom (0, 0, L, n);
    if (mainOut.getNumChannels() >= 2) mainOut.copyFrom (1, 0, R, n);
}

void CompProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    pluginState.writeTo (st);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void CompProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (t);
            pluginState.readFrom (t);
        }
}

juce::AudioProcessorEditor* CompProcessor::createEditor() { return new CompEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CompProcessor(); }
