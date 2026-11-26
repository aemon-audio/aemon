// Aemon Tune. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "TuneEngine.h"

namespace tunep
{
    using Engine = aemon::tune::Engine;
    constexpr const char* noteNames[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    /** With the flat sign. */
    juce::String noteName (int pitchClass);
    constexpr double lowestHz[] = { 65.0, 100.0, 130.0 };      // low, middle, high voice
    constexpr const char* stops[] = { "Invisible", "Natural", "Polished", "Tight", "Hard" };
    /** What the line of Simple means at x (0..1), in the units of the parameters: pull %, land ms, slide %, let go %, after ms, vibrato %. */
    std::array<float, 6> along (double x);
    constexpr const char* ownIds[] = { "pull", "land", "slide", "letgo", "after", "vib" };
    /** The settings a preset never touches: they belong to the song and the singer. */
    bool belongsToSong (const juce::String& id);
    struct Found { int root = -1; bool minor = false; float sure = 0, seconds = 0, cents = 0; };
    /** The key whose notes are exactly the ones that are on, if there is one; minor first when asked. */
    bool keyOf (const std::array<bool, 12>& on, int rootWish, bool minorWish, int& root, bool& minor);
}

class TuneProcessor : public aemon::EffectProcessor
{
public:
    TuneProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool acceptsMidi() const override { return true; }

    aemon::tune::Params readParams() const;
    std::array<bool, 12> notesOn() const;
    /** Sets the twelve notes (message thread, inside an edit). */
    void setNotes (const std::array<bool, 12>&);
    tunep::Found found() const;
    void listenAgain() { forget = true; }
    int popTune (tunep::Engine::Frame* dest, int max) noexcept;
    std::atomic<int> heldNote { -1 };
    bool mixing() const { return real ("mode") > 0.5f; }

protected:
    void prepareEngine (double fs) override;
    bool wouldChangeLatency() const override { return wanted() != now; }
    void applyParameters (bool withLatency) override;
    int engineLatency() const override { return engines[(size_t) now]->latency(); }
    void processEngine (float* L, float* R, int n) override;

private:
    static std::vector<aemon::Preset> factoryPresets();
    int wanted() const { return (mixing() ? 3 : 0) + juce::jlimit (0, 2, juce::roundToInt (real ("voice"))); }
    std::array<std::unique_ptr<tunep::Engine>, 6> engines; int now = 0, chans = 2;
    aemon::tune::Params params; std::array<bool, 128> held {};
    std::atomic<bool> forget { false }; int sinceFind = 0;
    std::atomic<int> foundRoot { -1 }; std::atomic<bool> foundMinor { false }; std::atomic<float> foundSure { 0 }, foundSeconds { 0 }, foundCents { 0 };
    juce::AbstractFifo fifo { 2048 }; std::vector<tunep::Engine::Frame> frames = std::vector<tunep::Engine::Frame> (2048);
    std::array<std::atomic<float>*, 12> noteP {}, centP {}; std::array<std::atomic<float>*, 6> ownP {};
};
