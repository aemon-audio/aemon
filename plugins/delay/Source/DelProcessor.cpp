// Aemon Delay. AGPL-3.0-or-later.
#include "DelProcessor.h"
#include "DelEditor.h"

using namespace aemon::del;

juce::AudioProcessorValueTreeState::ParameterLayout DelProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    const auto fl = AudioParameterFloatAttributes();
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, auto text) { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, fl.withStringFromValueFunction (text))); };
    StringArray divs; for (auto* d : delp::divisionNames) divs.add (d);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "sync", 1 }, "Time On The Beat", divs, 9));
    f ("ms", "Time", logRange (5, 2500, 1), 375, ms);
    f ("right", "Right Time", { 25, 150, 1 }, 100, pc);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style", StringArray { "Stereo", "Ping-pong", "Mono" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "char", 1 }, "Character", StringArray { "Digital", "Tape", "Lo-fi" }, 0));
    f ("fb", "Feedback", { 0, 110, 1 }, 30, pc);
    f ("lowcut", "Low Cut", logRange (20, 2000, 1), 150, hz);
    f ("highcut", "High Cut", logRange (1000, 20000, 10), 9000, hz);
    f ("drive", "Grit", { 0, 100, 1 }, 0, pc);
    f ("wobble", "Wobble", { 0, 100, 1 }, 20, pc);
    f ("duck", "Duck", { 0, 24, 0.1f }, 0, dB);
    f ("duckrel", "Duck Release", logRange (50, 1000, 1), 250, ms);
    f ("width", "Width", { 0, 150, 1 }, 100, pc);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "change", 1 }, "Time Changes", StringArray { "Glide", "Fade" }, 1));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "throwOnly", 1 }, "Only What Is Thrown", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "throw", 1 }, "Throw", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "reverse", 1 }, "Reverse", false));
    f ("mix", "Mix", { 0, 100, 1 }, 0, pc);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "send", 1 }, "On A Send", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    struct P
    {
        const char* cat; const char* name; const char* desc; int sync; int style; int ch; float fb, mix;
        float lowcut = 200, highcut = 8000, drive = 0, wobble = 20, duck = 0, duckrel = 250, width = 100, right = 100, ms = 375; int send = 0, throwOnly = 0, reverse = 0, change = 1;
    };
    const std::vector<P>& table()
    {
        static const std::vector<P> t = []
        {
            enum { free_, d32, t16, d16, p16, t8, d8, p8, t4, d4, p4, d2, p2, bar }; enum { stereo, pingpong, mono }; enum { digital, tape, lofi };
            std::vector<P> v; const char* VO = "Style: vocals"; const char* BE = "Style: beats";
            auto add = [&v] (P p) -> P& { v.push_back (p); return v.back(); };
            add ({ "", "Clean start: no echo yet", "Mix at zero. Bit-exact.", d4, stereo, digital, 30, 0 });

            { auto& p = add ({ VO, "Last word: quarter throw", "Nothing echoes until you throw it. Automate Throw on the last word.", d4, stereo, digital, 45, 45 }); p.lowcut = 300; p.highcut = 5500; p.throwOnly = 1; p.duck = 4; }
            { auto& p = add ({ VO, "Last word on a send: quarter throw", "For a send: ride the send on the last word.", d4, stereo, digital, 45, 100 }); p.lowcut = 300; p.highcut = 5500; p.send = 1; p.duck = 4; }
            { auto& p = add ({ VO, "Trap lead: quarter under the words", "Dark quarters that step back while the words play.", d4, stereo, digital, 28, 18 }); p.lowcut = 350; p.highcut = 4500; p.duck = 9; }
            { auto& p = add ({ VO, "Melodic rap lead: dotted eighth, floating", "The dotted eighth behind a tuned lead.", p8, stereo, tape, 40, 22 }); p.lowcut = 300; p.highcut = 6000; p.duck = 9; p.duckrel = 300; p.width = 120; }
            { auto& p = add ({ VO, "Melodic rap lead on a send: dotted eighth", "The same, for a send.", p8, stereo, tape, 40, 100 }); p.lowcut = 300; p.highcut = 6000; p.duck = 9; p.duckrel = 300; p.width = 120; p.send = 1; }
            { auto& p = add ({ VO, "Trap ad-libs: ping-pong", "Left, right, left, off the lead.", d8, pingpong, digital, 45, 26 }); p.lowcut = 400; p.highcut = 6500; p.duck = 10; }
            { auto& p = add ({ VO, "Triplet flow: eighth triplets", "Triplets under a triplet flow.", t8, stereo, digital, 35, 20 }); p.lowcut = 350; p.highcut = 5000; p.duck = 8; }
            { auto& p = add ({ VO, "Drill lead: cold slap", "One short dark echo, nothing after it.", d16, mono, digital, 0, 20 }); p.lowcut = 350; p.highcut = 3500; p.duck = 4; }
            { auto& p = add ({ VO, "Boom bap lead: rap slapback", "One quick echo behind the voice.", free_, mono, tape, 0, 22 }); p.ms = 105; p.lowcut = 250; p.highcut = 5000; p.wobble = 10; p.duck = 3; }
            { auto& p = add ({ VO, "50s slapback: rock and roll", "One warm echo, clearly heard.", free_, mono, tape, 8, 22 }); p.duck = 3; p.ms = 130; p.lowcut = 150; p.highcut = 6000; p.drive = 15; p.wobble = 15; }
            { auto& p = add ({ VO, "Rap double: wide and instant", "A double on each side from one take.", free_, stereo, digital, 0, 30 }); p.ms = 24; p.right = 140; p.lowcut = 200; p.highcut = 9000; p.width = 150; }
            { auto& p = add ({ VO, "90s East Coast lead: dark eighths", "Dark, short, out of the way.", d8, mono, tape, 25, 18 }); p.lowcut = 250; p.highcut = 4000; p.duck = 6; }
            { auto& p = add ({ VO, "West Coast lead: smooth quarters", "Wide and soft, a little swing in the tape.", d4, pingpong, tape, 35, 20 }); p.lowcut = 250; p.highcut = 6500; p.duck = 6; p.wobble = 30; }
            { auto& p = add ({ VO, "Southern slowed voice: half-time haze", "Half notes in a haze.", d2, stereo, tape, 45, 26 }); p.lowcut = 180; p.highcut = 3800; p.wobble = 60; p.drive = 20; p.duck = 6; p.duckrel = 400; }
            { auto& p = add ({ VO, "Memphis tape voice: crushed echo", "Broken, narrow and loud.", d8, mono, lofi, 50, 24 }); p.drive = 70; p.wobble = 50; p.lowcut = 400; p.highcut = 3500; p.duck = 9; }
            { auto& p = add ({ VO, "Cloud rap voice: long tail of quarters", "Echoes that melt into a cloud.", p4, pingpong, tape, 65, 25 }); p.lowcut = 300; p.highcut = 4500; p.wobble = 50; p.duck = 12; p.duckrel = 450; p.width = 130; }
            { auto& p = add ({ VO, "Rage lead: sixteenths, wide and torn", "Fast, torn, and behind the scream.", d16, pingpong, digital, 35, 18 }); p.drive = 60; p.lowcut = 600; p.highcut = 7000; p.duck = 16; p.duckrel = 180; p.width = 140; }
            { auto& p = add ({ VO, "Plugg lead: glassy eighths", "Bright and light.", d8, pingpong, digital, 40, 20 }); p.lowcut = 600; p.highcut = 14000; p.duck = 7; p.width = 125; }
            { auto& p = add ({ VO, "Gospel hook stack: wide quarters", "A choir that answers itself.", d4, pingpong, digital, 30, 17 }); p.lowcut = 300; p.highcut = 8000; p.width = 140; p.duck = 9; }
            { auto& p = add ({ VO, "R&B lead: silk eighth", "Soft dotted echoes you feel more than hear.", p8, stereo, tape, 30, 15 }); p.lowcut = 250; p.highcut = 5000; p.duck = 7; p.right = 133; }
            { auto& p = add ({ VO, "Pop lead: quarter and eighth", "Quarters left, eighths right.", d4, stereo, digital, 30, 18 }); p.right = 50; p.lowcut = 300; p.highcut = 7500; p.duck = 8; }
            { auto& p = add ({ VO, "Afrobeats lead: dotted bounce", "A bounce that sits in the percussion.", p8, pingpong, digital, 35, 18 }); p.lowcut = 400; p.highcut = 7000; p.duck = 6; }
            { auto& p = add ({ VO, "Big ballad: long quarter and a half", "Slow, wide, waiting for the end of the line.", p4, stereo, tape, 40, 22 }); p.lowcut = 250; p.highcut = 5500; p.duck = 10; p.duckrel = 500; p.width = 120; }
            { auto& p = add ({ VO, "Telephone echo", "Echoes from the other end of the line.", d8, mono, lofi, 45, 28 }); p.lowcut = 700; p.highcut = 2800; p.drive = 25; p.duck = 5; }
            { auto& p = add ({ VO, "Reversed swell before the word", "Words arrive backwards, swelling into the next.", d4, stereo, digital, 20, 28 }); p.reverse = 1; p.lowcut = 250; p.highcut = 6000; p.width = 130; p.duck = 9; }
            { auto& p = add ({ VO, "Spoken intro: station loudspeaker", "A worn echo in a big empty place.", free_, stereo, lofi, 45, 24 }); p.duck = 6; p.ms = 420; p.right = 115; p.lowcut = 350; p.highcut = 3200; p.drive = 35; p.wobble = 40; }
            { auto& p = add ({ VO, "Whisper layer: around the head", "Whispers left and right.", d16, pingpong, digital, 45, 35 }); p.lowcut = 900; p.highcut = 16000; p.width = 150; }

            { auto& p = add ({ BE, "Dub: feedback, kept safe", "Feeds itself. Turn Feedback past 100 % and it sings, never louder than the safe level.", p8, stereo, tape, 72, 35 }); p.lowcut = 250; p.highcut = 3500; p.drive = 35; p.wobble = 35; }
            { auto& p = add ({ BE, "Boom bap drums: sampler slap", "One gritty slap on the kit.", free_, mono, lofi, 0, 16 }); p.ms = 80; p.drive = 45; p.lowcut = 250; }
            { auto& p = add ({ BE, "Trap hats: triplet sparkle", "Triplets in the hats, wide.", t16, pingpong, digital, 30, 20 }); p.lowcut = 1500; p.highcut = 16000; p.width = 130; }
            { auto& p = add ({ BE, "Phonk cowbell: crushed eighths", "Eighths driven into the red.", d8, pingpong, lofi, 50, 28 }); p.drive = 80; p.lowcut = 400; p.highcut = 4000; }
            { auto& p = add ({ BE, "House stab: dotted ping-pong", "The classic dotted bounce.", p8, pingpong, digital, 50, 28 }); p.lowcut = 300; p.highcut = 8000; }
            { auto& p = add ({ BE, "Techno: sixteenth rumble", "A dark moving bed behind the kick.", p16, stereo, tape, 60, 25 }); p.lowcut = 120; p.highcut = 900; p.drive = 30; }
            { auto& p = add ({ BE, "Synth lead: wide eighths", "Eighths left and right of the lead.", d8, pingpong, digital, 35, 24 }); p.lowcut = 250; p.highcut = 9000; p.width = 130; }
            { auto& p = add ({ BE, "Keys: warm quarter", "Tape quarters under the chords.", d4, stereo, tape, 30, 22 }); p.lowcut = 200; p.highcut = 5000; p.wobble = 30; }
            { auto& p = add ({ BE, "Guitar: 80s dotted eighth", "One steady dotted eighth that plays the rhythm with you.", p8, mono, digital, 35, 35 }); p.lowcut = 150; p.highcut = 7000; }
            { auto& p = add ({ BE, "Shoegaze: reversed wash", "Everything backwards at once.", d2, pingpong, tape, 60, 45 }); p.reverse = 1; p.wobble = 60; p.drive = 25; p.width = 140; }
            { auto& p = add ({ BE, "Lo-fi beat: worn echo", "A tired echo from an old recorder.", d4, mono, lofi, 40, 20 }); p.drive = 40; p.wobble = 60; p.highcut = 3500; }
            { auto& p = add ({ BE, "Beat fill: bar repeat", "Plays the last bar again when you throw it.", bar, stereo, digital, 0, 50 }); p.throwOnly = 1; p.lowcut = 20; p.highcut = 20000; }
            return v;
        }();
        return t;
    }
}

