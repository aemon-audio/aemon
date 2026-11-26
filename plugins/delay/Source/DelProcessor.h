// Aemon Delay. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "DelEngine.h"

namespace delp
{
    constexpr int numDivisions = 14;
    constexpr const char* divisionNames[] = { "Free", "1/32", "1/16 triplet", "1/16", "1/16 dotted", "1/8 triplet", "1/8", "1/8 dotted", "1/4 triplet", "1/4", "1/4 dotted", "1/2", "1/2 dotted", "1 bar" };
    constexpr double divisionBeats[] = { 0, 0.125, 1.0 / 6, 0.25, 0.375, 1.0 / 3, 0.5, 0.75, 2.0 / 3, 1, 1.5, 2, 3, 4 };
}

class DelProcessor : public aemon::EffectProcessor
{
public:
    DelProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::del::Params readParams() const;
    double getTailLengthSeconds() const override { return juce::jmin (60.0, aemon::del::Engine::tailSeconds (readParams())); }
    int popDel (aemon::del::Engine::Frame* dest, int max) noexcept;
    std::atomic<double> bpm { 120.0 };

protected:
    void prepareEngine (double fs) override { engine->setParams (readParams()); engine->prepare (fs); }
    bool wouldChangeLatency() const override { return false; }
    void applyParameters (bool) override { engine->setParams (readParams()); }
    int engineLatency() const override { return 0; }
    void processEngine (float* L, float* R, int n) override;
    float frameAux() override;

private:
    static std::vector<aemon::Preset> factoryPresets();
    std::unique_ptr<aemon::del::Engine> engine = std::make_unique<aemon::del::Engine>();
    juce::AbstractFifo fifo { 512 };
    std::vector<aemon::del::Engine::Frame> frames = std::vector<aemon::del::Engine::Frame> (512);
};
