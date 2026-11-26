// Aemon Rider. AGPL-3.0-or-later.
#include "RiderProcessor.h"
#include "RiderEditor.h"

using namespace aemon::rider;

juce::AudioProcessorValueTreeState::ParameterLayout RiderProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto sp = [] (float v, int) { return String (roundToInt (v)) + " dB/s"; };
    const auto fl = AudioParameterFloatAttributes();
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "even", 1 }, "How Even", NormalisableRange<float> (0, 100, 1), 0.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "target", 1 }, "Target", NormalisableRange<float> (-48, 0, 0.1f), -20.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "up", 1 }, "Up At Most", NormalisableRange<float> (0, 12, 0.1f), 6.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "down", 1 }, "Down At Most", NormalisableRange<float> (0, 12, 0.1f), 6.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed", logRange (2, 60, 1), 20.0f, fl.withStringFromValueFunction (sp)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "words", 1 }, "Words To Phrases", logRange (100, 2500, 10), 300.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "below", 1 }, "Ignore Below", NormalisableRange<float> (6, 40, 1), 20.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "follow", 1 }, "Follow The Beat", NormalisableRange<float> (0, 100, 1), 0.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "beat", 1 }, "Beat Level", NormalisableRange<float> (-60, 0, 0.1f), -20.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "look", 1 }, "Lookahead", NormalisableRange<float> (0, 200, 1), 150.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "hears", 1 }, "Hears", StringArray { "Voice", "Anything" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "gaps", 1 }, "In Gaps", StringArray { "Hold", "Return" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rideMode", 1 }, "Ride Mode", StringArray { "Ride", "Write", "Play" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ride", 1 }, "Ride", NormalisableRange<float> (-12, 12, 0.01f), 0.0f, fl.withStringFromValueFunction ([] (float v, int) { return dbText (v, 2); })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    struct P
    {
        const char* cat; const char* name; const char* desc;
        float even, up, down, speed, words; float follow = 0, below = 20, look = 150; int hears = 0, gaps = 0;
    };
    const std::vector<P>& table()
    {
        static const std::vector<P> t = []
        {
            std::vector<P> v;
            auto add = [&v] (P p) -> P& { v.push_back (p); return v.back(); };
            add ({ "", "Clean start: changes nothing", "How even at zero. Bit-exact.", 0, 6, 6, 20, 300 });

            // styles
            add ({ "Style: vocals", "Trap lead: every word at one height", "Fast, word by word, at most 6 dB either way.", 90, 6, 6, 35, 200 });
            add ({ "Style: vocals", "Melodic rap lead: long notes steady", "Over half a second at a time, at most 5 dB.", 85, 5, 5, 18, 500 });
            add ({ "Style: vocals", "Boom bap lead: even, still alive", "Part of the way, at most 4 dB. The performance stays.", 65, 4, 4, 24, 300 });
            add ({ "Style: vocals", "Drill lead: bars level, cold and steady", "Firm, at most 6 dB either way.", 95, 6, 6, 28, 350 });
            add ({ "Style: vocals", "R&B lead: runs kept, lines level", "Over phrases, so runs keep their shape. At most 5 dB.", 85, 5, 5, 11, 1100 });
            add ({ "Style: vocals", "Trap ad-libs: never over the lead", "Only turns down, up to 8 dB.", 100, 0, 8, 45, 180 });
            { auto& p = add ({ "Style: vocals", "Gospel hook stack: one bed", "Slow, over phrases, at most 6 dB.", 95, 6, 6, 12, 900 }); p.hears = 1; }
            add ({ "Style: vocals", "Whisper layer: every word heard", "Lifts up to 10 dB, hardly turns anything down.", 95, 10, 2, 40, 200 });

            add ({ "Lead vocals", "Vocal: every word in place", "The everyday setting. Words come closer together, never more than 6 dB either way.", 80, 6, 6, 24, 300 });
            add ({ "Lead vocals", "Vocal: a light hand", "Half the way, at most 3 dB. For a good take that only drifts a little.", 50, 3, 3, 10, 500 });
            add ({ "Lead vocals", "Vocal: phrases, not words", "Slow. Brings whole lines together and leaves the words inside them alone. At most 6 dB.", 80, 6, 6, 8, 1500 });
            add ({ "Lead vocals", "Vocal: catch the swallowed words", "Only lifts, up to 9 dB. Loud words are left as they are.", 90, 9, 0, 40, 220 });
            add ({ "Lead vocals", "Vocal: hold the loud ones back", "Only turns down, up to 8 dB. Nothing is ever made louder.", 90, 0, 8, 40, 220 });
            add ({ "Lead vocals", "Before the compressor: less work for it", "Evens the voice out first, so the compressor can stay gentle. At most 6 dB.", 80, 6, 6, 24, 350 });
            add ({ "Lead vocals", "Very uneven take: rescue", "Fast and far, up to 12 dB either way. Expect to hear it work.", 95, 12, 12, 35, 250 });
            { auto& p = add ({ "Lead vocals", "Intimate vocal: breaths stay low", "Lifts quiet words and comes back to zero in the gaps, so breaths are not lifted for long. At most 5 dB.", 70, 5, 5, 25, 300 }); p.gaps = 1; }
            { auto& p = add ({ "Lead vocals", "Recording: no delay", "For singing through it. Reacts a moment later, at most 5 dB.", 85, 5, 5, 35, 220 }); p.look = 0; }

            add ({ "Rap", "Rap: every bar lands", "Quick, word by word, at most 5 dB either way.", 80, 5, 5, 25, 180 });
            add ({ "Rap", "Rap: shouts and whispers together", "Far and fast, up to 10 dB. Keeps the last word of the bar up.", 90, 10, 10, 40, 180 });
            add ({ "Rap", "Ad-libs: sit behind", "Only turns down, up to 10 dB, so no ad-lib jumps out.", 100, 0, 10, 45, 180 });

            { auto& p = add ({ "Stacks", "Doubles: same level as each other", "Even and slow, at most 8 dB. Put the same preset on each double.", 90, 8, 8, 12, 600 }); p.hears = 1; }
            { auto& p = add ({ "Stacks", "Backing vocals: one smooth bed", "Slow, over phrases, at most 6 dB.", 90, 6, 6, 9, 1200 }); p.hears = 1; }

            add ({ "Speech", "Podcast: one steady voice", "Sentence by sentence, at most 8 dB either way.", 85, 8, 8, 10, 1000 });
            add ({ "Speech", "Interview: near and far from the microphone", "For someone who moves. Up to 12 dB, over a second at a time.", 90, 12, 12, 15, 900 });
            { auto& p = add ({ "Speech", "Voice-over: steady, quiet gaps", "Evens the sentences and returns to zero in the pauses. At most 6 dB.", 80, 6, 6, 18, 600 }); p.gaps = 1; }

            { auto& p = add ({ "With the beat", "Stay on top of the beat", "Connect the beat to the sidechain. Half of every move the beat makes is added to the voice, at most 5 dB.", 60, 5, 5, 10, 400 }); p.follow = 50; }
            { auto& p = add ({ "With the beat", "Follow the beat only", "Leaves the singer's own level alone and follows the beat, all the way, at most 4 dB.", 0, 4, 4, 6, 400 }); p.follow = 100; }
            { auto& p = add ({ "With the beat", "Big chorus: voice rises with it", "Only lifts, up to 4 dB, when the beat gets louder.", 0, 4, 0, 8, 400 }); p.follow = 80; }

            { auto& p = add ({ "Instruments", "Bass: every note the same", "Note by note, at most 5 dB. For played bass.", 80, 5, 5, 20, 250 }); p.hears = 1; }
            { auto& p = add ({ "Instruments", "Lead or sax: smooth line", "Over phrases, at most 5 dB.", 70, 5, 5, 8, 900 }); p.hears = 1; }
            { auto& p = add ({ "Instruments", "Loop or sample: steady bed", "Slow, at most 4 dB.", 90, 4, 4, 8, 1500 }); p.hears = 1; }

            { auto& p = add ({ "Automation", "Write the ride: smooth curve", "A slow ride that is easy to edit by hand afterwards. At most 6 dB. Switch to Write and press play.", 85, 6, 6, 12, 600 }); p.look = 200; }
            return v;
        }();
        return t;
    }
}

