// Aemon Imager. AGPL-3.0-or-later.
#pragma once

#include "ImagerProcessor.h"

namespace imgui { class Display; }

class ImagerEditor : public aemon::EffectEditor
{
public:
    explicit ImagerEditor (ImagerProcessor&);
    ImagerProcessor& proc;

    void centreIt();
    void makeItStereo();
    void tighten();

    // for the display: quick and slow averages of input and output
    imgp::Cells in, out, inSlow, outSlow;
    float correlation = 1.0f, zoom = 1.0f;
    std::vector<float> dotsL, dotsR, dotsInL, dotsInR;
    bool pro = false;
    juce::String hot;
    double bandLoss (int band, bool& quiet) const;
    double splitHz (int i) const { const auto p = proc.readParams(); return p.split[(size_t) i]; }

private:
    void tick (bool simple, int frame) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    void listenToggle (aemon::Button&, const char* me, const char* other);
    imgui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
    imgp::Analyser analyser;
    int seen = 0;
};
