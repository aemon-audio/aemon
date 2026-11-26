// Aemon Reverb. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "RevEngine.h"
#include <complex>

namespace revp
{
    constexpr const char* spaceNames[] = { "Room", "Plate", "Hall", "Chamber", "Lo-fi", "Endless" };
    constexpr const char* spaceHelps[] = { "Room: close walls, quick and natural.", "Plate: dense at once, smooth. Made for voices.", "Hall: opens slowly, far walls.", "Chamber: dark and warm.", "Lo-fi: worn, wobbling, narrow at the top.", "Endless: a cloud that hardly ends." };
    /** Pre-delay on the beat: none, 1/64, 1/32, 1/16, dotted 1/16, 1/8. In quarter notes. */
    constexpr double syncBeats[] = { 0, 0.0625, 0.125, 0.25, 0.375, 0.5 };
    /** Seconds for 60 dB at a frequency, for the picture. */
    double decayAt (const aemon::rev::Params&, double hz);
    struct Facts { juce::String preset; bool send = false; };
}

class RevProcessor : public aemon::EffectProcessor
{
public:
    RevProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::rev::Params readParams() const;
    double getTailLengthSeconds() const override { return juce::jmin (60.0, aemon::rev::Engine::tailSeconds (readParams())); }
    int popRev (aemon::rev::Engine::Frame* dest, int max) noexcept;
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
    std::unique_ptr<aemon::rev::Engine> engine = std::make_unique<aemon::rev::Engine>();
    juce::AbstractFifo fifo { 512 };
    std::vector<aemon::rev::Engine::Frame> frames = std::vector<aemon::rev::Engine::Frame> (512);
};
