// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

juce::String EffectProcessor::dbText (float v, int decimals)
{
    auto s = decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v));
    if (s.getDoubleValue() == 0.0) s = s.replace ("-", "");
    return s.replace ("-", juce::String::charToString (0x2212)) + " dB";
}

juce::NormalisableRange<float> EffectProcessor::logRange (float lo, float hi, float step)
{
    return { lo, hi, [] (float s, float e, float x) { return s * std::pow (e / s, x); },
             [] (float s, float e, float y) { return std::log (y / s) / std::log (e / s); },
             [step] (float s, float e, float y) { return juce::jlimit (s, e, std::round (y / step) * step); } };
}

juce::AudioProcessor::BusesProperties EffectProcessor::buses (bool withSidechain)
{
    auto b = BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    return withSidechain ? b.withInput ("Sidechain", juce::AudioChannelSet::stereo(), false) : b;
}

EffectProcessor::EffectProcessor (const juce::String& t, const juce::Identifier& treeType, juce::AudioProcessorValueTreeState::ParameterLayout layout, std::vector<Preset> presets, bool withSidechain)
    : AudioProcessor (buses (withSidechain)),
      title (t), apvts (*this, nullptr, treeType, std::move (layout)), pluginState (*this, apvts, t, std::move (presets))
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
}

bool EffectProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (l.inputBuses.size() > 1) { const auto sc = l.getChannelSet (true, 1); if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false; }
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void EffectProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    prepareEngine (sampleRate);
    latencyNow = engineLatency();
    setLatencySamples (engineLatency());
    work.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    loudIn.prepare (sampleRate, 2); loudOut.prepare (sampleRate, 2);
    fade = Fade::none; fadePos = fadeWait = 0;
    frameLen = juce::jmax (1, juce::roundToInt (sampleRate * 0.016));
    frameCount = 0; accRaw = accOut = 0;
}

void EffectProcessor::applyFade (float* L, float* R, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float g = 1.0f;
        switch (fade)
        {
            case Fade::none: return;
            case Fade::out: g = 1.0f - (float) ++fadePos / (float) fadeLen; if (fadePos >= fadeLen) fade = Fade::muted; break;
            case Fade::muted: g = 0.0f; break;
            case Fade::waiting: g = 0.0f; if (--fadeWait <= 0) { fade = Fade::in; fadePos = 0; } break;
            case Fade::in: g = (float) ++fadePos / (float) fadeLen; if (fadePos >= fadeLen) fade = Fade::none; break;
        }
        L[i] *= g; R[i] *= g;
    }
}

void EffectProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const bool hasSide = getBusCount (true) > 1;
    const int n = buffer.getNumSamples(), ch = juce::jmin (2, buffer.getNumChannels(), hasSide ? getMainBusNumInputChannels() : 2);
    if (n == 0 || ch == 0) return;
    if (work.getNumSamples() < n) work.setSize (2, n, false, false, true);
    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    juce::FloatVectorOperations::copy (L, buffer.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (R, ch >= 2 ? buffer.getReadPointer (1) : L, n);
    const float* in[2] { L, R };
    loudIn.process (in, ch, n);
    ringIn.push (L, R, n);

    // a change of latency restarts the delay lines: fade out, change, fade in
    if (wouldChangeLatency())
    {
        if (fade == Fade::muted)
        {
            applyParameters (true);
            latencyNow = engineLatency();
            setLatencySamples (engineLatency());
            fade = Fade::waiting; fadeWait = engineLatency() + 1;
        }
        else
        {
            if (fade != Fade::out) { fadePos = fade == Fade::in ? fadeLen - fadePos : 0; fade = Fade::out; }
            applyParameters (false);
        }
    }
    else
    {
        applyParameters (true);
        if (fade == Fade::out || fade == Fade::muted) { fadePos = fade == Fade::out ? fadeLen - fadePos : 0; fade = Fade::in; }
    }

    const float* sc[2] { nullptr, nullptr };
    if (hasSide)
    {
        auto* bus = getBus (true, 1);
        const int c = bus != nullptr && bus->isEnabled() ? bus->getNumberOfChannels() : 0;
        if (c > 0 && buffer.getNumChannels() >= ch + c) { sc[0] = buffer.getReadPointer (ch); sc[1] = buffer.getReadPointer (ch + (c > 1 ? 1 : 0)); }
        sidechainConnected = sc[0] != nullptr;
    }
    const float gain = inputGain();
    for (int start = 0; start < n;)
    {
        const int len = juce::jmin (n - start, frameLen - frameCount);
        for (int i = start; i < start + len; ++i) accRaw = std::max (accRaw, std::max (std::abs (L[i]), std::abs (R[i])));
        sideL = sc[0] != nullptr ? sc[0] + start : nullptr; sideR = sc[1] != nullptr ? sc[1] + start : nullptr;
        processEngine (L + start, R + start, len);
        for (int i = start; i < start + len; ++i) accOut = std::max (accOut, std::max (std::abs (L[i]), std::abs (R[i])));
        frameCount += len; start += len;
        if (frameCount >= frameLen)
        {
            const MeterFrame f { juce::Decibels::gainToDecibels (accRaw, -120.0f), juce::Decibels::gainToDecibels (accRaw * gain, -120.0f), juce::Decibels::gainToDecibels (accOut, -120.0f), frameAux() };
            frameFifo.write (1).forEach ([&] (int idx) { frameBuf[(size_t) idx] = f; });
            frameCount = 0; accRaw = accOut = 0;
        }
    }
    applyFade (L, R, n);
    const float* out[2] { L, R };
    loudOut.process (out, ch, n);
    afterEngine (L, R, n);
    buffer.copyFrom (0, 0, L, n);
    if (buffer.getNumChannels() >= 2 && (! hasSide || ch >= 2)) buffer.copyFrom (1, 0, R, n);
}

int EffectProcessor::popFrames (MeterFrame* dest, int max) noexcept
{
    int n = 0;
    frameFifo.read (juce::jmin (max, frameFifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frameBuf[(size_t) i]; });
    return n;
}

void EffectProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    pluginState.writeTo (st);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void EffectProcessor::setStateInformation (const void* data, int size)
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

} // namespace aemon
