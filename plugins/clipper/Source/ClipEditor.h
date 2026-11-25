// Aemon Clipper. AGPL-3.0-or-later.
#pragma once

#include "ClipProcessor.h"

namespace clipui { class Display; }

class ClipEditor : public aemon::EffectEditor
{
public:
    explicit ClipEditor (ClipProcessor&);
    ClipProcessor& proc;
    bool shavePeaks (bool silentWithoutSignal = false);
    void less() { change ("push", std::max (0.0f, real ("push") - 2.0f)); toast ("Backed off 2 dB."); }
    int highlightedShape() const { return shape->getIndex(); }
    /** The most taken off a peak in the last seconds, dB. */
    float offPeaks() const;

private:
    void tick (bool simple, int frame) override;
    void presetChosen (const aemon::Preset&) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    clipui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
    aemon::SegControl* shape = nullptr;
};
