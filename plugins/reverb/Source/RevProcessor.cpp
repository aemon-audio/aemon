// Aemon Reverb. AGPL-3.0-or-later.
#include "RevProcessor.h"
#include "RevEditor.h"

using namespace aemon::rev;

juce::AudioProcessorValueTreeState::ParameterLayout RevProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; };
    const auto fl = AudioParameterFloatAttributes();
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, auto text) { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, fl.withStringFromValueFunction (text))); };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "space", 1 }, "Space", StringArray { "Room", "Plate", "Hall", "Chamber", "Lo-fi", "Endless" }, 1));
    f ("size", "Size", { 0, 100, 1 }, 50, pc);
    f ("decay", "Decay", logRange (0.1f, 30, 0.01f), 1.8f, [] (float v, int) { return String (v, v < 10 ? 2 : 1) + " s"; });
    f ("lowd", "Low Decay", { 30, 200, 1 }, 90, pc);
    f ("highd", "High Decay", { 10, 120, 1 }, 55, pc);
    f ("pre", "Pre-delay", { 0, 250, 1 }, 25, ms);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "sync", 1 }, "Pre-delay Sync", StringArray { "Off", "1/64", "1/32", "1/16", "1/16 dotted", "1/8" }, 0));
    f ("lowcut", "Low Cut", logRange (20, 1000, 1), 150, hz);
    f ("highcut", "High Cut", logRange (1000, 20000, 10), 12000, hz);
    f ("early", "Early Or Tail", { 0, 100, 1 }, 35, pc);
    f ("mod", "Movement", { 0, 100, 1 }, 40, pc);
    f ("width", "Width", { 0, 150, 1 }, 100, pc);
    f ("duck", "Duck", { 0, 24, 0.1f }, 0, dB);
    f ("duckrel", "Duck Release", logRange (50, 1000, 1), 250, ms);
    f ("gate", "Gate", { 0, 600, 1 }, 0, [] (float v, int) { return v < 1 ? String ("off") : String (roundToInt (v)) + " ms"; });
    f ("drive", "Grit", { 0, 100, 1 }, 0, pc);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "freeze", 1 }, "Freeze", false));
    f ("mix", "Mix", { 0, 100, 1 }, 0, pc);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "send", 1 }, "On A Send", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    struct P
    {
        const char* cat; const char* name; const char* desc; int space; float size, decay, pre, mix;
        float low = 80, high = 55, lowcut = 180, highcut = 11000, early = 30, mod = 40, width = 100, duck = 0, duckrel = 250, gate = 0, drive = 0; int send = 0, sync = 0;
    };
    const std::vector<P>& table()
    {
        static const std::vector<P> t = []
        {
            enum { room, plate, hall, chamber, lofi, endless };
            std::vector<P> v; const char* VO = "Style: vocals"; const char* BE = "Style: beats";
            auto add = [&v] (P p) -> P& { v.push_back (p); return v.back(); };
            add ({ "", "Clean start: no reverb yet", "Mix at zero. Bit-exact.", plate, 50, 1.8f, 25, 0 });

            { auto& p = add ({ VO, "Trap lead: tight bright plate, words first", "Short and shiny, steps back while the words play.", plate, 35, 1.1f, 25, 22 }); p.high = 75; p.lowcut = 250; p.highcut = 15000; p.duck = 8; p.duckrel = 200; }
            { auto& p = add ({ VO, "Trap lead on a send: tight bright plate", "The same plate for a send. Set the level with the send.", plate, 35, 1.1f, 25, 100 }); p.high = 75; p.lowcut = 250; p.highcut = 15000; p.duck = 8; p.duckrel = 200; p.send = 1; }
            { auto& p = add ({ VO, "Trap ad-libs on a send: wide and filtered", "Only the middle, far out to the sides.", hall, 60, 2.4f, 10, 100 }); p.lowcut = 400; p.highcut = 6500; p.width = 140; p.send = 1; }
            { auto& p = add ({ VO, "Melodic rap lead: long dreamy plate behind the words", "Long and soft, it blooms when the line ends.", plate, 80, 3.2f, 60, 30 }); p.low = 70; p.lowcut = 220; p.highcut = 9500; p.duck = 10; p.duckrel = 350; p.width = 115; }
            { auto& p = add ({ VO, "Melodic rap lead on a send: long dreamy plate", "The same, for a send.", plate, 80, 3.2f, 60, 100 }); p.low = 70; p.lowcut = 220; p.highcut = 9500; p.duck = 10; p.duckrel = 350; p.width = 115; p.send = 1; }
            { auto& p = add ({ VO, "Cloud rap voice: endless wash", "A cloud behind every word, held back while they play.", endless, 70, 8, 40, 40 }); p.lowcut = 260; p.highcut = 7000; p.duck = 9; p.duckrel = 500; p.width = 125; p.mod = 60; }
            { auto& p = add ({ VO, "Boom bap lead: dusty short room", "A small worn room, felt more than heard.", room, 40, 0.5f, 5, 18 }); p.high = 40; p.highcut = 6000; p.early = 60; p.drive = 20; }
            { auto& p = add ({ VO, "90s East Coast lead: dark plate", "Dark and tight, stays under the voice.", plate, 60, 1.6f, 30, 20 }); p.mod = 60; p.high = 35; p.lowcut = 220; p.highcut = 5500; }
            { auto& p = add ({ VO, "West Coast lead: smooth hall", "Wide and relaxed, with a clear gap before it.", hall, 50, 2.2f, 45, 22 }); p.lowcut = 200; p.highcut = 9000; p.duck = 4; }
            { auto& p = add ({ VO, "Southern slowed voice: haze", "Dark, long and moving, for a slowed-down track.", chamber, 90, 4, 20, 34 }); p.high = 35; p.highcut = 4500; p.mod = 70; p.drive = 15; p.duck = 4; }
            { auto& p = add ({ VO, "Memphis tape voice: lo-fi room", "A worn tape of a room.", lofi, 50, 1.4f, 10, 24 }); p.duck = 6; p.drive = 50; p.lowcut = 260; p.highcut = 4500; }
            { auto& p = add ({ VO, "Drill lead: short dark room", "Cold and close, gone before the next word.", room, 30, 0.4f, 8, 16 }); p.high = 35; p.lowcut = 220; p.highcut = 4500; p.early = 50; }
            { auto& p = add ({ VO, "Rage lead: glassy wide space", "Bright, wide and big, out of the way while the voice screams.", plate, 60, 2, 20, 28 }); p.high = 95; p.lowcut = 350; p.highcut = 18000; p.width = 140; p.duck = 8; p.duckrel = 150; }
            { auto& p = add ({ VO, "Plugg lead: soft glass", "Bright and soft, small halls on a cloud.", hall, 35, 1.8f, 30, 25 }); p.high = 85; p.lowcut = 300; p.highcut = 16000; p.width = 130; p.duck = 5; }
            { auto& p = add ({ VO, "Gospel hook stack: choir hall", "A big, warm hall for many voices.", hall, 80, 3, 40, 28 }); p.duck = 6; p.low = 95; p.high = 60; p.width = 130; p.early = 40; }
            { auto& p = add ({ VO, "R&B lead: silk plate", "Smooth, medium long, never in front.", plate, 50, 1.9f, 40, 22 }); p.highcut = 10000; p.duck = 4; }
            { auto& p = add ({ VO, "Pop lead: shiny plate, clear gap", "Air and sparkle after a gap, so the words stay close.", plate, 55, 2.2f, 55, 24 }); p.high = 85; p.lowcut = 260; p.highcut = 16000; p.duck = 6; }
            { auto& p = add ({ VO, "Big ballad: long plate", "Very long, after a long gap.", plate, 90, 3.6f, 90, 30 }); p.lowcut = 200; p.duck = 6; p.duckrel = 400; p.width = 115; }
            { auto& p = add ({ VO, "Afrobeats lead: small bright hall", "Light, quick and wide.", hall, 25, 1.2f, 25, 20 }); p.high = 80; p.lowcut = 300; p.highcut = 14000; p.width = 120; }
            { auto& p = add ({ VO, "70s soul voice: warm chamber", "A warm room under the house.", chamber, 50, 1.7f, 25, 24 }); p.low = 110; p.high = 45; p.highcut = 7000; }
            { auto& p = add ({ VO, "Hook stack: widener hall", "Short and very wide, more size than length.", hall, 30, 1.1f, 8, 28 }); p.duck = 5; p.early = 55; p.width = 150; p.lowcut = 250; }
            { auto& p = add ({ VO, "Whisper layer: close air", "Only air around the head.", room, 20, 0.6f, 2, 35 }); p.high = 100; p.lowcut = 500; p.highcut = 18000; p.width = 150; p.early = 70; }
            { auto& p = add ({ VO, "Lead: a room nobody hears", "Takes the dryness off. You notice when it is gone.", room, 20, 0.4f, 0, 14 }); p.early = 80; p.high = 60; p.lowcut = 220; p.highcut = 8000; }
            { auto& p = add ({ VO, "Doubles: pushed back", "A step behind the lead, darker.", chamber, 35, 1.1f, 0, 34 }); p.high = 40; p.highcut = 6000; p.lowcut = 300; p.width = 125; }
            { auto& p = add ({ VO, "Last word on a send: into the dark", "Automate the send on the last word. Long, dark, it does not stop.", endless, 80, 6, 0, 100 }); p.high = 35; p.lowcut = 280; p.highcut = 5000; p.send = 1; }
            { auto& p = add ({ VO, "Spoken intro: empty church", "Far walls, a long dark tail.", hall, 100, 4.5f, 30, 28 }); p.duck = 5; p.low = 110; p.high = 40; p.lowcut = 120; }
            { auto& p = add ({ VO, "Telephone voice: stairwell", "Narrow band in a hard stairwell.", chamber, 40, 2, 5, 34 }); p.lowcut = 500; p.highcut = 3500; p.early = 50; p.width = 60; }

            { auto& p = add ({ BE, "Boom bap drums: sampler room", "A gritty room glued to the kit.", room, 30, 0.45f, 0, 20 }); p.high = 45; p.drive = 35; p.lowcut = 160; p.highcut = 6000; p.early = 60; }
            { auto& p = add ({ BE, "80s drums: gated", "Big, then suddenly gone.", hall, 70, 2, 0, 36 }); p.gate = 220; p.lowcut = 150; p.high = 80; }
            { auto& p = add ({ BE, "Trap snare: bright plate", "A shiny tail on the snare only.", plate, 40, 1.1f, 5, 25 }); p.high = 85; p.lowcut = 350; p.highcut = 16000; }
            { auto& p = add ({ BE, "Beat: 808-safe room", "Room for everything above 250 Hz, nothing below.", room, 50, 0.8f, 5, 20 }); p.lowcut = 250; }
            { auto& p = add ({ BE, "Phonk cowbell: crushed tape space", "Driven, worn and loud.", lofi, 70, 2, 0, 32 }); p.drive = 80; p.lowcut = 300; p.highcut = 5000; }
            { auto& p = add ({ BE, "Gospel sample: big hall", "Opens an old loop up.", hall, 70, 2.6f, 20, 26 }); p.low = 90; p.lowcut = 160; p.width = 120; }
            { auto& p = add ({ BE, "House clap: wide plate", "A wide plate behind the clap.", plate, 50, 1.4f, 10, 28 }); p.lowcut = 400; p.width = 140; p.high = 75; }
            { auto& p = add ({ BE, "Dub: dark chamber", "Long, dark and dirty.", chamber, 80, 3.5f, 0, 32 }); p.high = 30; p.lowcut = 250; p.highcut = 4500; p.drive = 30; }
            { auto& p = add ({ BE, "Shoegaze: wall of wash", "Everything at once.", endless, 90, 7, 0, 55 }); p.drive = 35; p.width = 140; p.mod = 70; p.lowcut = 160; }
            { auto& p = add ({ BE, "Lo-fi beat: worn room", "An old recorder in a small room.", lofi, 40, 1, 0, 25 }); p.drive = 40; p.lowcut = 180; p.highcut = 5500; }
            { auto& p = add ({ BE, "Keys: small stage", "A little depth, quickly gone.", hall, 30, 1.4f, 12, 20 }); p.lowcut = 160; }
            { auto& p = add ({ BE, "West Coast synths: wide plate", "Wide and silky for leads and pads.", plate, 60, 2.4f, 15, 26 }); p.width = 130; p.lowcut = 220; }
            { auto& p = add ({ BE, "Pad machine: hold a chord with Freeze", "Play a chord, then switch Freeze on.", endless, 80, 12, 0, 50 }); p.width = 130; p.mod = 60; p.lowcut = 120; }
            return v;
        }();
        return t;
    }
}