bool riderp::aims (const juce::String& name) { for (auto& p : table()) if (name == p.name) return p.even > 0 || p.follow > 0; return false; }

std::vector<aemon::Preset> RiderProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table())
    {
        aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc;
        o.values = { { "even", p.even }, { "up", p.up }, { "down", p.down }, { "speed", p.speed }, { "words", p.words }, { "below", p.below }, { "follow", p.follow }, { "look", p.look }, { "hears", (float) p.hears }, { "gaps", (float) p.gaps } };
        v.push_back (std::move (o));
    }
    return v;
}

riderp::Heard riderp::hear (const Recent::Copy& c, const Params& p)
{
    juce::ScopedNoDenormals noDenormals;
    Heard o; o.voice = aemon::rider::hear (c.l, c.r, c.fs, p.hears, p.words);
    Listener li; li.prepare (c.fs, false); Listener::Hop h; std::vector<double> lv; double top = 0;
    for (size_t i = 0; i < c.bl.size(); ++i) if (li.push (c.bl[i], c.br[i], h)) { lv.push_back (std::pow (10.0, h.level / 10.0)); top = std::max (top, lv.back()); }
    double s = 0; int n = 0; for (double x : lv) { s += x; n += x > 0; }
    if (top > 1e-6 && n > 0) { o.beatOk = true; o.beat = dbOf (s / (double) lv.size()); }
    return o;
}

