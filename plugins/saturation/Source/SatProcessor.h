// Aemon Saturation. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "SatEngine.h"

namespace satp
{
    struct Char { const char* name; const char* help; };
    constexpr Char chars[] = { { "Warm", "Warm: gentle, mostly even harmonics. Rounds the top, fills the body." }, { "Thick", "Thick: odd harmonics from the low end. Weight on small speakers." },
                               { "Bright", "Bright: drives the top harder. Presence and air without an EQ boost." }, { "Broken", "Broken: folds back on itself. Grit, fuzz and attitude." } };
    /** Harmonics 1 to 9 of a 1 kHz tone at -12 dB, in dB relative to the tone, with everything around the curve included. */
    std::array<float, 9> harmonics (const aemon::sat::Params&);
    /** Total harmonics in percent for that tone. */
    float thd (const std::array<float, 9>&);
    /** Drive that adds about the wanted percentage of harmonics, with the other settings as they are. */
    float driveFor (aemon::sat::Params, float percent);
}

class SatProcessor : public aemon::EffectProcessor
{
public:
    SatProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::sat::Params readParams() const;
    aemon::sat::Curve curve() const { aemon::sat::Curve c; const auto p = readParams(); c.set (p.character, p.driveDb, p.bias); return c; }
    std::atomic<float> matchDb { 0.0f };

protected:
    void prepareEngine (double fs) override { active = readParams(); engine.setParams (active); engine.prepare (fs); }
    bool wouldChangeLatency() const override { return engine.changesLatency (readParams()); }
    void applyParameters (bool withLatency) override { auto p = readParams(); if (! withLatency) p.oversampling = active.oversampling; engine.setParams (p); active = p; }
    int engineLatency() const override { return engine.latencySamples(); }
    void processEngine (float* L, float* R, int n) override { engine.process (L, R, n); }
    float frameAux() override { matchDb = (float) engine.matchDb(); return 0.0f; }

private:
    static std::vector<aemon::Preset> factoryPresets();
    aemon::sat::Engine engine;
    aemon::sat::Params active;
};
