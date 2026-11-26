// Aemon Transient. AGPL-3.0-or-later.
#include "TransProcessor.h"
#include "TransEditor.h"

using namespace aemon::trans;

juce::AudioProcessorValueTreeState::ParameterLayout TransProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return (v > 0.05f ? "+" : "") + dbText (v); };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    const auto fl = AudioParameterFloatAttributes();
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", NormalisableRange<float> (-12, 12, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "body", 1 }, "Body", NormalisableRange<float> (-12, 12, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "alen", 1 }, "Attack Length", logRange (3, 80, 1), 20.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "blen", 1 }, "Body Length", logRange (50, 800, 5), 200.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "focus", 1 }, "Focus", StringArray { "All", "Lows", "Mids", "Highs" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output", NormalisableRange<float> (-12, 12, 0.1f), 0.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Equal Loudness", false, AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Hear The Change", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    struct P { const char* cat; const char* name; const char* desc; float attack, body, alen, blen; int focus; const char* drum; };
    const P table[] {
        { "", "Clean start: changes nothing", "Both knobs at zero. Bit-exact.", 0, 0, 20, 200, 0, nullptr },

        { "Style: beats", "Boom bap drums: knock", "The knock of the hit, up to 4 dB, a little less tail.", 4, -2, 14, 200, 0, "snare" },
        { "Style: beats", "Trap drums: snap and stop", "More snap, less tail, up to 5 dB each way.", 5, -5, 10, 160, 0, "snare" },
        { "Style: beats", "Drill drums: cold and dry", "The room goes, up to 7 dB. The hits stay.", 0, -7, 20, 220, 0, "clap" },
        { "Style: beats", "Phonk drums: all body", "The hit steps back 3, the body comes up, up to 6 dB.", -3, 6, 15, 300, 0, "snare" },
        { "Style: beats", "House kick: front and weight", "Click and tail, up to 4 dB each.", 4, 4, 18, 350, 0, "kick" },
        { "Style: beats", "80s drums: big and long", "A long body, up to 7 dB, with the crack on top.", 3, 7, 12, 350, 0, "snare" },
        { "Style: beats", "Lo-fi beat: soft hits", "Every hit rounded, up to 4 dB.", -4, 0, 25, 200, 0, "kick" },

        { "Kick", "Kick: more punch", "The first 25 ms stand out, up to 5 dB. The boom stays.", 5, 0, 25, 200, 0, "kick" },
        { "Kick", "Kick: short and tight", "The tail is pulled down, up to 7 dB. The hit stays.", 0, -7, 25, 180, 0, "kick" },
        { "Kick", "Kick: click for small speakers", "Only the top of the hit, up to 8 dB. The low end is not touched.", 8, 0, 8, 200, 3, "kick" },
        { "Kick", "Kick: longer boom", "The tail is lifted, up to 6 dB, so a short kick fills the beat.", 0, 6, 25, 400, 0, "kick" },
        { "Kick", "Kick: soft, behind the bass", "The hit steps back, up to 5 dB.", -5, 0, 20, 200, 0, "kick" },

        { "Snare", "Snare: crack", "The first 12 ms, up to 6 dB.", 6, 0, 12, 200, 0, "snare" },
        { "Snare", "Snare: crack and dry", "More hit, less room behind it, up to 6 dB each way.", 5, -6, 15, 200, 0, "snare" },
        { "Snare", "Snare: fatter", "The body comes up, up to 5 dB. The crack stays.", 0, 5, 12, 250, 0, "snare" },
        { "Snare", "Snare: thick mids", "Only the middle of the body, up to 6 dB.", 0, 6, 15, 250, 2, "snare" },
        { "Snare", "Clap: tighter", "The room behind the clap goes down, up to 8 dB.", 0, -8, 30, 200, 0, "clap" },

        { "Hats and percussion", "Hats: crisper", "The tip of every hit, up to 4 dB.", 4, 0, 6, 100, 0, "hat" },
        { "Hats and percussion", "Hats: shorter", "The ring goes down, up to 8 dB.", 0, -8, 8, 120, 0, "hat" },
        { "Hats and percussion", "Hats: softer sticks", "The tip steps back, up to 5 dB, the shimmer stays.", -5, 0, 8, 100, 0, "hat" },
        { "Hats and percussion", "Percussion: alive", "A little more hit, a little less tail, up to 4 dB.", 4, -3, 15, 150, 0, "tom" },

        { "Toms", "Toms: attack and length", "More hit and a longer note, up to 4 dB.", 4, 4, 20, 350, 0, "tom" },
        { "Toms", "Toms: no ringing", "The note is cut short, up to 9 dB.", 0, -9, 30, 250, 0, "tom" },

        { "Drum bus", "Loop: more punch", "Up to 3 dB on every hit.", 3, 0, 20, 200, 0, "snare" },
        { "Drum bus", "Loop: dry it out", "Takes room and reverb down between the hits, up to 6 dB.", 0, -6, 25, 250, 0, "snare" },
        { "Drum bus", "Loop: more room", "Brings the space between the hits up, up to 5 dB.", 0, 5, 20, 300, 0, "clap" },
        { "Drum bus", "Loop: kick forward, cymbals untouched", "Only the lows of every hit, up to 5 dB.", 5, 0, 30, 200, 1, "kick" },
        { "Drum bus", "Loop: tame spiky hits", "The hits step back, up to 4 dB, before a limiter does it less kindly.", -4, 0, 10, 200, 0, "clap" },

        { "Instruments", "Guitar or piano: more pick", "The start of every note, up to 4 dB.", 4, 0, 20, 300, 0, "tom" },
        { "Instruments", "Guitar or piano: longer notes", "Notes hang on, up to 5 dB.", 0, 5, 20, 600, 0, "tom" },
        { "Instruments", "Bass: slower start", "The pluck steps back, up to 5 dB, the note stays.", -5, 0, 25, 300, 0, "tom" },
    };
}

