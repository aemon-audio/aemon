// Aemon core. AGPL-3.0-or-later.
#include <BinaryData_AemonFonts.h>

namespace aemon
{

namespace
{
    juce::Typeface::Ptr load (const void* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    struct Faces
    {
        juce::Typeface::Ptr display = load (AemonFonts::UnboundedSemiBold_ttf, AemonFonts::UnboundedSemiBold_ttfSize);
        juce::Typeface::Ptr label = load (AemonFonts::IBMPlexSansCondensedSemiBold_ttf, AemonFonts::IBMPlexSansCondensedSemiBold_ttfSize);
        juce::Typeface::Ptr labelMedium = load (AemonFonts::IBMPlexSansCondensedMedium_ttf, AemonFonts::IBMPlexSansCondensedMedium_ttfSize);
        juce::Typeface::Ptr mono = load (AemonFonts::IBMPlexMonoMedium_ttf, AemonFonts::IBMPlexMonoMedium_ttfSize);
        juce::Typeface::Ptr text = load (AemonFonts::IBMPlexSansRegular_ttf, AemonFonts::IBMPlexSansRegular_ttfSize);
        juce::Typeface::Ptr textMedium = load (AemonFonts::IBMPlexSansMedium_ttf, AemonFonts::IBMPlexSansMedium_ttfSize);
    };
    Faces& faces() { static Faces f; return f; }
    juce::Font make (juce::Typeface::Ptr t, float h) { return juce::Font (juce::FontOptions (t).withHeight (h)); }
}

juce::Font Fonts::display (float h)     { return make (faces().display, h); }
juce::Font Fonts::label (float h)       { return make (faces().label, h); }
juce::Font Fonts::labelMedium (float h) { return make (faces().labelMedium, h); }
juce::Font Fonts::mono (float h)        { return make (faces().mono, h); }
juce::Font Fonts::text (float h)        { return make (faces().text, h); }
juce::Font Fonts::textMedium (float h)  { return make (faces().textMedium, h); }

void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                   juce::Colour c, juce::Justification j, float tracking, bool medium)
{
    auto f = (medium ? Fonts::labelMedium (height) : Fonts::label (height)).withExtraKerningFactor (tracking);
    g.setFont (f);
    g.setColour (c);
    g.drawText (text.toUpperCase(), area, j, false);
}

void drawMark (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ring)
{
    const float s = juce::jmin (area.getWidth(), area.getHeight()) / 100.0f;
    const auto o = area.getCentre() - juce::Point<float> (50.0f * s, 50.0f * s);
    juce::Path p;
    p.addCentredArc (o.x + 50 * s, o.y + 50 * s, 36 * s, 36 * s, 0.0f,
                     juce::degreesToRadians (-140.0f), juce::degreesToRadians (140.0f), true);
    g.setColour (ring);
    g.strokePath (p, juce::PathStrokeType (15 * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::ember);
    g.fillEllipse (o.x + (50 - 8.5f) * s, o.y + (31 - 8.5f) * s, 17 * s, 17 * s);
}

void drawRaised (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool pressed)
{
    if (! pressed)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0, 1.0f), radius);
    }
    juce::ColourGradient grad (pressed ? col::basalt2 : col::basalt4, r.getX(), r.getY(),
                               col::basalt3, r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    g.setColour (col::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    if (! pressed)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + radius, r.getRight() - radius);
    }
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    juce::ColourGradient grad (juce::Colour (0xff16181e), r.getCentreX(), r.getY(), col::basalt0, r.getCentreX(), r.getY() + r.getHeight() * 0.7f, true);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, radius);
    // inner shadow along the top edge
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.55f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 10, false);
    g.setGradientFill (sh);
    g.fillRoundedRectangle (r.withHeight (juce::jmin (12.0f, r.getHeight())), radius);
    g.setColour (juce::Colour (0xff08090b));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + radius, r.getRight() - radius);
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1c22));
    setColour (juce::PopupMenu::textColourId, col::bone2);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::basalt3);
    setColour (juce::PopupMenu::highlightedTextColourId, col::bone1);
    setColour (juce::PopupMenu::headerTextColourId, col::bone3);
    setColour (juce::TextEditor::textColourId, col::emberHi);
    setColour (juce::TextEditor::backgroundColourId, col::basalt0);
    setColour (juce::TextEditor::highlightColourId, col::emberLo.withAlpha (0.6f));
    setColour (juce::TextEditor::highlightedTextColourId, col::bone1);
    setColour (juce::TextEditor::outlineColourId, col::ember);
    setColour (juce::TextEditor::focusedOutlineColourId, col::ember);
    setColour (juce::CaretComponent::caretColourId, col::emberHi);
    setColour (juce::Label::textColourId, col::bone1);
    setColour (juce::Label::textWhenEditingColourId, col::emberHi);
    setColour (juce::Label::backgroundWhenEditingColourId, col::basalt0);
    setColour (juce::Label::outlineWhenEditingColourId, col::ember);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff26292f));
    setColour (juce::TooltipWindow::textColourId, col::bone1);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff1a1c22));
    g.setColour (col::edge);
    g.drawRect (0, 0, w, h);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool, const juce::String& text, const juce::String&,
                                     const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (col::lineSoft);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.reduced (4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour (col::basalt3);
        g.fillRoundedRectangle (r, 4.0f);
    }
    g.setFont (Fonts::text (14.0f));
    g.setColour (! isActive ? col::bone3 : isTicked ? col::emberHi : (isHighlighted ? col::bone1 : col::bone2));
    g.drawText (text, r.reduced (10, 0), juce::Justification::centredLeft, true);
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    drawEngraved (g, name, area.toFloat().reduced (14, 0).withTrimmedTop (6), 10.5f, col::bone3, juce::Justification::centredLeft, 0.18f);
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::basalt0);
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 3.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (col::ember);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 3.0f, 1.0f);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getProperties()["kind"].toString() == "chip")
    {
        g.setColour (col::basalt0); g.fillRoundedRectangle (r, 3);
        g.setColour (over ? col::emberLo : col::lineSoft); g.drawRoundedRectangle (r, 3, 1);
        return;
    }
    drawRaised (g, r, 5, down);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool)
{
    if (b.getProperties()["kind"].toString() == "chip")
    {
        g.setFont (Fonts::mono (9.5f));
        g.setColour (col::emberHi);
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
        return;
    }
    drawEngraved (g, b.getButtonText(), b.getLocalBounds().toFloat(), 10.5f, over ? col::bone1 : col::bone2);
}

} // namespace aemon
