// Aemon Saturation. AGPL-3.0-or-later.
#pragma once

#include "SatProcessor.h"

namespace satui { class Display; }

class SatEditor : public aemon::EffectEditor
{
public:
    explicit SatEditor (SatProcessor&);
    SatProcessor& proc;
    void setAmountForMe();
    void cleaner() { change ("drive", std::max (0.0f, real ("drive") - 3.0f)); toast ("Backed off 3 dB."); }
    std::array<float, 9> harm {};
    float thdNow = 0.0f;       // percent, after Mix

private:
    void tick (bool simple, int frame) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    satui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
};
