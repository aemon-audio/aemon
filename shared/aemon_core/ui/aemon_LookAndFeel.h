// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** The approved Aemon palette (see design/style-sheet.html). */
namespace col
{
    const juce::Colour basalt0 { 0xff0d0e12 }, basalt1 { 0xff14151a }, basalt2 { 0xff1b1d23 }, basalt3 { 0xff24272f }, basalt4 { 0xff30343e };
    const juce::Colour groove { 0xff07080a }, edge { 0xff0a0b0e };
    const juce::Colour line { 0xff2d313b }, lineSoft { 0xff22252d };
    const juce::Colour bone1 { 0xffede7dc }, bone2 { 0xffb4afa5 }, bone3 { 0xff737684 };
    const juce::Colour ember { 0xfff0a35b }, emberHi { 0xffffc58e }, emberLo { 0xff9a6433 };
    const juce::Colour phosphor { 0xff86e0cb }, lilac { 0xffb8a6ff };
    const juce::Colour sigOk { 0xff62cf98 }, sigHot { 0xffe6c15a }, sigClip { 0xffff5c50 };
}

/** Bundled fonts (SIL OFL). */
struct Fonts
{
    static juce::Font display (float h);   // Unbounded SemiBold: wordmark, plugin names
    static juce::Font label (float h);     // Plex Sans Condensed SemiBold: engraved labels, buttons
    static juce::Font labelMedium (float h);
    static juce::Font mono (float h);      // Plex Mono Medium: every number
    static juce::Font text (float h);      // Plex Sans Regular: help, presets
    static juce::Font textMedium (float h);
};

/** Draws an uppercase label with letter-spacing, like engraving. */
void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                   juce::Colour c, juce::Justification j = juce::Justification::centred, float tracking = 0.16f, bool medium = false);

/** The Aemon dial mark: a ring with its travel gap at the bottom, pointer dot at twelve o'clock. */
void drawMark (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ring);

/** Raised button body (gradient, bevel, shadow). */
void drawRaised (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool pressed);

/** Recessed well (displays, readouts). */
void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Font getPopupMenuFont() override { return Fonts::text (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return Fonts::mono (12.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};

} // namespace aemon
