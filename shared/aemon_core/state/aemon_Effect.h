// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Display frame, about 16 ms. rawDb is the input before any gain of the plugin. */
struct MeterFrame { float rawDb = -120, inDb = -120, outDb = -120, aux = 0; };

/** Turns the processed sound down to the loudness of the original while listening (never in an export). */
class LoudnessMatch
{
public:
    void prepare (double fs) { coef = 1.0f - (float) std::exp (-1.0 / (0.08 * fs)); gain = target = 1.0f; }
    void process (float* L, float* R, int n, bool on, bool bypassed, const LoudnessMeter& in, const LoudnessMeter& out, float minDb = -30.0f, float maxDb = 0.0f) noexcept
    {
        if (! on || bypassed) target = 1.0f;
        else
        {
            const float li = in.getShortTerm(), lo = out.getShortTerm();
            if (std::isfinite (li) && std::isfinite (lo) && li > -60.0f && lo > -60.0f) target = juce::Decibels::decibelsToGain (juce::jlimit (minDb, maxDb, li - lo));
        }
        if (gain == 1.0f && target == 1.0f) { nowDb = 0.0f; return; }
        for (int i = 0; i < n; ++i) { gain += coef * (target - gain); L[i] *= gain; R[i] *= gain; }
        if (std::abs (gain - target) < 1.0e-5f) gain = target;
        nowDb = juce::Decibels::gainToDecibels (gain);
    }
    std::atomic<float> nowDb { 0.0f };
private:
    float gain = 1.0f, target = 1.0f, coef = 0.001f;
};

/** Everything an Aemon effect needs besides its engine (with an optional side-chain input): parameters, presets, state, buses, stereo
    working buffers, loudness in and out, display frames, and a fade around changes that alter the latency. */
class EffectProcessor : public juce::AudioProcessor
{
public:
    EffectProcessor (const juce::String& title, const juce::Identifier& treeType, juce::AudioProcessorValueTreeState::ParameterLayout layout, std::vector<Preset> presets, bool withSidechain = false);

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Aemon " + title; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    float real (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    int popFrames (MeterFrame* dest, int max) noexcept;
    /** Makes a parameter's value text; used by the layouts. */
    static juce::String dbText (float v, int decimals = 1);
    static juce::NormalisableRange<float> logRange (float lo, float hi, float step);

    const juce::String title;
    juce::AudioProcessorValueTreeState apvts;
    PluginState pluginState;
    LoudnessMeter loudIn, loudOut;
    MonoRing ringIn;
    std::atomic<bool> sidechainConnected { false };
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<int> latencyNow { 0 }, stateLoads { 0 };

protected:
    // what a plugin provides
    virtual void prepareEngine (double fs) = 0;                 // read parameters, prepare, reset
    virtual bool wouldChangeLatency() const = 0;                // with the current parameters
    virtual void applyParameters (bool includingLatency) = 0;   // to the engine
    virtual int engineLatency() const = 0;
    virtual void processEngine (float* L, float* R, int n) = 0;
    virtual float frameAux() { return 0.0f; }                   // read once per display frame, audio thread
    virtual float inputGain() const { return 1.0f; }            // for the display's "in" level
    virtual void afterEngine (float*, float*, int) {}           // e.g. loudness matching
    /** The side-chain for the samples handed to processEngine, or null. */
    const float* sideL = nullptr; const float* sideR = nullptr;

private:
    void applyFade (float* L, float* R, int n) noexcept;
    juce::AudioParameterBool* bypassParam = nullptr;
    static BusesProperties buses (bool withSidechain);
    juce::AudioBuffer<float> work;
    enum class Fade { none, out, muted, waiting, in };
    Fade fade = Fade::none;
    int fadePos = 0, fadeWait = 0;
    static constexpr int fadeLen = 128;
    juce::AbstractFifo frameFifo { 512 };
    std::array<MeterFrame, 512> frameBuf {};
    int frameCount = 0, frameLen = 768;
    float accRaw = 0, accOut = 0;
};

} // namespace aemon
