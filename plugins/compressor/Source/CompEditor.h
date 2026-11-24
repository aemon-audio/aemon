// Aemon Compressor. AGPL-3.0-or-later.
#pragma once

#include "CompProcessor.h"

namespace compui { class Display; class Check; class GrMeter; }

class CompEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CompEditor (CompProcessor&);
    ~CompEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void tickForTest() { for (auto* k : knobs) k->snapToTarget(); timerCallback(); }

    CompProcessor& proc;
    aemon::Shell& getShell() { return *shell; }
    void begin() { shell->beginEdit(); }
    void end() { shell->endEdit(); }
    float getReal (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void setReal (const juce::String& id, float v);
    void applyStyle (int style);
    int highlightedStyle() const { return styleSeg->getIndex(); }   // -1 when the settings match no style
    void setAmountForMe();
    void lessPumping();

    // display history (newest last) and live statistics
    std::vector<compp::Frame> history;
    float avgGr = 0, crestIn = 0, crestOut = 0, grSpread = 0, levelDiff = 0;

private:
    void timerCallback() override;
    void buildPro();
    void buildSimple();
    void updateStyle();
    aemon::Knob& knob (juce::Component& parent, const juce::String& id, aemon::Knob::Options o);
    aemon::Button& toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help);

    aemon::LookAndFeel lnf;
    std::unique_ptr<aemon::Shell> shell;
    std::unique_ptr<juce::Component> simpleBody, proBody, simpleDeck, proDeck;
    compui::Display *displaySimple = nullptr, *displayPro = nullptr;
    compui::Check* check = nullptr;
    aemon::LevelMeterView *meterIn = nullptr, *meterOut = nullptr;
    compui::GrMeter* grMeter = nullptr;
    aemon::SegControl* styleSeg = nullptr;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::vector<aemon::Knob*> knobs;
    float msIn[2] { 1e-9f, 1e-9f }, msOut[2] { 1e-9f, 1e-9f };
    double lastTick = 0;
    int frame = 0, shownStyle = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompEditor)
};
