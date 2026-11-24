// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

float meterPos (float db) noexcept
{
    float f;
    if (db < -70.0f) f = 0.0f;
    else if (db < -60.0f) f = (db + 70.0f) * 0.0025f;
    else if (db < -50.0f) f = (db + 60.0f) * 0.005f + 0.025f;
    else if (db < -40.0f) f = (db + 50.0f) * 0.0075f + 0.075f;
    else if (db < -30.0f) f = (db + 40.0f) * 0.015f + 0.15f;
    else if (db < -20.0f) f = (db + 30.0f) * 0.02f + 0.3f;
    else if (db < 0.0f) f = (db + 20.0f) * 0.025f + 0.5f;
    else f = 1.0f + db * 0.025f;
    return juce::jlimit (0.0f, 1.0f, f / 1.15f);
}

void LevelMeterView::push (const float peakDb[2], const float rmsDb[2], float dt)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 2; ++i)
    {
        pk[i] = std::max (peakDb[i], pk[i] - dt * 26.0f);
        rms[i] = rmsDb[i];
        if (peakDb[i] >= hold[i]) { hold[i] = peakDb[i]; holdT[i] = now; }
        else if (now - holdT[i] > 1600.0) hold[i] -= dt * 40.0f;
        if (peakDb[i] > -0.1f) clip[i] = true;
    }
    repaint();
}

void LevelMeterView::reset()
{
    clip[0] = clip[1] = false;
    hold[0] = hold[1] = -120.0f;
    repaint();
}

void LevelMeterView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float sw = side == ScaleSide::none ? 0.0f : 20.0f, top = 9.0f, gap = 3.0f;
    const float bx = side == ScaleSide::left ? sw : 0.0f;
    const float bw = (b.getWidth() - sw - gap) / 2.0f, bh = b.getHeight() - top;
    auto y = [&] (float db) { return top + bh * (1.0f - meterPos (db)); };

    juce::ColourGradient grad (col::sigClip, 0, top, col::sigOk, 0, b.getHeight(), false);
    auto stop = [&] (float db) { return juce::jlimit (0.0, 1.0, (double) ((y (db) - top) / bh)); };
    grad.addColour (stop (0.0f), col::sigClip);
    grad.addColour (stop (-0.6f), col::sigHot);
    grad.addColour (stop (-6.0f), col::sigHot);
    grad.addColour (stop (-14.0f), col::sigOk);

    for (int i = 0; i < 2; ++i)
    {
        const float x = bx + (float) i * (bw + gap);
        const juce::Rectangle<float> bar (x, top, bw, bh);
        g.setColour (col::groove);
        g.fillRoundedRectangle (bar, 2.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clipPath; clipPath.addRoundedRectangle (bar, 2.0f);
            g.reduceClipRegion (clipPath);
            g.setGradientFill (grad);
            g.setOpacity (0.3f);
            const float yp = y (pk[i]);
            g.fillRect (x, yp, bw, b.getHeight() - yp);
            g.setOpacity (1.0f);
            const float yr = y (rms[i]);
            g.fillRect (x + 1, yr, bw - 2, b.getHeight() - yr);
            g.setColour (col::groove.withAlpha (0.55f));
            for (float yy = top + 2; yy < b.getHeight(); yy += 3) g.fillRect (x, yy, bw, 1.0f);
            if (hold[i] > -70.0f) { g.setColour (col::bone1); g.fillRect (x, y (hold[i]) - 1, bw, 2.0f); }
        }
        if (clip[i])
        {
            g.setColour (col::sigClip.withAlpha (0.35f));
            g.fillRoundedRectangle (x - 1, -1, bw + 2, 7, 2.5f);
        }
        g.setColour (clip[i] ? col::sigClip : juce::Colour (0xff2a2d35));
        g.fillRoundedRectangle (x, 0, bw, 5, 1.5f);
    }

    if (side != ScaleSide::none)
    {
        g.setFont (Fonts::mono (8.0f));
        g.setColour (col::bone3);
        for (int db : { 6, 0, -6, -12, -18, -24, -30, -40, -50, -60 })
        {
            const juce::String t = db > 0 ? "+" + juce::String (db) : juce::String (db).replace ("-", juce::String::charToString (0x2212));
            const float yy = y ((float) db);
            if (side == ScaleSide::left) g.drawText (t, juce::Rectangle<float> (0, yy - 5, sw - 4, 10), juce::Justification::centredRight);
            else g.drawText (t, juce::Rectangle<float> (b.getWidth() - sw + 4, yy - 5, sw - 4, 10), juce::Justification::centredLeft);
        }
    }
}

void CorrelationView::push (float v, float dt)
{
    if (! std::isfinite (v)) v = 1.0f;
    value += (v - value) * (1.0f - std::exp (-dt / 0.2f));
    low = std::min (value, low + dt * 0.25f);
    repaint();
}

void CorrelationView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float h = r.getHeight(), w = r.getWidth();
    juce::Path shape; shape.addRoundedRectangle (r, h / 2);
    g.setColour (col::groove);
    g.fillPath (shape);
    auto x = [&] (float v) { return (v + 1.0f) / 2.0f * w; };
    const auto c = value < 0 ? col::sigClip : (value < 0.25f ? col::sigHot : col::sigOk);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (shape);
        g.setColour (col::sigClip.withAlpha (0.1f)); g.fillRect (0.0f, 0.0f, w / 2, h);
        g.setColour (col::sigOk.withAlpha (0.1f)); g.fillRect (w / 2, 0.0f, w / 2, h);
        g.setColour (c.withAlpha (0.55f));
        g.fillRect (juce::jmin (x (0), x (value)), 2.0f, std::abs (x (value) - x (0)), h - 4);
        g.setColour (col::bone1.withAlpha (0.35f)); g.fillRect (x (low) - 0.5f, 1.0f, 1.0f, h - 2);
        g.setColour (col::bone1.withAlpha (0.18f)); g.fillRect (x (0) - 0.5f, 0.0f, 1.0f, h);
    }
    g.setColour (c.withAlpha (0.35f));
    g.fillRoundedRectangle (x (value) - 4, -2, 8, h + 4, 3);
    g.setColour (col::bone1);
    g.fillRoundedRectangle (x (value) - 2, 0, 4, h, 2);
}

} // namespace aemon