std::vector<aemon::Preset> RevProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table())
    {
        aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc;
        // the facts are written from the settings, so they are always true
        if (p.mix > 0) o.description << " " << juce::String (p.decay, p.decay < 10 ? 1 : 0).replace (".0", "") << " s, " << (p.pre > 0 ? juce::String (juce::roundToInt (p.pre)) + " ms before it" : juce::String ("no gap")) << (p.duck > 0 ? ", ducks " + juce::String (juce::roundToInt (p.duck)) + " dB" : juce::String()) << (p.send ? ", 100 % wet." : ", mix " + juce::String (juce::roundToInt (p.mix)) + " %.");
        o.values = { { "space", (float) p.space }, { "size", p.size }, { "decay", p.decay }, { "lowd", p.low }, { "highd", p.high }, { "pre", p.pre }, { "sync", (float) p.sync }, { "lowcut", p.lowcut }, { "highcut", p.highcut }, { "early", p.early }, { "mod", p.mod }, { "width", p.width }, { "duck", p.duck }, { "duckrel", p.duckrel }, { "gate", p.gate }, { "drive", p.drive }, { "mix", p.mix }, { "send", (float) p.send } };
        v.push_back (std::move (o));
    }
    return v;
}

RevProcessor::RevProcessor() : EffectProcessor ("Reverb", "AemonReverb", createLayout(), factoryPresets()) {}

