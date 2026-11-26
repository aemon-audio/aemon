// Aemon Delay. AGPL-3.0-or-later.
#pragma once

#include "DelProcessor.h"

namespace delui { class Display; class Throw; }

class DelEditor : public aemon::EffectEditor
{
public:
    explicit DelEditor (DelProcessor&);
    DelProcessor& proc;

    void clearWords();
    void calm();

    static constexpr int shown = 300;
    std::vector<aemon::del::Engine::Frame> past;

private:
    void tick (bool simple, int frame) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    delui::Display *displaySimple = nullptr, *displayPro = nullptr;
    delui::Throw *throwSimple = nullptr, *throwPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
};
