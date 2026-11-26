// Aemon Tune. AGPL-3.0-or-later.
#include "TuneProcessor.h"
#include "TuneEditor.h"

using namespace aemon::tune;

juce::String tunep::noteName (int pc) { const juce::String n (noteNames[((pc % 12) + 12) % 12]); return n.length() > 1 ? n.substring (0, 1) + juce::String::charToString (0x266d) : n; }

std::array<float, 6> tunep::along (double x)
{
    const auto m = aemon::tune::along (x);
    return { (float) (m.pull * 100), (float) m.landMs, (float) (m.slideKeep * 100), (float) (m.letGo * 100), (float) m.letGoMs, (float) (m.vibratoKeep * 100) };
}

bool tunep::belongsToSong (const juce::String& id)
{
    return id == "ref" || id == "voice" || id == "source" || id == "root" || id == "scale" || ((id.startsWith ("n") || id.startsWith ("c")) && id.substring (1).containsOnly ("0123456789"));
}

bool tunep::keyOf (const std::array<bool, 12>& on, int rootWish, bool minorWish, int& root, bool& minor)
{
    auto is = [&] (int r, bool m) { return KeyFinder::notesOf (r, m) == on; };
    if (rootWish >= 0 && is (rootWish, minorWish)) { root = rootWish; minor = minorWish; return true; }
    for (int r = 0; r < 12; ++r) if (is (r, minorWish)) { root = r; minor = minorWish; return true; }
    return false;
}

