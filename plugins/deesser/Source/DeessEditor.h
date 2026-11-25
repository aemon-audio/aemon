// Aemon De-esser. AGPL-3.0-or-later.
#pragma once

#include "DeessProcessor.h"

namespace deessui { class Display; }

class DeessEditor : public aemon::EffectEditor
{
public:
    explicit DeessEditor (DeessProcessor&);
    DeessProcessor& proc;

    /** Listens back over the last seconds, aims and sets a safe amount. */
    void findMyS();
    void less();

    // for the display
    static constexpr int shown = 312;                       // frames of 16 ms: five seconds
    std::vector<aemon::deess::Engine::Frame> past;          // newest last
    std::vector<float> voiceSpec, sSpec;                    // averages, power per bin
    double binHz = 48000.0 / 1024.0;
    float cutNow = 0;

private:
    void tick (bool simple, int frame) override;
    void presetChosen (const aemon::Preset&) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    void updateSpectra();
    deessp::Found listen (bool keepFreq) const { double fs; const auto x = proc.recent.copy (fs); return deessp::find (x, fs, proc.readParams(), keepFreq); }
    float heardCentre() const { return deessp::centreOf (sSpec, binHz); }
    deessui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
    std::unique_ptr<juce::dsp::FFT> fft;
    int fftOrder = 0, sSeen = 0, voiceSeen = 0;
    std::vector<float> fftBuf;
};
