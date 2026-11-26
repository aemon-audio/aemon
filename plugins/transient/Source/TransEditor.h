// Aemon Transient. AGPL-3.0-or-later.
#pragma once

#include "TransProcessor.h"

namespace transui { class Display; }

class TransEditor : public aemon::EffectEditor
{
public:
    explicit TransEditor (TransProcessor&);
    TransProcessor& proc;
    using Point = aemon::trans::Engine::Point;

    void punch();
    void tighter();
    void samePeaks();

    static constexpr int pre = 15, hitLen = 420, stripLen = 4000;      // milliseconds
    /** The average of the last hits, dB per millisecond, in and out. */
    std::vector<float> avgIn, avgOut;
    int hits = 0;
    float hitChange = 0, bodyChange = 0, lastPeak = 0;
    std::vector<Point> strip;      // newest last

private:
    void tick (bool simple, int frame) override;
    void buildPro();
    void buildSimple();
    void updateCheck();
    void take();
    transui::Display *displaySimple = nullptr, *displayPro = nullptr;
    aemon::CheckCards* check = nullptr;
    aemon::ReadoutPanel* panel = nullptr;
    int cooled = 0; std::vector<int> waiting;      // onsets in the strip, counted from its end
    juce::String settings;
};