juce::AudioProcessorValueTreeState::ParameterLayout TuneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l; const auto fl = AudioParameterFloatAttributes();
    auto f = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, auto text) { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, fl.withStringFromValueFunction (text))); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; }; auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; }; auto ct = [] (float v, int) { return String (v, 1) + " ct"; };
    f ("hard", "How Hard", { 0, 100, 0.1f }, 0, pc);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "own", 1 }, "Own Shape", false));
    f ("pull", "Pull", { 0, 100, 0.1f }, 100, pc); f ("land", "Land", { 0, 300, 0.1f, 0.5f }, 20, ms); f ("slide", "Slide In Kept", { 0, 100, 0.1f }, 20, pc); f ("letgo", "Let Go", { 0, 100, 0.1f }, 0, pc); f ("after", "Let Go After", { 60, 1000, 1, 0.6f }, 300, ms); f ("vib", "Vibrato Kept", { 0, 100, 0.1f }, 40, pc);
    f ("throat", "Throat", { 70, 140, 0.1f }, 100, [] (float v, int) { return v > 100.05f ? String (v / 100.0f, 2) + " deeper" : (v < 99.95f ? String (100.0f / v, 2) + " smaller" : String ("as sung")); });
    f ("trans", "Transpose", { -12, 12, 1 }, 0, [] (float v, int) { return String (roundToInt (v)) + " st"; });
    for (int i = 0; i < 12; ++i) l.add (std::make_unique<AudioParameterBool> (ParameterID { "n" + String (i), 1 }, String ("Note ") + tunep::noteNames[i], true));
    for (int i = 0; i < 12; ++i) f ("c" + String (i), String ("Cents ") + tunep::noteNames[i], { -50, 50, 0.1f }, 0, ct);
    f ("ref", "A4", { 430, 450, 0.1f }, 440, [] (float v, int) { return String (v, 1) + " Hz"; });
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Notes Come From", StringArray { "Notes that are on", "MIDI" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "I Am", StringArray { "Recording", "Mixing" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "voice", 1 }, "Voice", StringArray { "Low", "Middle", "High" }, 0));
    f ("mix", "Mix", { 0, 100, 0.1f }, 100, pc);
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    struct P { const char* cat; const char* name; const char* desc; float hard; float throat = 100; int mode = 1; std::array<float, 6> own { -1, 0, 0, 0, 0, 0 }; float trans = 0; float mix = 100; };
    const std::vector<P>& table()
    {
        const char* VO = "Style: vocals"; const char* RE = "Recording: least delay"; const char* FX = "Repair a take"; const char* VX = "Voice as an effect";
        static const std::vector<P> t {
            { "", "Clean start: leaves you as you are", "The line at its left end. Bit-exact.", 0 },
            { VO, "Trap lead: hard and locked", "Every note on its place at once, the vibrato gone. Set the key first.", 100 },
            { VO, "Trap ad-libs: hard and small", "Locked, with a smaller throat, so they never sound like the lead.", 100, 90 },
            { VO, "Melodic rap lead: tuned and floating", "Lands in about 18 ms, long notes lie still, a trace of the voice left.", 80 },
            { VO, "Rage lead: locked and deeper", "Hard, from a bigger throat.", 100, 108 },
            { VO, "Plugg lead: glassy and small", "Hard and light.", 100, 92 },
            { VO, "Cloud rap voice: soft lock", "Tight but slow to land, notes melt into each other.", 0, 100, 1, { 100, 60, 10, 0, 400, 25 } },
            { VO, "Drill lead: flat and cold", "Almost hard, the words stay short.", 92, 104 },
            { VO, "Southern slowed voice: deep and sliding", "Natural landing from a big throat, slides kept.", 0, 118, 1, { 90, 70, 80, 10, 300, 60 } },
            { VO, "Memphis tape voice: low and locked", "Hard and deep.", 96, 116 },
            { VO, "Boom bap lead: hardly there", "For sung lines between the verses. Only what is clearly off moves.", 0, 100, 1, { 55, 140, 100, 50, 220, 100 } },
            { VO, "90s East Coast hook: sung straight", "Natural, the edges left rough.", 30 },
            { VO, "West Coast hook: smooth and laid back", "Polished, slides kept.", 0, 100, 1, { 88, 70, 85, 20, 300, 90 } },
            { VO, "R&B lead: runs stay, notes land", "Fast runs find their notes, the vibrato is the singer's.", 0, 100, 1, { 88, 34, 45, 25, 260, 100 } },
            { VO, "Pop lead: polished", "Clean notes that still sound sung.", 52 },
            { VO, "Afrobeats lead: light lock", "Tight and quick, a little slide left.", 0, 98, 1, { 100, 24, 35, 0, 400, 45 } },
            { VO, "Gospel stack: one choir", "Every voice of the stack rests on the same notes, vibrato kept.", 0, 100, 1, { 94, 45, 40, 0, 400, 80 } },
            { VO, "70s soul voice: only held notes", "Moves nothing quickly. Long notes settle.", 0, 100, 1, { 62, 170, 100, 30, 400, 100 } },
            { VO, "80s lead: polished and bright", "Polished, from a slightly smaller throat.", 55, 96 },
            { VO, "House hook: tight and clean", "Tight enough to loop.", 78 },
            { VO, "Hook stack: doubles locked", "Doubles of the lead without its waver.", 88 },
            { VO, "Doubles: another singer", "The same take from a deeper throat, so it does not phase like a copy.", 60, 107 },
            { VO, "Whisper layer: leave it", "Moves only what clearly sings. Whispers pass untouched.", 12 },
            { RE, "Recording, hard: hear it while you sing", "The sound of the hard lock with the least delay.", 100, 100, 0 },
            { RE, "Recording, tight", "For melodic takes, a trace of the voice left.", 80, 100, 0 },
            { RE, "Recording, natural: only a safety net", "Enough to stay confident, nothing to hear.", 25, 100, 0 },
            { FX, "Pitchy take: pulled together", "Brings the notes close, keeps how it was sung.", 0, 100, 1, { 92, 60, 70, 15, 320, 95 } },
            { FX, "Flat endings: held up", "Long notes do not sink at their end.", 0, 100, 1, { 85, 90, 100, 0, 1000, 100 } },
            { FX, "Shaky long notes: steadied", "Takes most of the waver out of held notes.", 0, 100, 1, { 85, 60, 90, 0, 1000, 25 } },
            { FX, "One note lower: whole take", "Two half tones down with the voice kept. Set the new key.", 0, 100, 1, { 0, 180, 100, 60, 200, 100 }, -2 },
            { FX, "One note higher: whole take", "Two half tones up with the voice kept. Set the new key.", 0, 100, 1, { 0, 180, 100, 60, 200, 100 }, 2 },
            { VX, "Giant", "A very big throat, the notes untouched.", 0, 138, 1, { 0, 180, 100, 60, 200, 100 } },
            { VX, "Small creature", "A very small throat, the notes untouched.", 0, 72, 1, { 0, 180, 100, 60, 200, 100 } },
            { VX, "Robot: one line", "Hard, and the octave below.", 100, 100, 1, { -1, 0, 0, 0, 0, 0 }, -12 },
            { VX, "Second voice under the lead: an octave down", "Half under the dry voice, locked. Mix to taste.", 100, 106, 1, { -1, 0, 0, 0, 0, 0 }, -12, 35 },
        };
        return t;
    }
}

std::vector<aemon::Preset> TuneProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table())
    {
        aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc; o.values = { { "hard", p.hard }, { "throat", p.throat }, { "mode", (float) p.mode }, { "trans", p.trans }, { "mix", p.mix } };
        if (p.own[0] >= 0) { o.values.push_back ({ "own", 1.0f }); for (int i = 0; i < 6; ++i) o.values.push_back ({ tunep::ownIds[i], p.own[(size_t) i] }); }
        v.push_back (std::move (o));
    }
    return v;
}

