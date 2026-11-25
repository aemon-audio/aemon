// Aemon Clipper. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "ClipEngine.h"

namespace clipp
{
    /** Shapes set the softness; the highlighted one is read from it. */
    struct Shape { const char* name; float soft; const char* help; };
    constexpr Shape shapes[] = { { "Hard", 0.0f, "Hard: untouched right up to the ceiling, then cut." }, { "Round", 35.0f, "Round: a short bend before the cut." }, { "Soft", 100.0f, "Soft: bends early, the warmest." } };
    inline int matchingShape (float soft) { for (int i = 0; i < 3; ++i) if (std::abs (soft - shapes[i].soft) < 0.5f) return i; return -1; }

    /** How often the input has been how loud (louder side), quarter dB steps, forgetting over about 15 seconds. Audio thread writes, window reads. */
    class LevelHistory
    {
    public:
        static constexpr int bins = 400;        // -80 .. +20 dB
        static float dbOf (int bin) { return -80.0f + 0.25f * (float) (bin + 1); }     // upper edge
        void clear() { for (auto& b : count) b = 0.0f; }
        void push (const float* L, const float* R, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                const float m = std::max (std::abs (L[i]), std::abs (R[i]));
                if (m < 1.0e-4f) continue;
                const int k = juce::jlimit (0, bins - 1, (int) ((6.0206f * std::log2 (m) + 80.0f) * 4.0f));
                count[(size_t) k].store (count[(size_t) k].load (std::memory_order_relaxed) + 1.0f, std::memory_order_relaxed);
            }
        }
        void forget (float seconds) noexcept { const float g = std::exp (-seconds / 15.0f); for (auto& b : count) b.store (b.load (std::memory_order_relaxed) * g, std::memory_order_relaxed); }
        std::array<float, (size_t) bins> copy() const { std::array<float, (size_t) bins> c; for (size_t i = 0; i < c.size(); ++i) c[i] = count[i].load (std::memory_order_relaxed); return c; }
    private:
        std::array<std::atomic<float>, (size_t) bins> count {};
    };

    /** The most Push that takes at most 3 dB off the tallest peaks and touches at most 1 % of the sound. */
    struct Suggestion { bool ok = false; float push = 0, off = 0, percent = 0; };
    Suggestion suggest (const std::array<float, (size_t) LevelHistory::bins>&, float ceiling, float softness);
}

class ClipProcessor : public aemon::EffectProcessor
{
public:
    ClipProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::clip::Params readParams() const;
    /** Share of time that is clipped, percent, over about the last four seconds. */
    std::atomic<float> clippedPercent { 0.0f };
    aemon::LoudnessMatch match;
    clipp::LevelHistory levels;

protected:
    void prepareEngine (double fs) override { active = readParams(); engine.setParams (active); engine.prepare (fs); match.prepare (fs); levels.clear(); }
    bool wouldChangeLatency() const override { return engine.changesLatency (readParams()); }
    void applyParameters (bool withLatency) override { auto p = readParams(); if (! withLatency) p.oversampling = active.oversampling; engine.setParams (p); active = p; }
    int engineLatency() const override { return engine.latencySamples(); }
    void processEngine (float* L, float* R, int n) override { levels.push (L, R, n); engine.process (L, R, n); samples += n; }
    float frameAux() override;
    float inputGain() const override { return real ("bypass") > 0.5f ? 1.0f : juce::Decibels::decibelsToGain (real ("push")); }
    void afterEngine (float* L, float* R, int n) override { match.process (L, R, n, real ("match") > 0.5f && ! isNonRealtime() && real ("delta") < 0.5f, real ("bypass") > 0.5f, loudIn, loudOut); }

private:
    static std::vector<aemon::Preset> factoryPresets();
    aemon::clip::Engine engine;
    aemon::clip::Params active;
    int samples = 0;
};
