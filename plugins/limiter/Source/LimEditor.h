// Aemon Limiter. AGPL-3.0-or-later.
#pragma once

#include "LimProcessor.h"

namespace limui { class Display; class Check; class LoudPanel; }

class LimEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LimEditor (LimProcessor&);
    ~LimEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void tickForTest() { for (auto* k : knobs) k->snapToTarget(); timerCallback(); }

    LimProcessor& proc;
    aemon::Shell& getShell() { return *shell; }
    void begin() { shell->beginEdit(); }
    void end() { shell->endEdit(); }
    float getReal (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void setReal (const juce::String& id, float v);

    /** Measures the last seconds of music and sets Push for the current target. False if there was no signal. */
    bool makeItLoud (bool silentWithoutSignal = false);
    void lessSquashed();
    void setTarget (int t);
    int highlightedStyle() const { return styleSeg->getIndex(); }
    int highlightedTarget() const { return targetSeg->getIndex(); }

    struct Point { float inDb, outDb, grDb, st; };
    std::vector<Point> history;          // newest last, one per 16 ms
    float avgGr = 0.0f;                  // average reduction over about 4 s, positive dB

private:
    void timerCallback() override;
    void buildPro();
    void buildSimple();
    void syncSegments();
    juce::String targetText() const;
    aemon::Knob& knob (juce::Component& parent, const juce::String& id, aemon::Knob::Options o);
    aemon::Button& toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help);

    aemon::LookAndFeel lnf;
    std::unique_ptr<aemon::Shell> shell;
    std::unique_ptr<juce::Component> simpleBody, proBody, simpleDeck, proDeck;
    limui::Display *displaySimple = nullptr, *displayPro = nullptr;
    limui::Check* check = nullptr;
    limui::LoudPanel* loud = nullptr;
    aemon::SegControl *styleSeg = nullptr, *targetSeg = nullptr;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::vector<aemon::Knob*> knobs;
    juce::String shownTargetText, lastPreset;
    int frame = 0, shownLatency = -1, seenLoads = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimEditor)
};
