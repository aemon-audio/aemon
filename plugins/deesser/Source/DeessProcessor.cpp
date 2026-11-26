// Aemon De-esser. AGPL-3.0-or-later.
#include "DeessProcessor.h"
#include "DeessEditor.h"

using namespace aemon::deess;

juce::AudioProcessorValueTreeState::ParameterLayout DeessProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float v, int) { return dbText (v); };
    auto pc = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ms = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; };
    auto hz = [] (float v, int) { return String (v / 1000.0f, 2) + " kHz"; };
    const auto fl = AudioParameterFloatAttributes();
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount", NormalisableRange<float> (0, 100, 1), 0.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "freq", 1 }, "Where", logRange (3000, 12000, 10), 6500.0f, fl.withStringFromValueFunction (hz)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "line", 1 }, "Line", NormalisableRange<float> (-24, 6, 0.1f), -10.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "range", 1 }, "At Most", NormalisableRange<float> (0, 24, 0.1f), 10.0f, fl.withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "strict", 1 }, "Strictness", NormalisableRange<float> (0, 100, 1), 50.0f, fl.withStringFromValueFunction (pc)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "rel", 1 }, "Release", logRange (5, 150, 1), 40.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "look", 1 }, "Lookahead", NormalisableRange<float> (0, 5, 0.1f), 2.0f, fl.withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "shape", 1 }, "Cut", StringArray { "Above", "Just S", "Whole" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "on", 1 }, "Works On", StringArray { "Stereo", "Mid", "Side" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "follow", 1 }, "Line Follows", StringArray { "Voice", "Fixed" }, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "solo", 1 }, "Hear Only The S", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "removed", 1 }, "Hear Removed", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    return l;
}

