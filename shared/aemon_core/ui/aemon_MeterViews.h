// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** IEC 60268-18 style meter law, -60..+6 dB mapped to 0..1. */
float meterPos (float db) noexcept;

/** Stereo level meter: RMS body, peak shadow, hold line, latching clip lamps. */
class LevelMeterView : public juce::Component, public HelpProvider
{
public:
    enum class ScaleSide { left, right, none };
    explicit LevelMeterView (ScaleSide s) : side (s) {}
    void push (const float peakDb[2], const float rmsDb[2], float dt);
    void reset();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { reset(); }
    juce::String getHelpText() const override
    { return "Level meter: the solid bar is RMS (average), the lighter bar is peak, the line holds the last peak. Click to clear the clip lamps."; }

private:
    ScaleSide side;
    float pk[2] { -120, -120 }, rms[2] { -120, -120 }, hold[2] { -120, -120 };
    double holdT[2] { 0, 0 };
    bool clip[2] { false, false };
};

/** Phase correlation strip, -1..+1. */
class CorrelationView : public juce::Component, public HelpProvider
{
public:
    void push (float v, float dt);
    float getSmoothed() const noexcept { return value; }
    void paint (juce::Graphics&) override;
    juce::String getHelpText() const override
    { return "Phase correlation: +1 means fully mono-safe, 0 means very wide, below 0 means parts will cancel when summed to mono."; }

private:
    float value = 1.0f, low = 1.0f;
};

} // namespace aemon
