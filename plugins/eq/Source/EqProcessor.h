// Aemon EQ. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "EqEngine.h"
#include "LinearPhase.h"

namespace eqp
{
    constexpr int numBands = aemon::eq::maxBands;
    inline juce::String id (int band, const char* what) { return "b" + juce::String (band + 1) + what; }
    enum class Phase { zeroLatency = 0, linearMedium, linearHigh };

    /** Simple-mode controls live in fixed bands. */
    enum Role { lowCut = 0, bass, mud, presence, air, numRoles };
    struct RoleDefault { aemon::eq::Type type; float freq, q; int slopeIndex; };
    constexpr RoleDefault roleDefaults[numRoles] = {
        { aemon::eq::Type::lowCut, 20.0f, 0.71f, 1 },
        { aemon::eq::Type::lowShelf, 110.0f, 0.7f, 0 },
        { aemon::eq::Type::bell, 320.0f, 1.2f, 0 },
        { aemon::eq::Type::bell, 3200.0f, 0.9f, 0 },
        { aemon::eq::Type::highShelf, 11000.0f, 0.7f, 0 },
    };
    constexpr int slopes[4] = { 12, 24, 48, 96 };
}

class EqProcessor : public juce::AudioProcessor,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::Thread
{
public:
    EqProcessor();
    ~EqProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Aemon EQ"; }
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
    aemon::eq::BandParams bandParams (int band) const;
    std::array<aemon::eq::BandParams, eqp::numBands> allBands() const;
    eqp::Phase phase() const { return (eqp::Phase) juce::roundToInt (apvts.getRawParameterValue ("phase")->load()); }
    /** Loudness-weighted average of the curve, used by Auto gain (dB to add). */
    static float autoGainFor (const std::array<aemon::eq::BandParams, eqp::numBands>& bands);

    juce::AudioProcessorValueTreeState apvts;
    aemon::PluginState pluginState;

    aemon::MonoRing preRing, postRing;
    aemon::StereoLevelTap outTap;
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> autoGainDb { 0.0f };

private:
    static std::vector<aemon::Preset> factoryPresets();
    void parameterChanged (const juce::String&, float) override { ++paramVersion; notify(); }
    void run() override;                     // background: linear-phase kernels and auto gain
    void rebuild();

    aemon::eq::Engine zeroLatency;
    aemon::eq::LinearPhaseEngine linearMedium, linearHigh;
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> work, dryDelay;
    int dryWrite = 0, activeLatency = 0;
    eqp::Phase activePhase = eqp::Phase::zeroLatency;
    juce::SmoothedValue<float> wetMix { 1.0f };
    std::atomic<int> paramVersion { 0 };
    int builtVersion = -1;
    std::atomic<bool> prepared { false };
    juce::CriticalSection buildLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqProcessor)
};
