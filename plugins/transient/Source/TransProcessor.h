// Aemon Transient. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "TransEngine.h"

namespace transp
{
    /** What a factory preset is for; the tests check it on that drum. */
    struct Intent { float attack = 0, body = 0; const char* drum = nullptr; int focus = 0; };
    Intent intentOf (const juce::String& presetName);
}

class TransProcessor : public aemon::EffectProcessor
{
public:
    TransProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::trans::Params readParams() const;
    /** Milliseconds of sound for the picture, oldest first. */
    int popPoints (aemon::trans::Engine::Point* dest, int max) noexcept;
    /** The loudest peak in and out of late, dB, and the furthest the gain went. Audio thread writes. */
    std::atomic<float> peakIn { -120.0f }, peakOut { -120.0f };
    aemon::LoudnessMatch match;

protected:
    void prepareEngine (double fs) override { engine->setParams (readParams()); engine->prepare (fs); match.prepare (fs); peakIn = peakOut = -120.0f; }
    bool wouldChangeLatency() const override { return false; }
    void applyParameters (bool) override { engine->setParams (readParams()); }
    int engineLatency() const override { return engine->latencySamples(); }
    void processEngine (float* L, float* R, int n) override;
    float frameAux() override;
    void afterEngine (float* L, float* R, int n) override { match.process (L, R, n, real ("match") > 0.5f && ! isNonRealtime() && real ("delta") < 0.5f, real ("bypass") > 0.5f, loudIn, loudOut, -12.0f, 12.0f); }

private:
    static std::vector<aemon::Preset> factoryPresets();
    std::unique_ptr<aemon::trans::Engine> engine = std::make_unique<aemon::trans::Engine>();
    juce::AbstractFifo fifo { 4096 };
    std::vector<aemon::trans::Engine::Point> points = std::vector<aemon::trans::Engine::Point> (4096);
};
