// Aemon De-esser. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "DeessEngine.h"

namespace deessp
{
    constexpr const char* shapeNames[] = { "Above", "Just S", "Whole" };
    constexpr const char* shapeHelps[] = { "Everything above: a smooth cut from the S upwards. The most natural.", "Just the S: only the S region dips, the air above stays.", "Whole voice: the voice dips as one. Never changes the tone." };

    /** The last seconds of the input, for Find my S. Audio thread writes. */
    class Recent
    {
    public:
        void prepare (double fs) { const juce::ScopedLock l (lock); buf.assign ((size_t) (fs * 12.0), 0.0f); rate = fs; pos = 0; filled = 0; }
        void push (const float* L, const float* R, int n, int side) noexcept
        {
            const int size = (int) buf.size(); if (size == 0) return;
            int w = pos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) { buf[(size_t) w] = 0.5f * (side == 2 ? L[i] - R[i] : L[i] + R[i]); if (++w == size) w = 0; }
            pos.store (w, std::memory_order_release);
            filled = std::min (size, filled.load() + n);
        }
        std::vector<float> copy (double& fs) const
        {
            const juce::ScopedLock l (lock); fs = rate;
            const int size = (int) buf.size(), n = std::min (size, filled.load()), w = pos.load (std::memory_order_acquire);
            std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = buf[(size_t) ((w - n + i + size) % size)];
            return v;
        }
    private:
        std::vector<float> buf; double rate = 48000; std::atomic<int> pos { 0 }, filled { 0 }; juce::CriticalSection lock;
    };

    /** Where the S of this sound sits and how loud each one is, next to the line's reference. */
    struct Found
    {
        bool ok = false;
        float freq = 0;
        std::vector<float> levels;      // loudest moment of every S, dB over the reference, sorted
        float sharp() const { return levels[(size_t) juce::jlimit (0, (int) levels.size() - 1, (int) (0.8 * (double) levels.size()))]; }
    };
    /** keepFreq: listen where the plugin is aimed now instead of looking for the S. */
    Found find (const std::vector<float>& mono, double fs, aemon::deess::Params, bool keepFreq = false);
    /** The line that brings the sharper S sounds down by the wanted dB. */
    float lineFor (const Found&, float wantedDb, float amount);
    /** Centre of an S spectrum: bins of an FFT, 3 to 13 kHz. 0 when there is none. */
    float centreOf (const std::vector<float>& power, double binHz);

    /** What a factory preset is for: how far the sharper S sounds should come down, and whether it aims itself. */
    struct Intent { float downDb = 0; bool aim = false; };
    Intent intentOf (const juce::String& presetName);
}

class DeessProcessor : public aemon::EffectProcessor
{
public:
    DeessProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::deess::Params readParams() const;

    int popS (aemon::deess::Engine::Frame* dest, int max) noexcept;
    /** Depth of the last S sounds made with the settings as they are now, newest last. */
    std::vector<float> recentCuts() const;
    deessp::Recent recent;
    std::atomic<float> vowelCut { 0.0f };
    std::atomic<int> heard { 0 };

protected:
    void prepareEngine (double fs) override { active = readParams(); engine.setParams (active); engine.prepare (fs); recent.prepare (fs); heard = 0; validFrom = 0; vowelCut = 0; }
    bool wouldChangeLatency() const override { return engine.changesLatency (readParams()); }
    void applyParameters (bool withLatency) override
    {
        const auto p = readParams();
        if (p.amount != active.amount || p.line != active.line || p.range != active.range || p.freq != active.freq || p.strictness != active.strictness || p.fixedLine != active.fixedLine || p.on != active.on) validFrom = heard.load();
        active = p; engine.setParams (active); if (withLatency) engine.applyLook();
    }
    int engineLatency() const override { return engine.latencySamples(); }
    void processEngine (float* L, float* R, int n) override { recent.push (L, R, n, (int) active.on); engine.process (L, R, n); }
    float frameAux() override;

private:
    static std::vector<aemon::Preset> factoryPresets();
    aemon::deess::Engine engine;
    aemon::deess::Params active;
    juce::AbstractFifo fifo { 512 };
    std::array<aemon::deess::Engine::Frame, 512> frames {};
    static constexpr int keep = 24;
    std::atomic<int> validFrom { 0 };
    std::array<std::atomic<float>, keep> cuts {};
};
