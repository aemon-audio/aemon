// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Anything that can explain itself in the status strip. */
struct HelpProvider
{
    virtual ~HelpProvider() = default;
    virtual juce::String getHelpText() const = 0;
};

/** Attach a fixed help sentence to any component. */
inline void setHelp (juce::Component& c, const juce::String& text) { c.getProperties().set ("aemonHelp", text); }

/** Rotary control. Drag up/down, Shift for fine, double-click resets, scroll wheel,
    click the value to type. Glides to new values when a preset, A/B or undo changes it. */
class Knob : public juce::Component, public HelpProvider, private juce::Timer
{
public:
    enum class Style { hero, compact };
    struct Options
    {
        Style style = Style::hero;
        bool bipolar = false;
        float centre = 0.0f;          // real value of the centre detent (bipolar knobs)
        bool detent = false;
        int ticks = 11;
        juce::String minLabel, maxLabel, centreLabel;
        std::function<juce::String (float)> format;
        std::function<std::optional<float> (const juce::String&)> parse;
        juce::String name, help;
        bool showValue = true;
        bool inverted = false;        // turning up lowers the parameter (e.g. Amount driving a threshold)
    };

    Knob (juce::RangedAudioParameter& p, Options o);
    ~Knob() override;

    std::function<void()> onGestureStart, onGestureEnd;
    std::function<void (float)> onUserChange;   // real value, called only for changes made with this knob

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    juce::String getHelpText() const override;

    /** Start the power-on sweep from the minimum. */
    void sweepIn (int delayMs);
    void snapToTarget() { sweepAt = 0; shown = target; valueLabel.setText (valueText(), juce::dontSendNotification); repaint(); }
    juce::String valueText() const;

private:
    void timerCallback() override;
    void setNormFromUi (float n);
    void beginGesture();
    void endGesture();
    float centreNorm() const { return ui (param.convertTo0to1 (opt.centre)); }
    float ui (float n) const { return opt.inverted ? 1.0f - n : n; }   // parameter <-> knob position (its own inverse)
    juce::Rectangle<float> knobArea() const;
    void paintHero (juce::Graphics&, juce::Rectangle<float>, float n);
    void paintCompact (juce::Graphics&, juce::Rectangle<float>, float n);

    juce::RangedAudioParameter& param;
    Options opt;
    juce::ParameterAttachment attachment;
    juce::Label valueLabel;
    float target = 0, shown = 0, dragNorm = 0;
    std::optional<int> lastDragY;
    bool dragging = false, inGesture = false;
    double wheelEndAt = 0, sweepAt = 0;
};

/** Raised button. Optional lamp (latching), attention state, small hint text on the right. */
class Button : public juce::Button, public HelpProvider
{
public:
    explicit Button (const juce::String& text, bool withLamp = false);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    juce::String getHelpText() const override { return help; }

    juce::String help, hint;
    bool lamp = false, attention = false, small = false;
    juce::Path icon;      // optional, drawn in a 0..12 box
};

/** One-of-N switch. Works with a choice parameter or with plain UI state. */
class SegControl : public juce::Component, public HelpProvider
{
public:
    explicit SegControl (juce::StringArray labels, juce::StringArray helps = {});
    void attach (juce::RangedAudioParameter& p, std::function<void()> begin, std::function<void()> end);
    void setIndex (int i, bool notify);
    int getIndex() const { return index; }
    std::function<void (int)> onChange;
    float textHeight = 10.5f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    juce::String getHelpText() const override;

private:
    int hitIndex (juce::Point<float>) const;
    juce::Rectangle<float> segBounds (int i) const;
    juce::StringArray labels, helps;
    int index = 0, hover = -1;
    std::unique_ptr<juce::ParameterAttachment> att;
    std::function<void()> beginFn, endFn;
};

} // namespace aemon
