// Aemon Rider. AGPL-3.0-or-later.
#pragma once

#include "RiderProcessor.h"

namespace riderui { class Stage; class Fader; class Sentence; class Speaker; }

class RiderEditor : public aemon::EffectEditor
{
public:
    explicit RiderEditor (RiderProcessor&);
    RiderProcessor& proc;

    /** Listens back over the last seconds and sets the target, how far and how even. */
    void setForMe();
    void gentler();
    void followBeat();

    juce::String sentenceText (bool simple) const;
    /** Drags a value of the sentences upwards like a mouse would. False when it is not there. */
    bool dragForTest (bool simple, const juce::String& id, int pixels);

    static constexpr int shown = 560;                       // frames of 16 ms: nine seconds
    std::vector<aemon::rider::Engine::Frame> past;          // newest last

private:
    void tick (bool simple, int frame) override;
    void presetChosen (const aemon::Preset&) override;
    struct Parts { riderui::Stage* stage = nullptr; riderui::Fader* fader = nullptr; riderui::Sentence* sentence = nullptr; riderui::Speaker* speaker = nullptr; };
    void build (bool simple, Parts&, std::unique_ptr<juce::Component>& body);
    void updateCheck (bool simple);
    riderp::Heard listen() const { return riderp::hear (proc.recent.copy(), proc.readParams()); }
    Parts simpleParts, proParts;
};
