// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Draws the engraved titles, sub-labels and grooves of a control deck. Sits on top, ignores the mouse. */
class DeckPainter : public juce::Component
{
public:
    struct Group
    {
        Group (juce::Rectangle<int> area, const char* text) : r (area), title ([t = juce::String (text)] { return t; }) {}
        Group (juce::Rectangle<int> area, std::function<juce::String()> live) : r (area), title (std::move (live)) {}
        juce::Rectangle<int> r;
        std::function<juce::String()> title;      // read at every repaint
    };
    std::vector<Group> groups;
    std::vector<int> seps;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> subs;
    DeckPainter() { setInterceptsMouseClicks (false, false); }
    void clear() { groups.clear(); seps.clear(); subs.clear(); }
    /** A small label right under a control. */
    void under (juce::Component& c, const juce::String& text) { subs.push_back ({ c.getBounds().withY (c.getBottom() + 1).withHeight (11), text }); }
    void paint (juce::Graphics&) override;
};

/** A component laid out by a function. */
struct LayoutBox : juce::Component
{
    std::function<void (juce::Rectangle<int>)> layout;
    void resized() override { if (layout) layout (getLocalBounds()); }
};

/** Simple mode: four plain-language checks, each with an optional one-click fix. */
class CheckCards : public juce::Component
{
public:
    enum class S { ok, warn, bad };
    struct Row { S s = S::ok; juce::String title, text, fixLabel; std::function<void()> fix; };
    struct Metrics { int top = 26, plain = 60, withFix = 84, gap = 6, padY = 8, fixUp = 30; bool tightWhenBusy = false; };

    CheckCards (const juce::String& title, const juce::String& helpText, Metrics);
    std::array<Row, 4> rows;
    /** Call after changing rows. */
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int fixCount() const;
    bool busy() const { return m.tightWhenBusy && fixCount() >= 3; }
    int rowH (size_t i) const { return rows[i].fix ? (busy() ? m.withFix - 6 : m.withFix) : m.plain; }
    int gap() const { return busy() ? m.gap - 2 : m.gap; }
    const juce::String title;
    const Metrics m;
    std::array<std::unique_ptr<Button>, 4> fixes;
};

} // namespace aemon
