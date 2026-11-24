// Aemon Limiter. AGPL-3.0-or-later.
#include "LimProcessor.h"
#include "LimEditor.h"

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
    constexpr double recentSeconds = 6.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout LimProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const int v = 1;
    auto dB = [] (float x, int) { return minus (String (x, 1)) + " dB"; };
    auto ms = [] (float x, int) { return fmtMs (x); };
    StringArray targetNames;
    for (auto& t : limp::targets) targetNames.add (t.name);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "push", v }, "Push", NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "ceil", v }, "Ceiling", NormalisableRange<float> (-6.0f, 0.0f, 0.1f), -1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (dB)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "rel", v }, "Release", logRange (5.0f, 1000.0f, 1.0f), 80.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "autoRel", v }, "Auto Release", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "look", v }, "Lookahead", NormalisableRange<float> (0.1f, 10.0f, 0.1f), 1.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "link", v }, "Stereo Link", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "tp", v }, "True Peak", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", v }, "Oversampling", StringArray { "Off", "2x", "4x", "8x" }, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dither", v }, "Dither", StringArray { "Off", "16-bit", "24-bit" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "target", v }, "Loudness Target", targetNames, 0,
        AudioParameterChoiceAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "match", v }, "Equal Loudness", false, AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "delta", v }, "Hear Removed", false, AudioParameterBoolAttributes().withAutomatable (false)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", v }, "Bypass", false));
    return l;
}

std::vector<aemon::Preset> LimProcessor::factoryPresets()
{
    std::vector<aemon::Preset> v;
    using P = std::pair<juce::String, float>;
    auto add = [&v] (const char* cat, const char* name, const char* desc, std::vector<P> vals)
    {
        aemon::Preset p; p.name = juce::String::fromUTF8 (name); p.category = cat; p.description = juce::String::fromUTF8 (desc); p.values = std::move (vals); v.push_back (std::move (p));
    };
    constexpr float clean = 80, punchy = 30, dense = 200, safe = 400;
    add ("", "Safety net only: catch stray peaks", "Does nothing until a peak would pass \xe2\x88\x92""1 dBTP, then catches it. Your mix keeps its dynamics.", {});
    add ("Loudness targets", "As loud as it gets, cleanly", "Pushes until the limiter starts to audibly hurt the sound, then stops just before. Peaks below \xe2\x88\x92""1 dBTP.", { { "push", 8 }, { "target", 3 } });
    add ("Loudness targets", "Streaming-ready (\xe2\x88\x92""14 LUFS)", "The level Spotify, YouTube and Tidal play at. Nothing gets turned down, nothing gets squashed.", { { "push", 4 }, { "target", 0 } });
    add ("Loudness targets", "Apple Music (\xe2\x88\x92""16 LUFS)", "Apple Music plays at \xe2\x88\x92""16 LUFS. More dynamic, a little quieter.", { { "push", 2 }, { "target", 4 } });
    add ("Loudness targets", "Loud streaming (\xe2\x88\x92""11 LUFS, safe peaks)", "Louder than the streaming norm for a modern sound, still clean. Will be turned down a little on Spotify.", { { "push", 7 }, { "rel", dense }, { "target", 5 } });
    add ("Loudness targets", "Club master: loud and punchy (\xe2\x88\x92""8 LUFS)", "Loud for DJs and club systems, keeping the kick and snare hitting.", { { "push", 10 }, { "rel", punchy }, { "target", 6 } });
    add ("Loudness targets", "Broadcast-safe EBU R128 (\xe2\x88\x92""23 LUFS)", "The TV and radio standard: \xe2\x88\x92""23 LUFS, peaks below \xe2\x88\x92""1 dBTP.", { { "target", 2 } });
    add ("Loudness targets", "Podcast and voice (\xe2\x88\x92""16 LUFS)", "Clear, even speech at the level podcast apps expect.", { { "push", 6 }, { "rel", 150 }, { "target", 4 } });
    add ("Gentle", "Gentle: 1 to 2 dB of limiting", "Just enough to add a little loudness without anyone noticing.", { { "push", 2 }, { "rel", clean } });
    add ("Gentle", "Transparent master, no colour", "Clean limiting for mastering, as invisible as possible.", { { "push", 4 }, { "rel", 120 } });
    add ("Gentle", "Keep the punch (drum-heavy music)", "Fast recovery so drums keep their attack while the level goes up.", { { "push", 5 }, { "rel", 25 } });
    add ("Mix bus", "Mix bus: catch peaks before the master", "Sits on the mix bus to stop occasional overs. Leave the real limiting for the master.", { { "push", 1 }, { "ceil", -3 }, { "rel", safe } });
    add ("Mix bus", "Drum bus: tame transients", "Shaves the very tops off drum hits so they sit better in the mix.", { { "push", 4 }, { "ceil", -3 }, { "rel", 20 } });
    add ("Mix bus", "Vocal: stop the loudest words jumping out", "Catches only the loudest syllables, after your compressor.", { { "push", 3 }, { "ceil", -6 }, { "rel", 60 } });
    add ("Creative", "Brickwall squash (effect)", "Deliberately over-limited for an aggressive, pumping effect. Not for a clean master.", { { "push", 18 }, { "rel", 10 }, { "autoRel", 0 } });
    return v;
}

