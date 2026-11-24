// Aemon Limiter. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "LimEngine.h"

namespace limp
{
    /** One display frame (about 16 ms) from the audio thread. rawDb is the input before Push. */
    struct Frame { float inDb, outDb, grDb, rawDb; };

    /** Styles set the release, the way the limiter recovers. The highlighted one is read from the settings. */
    struct Style { const char* name; float release; const char* help; };
    constexpr Style styles[] = {
        { "Clean", 80.0f, "Clean: even and hard to hear. For mastering." },
        { "Punchy", 30.0f, "Punchy: recovers fast, so drums keep their attack." },
        { "Dense", 200.0f, "Dense: holds the level firmly for loud, modern masters." },
        { "Safe", 400.0f, "Safe: slow and gentle, only catches what gets through." },
    };
    constexpr int numStyles = 4;
    inline int matchingStyle (float releaseMs)
    {
        for (int i = 0; i < numStyles; ++i) if (std::abs (releaseMs - styles[i].release) < 0.5f) return i;
        return -1;
    }

    /** Loudness targets. The first four are the Simple switch, the rest come from presets. */
    struct Target { const char* name; float lufs; };
    constexpr Target targets[] = { { "Streaming", -14.0f }, { "Club", -9.0f }, { "Broadcast", -23.0f }, { "Loudest clean", 0.0f },
                                   { "Apple Music", -16.0f }, { "Loud streaming", -11.0f }, { "Club, loud", -8.0f } };
    constexpr int numTargets = 7, loudestClean = 3;

    /** What "Make it loud safely" found. */
    struct LoudPlan
    {
        bool measured = false;         // false: not enough signal yet
        float push = 0.0f, lufs = 0.0f, avgGr = 0.0f, p95Gr = 0.0f;
        float shortBy = 0.0f;          // dB under the target because more would squash it
        bool overTarget = false;       // the track is already louder than the target without any push
    };
    /** Finds the push for a target by running the real engine on recent audio (stereo, before Push).
        Not for the audio thread. Stops where the limiting would start to hurt. */
    LoudPlan planLoudness (const juce::AudioBuffer<float>& recent, double sampleRate, aemon::lim::Params base, int target);
}

class LimProcessor : public juce::AudioProcessor
{
public:
    LimProcessor();
    ~LimProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Aemon Limiter"; }
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::lim::Params readParams() const;
    int target() const { return juce::jlimit (0, limp::numTargets - 1, juce::roundToInt (apvts.getRawParameterValue ("target")->load())); }

    /** The last seconds of input (before Push), oldest first. Message thread. */
    juce::AudioBuffer<float> recentInput() const;

    juce::AudioProcessorValueTreeState apvts;
    aemon::PluginState pluginState;
    aemon::LoudnessMeter loudIn, loudOut;    // out is measured before the equal-loudness trim
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> grNow { 0.0f }, matchNowDb { 0.0f };
    std::atomic<int> latencyNow { 0 }, stateLoads { 0 };

    int popFrames (limp::Frame* dest, int max) noexcept
    {
        int n = 0;
        frameFifo.read (juce::jmin (max, frameFifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frameBuf[(size_t) i]; });
        return n;
    }

private:
    static std::vector<aemon::Preset> factoryPresets();
    void applyFade (float* L, float* R, int n) noexcept;

    aemon::lim::Engine engine;
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> work;

    // a lookahead change restarts the delay lines: fade out, change, fade in
    enum class Fade { none, out, muted, waiting, in };
    Fade fade = Fade::none;
    int fadePos = 0, fadeWait = 0;
    static constexpr int fadeLen = 128;
    aemon::lim::Params active;

    float matchGain = 1.0f, matchTarget = 1.0f, matchCoef = 0.001f;

    juce::AudioBuffer<float> ring;           // recent input for the loudness helper
    std::atomic<int> ringPos { 0 }, ringFilled { 0 };

    juce::AbstractFifo frameFifo { 512 };
    std::array<limp::Frame, 512> frameBuf {};
    int frameCount = 0, frameLen = 768;
    float accRaw = 0, accOut = 0, accGr = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimProcessor)
};