namespace
{
    /** Everything a factory preset is. down: how far the sharper S sounds should come down on the voice it meets. */
    struct P
    {
        const char* cat; const char* name; const char* desc;
        float amount, down, range; bool aim = true;
        float freq = 6500, line = -9, strict = 50, rel = 40, look = 2; int shape = 0, on = 0, follow = 0;
    };
    const std::vector<P>& table()
    {
        static const std::vector<P> t = []
        {
            std::vector<P> v;
            auto add = [&v] (P p) -> P& { v.push_back (p); return v.back(); };
            add ({ "", "Clean start: changes nothing", "Amount at zero. Bit-exact.", 0, 0, 10, false }).line = -10;

            // styles
            { auto& p = add ({ "Style: vocals", "Trap lead: bright but never sharp", "For a bright lead. Fast, about 7 dB off the sharper S.", 85, 7, 11 }); p.rel = 20; p.freq = 7200; p.line = -11; }
            { auto& p = add ({ "Style: vocals", "Melodic rap lead: tuned voice, smooth S", "A tuned voice hisses more. The whole voice dips a little with it, about 5 dB.", 80, 5, 9 }); p.shape = 2; p.strict = 60; p.rel = 50; p.line = -10; }
            { auto& p = add ({ "Style: vocals", "R&B lead: silk", "About 3 dB, soft and slow. The breath stays.", 60, 3, 6 }); p.rel = 70; p.strict = 30; p.line = -8; }
            { auto& p = add ({ "Style: vocals", "Pop lead: S down, shine kept", "Only the S itself, about 6 dB. The air above stays open.", 85, 6, 11 }); p.shape = 1; p.freq = 7200; p.line = -11; }
            { auto& p = add ({ "Style: vocals", "Boom bap lead: just the spikes", "About 1.5 dB off the sharpest ones, the grit stays.", 90, 1.5f, 5 }); p.line = -6; }
            { auto& p = add ({ "Style: vocals", "Drill lead: cold but not sharp", "Quick, about 4 dB.", 75, 4, 8 }); p.rel = 15; p.look = 3; p.line = -9; }
            { auto& p = add ({ "Style: vocals", "Trap ad-libs: S out of the way", "About 9 dB down, so the lead keeps the words.", 90, 9, 14 }); p.line = -14; p.strict = 55; p.rel = 25; }
            { auto& p = add ({ "Style: vocals", "Gospel hook stack: one S", "About 11 dB, so a stack does not hiss.", 100, 11, 17 }); p.line = -17; p.strict = 55; p.rel = 60; }
            { auto& p = add ({ "Style: vocals", "Harsh cheap microphone: needles out", "About 8 dB, aimed lower, from the first instant.", 90, 8, 13 }); p.freq = 5600; p.strict = 60; p.look = 3.5f; p.line = -13; }
            { auto& p = add ({ "Style: vocals", "Whisper layer: breath stays, S goes", "Only clear S sounds and only their own range, about 10 dB.", 95, 10, 16 }); p.shape = 1; p.strict = 25; p.line = -15; }

            add ({ "Lead vocals", "Vocal: smooth, never lisps", "The everyday setting. Sharp S sounds come down about 4 dB, soft ones stay.", 70, 4, 8 });
            add ({ "Lead vocals", "Vocal: just a touch", "Two dB off the sharpest ones. For a good recording.", 50, 2, 4 }).line = -6;
            { auto& p = add ({ "Lead vocals", "Vocal: tame a bright microphone", "About 7 dB, for a mic or an EQ that spits.", 85, 7, 12 }); p.line = -12; p.freq = 6200; p.strict = 60; }
            { auto& p = add ({ "Lead vocals", "Pop vocal: keep the shine", "Only the S itself dips, the air above stays open.", 85, 5, 10 }); p.shape = 1; p.freq = 7000; }
            { auto& p = add ({ "Lead vocals", "Soft vocal: keep the breath", "Only clear S sounds are touched. Breaths and whispers stay.", 60, 3, 6 }); p.strict = 20; p.line = -8; }
            { auto& p = add ({ "Lead vocals", "Loud chorus: only the sharpest ones", "Leaves normal S sounds alone and stops the ones that jump out.", 100, 3.5f, 9 }); p.line = -6; }
            { auto& p = add ({ "Lead vocals", "Compressed vocal: bring the S back down", "For a voice whose S became as loud as its vowels.", 85, 8, 12 }); p.line = -13; p.rel = 60; }
            { auto& p = add ({ "Lead vocals", "High voice: S up top", "Starts higher, keeps the body of the S.", 70, 4.5f, 9 }); p.freq = 8200; }
            { auto& p = add ({ "Lead vocals", "Low voice: S and SH", "Starts lower, so SH and CH are included.", 70, 4.5f, 9 }); p.freq = 5000; p.strict = 60; }

            { auto& p = add ({ "Rap", "Rap: fast and tight", "Catches quick S sounds and lets go at once. Words stay sharp.", 75, 5, 9 }); p.rel = 12; p.look = 3; }
            { auto& p = add ({ "Rap", "Shouted vocal: spit under control", "S and T together, from the very first instant.", 85, 7, 12 }); p.strict = 65; p.look = 4; p.rel = 25; p.line = -12; }
            { auto& p = add ({ "Rap", "Ad-libs: S out of the way", "Far down, so the lead keeps the words.", 90, 10, 16 }); p.line = -15; p.strict = 60; p.rel = 25; }

            { auto& p = add ({ "Stacks", "Backing vocals: S almost gone", "Let the lead say the S. The stack keeps the vowels.", 100, 12, 18 }); p.line = -18; p.strict = 55; }
            { auto& p = add ({ "Stacks", "Doubles: one S, not three", "As good as no S at all, for tracks layered under a lead.", 100, 18, 24 }); p.line = -24; p.strict = 60; p.rel = 25; }
            { auto& p = add ({ "Stacks", "Choir or gang: blend the S together", "Smooth and slow, so many voices sound like one.", 80, 8, 12 }); p.line = -14; p.rel = 90; p.strict = 55; }

            { auto& p = add ({ "Speech", "Podcast: clear and soft", "Gentle on speech. Every word stays readable.", 60, 3.5f, 6 }); p.freq = 6000; p.line = -8; }
            { auto& p = add ({ "Speech", "Voice-over: whole voice dips", "Turns the S down without changing the tone.", 70, 4, 7 }); p.shape = 2; p.line = -9; }
            { auto& p = add ({ "Speech", "Close microphone: S, T and breaths", "For a mic that hears every mouth noise.", 85, 6, 10 }); p.strict = 70; p.look = 3.5f; p.line = -13; }
            { auto& p = add ({ "Speech", "Phone or laptop microphone: less hiss on S", "Aims lower, where cheap microphones are harsh.", 85, 7, 12 }); p.freq = 5000; p.strict = 55; p.line = -12; }

            { auto& p = add ({ "Before effects", "Before a reverb: no S splashes", "Put it on the send. The reverb stays smooth, the voice keeps its S.", 100, 14, 24 }); p.line = -20; p.strict = 60; }
            { auto& p = add ({ "Before effects", "Before a delay: echoes without the S", "Put it on the send. The echoes stop clicking over the words.", 100, 18, 24 }); p.line = -24; p.strict = 60; p.rel = 25; p.look = 3; }

            { auto& p = add ({ "Mix and master", "Mix: the vocal's S in the middle", "Works on the centre only, at most 4 dB. The sides are untouched.", 70, 3, 4, false }); p.on = 1; p.strict = 100; p.shape = 1; p.freq = 7000; p.line = -12; }
            { auto& p = add ({ "Mix and master", "Master: one S too sharp", "At most 2.5 dB in the middle. The last resort before the limiter.", 60, 2, 2.5f, false }); p.on = 1; p.strict = 100; p.shape = 1; p.freq = 7000; p.line = -10; p.look = 4; }

            { auto& p = add ({ "Instruments", "Hi-hats: take the edge off", "A fixed line. Loud hits come down, quiet ones keep their life.", 60, 4, 6, false }); p.follow = 1; p.strict = 80; p.freq = 8500; p.line = -2; p.rel = 25; }
            { auto& p = add ({ "Instruments", "Overheads: cymbals less piercing", "Slow and gentle, with a fixed line.", 60, 3, 5, false }); p.follow = 1; p.strict = 100; p.freq = 6500; p.line = -4; p.rel = 80; }
            { auto& p = add ({ "Instruments", "Acoustic guitar: softer string squeaks", "Catches fingers sliding on the strings. The notes stay bright.", 90, 6, 10, false }); p.shape = 1; p.strict = 70; p.freq = 5200; p.line = -18; p.rel = 60; }

            { auto& p = add ({ "Creative", "Lisp (effect)", "Far too much, on purpose.", 100, 24, 24, false }); p.freq = 4500; p.strict = 100; p.line = -24; }
            return v;
        }();
        return t;
    }
}

