// Aemon Low End. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "LowMeasure.h"

namespace lowp
{
    /** How deep a factory preset ducks; the tests check it. */
    struct Intent { float duck = 0; bool clash = true; };
    Intent intentOf (const juce::String& presetName);
}

class LowProcessor : public aemon::EffectProcessor
{
public:
    LowProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::low::Params readParams() const;
    int popLow (aemon::low::Engine::Frame* dest, int max) noexcept;
    /** The last seconds of bass and kick around 3 kHz, as they came in. */
    aemon::low::Tracks tracks() const;

protected:
    void prepareEngine (double fs) override;
    bool wouldChangeLatency() const override { return false; }
    void applyParameters (bool) override { engine->setParams (readParams()); }
    int engineLatency() const override { return engine->latencySamples(); }
    void processEngine (float* L, float* R, int n) override;
    float frameAux() override;

private:
    static std::vector<aemon::Preset> factoryPresets();
    std::unique_ptr<aemon::low::Engine> engine = std::make_unique<aemon::low::Engine>();
    aemon::low::Tap tap;
    std::vector<float> ringB, ringK; std::atomic<int> pos { 0 }, filled { 0 }; juce::CriticalSection lock;
    juce::AbstractFifo fifo { 512 };
    std::vector<aemon::low::Engine::Frame> frames = std::vector<aemon::low::Engine::Frame> (512);
};
