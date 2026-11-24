// Aemon Utility. AGPL-3.0-or-later.
#pragma once

#include "PluginProcessor.h"

namespace aemon::utility::ui
{
class StereoField;
class LoudnessPanel;
class Gauge;
class Health;
class MeterColumn;
class Deck;
}

class UtilityEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UtilityEditor (UtilityProcessor&);
    ~UtilityEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    /** For headless screenshots: runs one UI frame and settles animations. */
    void tickForTest() { for (auto* k : knobs) k->snapToTarget(); timerCallback(); }

    // used by the panels
    void setReal (const juce::String& id, float value);
    float getReal (const juce::String& id) const;
    void begin() { shell->beginEdit(); }
    void end() { shell->endEdit(); }
    aemon::Shell& getShell() { return *shell; }
    UtilityProcessor& proc;

    // Simple-mode helpers
    void matchLoudness();
    void fixPhase();
    void centreBalance();
    void lowerForHeadroom();
    float target() const { return (float) (double) proc.pluginState.ui.getProperty ("target", -14.0); }
    juce::String targetName() const;

    // live stats shared by the panels
    float stOut = -INFINITY, peakHold = -120.0f, corrAvg = 1.0f, balanceDb = 0.0f;

private:
    void timerCallback() override;
    void buildSimple();
    void buildPro();
    aemon::Knob& addKnob (juce::Component& parent, const juce::String& id, aemon::Knob::Options o);
    aemon::Button& addToggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help);

    aemon::LookAndFeel lnf;
    std::unique_ptr<aemon::Shell> shell;

    std::unique_ptr<juce::Component> simpleBody, proBody;
    std::unique_ptr<aemon::utility::ui::Deck> simpleDeck, proDeck;
    aemon::utility::ui::StereoField *fieldSimple = nullptr, *fieldPro = nullptr;
    aemon::utility::ui::LoudnessPanel* loudPanel = nullptr;
    aemon::utility::ui::Gauge* gauge = nullptr;
    aemon::utility::ui::Health* health = nullptr;
    aemon::utility::ui::MeterColumn *inCol = nullptr, *outCol = nullptr;
    aemon::Button *matchBtn = nullptr, *fixBtn = nullptr;

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> paramAtts;
    std::vector<aemon::Knob*> knobs;

    std::vector<juce::Point<float>> pts;
    double lastTick = 0;
    float msOut[2] { 1e-9f, 1e-9f }, msIn[2] { 1e-9f, 1e-9f }, balL = 1e-9f, balR = 1e-9f;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UtilityEditor)
};
