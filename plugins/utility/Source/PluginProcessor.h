// Aemon Utility. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "UtilityEngine.h"

class UtilityProcessor : public juce::AudioProcessor
{
public:
    UtilityProcessor();
    ~UtilityProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Aemon Utility"; }
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
    aemon::utility::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    aemon::PluginState pluginState;

    // metering, read by the editor
    aemon::StereoLevelTap inTap, outTap;
    aemon::LoudnessMeter loudIn, loudOut;
    aemon::ScopeFifo scope;
    std::atomic<double> currentRate { 48000.0 };

private:
    static std::vector<aemon::Preset> factoryPresets();
    aemon::utility::Engine engine;
    juce::AudioParameterBool* bypassParam = nullptr;
    juce::AudioBuffer<float> work;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UtilityProcessor)
};
