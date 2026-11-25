// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Number texts and parsers shared by the plugin windows. */
namespace txt
{
    inline juce::String minusSign() { return juce::String::charToString (0x2212); }
    inline juce::String num (float v, int d = 1) { auto s = d > 0 ? juce::String (v, d) : juce::String (juce::roundToInt (v)); if (s.getDoubleValue() == 0.0) s = s.replace ("-", ""); return s.replace ("-", minusSign()); }
    inline juce::String db (float v) { return (v > 0.05f ? "+" : "") + num (v) + " dB"; }
    inline juce::String percent (float v) { return juce::String (juce::roundToInt (v)) + " %"; }
    inline std::optional<float> parse (const juce::String& s)
    {
        auto t = s.replace (minusSign(), "-").trim();
        if (! t.containsAnyOf ("0123456789")) return {};
        return t.retainCharacters ("-.0123456789").getFloatValue();
    }
}

/** Base for the windows of new plugins: shell, resizing, knobs and buttons tied to parameters, undo keys, frame pump. */
class EffectEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    EffectEditor (EffectProcessor&, int baseW = 960, int baseH = 600);
    ~EffectEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override { shell->setBounds (getLocalBounds()); }
    bool keyPressed (const juce::KeyPress&) override;
    void tickForTest() { for (auto* k : knobs) k->snapToTarget(); timerCallback(); }

    EffectProcessor& base;
    Shell& getShell() { return *shell; }
    void begin() { shell->beginEdit(); }
    void end() { shell->endEdit(); }
    float real (const char* id) const { return base.real (id); }
    void setReal (const juce::String& id, float v);
    /** One undoable change. */
    void change (const juce::String& id, float v) { begin(); setReal (id, v); end(); }
    void toast (const juce::String& t) { shell->toast (t); }

    static constexpr int historyLen = 400;
    std::vector<MeterFrame> history;      // newest last

protected:
    /** Call at the end of the derived constructor, after building the four parts. */
    void start();
    virtual void tick (bool simple, int frameNumber) = 0;
    virtual void presetChosen (const Preset&) {}        // by the user, while the window is open

    Knob& knob (juce::Component& parent, const juce::String& id, Knob::Options);
    Button& toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help, bool small = false);
    Button& button (juce::Component& parent, const juce::String& text, const juce::String& hint, const juce::String& help, std::function<void()>);
    SegControl& seg (juce::Component& parent, juce::StringArray names, juce::StringArray helps, float textHeight = 9.5f);
    /** A switch tied to a choice parameter. */
    SegControl& paramSeg (juce::Component& parent, const juce::String& id, juce::StringArray names, juce::StringArray helps, float textHeight = 9.0f);
    DeckPainter& painter (juce::Component& parent);
    template <typename T, typename... A> T& make (juce::Component& parent, A&&... a) { auto c = std::make_unique<T> (std::forward<A> (a)...); auto& r = *c; parent.addAndMakeVisible (r); owned.push_back (std::move (c)); return r; }

    std::unique_ptr<juce::Component> simpleBody, proBody, simpleDeck, proDeck;

private:
    void timerCallback() override;
    LookAndFeel lnf;
    std::unique_ptr<Shell> shell;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::vector<Knob*> knobs;
    juce::String lastPreset;
    int frame = 0, shownLatency = -1, seenLoads = 0;
    const int baseW, baseH;
};

/** Dashed horizontal or diagonal line for the displays. */
void drawDashed (juce::Graphics&, juce::Line<float>, juce::Colour, float thickness = 1.2f, float on = 4.0f, float off = 4.0f);

/** Grid and labels of a level axis in dB; returns the y position function. */
void drawDbAxis (juce::Graphics&, juce::Rectangle<float> area, std::initializer_list<float> dbs, const std::function<float (float)>& y, bool plus = false);

/** Pro side panel: one big value and rows under it. */
class ReadoutPanel : public juce::Component
{
public:
    struct Row { juce::String name, value; bool hot = false; };
    juce::String heading, note, bigName, bigValue, unit;
    std::vector<Row> rows;
    void paint (juce::Graphics&) override;
};

} // namespace aemon