LimProcessor::LimProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AemonLimiter", createLayout()),
      pluginState (*this, apvts, "Limiter", factoryPresets())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool LimProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

aemon::lim::Params LimProcessor::readParams() const
{
    auto f = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    aemon::lim::Params p;
    p.pushDb = f ("push"); p.ceilingDb = f ("ceil"); p.releaseMs = f ("rel"); p.autoRelease = f ("autoRel") > 0.5f;
    p.lookaheadMs = f ("look"); p.link = f ("link") / 100.0;
    p.truePeak = f ("tp") > 0.5f;
    p.oversampling = 1 << juce::jlimit (0, 3, juce::roundToInt (f ("os")));
    const int d = juce::roundToInt (f ("dither"));
    p.ditherBits = d == 1 ? 16 : (d == 2 ? 24 : 0);
    p.delta = f ("delta") > 0.5f;
    p.bypass = f ("bypass") > 0.5f;
    return p;
}

void LimProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    active = readParams();
    engine.setParams (active);
    engine.prepare (sampleRate);
    latencyNow = engine.latencySamples();
    setLatencySamples (engine.latencySamples());
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    loudIn.prepare (sampleRate, 2); loudOut.prepare (sampleRate, 2);
    ring.setSize (2, juce::jmax (1, juce::roundToInt (sampleRate * recentSeconds)), false, true, true);
    ringPos = 0; ringFilled = 0;
    fade = Fade::none; fadePos = fadeWait = 0;
    matchGain = matchTarget = 1.0f;
    matchCoef = 1.0f - (float) std::exp (-1.0 / (0.08 * sampleRate));
    frameLen = juce::jmax (1, juce::roundToInt (sampleRate * 0.016));
    frameCount = 0; accRaw = accOut = accGr = 0;
}

void LimProcessor::applyFade (float* L, float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float g = 1.0f;
        switch (fade)
        {
            case Fade::none: return;
            case Fade::out: g = 1.0f - (float) ++fadePos / (float) fadeLen; if (fadePos >= fadeLen) { fade = Fade::muted; } break;
            case Fade::muted: g = 0.0f; break;
            case Fade::waiting: g = 0.0f; if (--fadeWait <= 0) { fade = Fade::in; fadePos = 0; } break;
            case Fade::in: g = (float) ++fadePos / (float) fadeLen; if (fadePos >= fadeLen) fade = Fade::none; break;
        }
        L[i] *= g; R[i] *= g;
    }
}

void LimProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), ch = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || ch == 0) return;
    if (work.getNumSamples() < n) work.setSize (2, n, false, false, true);
    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (R, ch >= 2 ? buffer.getReadPointer (1) : L, n);

    const float* in[2] { L, R };
    loudIn.process (in, ch, n);
    {
        const int size = ring.getNumSamples();
        int w = ringPos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { ring.setSample (0, w, L[i]); ring.setSample (1, w, R[i]); if (++w >= size) w = 0; }
        ringPos.store (w, std::memory_order_release);
        ringFilled.store (juce::jmin (size, ringFilled.load (std::memory_order_relaxed) + n), std::memory_order_release);
    }

    // parameters; a different lookahead waits for the output to be faded out
    auto want = readParams();
    if (engine.changesLatency (want))
    {
        if (fade == Fade::muted)
        {
            engine.setParams (want); active = want;
            latencyNow = engine.latencySamples();
            setLatencySamples (engine.latencySamples());
            fade = Fade::waiting; fadeWait = engine.latencySamples() + 1;
        }
        else
        {
            if (fade != Fade::out) { fadePos = fade == Fade::in ? fadeLen - fadePos : 0; fade = Fade::out; }
            want.lookaheadMs = active.lookaheadMs;
            engine.setParams (want); active = want;
        }
    }
    else
    {
        engine.setParams (want); active = want;
        if (fade == Fade::out || fade == Fade::muted) { fadePos = fade == Fade::out ? fadeLen - fadePos : 0; fade = Fade::in; }   // changed back in time
    }

    const float push = juce::Decibels::decibelsToGain ((float) want.pushDb);
    float deepest = 0.0f;
    for (int start = 0; start < n;)
    {
        const int len = juce::jmin (n - start, frameLen - frameCount, 256);
        for (int i = start; i < start + len; ++i) accRaw = std::max (accRaw, std::max (std::abs (L[i]), std::abs (R[i])));
        engine.process (L + start, R + start, len);
        const float gr = (float) engine.lastBlockDeepestDb();
        deepest = std::min (deepest, gr);
        accGr = std::min (accGr, gr);
        for (int i = start; i < start + len; ++i) accOut = std::max (accOut, std::max (std::abs (L[i]), std::abs (R[i])));
        frameCount += len; start += len;
        if (frameCount >= frameLen)
        {
            const limp::Frame f { juce::Decibels::gainToDecibels (accRaw * (want.bypass ? 1.0f : push), -120.0f), juce::Decibels::gainToDecibels (accOut, -120.0f),
                                  want.bypass ? 0.0f : accGr, juce::Decibels::gainToDecibels (accRaw, -120.0f) };
            frameFifo.write (1).forEach ([&] (int idx) { frameBuf[(size_t) idx] = f; });
            frameCount = 0; accRaw = accOut = accGr = 0;
        }
    }
    grNow = want.bypass ? 0.0f : deepest;
    applyFade (L, R, n);
    const float* out[2] { L, R };
    loudOut.process (out, ch, n);

    // equal loudness: the limited sound is turned down to the level of the original, only while listening
    // (never in an export), and never while bypassed, so bypass is the untouched original at the same loudness
    const bool match = apvts.getRawParameterValue ("match")->load() > 0.5f && ! isNonRealtime() && ! want.delta;
    if (! match) matchTarget = 1.0f;
    else if (want.bypass) matchTarget = 1.0f;
    else
    {
        const float li = loudIn.getShortTerm(), lo = loudOut.getShortTerm();
        if (std::isfinite (li) && std::isfinite (lo) && li > -60.0f && lo > -60.0f)
            matchTarget = juce::Decibels::decibelsToGain (juce::jlimit (-30.0f, 0.0f, li - lo));
    }
    if (matchGain != matchTarget || matchGain != 1.0f)
    {
        for (int i = 0; i < n; ++i)
        {
            matchGain += matchCoef * (matchTarget - matchGain);
            L[i] *= matchGain; R[i] *= matchGain;
        }
        if (std::abs (matchGain - matchTarget) < 1.0e-5f) matchGain = matchTarget;
    }
    matchNowDb = juce::Decibels::gainToDecibels (matchGain);

    buffer.copyFrom (0, 0, L, n);
    if (buffer.getNumChannels() >= 2) buffer.copyFrom (1, 0, R, n);
}

juce::AudioBuffer<float> LimProcessor::recentInput() const
{
    const int size = ring.getNumSamples(), filled = ringFilled.load (std::memory_order_acquire), w = ringPos.load (std::memory_order_acquire);
    juce::AudioBuffer<float> b (2, juce::jmax (1, filled));
    b.clear();
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < filled; ++i) b.setSample (c, i, ring.getSample (c, (w - filled + i + size) % size));
    return b;
}