std::vector<aemon::Preset> DelProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table())
    {
        aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc;
        if (p.mix > 0) o.description << " " << (p.sync == 0 ? juce::String (juce::roundToInt (p.ms)) + " ms" : juce::String (delp::divisionNames[p.sync])) << (p.fb > 0 ? ", feedback " + juce::String (juce::roundToInt (p.fb)) + " %" : juce::String (", one echo")) << (p.duck > 0 ? ", ducks " + juce::String (juce::roundToInt (p.duck)) + " dB" : juce::String()) << (p.send ? ", 100 % wet." : ", mix " + juce::String (juce::roundToInt (p.mix)) + " %.");
        o.values = { { "sync", (float) p.sync }, { "ms", p.ms }, { "right", p.right }, { "style", (float) p.style }, { "char", (float) p.ch }, { "fb", p.fb }, { "lowcut", p.lowcut }, { "highcut", p.highcut }, { "drive", p.drive }, { "wobble", p.wobble }, { "duck", p.duck }, { "duckrel", p.duckrel }, { "width", p.width }, { "change", (float) p.change }, { "throwOnly", (float) p.throwOnly }, { "reverse", (float) p.reverse }, { "mix", p.mix }, { "send", (float) p.send } };
        v.push_back (std::move (o));
    }
    return v;
}