deessp::Intent deessp::intentOf (const juce::String& name) { for (auto& p : table()) if (name == p.name && p.down > 0 && p.down < 24) return { p.down, p.aim }; return {}; }

std::vector<aemon::Preset> DeessProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    for (auto& p : table())
    {
        aemon::Preset o; o.name = p.name; o.category = p.cat; o.description = p.desc;
        o.values = { { "amount", p.amount }, { "freq", p.freq }, { "line", p.line }, { "range", p.range }, { "strict", p.strict }, { "rel", p.rel }, { "look", p.look }, { "shape", (float) p.shape }, { "on", (float) p.on }, { "follow", (float) p.follow } };
        v.push_back (std::move (o));
    }
    return v;
}

DeessProcessor::DeessProcessor() : EffectProcessor ("De-esser", "AemonDeesser", createLayout(), factoryPresets()) {}

Params DeessProcessor::readParams() const
{
    Params p;
    p.amount = real ("amount") / 100.0; p.freq = real ("freq"); p.line = real ("line"); p.range = real ("range"); p.strictness = real ("strict") / 100.0;
    p.releaseMs = real ("rel"); p.lookMs = real ("look");
    p.shape = (Shape) juce::jlimit (0, 2, juce::roundToInt (real ("shape"))); p.on = (On) juce::jlimit (0, 2, juce::roundToInt (real ("on")));
    p.fixedLine = real ("follow") > 0.5f; p.solo = real ("solo") > 0.5f; p.removed = real ("removed") > 0.5f; p.bypass = real ("bypass") > 0.5f;
    return p;
}

float DeessProcessor::frameAux()
{
    const auto f = engine.takeFrame();
    fifo.write (1).forEach ([&] (int i) { frames[(size_t) i] = f; });
    if (f.eventCut >= 0) { const int n = heard.load(); cuts[(size_t) (n % keep)] = f.eventCut; heard = n + 1; }
    const float held = vowelCut.load();
    vowelCut = std::max (f.vowelCut, held > 0.02f ? held * 0.995f : 0.0f);
    return f.cut;
}

int DeessProcessor::popS (Engine::Frame* dest, int max) noexcept
{
    int n = 0;
    fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frames[(size_t) i]; });
    return n;
}