RiderProcessor::RiderProcessor() : EffectProcessor ("Rider", "AemonRider", createLayout(), factoryPresets(), true) { startTimerHz (50); }

Params RiderProcessor::readParams() const
{
    Params p;
    p.even = real ("even") / 100.0; p.target = real ("target"); p.up = real ("up"); p.down = real ("down"); p.speed = real ("speed"); p.words = real ("words"); p.below = real ("below");
    p.follow = real ("follow") / 100.0; p.beat = real ("beat"); p.lookMs = real ("look"); p.hears = real ("hears") > 0.5f ? Hears::anything : Hears::voice; p.gaps = real ("gaps") > 0.5f ? Gaps::back : Gaps::hold;
    p.mode = (Mode) juce::jlimit (0, 2, juce::roundToInt (real ("rideMode"))); p.played = real ("ride"); p.bypass = real ("bypass") > 0.5f;
    return p;
}

void RiderProcessor::processEngine (float* L, float* R, int n)
{
    recent.push (L, R, sideL, sideR, n);
    engine->process (L, R, n, sideL, sideR);
    toWrite = (float) engine->gainToWrite();
    bool now = true;
    if (auto* ph = getPlayHead()) if (const auto pos = ph->getPosition()) now = pos->getIsPlaying();
    playing = now; ++blocks;
}

void RiderProcessor::writeStep()
{
    const int b = blocks.load(); const bool moving = b != seenBlocks; seenBlocks = b;
    const bool want = real ("rideMode") > 0.5f && real ("rideMode") < 1.5f && real ("bypass") < 0.5f && playing.load();
    auto* prm = apvts.getParameter ("ride");
    if (! want) { endWriting(); return; }
    if (! moving) return;
    if (! gesture) { prm->beginChangeGesture(); gesture = true; pluginState.ui.setProperty ("rideWritten", true, nullptr); }
    const float v = prm->convertTo0to1 (juce::jlimit (-12.0f, 12.0f, toWrite.load()));
    if (v != prm->getValue()) prm->setValueNotifyingHost (v);
}

void RiderProcessor::endWriting()
{
    if (! gesture) return;
    apvts.getParameter ("ride")->endChangeGesture(); gesture = false;
}

void RiderProcessor::setStateInformation (const void* data, int size)
{
    EffectProcessor::setStateInformation (data, size);
    // a project never opens in Write: it would overwrite the ride that was written
    if (real ("rideMode") > 0.5f && real ("rideMode") < 1.5f)
        if (auto* m = apvts.getParameter ("rideMode")) m->setValueNotifyingHost (m->convertTo0to1 ((bool) pluginState.ui.getProperty ("rideWritten", false) ? 2.0f : 0.0f));
}

float RiderProcessor::frameAux()
{
    const auto f = engine->takeFrame();
    fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; });
    return f.gain;
}

int RiderProcessor::popRide (Engine::Frame* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; });
    return n;
}

juce::AudioProcessorEditor* RiderProcessor::createEditor() { return new RiderEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RiderProcessor(); }
