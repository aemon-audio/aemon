// Aemon Rider. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "RiderEngine.h"

namespace riderp
{
    /** The last seconds of the voice and of the beat, for "Set it for me". The audio thread writes. */
    class Recent
    {
    public:
        struct Copy { std::vector<float> l, r, bl, br; double fs = 48000; };
        void prepare (double rate) { const juce::ScopedLock s (lock); fs = rate; for (auto& b : buf) b.assign ((size_t) (fs * 12.0), 0.0f); pos = 0; filled = 0; }
        void push (const float* L, const float* R, const float* bl, const float* br, int n) noexcept
        {
            const int size = (int) buf[0].size(); if (size == 0) return;
            int w = pos.load (std::memory_order_relaxed); const float* in[] { L, R, bl, br };
            for (int i = 0; i < n; ++i) { for (int c = 0; c < 4; ++c) buf[(size_t) c][(size_t) w] = in[c] != nullptr ? in[c][i] : 0.0f; if (++w == size) w = 0; }
            pos.store (w, std::memory_order_release); filled = std::min (size, filled.load() + n);
        }
        Copy copy() const
        {
            const juce::ScopedLock s (lock); Copy o; o.fs = fs; const int size = (int) buf[0].size(), n = std::min (size, filled.load()), w = pos.load (std::memory_order_acquire);
            std::vector<float>* out[] { &o.l, &o.r, &o.bl, &o.br };
            for (int c = 0; c < 4; ++c) { out[c]->resize ((size_t) n); for (int i = 0; i < n; ++i) (*out[c])[(size_t) i] = buf[(size_t) c][(size_t) ((w - n + i + size) % size)]; }
            return o;
        }
    private:
        std::array<std::vector<float>, 4> buf; double fs = 48000; std::atomic<int> pos { 0 }, filled { 0 }; juce::CriticalSection lock;
    };

    /** What was heard: the level the words are at, and the beat's. */
    struct Heard { aemon::rider::Heard voice; bool beatOk = false; double beat = -20; };
    Heard hear (const Recent::Copy&, const aemon::rider::Params&);

    /** A factory preset that sets its target by the voice it has heard (all but the clean start). */
    bool aims (const juce::String& presetName);
}

class RiderProcessor : public aemon::EffectProcessor, private juce::Timer
{
public:
    RiderProcessor();
    ~RiderProcessor() override { stopTimer(); endWriting(); }
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::rider::Params readParams() const;
    int popRide (aemon::rider::Engine::Frame* dest, int max) noexcept;
    void setStateInformation (const void*, int) override;

    /** Sends the ride to the DAW as automation while it plays in Write. Called about 50 times a second on the message thread. */
    void writeStep();
    bool isWriting() const { return gesture; }

    riderp::Recent recent;

protected:
    void prepareEngine (double fs) override { engine->setParams (readParams()); engine->prepare (fs); recent.prepare (fs); }
    bool wouldChangeLatency() const override { return engine->changesLatency (readParams()); }
    void applyParameters (bool withLatency) override { engine->setParams (readParams()); if (withLatency) engine->applyLatency(); }
    int engineLatency() const override { return engine->latencySamples(); }
    void processEngine (float* L, float* R, int n) override;
    float frameAux() override;

private:
    void timerCallback() override { writeStep(); }
    void endWriting();
    static std::vector<aemon::Preset> factoryPresets();
    std::unique_ptr<aemon::rider::Engine> engine = std::make_unique<aemon::rider::Engine>();
    juce::AbstractFifo fifo { 512 };
    std::vector<aemon::rider::Engine::Frame> frames = std::vector<aemon::rider::Engine::Frame> (512);
    std::atomic<float> toWrite { 0.0f };
    std::atomic<bool> playing { false };
    std::atomic<int> blocks { 0 };
    int seenBlocks = 0;
    bool gesture = false;
};
