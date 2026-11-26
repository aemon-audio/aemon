// Aemon Reverb. AGPL-3.0-or-later.
#pragma once

#include "RevProcessor.h"

namespace revui { class Display; }

class RevEditor : public aemon::EffectEditor
{
public:
    explicit RevEditor (RevProcessor&);
    RevProcessor& proc;

    void clearWords();
    void cleanLows();
    void shorter();

    static constexpr int shown = 300;
    std::vector<aemon::rev::Engine::Frame> past;

private:
    void tick (bool simple, int frame) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    revui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
};
