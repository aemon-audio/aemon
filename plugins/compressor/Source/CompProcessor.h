// Aemon Compressor. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "CompEngine.h"

namespace compp
{
    /** One display/meter frame from the audio thread. */
    struct Frame { float inDb, outDb, grDb, inRms, outRms; };

    /** Simple-mode styles: what each sets. */
    struct Style { const char* name; float ratio, attack, release, knee; bool autoRelease; float mix; const char* sub; };
    constexpr Style styles[] = {
        { "Gentle", 2.0f, 30.0f, 250.0f, 10.0f, false, 100.0f, "2:1  slow attack  smooth release" },
        { "Punchy", 4.0f, 25.0f, 90.0f, 4.0f, false, 100.0f, "4:1  transients through  tight body" },
        { "Glue", 2.0f, 10.0f, 300.0f, 8.0f, true, 100.0f, "2:1  soft knee  auto release" },
        { "Vocal", 3.0f, 4.0f, 120.0f, 6.0f, false, 100.0f, "3:1  catches words  smooth" },
        { "Smash", 10.0f, 1.0f, 60.0f, 2.0f, false, 55.0f, "10:1  fast  blended in parallel" },
    };
    constexpr int numStyles = 5;

    /** The style the current settings correspond to, or -1 (custom). Read from the parameters, so it is right
        after presets, undo, A/B, automation and session recall. */
    inline int matchingStyle (const juce::AudioProcessorValueTreeState& a)
    {
        auto get = [&a] (const char* id) { return a.getRawParameterValue (id)->load(); };
        auto same = [] (float x, float y) { return std::abs (x - y) <= 0.01f * std::max (1.0f, std::abs (y)); };
        for (int i = 0; i < numStyles; ++i)
        {
            const auto& s = styles[i];
            if (same (get ("ratio"), s.ratio) && same (get ("atk"), s.attack) && same (get ("rel"), s.release) && same (get ("knee"), s.knee)
                && (get ("autoRel") > 0.5f) == s.autoRelease && same (get ("mix"), s.mix)) return i;
        }
        return -1;
    }
}

class CompProcessor : public juce::AudioProcessor
{
public:
    CompProcessor();
    ~CompProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Aemon Compressor"; }
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
    aemon::comp::Params readParams() const;
    bool sidechainConnected() const { return scConnected.load(); }

    juce::AudioProcessorValueTreeState apvts;
    aemon::PluginState pluginState;

    aemon::StereoLevelTap inTap, outTap;
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> grNow { 0.0f }, makeupNow { 0.0f };

    /** Frames for the history display (audio thread writes about every 5 ms). */
    int popFrames (compp::Frame* dest, int max) noexcept
    {
        int n = 0;
        frameFifo.read (juce::jmin (max, frameFifo.getNumReady())).forEach ([&] (int i) { dest[n++] = frameBuf[(size_t) i]; });
        return n;
    }

private:
    static std::vector<aemon::Preset> factoryPresets();
    aemon::comp::Engine engine;
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> work, side;
    std::atomic<bool> scConnected { false };
    int lastLatency = -1;

    juce::AbstractFifo frameFifo { 1024 };
    std::array<compp::Frame, 1024> frameBuf {};
    int frameCount = 0, frameLen = 240;
    float accIn = 0, accOut = 0, accGr = 0;
    double accInSq = 0, accOutSq = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompProcessor)
};