transp::Intent transp::intentOf (const juce::String& name) { for (auto& p : table) if (name == p.name) return { p.attack, p.body, p.drum, p.focus }; return {}; }

std::vector<aemon::Preset> TransProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table) { aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc; o.values = { { "attack", p.attack }, { "body", p.body }, { "alen", p.alen }, { "blen", p.blen }, { "focus", (float) p.focus } }; v.push_back (std::move (o)); }
    return v;
}

TransProcessor::TransProcessor() : EffectProcessor ("Transient", "AemonTransient", createLayout(), factoryPresets()) {}

Params TransProcessor::readParams() const
{
    Params p;
    p.attack = real ("attack"); p.body = real ("body"); p.attackMs = real ("alen"); p.bodyMs = real ("blen"); p.focus = (Focus) juce::jlimit (0, 3, juce::roundToInt (real ("focus")));
    p.outDb = real ("out"); p.delta = real ("delta") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    return p;
}

void TransProcessor::processEngine (float* L, float* R, int n)
{
    engine->process (L, R, n);
    const int k = juce::jmin (engine->numPoints, fifo.getFreeSpace()); int i = 0;
    fifo.write (k).forEach ([&] (int at) { points[(size_t) at] = engine->points[(size_t) i++]; });
    engine->numPoints = 0;
}

float TransProcessor::frameAux()
{
    const auto f = engine->takeFrame();
    peakIn = std::max (f.in, peakIn.load() - 0.1f); peakOut = std::max (f.out, peakOut.load() - 0.1f);
    return std::abs (f.up) > std::abs (f.down) ? f.up : f.down;
}

int TransProcessor::popPoints (Engine::Point* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = points[(size_t) i]; });
    return n;
}

juce::AudioProcessorEditor* TransProcessor::createEditor() { return new TransEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TransProcessor(); }