//==============================================================================
limp::LoudPlan limp::planLoudness (const juce::AudioBuffer<float>& recent, double sampleRate, aemon::lim::Params base, int target)
{
    LoudPlan plan;
    int n = recent.getNumSamples();
    const int wanted = juce::roundToInt (sampleRate * (sampleRate > 100000.0 ? 4.0 : 6.0));
    const int first = juce::jmax (0, n - wanted);
    n -= first;
    if (n < juce::roundToInt (sampleRate * 1.5)) return plan;

    base.bypass = false; base.delta = false; base.ditherBits = 0;
    if (base.truePeak) base.oversampling = juce::jmin (base.oversampling, 2);   // a measurement: the output check runs anyway
    struct Result { float lufs = -120, avg = 0, p95 = 0; };
    auto run = [&] (float push, bool quick)
    {
        auto p = base; p.pushDb = push;
        if (quick) { p.truePeak = false; p.oversampling = 1; }
        aemon::lim::Engine e; e.setParams (p); e.prepare (sampleRate);
        aemon::LoudnessMeter m; m.prepare (sampleRate, 2);
        juce::AudioBuffer<float> b (2, 512);
        std::vector<float> gr;
        for (int pos = 0; pos < n; pos += 512)
        {
            const int len = juce::jmin (512, n - pos);
            float pk = 0;
            for (int c = 0; c < 2; ++c) { b.copyFrom (c, 0, recent, c, first + pos, len); pk = std::max (pk, b.getMagnitude (c, 0, len)); }
            e.process (b.getWritePointer (0), b.getWritePointer (1), len);
            if (pk > 0.001f) gr.push_back (-(float) e.lastBlockDeepestDb());
            const float* ptr[2] { b.getReadPointer (0), b.getReadPointer (1) };
            m.process (ptr, 2, len);
        }
        Result r;
        r.lufs = m.getIntegrated();
        if (! std::isfinite (r.lufs)) r.lufs = m.getMomentary();
        if (! gr.empty())
        {
            double sum = 0; for (float g : gr) sum += g;
            r.avg = (float) (sum / (double) gr.size());
            std::sort (gr.begin(), gr.end());
            r.p95 = gr[(size_t) ((float) (gr.size() - 1) * 0.95f)];
        }
        return r;
    };

    const auto dry = run (0.0f, true);
    if (! std::isfinite (dry.lufs) || dry.lufs < -60.0f) return plan;
    const bool max = target == loudestClean;
    const float goal = targets[juce::jlimit (0, numTargets - 1, target)].lufs;
    const float avgLimit = max ? 2.5f : 4.0f, p95Limit = max ? 6.0f : 8.0f;
    plan.measured = true;
    if (! max && dry.lufs >= goal) { plan.overTarget = dry.lufs > goal + 1.0f; plan.push = 0.0f; plan.lufs = dry.lufs; plan.avgGr = dry.avg; plan.p95Gr = dry.p95; return plan; }

    float lo = 0.0f, hi = 24.0f;                       // the largest push that is safe and not past the goal
    auto fits = [&] (const Result& r) { return r.avg <= avgLimit && r.p95 <= p95Limit && (max || r.lufs <= goal + 0.05f); };
    for (int i = 0; i < 8; ++i)                        // search on sample peaks, which is quick
    {
        const float mid = 0.5f * (lo + hi);
        if (fits (run (mid, true))) lo = mid; else hi = mid;
    }
    plan.push = std::floor (lo * 10.0f) / 10.0f;
    auto best = run (plan.push, false);                // then confirm with the real settings
    for (int i = 0; i < 6 && plan.push > 0.0f && (best.avg > avgLimit || best.p95 > p95Limit); ++i)
    {
        // true peak works harder than the quick search assumed: step back by the excess
        const float excess = std::max (best.avg - avgLimit, best.p95 - p95Limit);
        plan.push = std::max (0.0f, std::floor ((plan.push - std::max (0.1f, excess)) * 10.0f) / 10.0f);
        best = run (plan.push, false);
    }
    plan.lufs = best.lufs; plan.avgGr = best.avg; plan.p95Gr = best.p95;
    if (! max) plan.shortBy = std::max (0.0f, goal - best.lufs);
    return plan;
}

void LimProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    pluginState.writeTo (st);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void LimProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (t);
            pluginState.readFrom (t);
            ++stateLoads;
        }
}

juce::AudioProcessorEditor* LimProcessor::createEditor() { return new LimEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LimProcessor(); }
