// Aemon EQ. AGPL-3.0-or-later.
#pragma once

#include "EqProcessor.h"

namespace equi { class Display; class BandPanel; class ToneBalance; }

class EqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EqEditor (EqProcessor&);
    ~EqEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void tickForTest() { for (auto* k : knobs) k->snapToTarget(); timerCallback(); }

    EqProcessor& proc;
    aemon::Shell& getShell() { return *shell; }
    void begin() { shell->beginEdit(); }
    void end() { shell->endEdit(); }
    float getReal (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void setReal (const juce::String& id, float v, bool gesture = true);

    int selected = 0;
    void select (int band);
    int addBand (aemon::eq::Type type, float freq, float gain, float q);   // returns band or -1
    void removeBand (int band);
    void ensureRoleBand (int role);

    // spectra (dB per FFT bin), filled by the timer
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    std::vector<float> preSpec, postSpec;
    float specAt (const std::vector<float>& spec, float f0, float f1) const;

    void tameHarsh();
    void cleanMud();

private:
    void timerCallback() override;
    void buildPro();
    void buildSimple();
    void analyse (aemon::MonoRing& ring, std::vector<float>& spec);
    aemon::Knob& knob (juce::Component& parent, const juce::String& id, aemon::Knob::Options o);
    aemon::Button& toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help);

    aemon::LookAndFeel lnf;
    std::unique_ptr<aemon::Shell> shell;
    std::unique_ptr<juce::Component> simpleBody, proBody, simpleDeck, proDeck;
    equi::Display *displaySimple = nullptr, *displayPro = nullptr;
    equi::ToneBalance* tone = nullptr;
    aemon::LevelMeterView* meter = nullptr;
    std::vector<equi::BandPanel*> panels;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::vector<aemon::Knob*> knobs;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> fftBuf, window;
    float msOut[2] { 1e-9f, 1e-9f };
    double lastTick = 0;
    int frame = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqEditor)
};