DelProcessor::DelProcessor() : EffectProcessor ("Delay", "AemonDelay", createLayout(), factoryPresets()) {}

Params DelProcessor::readParams() const
{
    Params p;
    const int s = juce::jlimit (0, delp::numDivisions - 1, juce::roundToInt (real ("sync"))); p.ms = s == 0 ? (double) real ("ms") : delp::divisionBeats[s] * 60000.0 / juce::jlimit (30.0, 300.0, bpm.load());
    while (p.ms > maxMs) p.ms *= 0.5;      // a bar at a slow tempo is longer than the memory: half of it
    p.right = real ("right") / 100.0; p.style = (Style) juce::jlimit (0, 2, juce::roundToInt (real ("style"))); p.character = (Character) juce::jlimit (0, 2, juce::roundToInt (real ("char")));
    p.feedback = real ("fb") / 100.0; p.lowCut = real ("lowcut"); p.highCut = real ("highcut"); p.drive = real ("drive") / 100.0; p.wobble = real ("wobble") / 100.0; p.duck = real ("duck"); p.duckMs = real ("duckrel"); p.width = real ("width") / 100.0;
    p.change = real ("change") > 0.5f ? Change::fade : Change::glide; p.throwOnly = real ("throwOnly") > 0.5f; p.throwNow = real ("throw") > 0.5f; p.reverse = real ("reverse") > 0.5f; p.mix = real ("send") > 0.5f ? 1.0 : real ("mix") / 100.0; p.bypass = real ("bypass") > 0.5f;
    return p;
}

void DelProcessor::processEngine (float* L, float* R, int n)
{
    if (auto* ph = getPlayHead()) if (const auto pos = ph->getPosition()) if (const auto b = pos->getBpm()) bpm = *b;
    engine->process (L, R, n);
}

float DelProcessor::frameAux()
{
    const auto f = engine->takeFrame();
    fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; });
    return f.duck;
}

int DelProcessor::popDel (Engine::Frame* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; });
    return n;
}

juce::AudioProcessorEditor* DelProcessor::createEditor() { return new DelEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DelProcessor(); }