TuneProcessor::TuneProcessor() : EffectProcessor ("Tune", "AemonTune", createLayout(), factoryPresets())
{
    for (auto& e : engines) e = std::make_unique<Engine>();
    for (int i = 0; i < 12; ++i) { noteP[(size_t) i] = apvts.getRawParameterValue ("n" + juce::String (i)); centP[(size_t) i] = apvts.getRawParameterValue ("c" + juce::String (i)); }
    for (int i = 0; i < 6; ++i) ownP[(size_t) i] = apvts.getRawParameterValue (tunep::ownIds[i]);
}

std::array<bool, 12> TuneProcessor::notesOn() const { std::array<bool, 12> n {}; for (size_t i = 0; i < 12; ++i) n[i] = noteP[i]->load() > 0.5f; return n; }

void TuneProcessor::setNotes (const std::array<bool, 12>& on)
{
    for (int i = 0; i < 12; ++i) if (auto* p = apvts.getParameter ("n" + juce::String (i))) { const float v = on[(size_t) i] ? 1.0f : 0.0f; if (p->getValue() != v) { p->beginChangeGesture(); p->setValueNotifyingHost (v); p->endChangeGesture(); } }
}

Params TuneProcessor::readParams() const
{
    Params p;
    if (real ("own") > 0.5f) p.manner = { ownP[0]->load() / 100.0, ownP[1]->load(), ownP[2]->load() / 100.0, ownP[3]->load() / 100.0, ownP[4]->load(), ownP[5]->load() / 100.0 };
    else p.manner = along (real ("hard") / 100.0);
    p.notes = notesOn(); for (size_t i = 0; i < 12; ++i) p.cents[i] = centP[i]->load();
    p.reference = real ("ref"); p.throat = real ("throat") / 100.0; p.transpose = std::round (real ("trans")); p.bypass = real ("bypass") > 0.5f; p.mix = real ("mix") / 100.0;
    if (real ("source") > 0.5f) { p.midiNote = heldNote.load(); if (p.midiNote < 0) p.notes.fill (false); }      // MIDI and no key held: nothing is moved
    return p;
}

void TuneProcessor::prepareEngine (double fs)
{
    chans = juce::jlimit (1, 2, getMainBusNumInputChannels());
    for (int i = 0; i < 6; ++i) engines[(size_t) i]->prepare (fs, chans, i >= 3, tunep::lowestHz[i % 3]);
    now = wanted(); params = readParams(); held.fill (false); heldNote = -1; sinceFind = 0;
}

void TuneProcessor::applyParameters (bool withLatency)
{
    params = readParams();
    if (withLatency && wanted() != now) { const auto heard = engines[(size_t) now]->finder; now = wanted(); engines[(size_t) now]->restart(); engines[(size_t) now]->finder = heard; }
}

void TuneProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m)
{
    for (const auto meta : m)
    {
        const auto e = meta.getMessage(); if (e.isNoteOn()) held[(size_t) e.getNoteNumber()] = true; else if (e.isNoteOff()) held[(size_t) e.getNoteNumber()] = false; else if (e.isAllNotesOff() || e.isAllSoundOff()) held.fill (false); else continue;
        int top = -1; for (int i = 127; i >= 0; --i) if (held[(size_t) i]) { top = i; break; }
        heldNote = e.isNoteOn() ? e.getNoteNumber() : top;
    }
    EffectProcessor::processBlock (b, m);
}

void TuneProcessor::processEngine (float* L, float* R, int n)
{
    auto& e = *engines[(size_t) now]; if (forget.exchange (false)) e.finder.reset();
    int k = 0; e.process (L, chans > 1 ? R : nullptr, n, params, [&] (const Engine::Frame& f) { if ((++k & 1) == 0) return; fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; }); });
    if (chans == 1 && R != L) juce::FloatVectorOperations::copy (R, L, n);
    if ((sinceFind += n) > currentRate.load() * 0.25) { sinceFind = 0; const auto f = e.finder.find(); foundRoot = f.root; foundMinor = f.minor; foundSure = (float) f.sure; foundSeconds = (float) f.seconds; foundCents = (float) (f.tuning * 100); }
}

tunep::Found TuneProcessor::found() const { return { foundRoot.load(), foundMinor.load(), foundSure.load(), foundSeconds.load(), foundCents.load() }; }

int TuneProcessor::popTune (Engine::Frame* dest, int max) noexcept { int n = 0; fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; }); return n; }

juce::AudioProcessorEditor* TuneProcessor::createEditor() { return new TuneEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TuneProcessor(); }
