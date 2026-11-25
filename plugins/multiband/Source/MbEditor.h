// Aemon Multiband. AGPL-3.0-or-later.
#pragma once

#include "MbProcessor.h"

namespace mbui { class Display; }

class MbEditor : public aemon::EffectEditor
{
public:
    explicit MbEditor (MbProcessor&);
    MbProcessor& proc;

    void chooseGoal (int);
    /** Sets every threshold to the music of the last seconds: as the preset or goal wants, or about 3 dB where nothing else is known. */
    bool fitToMusic (bool silentWithoutSignal = false);
    void less();
    void select (int band);

    int selected = 1;
    std::array<float, mbp::N> level {}, gain {};
    std::array<double, 4> splits() const;
    std::vector<float> spectrum; double binHz = 48000.0 / 2048.0;
    juce::String hot;
    bool pro = false;
    juce::String bandName (int) const;

private:
    void tick (bool simple, int frame) override;
    void presetChosen (const aemon::Preset&) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    const mbp::Recipe* current() const;
    bool fit (const mbp::Recipe&);
    mbui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
    aemon::SegControl *goalSeg = nullptr, *bandSeg = nullptr;
    std::array<std::vector<juce::Component*>, mbp::N> perBand;
    std::array<float, mbp::N> avgDown {}, avgUp {};
    std::unique_ptr<juce::dsp::FFT> fft; int fftOrder = 0; std::vector<float> fftBuf;
};
