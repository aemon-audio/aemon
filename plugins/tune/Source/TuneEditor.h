// Aemon Tune. AGPL-3.0-or-later.
#pragma once

#include "TuneProcessor.h"

namespace tuneui { class Ring; class Line; class KeyPanel; class Throat; class Take; class Glass; class Mouth; class Source; }

class TuneEditor : public aemon::EffectEditor
{
public:
    explicit TuneEditor (TuneProcessor&);
    TuneProcessor& proc;

    /** Changes one of the six shape settings; the line of Simple lets go first, at the same sound. Inside an edit. */
    void own (int which, float v);
    /** The line: one undoable gesture sets everything. Inside an edit. */
    void line (float hard);
    /** Where the line stands: the place itself, or the nearest one to a shape of one's own. */
    float linePlace() const;
    bool hasOwn() const { return real ("own") > 0.5f; }
    std::array<float, 6> shape() const;
    void toggleNote (int pitchClass);
    void useKey (int root, bool minor);
    void allNotes();
    void keyMenu (juce::Component& at);
    juce::String keyText() const;
    juce::String sentence() const;

    static constexpr int shown = 1300;                      // looks of 5 ms: six and a half seconds
    std::vector<aemon::tune::Engine::Frame> past;           // newest last
    /** The last note that was held for a while: cents from where it was meant, every 5 ms. */
    std::vector<float> lastNote; int lastTarget = 60;
    double lag() const;                                      // looks by which the picture is ahead of what is heard
    juce::uint32 changedByHand = 0;

private:
    void tick (bool simple, int frame) override;
    void presetChosen (const aemon::Preset&) override;
    std::vector<std::pair<juce::String, float>> song, kept;  // the settings that belong to the song
    std::vector<std::pair<juce::String, float>> songNow() const;
    std::vector<float> building; int buildingTarget = -1; juce::String lastName;
    tuneui::Ring* ring = nullptr; tuneui::Line* lineC = nullptr; tuneui::KeyPanel* key = nullptr; tuneui::Throat* throat = nullptr; tuneui::Take* take = nullptr; tuneui::Glass* glass = nullptr; tuneui::Mouth* mouth = nullptr; tuneui::Source* source = nullptr;
};