std::vector<float> DeessProcessor::recentCuts() const
{
    const int n = heard.load(), c = juce::jlimit (0, keep, n - validFrom.load());
    std::vector<float> v; for (int i = n - c; i < n; ++i) v.push_back (cuts[(size_t) (i % keep)].load());
    return v;
}

float deessp::centreOf (const std::vector<float>& p, double binHz)
{
    const int k0 = (int) std::ceil (3000.0 / binHz), k1 = std::min ((int) p.size() - 1, (int) (13000.0 / binHz));
    if (k1 <= k0 + 4) return 0;
    auto smooth = [&p] (int k) { float a = 0; for (int j = -2; j <= 2; ++j) a += p[(size_t) juce::jlimit (0, (int) p.size() - 1, k + j)]; return a; };
    float top = 0; for (int k = k0; k < k1; ++k) top = std::max (top, smooth (k));
    if (top <= 0) return 0;
    double num = 0, den = 0; for (int k = k0; k < k1; ++k) { const float s = smooth (k); if (s > 0.25f * top) { num += s * std::log ((double) k); den += s; } }
    return juce::jlimit (3500.0f, 11000.0f, (float) (std::exp (num / den) * binHz));
}

deessp::Found deessp::find (const std::vector<float>& x, double fs, Params p, bool keepFreq)
{
    juce::ScopedNoDenormals noDenormals;
    Found out;
    if (! keepFreq)
    {
        const int order = 10 + juce::jmax (0, juce::roundToInt (std::log2 (fs / 48000.0))), N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), buf ((size_t) (2 * N)), sum ((size_t) (N / 2), 0.0f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        Bands bands; bands.prepare (fs); bands.setRange (3500.0, std::min (14000.0, fs * 0.4));
        int hops = 0, used = 0;
        for (int i = 0; i < (int) x.size(); ++i)
        {
            if (! bands.push (x[(size_t) i], 0, false)) continue;
            if (++hops % 8 != 0 || bands.sDb < -75.0 || bands.sDb - bands.bDb < 2.0) continue;
            const int from = i - bands.halfWindow() * bands.hopLength() - N / 2;
            if (from < 0 || from + N > (int) x.size()) continue;
            for (int j = 0; j < N; ++j) buf[(size_t) j] = x[(size_t) (from + j)] * win[(size_t) j];
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            float e = 1e-20f; const int k0 = (int) std::ceil (3000.0 * N / fs);
            for (int k = k0; k < N / 2; ++k) e += buf[(size_t) k] * buf[(size_t) k];
            for (int k = k0; k < N / 2; ++k) sum[(size_t) k] += buf[(size_t) k] * buf[(size_t) k] / e;     // every S counts the same
            ++used;
        }
        if (used < 8) return out;
        out.freq = std::round (centreOf (sum, fs / N) / 50.0f) * 50.0f;
        if (out.freq < 3000.0f) return out;
        p.freq = out.freq;
    }
    else out.freq = (float) p.freq;

    Bands bands; bands.prepare (fs); bands.setRange (bandLow (p.freq), bandHigh (p.freq));
    Judge judge; judge.prepare (bands.hopLength() / fs);
    p.amount = 0;
    double best = -200; int len = 0; bool first = true;      // the first S comes before the singer's level is known
    auto close = [&] { if (len * bands.hopLength() / fs > 0.02) { if (first && ! p.fixedLine) first = false; else out.levels.push_back ((float) best); } best = -200; len = 0; };
    for (size_t i = 0; i < x.size(); ++i)
        if (bands.push (x[i], 0, false))
        {
            judge.step (bands.sDb, bands.bDb, p);
            if (judge.sib > 0.5) { ++len; best = std::max (best, bands.sDb - judge.ref); if (len * bands.hopLength() / fs > 0.25) close(); }      // cymbals never stop: count them in quarter seconds
            else if (len > 0) close();
        }
    if (len > 0) close();
    std::sort (out.levels.begin(), out.levels.end());
    out.ok = out.levels.size() >= 2;
    return out;
}

float deessp::lineFor (const Found& f, float wanted, float amount)
{
    const float o = wanted / juce::jmax (0.05f, amount), over = o >= 3.0f ? o : std::sqrt (12.0f * o) - 3.0f;     // the knee, backwards
    return juce::jlimit (-24.0f, 6.0f, std::round ((f.sharp() - over) * 10.0f) / 10.0f);
}

juce::AudioProcessorEditor* DeessProcessor::createEditor() { return new DeessEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DeessProcessor(); }
