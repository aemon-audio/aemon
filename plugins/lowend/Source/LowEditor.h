// Aemon Low End. AGPL-3.0-or-later.
#pragma once

#include "LowProcessor.h"

namespace lowui { class Waves; class Curve; }

class LowEditor : public aemon::EffectEditor
{
public:
    explicit LowEditor (LowProcessor&);
    ~LowEditor() override { worker.stop(); }
    LowProcessor& proc;

    /** Measures the last seconds now and moves the bass if that really helps. */
    void lineUp();
    void putBack();
    void duckClash();
    void measureNow (bool withAdvice);

    // for the pictures
    aemon::low::Measure measure;
    aemon::low::Advice advice;
    std::vector<float> kickWave, bassWave, curve;       // the last hit, from -15 ms, at measure.fs; the sum against the delay
    double reading = 0;
    int hits = 0;
    static constexpr int shown = 250;
    std::vector<aemon::low::Engine::Frame> past;
    aemon::low::Align alignNow() const { return proc.readParams().align; }
    bool searching() const { return thinking.load(); }

private:
    void tick (bool simple, int frame) override;
    void presetChosen (const aemon::Preset&) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    void setAlign (const aemon::low::Align&);
    lowui::Waves* waves[2] { nullptr, nullptr }; lowui::Curve* curves[2] { nullptr, nullptr };
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
    /** Does one thing at a time beside the window. Stopped without a time limit: a thread killed by force can leave a Windows lock behind. */
    struct Worker : juce::Thread
    {
        Worker() : juce::Thread ("Aemon Low End search") {}
        ~Worker() override { stop(); }
        void stop() { stopThread (-1); }
        void give (std::function<void()> j) { { const juce::ScopedLock sl (lock); job = std::move (j); } if (! isThreadRunning()) startThread (juce::Thread::Priority::normal); notify(); }
        void run() override { while (! threadShouldExit()) { std::function<void()> j; { const juce::ScopedLock sl (lock); std::swap (j, job); } if (j) j(); else wait (-1); } }
        juce::CriticalSection lock; std::function<void()> job;
    } worker;
    aemon::low::Align kept; juce::String lastName;
    std::atomic<bool> thinking { false };
};
