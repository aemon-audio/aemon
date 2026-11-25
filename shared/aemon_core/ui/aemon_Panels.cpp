// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

void DeckPainter::paint (juce::Graphics& g)
{
    for (auto& gr : groups) drawEngraved (g, gr.title(), gr.r.toFloat().withHeight (16), 10.0f, col::bone2, juce::Justification::centred, 0.2f);
    for (auto& [r, t] : subs) drawEngraved (g, t, r.toFloat(), 9.0f, col::bone3, juce::Justification::centred, 0.16f, true);
    for (int x : seps)
    {
        juce::ColourGradient sg (juce::Colour (0x000b0c0f), (float) x, 0, juce::Colour (0x000b0c0f), (float) x, (float) getHeight(), false);
        sg.addColour (0.2, juce::Colour (0xff0b0c0f)); sg.addColour (0.8, juce::Colour (0xff0b0c0f));
        g.setGradientFill (sg); g.fillRect ((float) x, 0.0f, 1.0f, (float) getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.04f)); g.fillRect ((float) x + 1, 20.0f, 1.0f, (float) getHeight() - 40);
    }
}

CheckCards::CheckCards (const juce::String& t, const juce::String& helpText, Metrics metrics) : title (t), m (metrics)
{
    for (auto& b : fixes) { b = std::make_unique<Button> ("Fix"); b->small = true; addChildComponent (*b); }
    setHelp (*this, helpText);
}

int CheckCards::fixCount() const { int n = 0; for (auto& r : rows) n += r.fix != nullptr; return n; }

void CheckCards::refresh()
{
    for (size_t i = 0; i < 4; ++i) { fixes[i]->setVisible (rows[i].fix != nullptr); fixes[i]->setButtonText (rows[i].fixLabel); fixes[i]->onClick = rows[i].fix; }
    resized(); repaint();
}

void CheckCards::resized()
{
    auto r = getLocalBounds().reduced (12).withTrimmedTop (m.top);
    for (size_t i = 0; i < 4; ++i)
    {
        auto rr = r.removeFromTop (rowH (i)); r.removeFromTop (gap());
        if (fixes[i]->isVisible())
        {
            const int w = (int) juce::GlyphArrangement::getStringWidth (Fonts::label (10.0f).withExtraKerningFactor (0.12f), rows[i].fixLabel.toUpperCase()) + 20;
            fixes[i]->setBounds (rr.getX() + 28, rr.getBottom() - m.fixUp, w, 22);
        }
    }
}

void CheckCards::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawWell (g, b, 8);
    auto r = b.reduced (12);
    drawEngraved (g, title, r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredLeft);
    drawEngraved (g, "live", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredRight);
    r.removeFromTop ((float) m.top);
    for (size_t i = 0; i < 4; ++i)
    {
        auto rr = r.removeFromTop ((float) rowH (i)); r.removeFromTop ((float) gap());
        const auto& row = rows[i];
        const auto cc = row.s == S::ok ? col::sigOk : (row.s == S::warn ? col::sigHot : col::sigClip);
        g.setColour (row.s == S::bad ? col::sigClip.withAlpha (0.05f) : juce::Colours::white.withAlpha (0.02f)); g.fillRoundedRectangle (rr, 5);
        g.setColour (row.s == S::bad ? col::sigClip.withAlpha (0.35f) : col::lineSoft); g.drawRoundedRectangle (rr.reduced (0.5f), 5, 1);
        auto in = rr.reduced (10, (float) m.padY);
        auto d = juce::Rectangle<float> (8, 8).withPosition (in.getX(), in.getY() + 3);
        g.setColour (cc.withAlpha (0.4f)); g.fillEllipse (d.expanded (3));
        g.setColour (cc); g.fillEllipse (d);
        in.removeFromLeft (18);
        g.setFont (Fonts::textMedium (12.5f)); g.setColour (col::bone1);
        g.drawText (row.title, in.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (Fonts::text (11.5f)); g.setColour (col::bone2);
        g.drawFittedText (row.text, in.removeFromTop (32).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }
}

} // namespace aemon