Params RevProcessor::readParams() const
{
    Params p;
    p.space = (Space) juce::jlimit (0, 5, juce::roundToInt (real ("space"))); p.size = real ("size") / 100.0; p.decay = real ("decay"); p.lowDecay = real ("lowd") / 100.0; p.highDecay = real ("highd") / 100.0;
    const int s = juce::jlimit (0, 5, juce::roundToInt (real ("sync"))); p.preMs = s == 0 ? (double) real ("pre") : juce::jmin (550.0, revp::syncBeats[s] * 60000.0 / juce::jlimit (30.0, 300.0, bpm.load()));
    p.lowCut = real ("lowcut"); p.highCut = real ("highcut"); p.early = real ("early") / 100.0; p.mod = real ("mod") / 100.0; p.width = real ("width") / 100.0; p.duck = real ("duck"); p.duckMs = real ("duckrel"); p.gateMs = real ("gate"); p.drive = real ("drive") / 100.0;
    p.freeze = real ("freeze") > 0.5f; p.mix = real ("send") > 0.5f ? 1.0 : real ("mix") / 100.0; p.bypass = real ("bypass") > 0.5f;
    return p;
}

double revp::decayAt (const Params& p, double f)
{
    // the same two one-pole shelves as in the loop, on a middle line
    const auto& m = makeOf (p.space); const double d = 0.001 * std::sqrt (m.lo * m.hi) * scaleOf (p.size) + 0.010; auto g = [&] (double t) { return std::pow (10.0, -3 * d / std::max (0.05, t)); };
    const double gm = g (p.decay), gl = g (p.decay * p.lowDecay), gh = g (p.decay * p.highDecay);
    const std::complex<double> jl (0, f / 230.0), jh (0, f / 3800.0); const auto low = 1.0 / (1.0 + jl), high = jh / (1.0 + jh);
    const double a = std::min (0.99999, std::abs (gm + (gl - gm) * low + (gh - gm) * high)); return -3 * d / std::log10 (a);
}

void RevProcessor::processEngine (float* L, float* R, int n)
{
    if (auto* ph = getPlayHead()) if (const auto pos = ph->getPosition()) if (const auto b = pos->getBpm()) bpm = *b;
    engine->process (L, R, n);
}

float RevProcessor::frameAux()
{
    const auto f = engine->takeFrame();
    fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; });
    return f.duck;
}

int RevProcessor::popRev (Engine::Frame* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; });
    return n;
}

juce::AudioProcessorEditor* RevProcessor::createEditor() { return new RevEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RevProcessor(); }
