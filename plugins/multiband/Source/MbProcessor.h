// Aemon Multiband. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "MbEngine.h"

namespace mbp
{
    constexpr int N = aemon::mb::maxBands;
    inline juce::String id (char what, int band) { return juce::String::charToString ((juce::juce_wchar) what) + juce::String (band + 1); }     // t1 threshold, r1 ratio, a1 attack, e1 release, u1 up threshold, v1 up ratio, g1 gain, o1 on, s1 solo, x1 split

    /** What a preset wants from a band: how many dB off its loudest tenth, how many dB onto its quietest tenth. */
    struct Want { float down = 0, ratio = 2, attack = 20, release = 150, up = 0, upRatio = 1.5f, gain = 0; };
    struct Recipe
    {
        const char* cat; const char* name; const char* desc;
        int bands = 4; std::array<float, N - 1> split { 120, 800, 5000, 10000 };
        std::array<Want, N> want {};
        float amount = 100, mix = 100, look = 0, knee = 6; bool straight = false, autoLevel = true; int slope = 0;
    };
    const std::vector<Recipe>& recipes();
    const Recipe* find (const juce::String& presetName);
    /** The four goals of the simple mode are recipes too. */
    struct Goal { const char* name; const char* help; const char* recipe; };
    constexpr Goal goals[] = { { "Tighter lows", "Tighter lows: the low end stops swelling, the rest is left alone.", "Goal: tighter lows" }, { "Tame harsh", "Tame harsh: holds the biting middle and top only when they bite.", "Goal: tame harshness" },
                               { "Even & dense", "Even & dense: loud parts come down everywhere, quiet parts come up where the music leaves room.", "Goal: even and dense" }, { "Glue", "Glue: a little of everything, slow and gentle.", "Goal: glue" } };
    constexpr int numGoals = 4;

    /** Levels of every band: how loud the loud parts and the quiet parts are, dB. */
    struct Levels { bool ok = false; std::array<double, N> loud {}, quiet {}; std::array<std::vector<float>, N> series; double step = 0.0005; };      // series: the detector's level, twice a millisecond
    Levels measure (const std::vector<float>& L, const std::vector<float>& R, double fs, const aemon::mb::Params&);
    Levels assumed();
    float thresholdFor (double loud, float downDb, float ratio, float knee);
    float upThresholdFor (double quiet, float upDb, float ratio, float knee);
    /** Thresholds for what is wanted: worked out by running the band's own attack and release over the sound that was heard. */
    void aim (const Want&, float knee, const std::vector<float>& series, double step, float& threshold, float& upThreshold);
    aemon::Preset presetOf (const Recipe&, const Levels&);

    /** The last seconds of the input. Audio thread writes. */
    class Recent
    {
    public:
        void prepare (double rate) { const juce::ScopedLock s (lock); fs = rate; const int n = (int) (fs * (fs > 100000 ? 4.0 : 8.0)); l.assign ((size_t) n, 0.0f); r = l; pos = 0; filled = 0; }
        void push (const float* L, const float* R, int n) noexcept { const int size = (int) l.size(); if (size == 0) return; int w = pos.load (std::memory_order_relaxed); for (int i = 0; i < n; ++i) { l[(size_t) w] = L[i]; r[(size_t) w] = R[i]; if (++w == size) w = 0; } pos.store (w, std::memory_order_release); filled = std::min (size, filled.load() + n); }
        void copy (std::vector<float>& L, std::vector<float>& R, double& rate) const { const juce::ScopedLock s (lock); rate = fs; const int size = (int) l.size(), n = std::min (size, filled.load()), w = pos.load (std::memory_order_acquire); L.resize ((size_t) n); R.resize ((size_t) n); for (int i = 0; i < n; ++i) { const auto q = (size_t) ((w - n + i + size) % size); L[(size_t) i] = l[q]; R[(size_t) i] = r[q]; } }
    private:
        std::vector<float> l, r; double fs = 48000; std::atomic<int> pos { 0 }, filled { 0 }; juce::CriticalSection lock;
    };
}

class MbProcessor : public aemon::EffectProcessor
{
public:
    MbProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    aemon::mb::Params readParams() const;
    float get (const juce::String& pid) const { return apvts.getRawParameterValue (pid)->load(); }
    int bandCount() const { return 2 + juce::jlimit (0, 3, juce::roundToInt (real ("bands"))); }
    int popBands (aemon::mb::Engine::Frame* dest, int max) noexcept;
    /** The goal the settings correspond to, or -1. */
    int matchingGoal() const;

    mbp::Recent recent;
    std::atomic<float> matchDb { 0.0f };

protected:
    void prepareEngine (double fs) override { engine->setParams (readParams()); engine->prepare (fs); recent.prepare (fs); }
    bool wouldChangeLatency() const override { return engine->changesLatency (readParams()); }
    void applyParameters (bool withLatency) override { engine->setParams (readParams()); if (withLatency) engine->applyLatency(); }
    int engineLatency() const override { return engine->latencySamples(); }
    void processEngine (float* L, float* R, int n) override { recent.push (L, R, n); engine->process (L, R, n, sideL, sideR); }
    float frameAux() override;

private:
    static std::vector<aemon::Preset> factoryPresets();
    std::unique_ptr<aemon::mb::Engine> engine = std::make_unique<aemon::mb::Engine>();
    juce::AbstractFifo fifo { 256 };
    std::array<aemon::mb::Engine::Frame, 256> frames {};
};
