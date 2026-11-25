// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

EffectEditor::EffectEditor (EffectProcessor& p, int w, int h) : AudioProcessorEditor (p), base (p), baseW (w), baseH (h)
{
    setLookAndFeel (&lnf);
    shell = std::make_unique<Shell> (*this, p.pluginState, p.title, baseW, baseH);
    addAndMakeVisible (*shell);
}

EffectEditor::~EffectEditor()
{
    stopTimer();
    atts.clear();
    shell.reset();
    setLookAndFeel (nullptr);
}

void EffectEditor::start()
{
    shell->setModeComponents (simpleBody.get(), simpleDeck.get(), proBody.get(), proDeck.get());
    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    const double s = (double) base.pluginState.ui.getProperty ("scale", 1.0);
    setSize (juce::roundToInt (baseW * juce::jlimit (0.6, 2.0, s)), juce::roundToInt (baseH * juce::jlimit (0.6, 2.0, s)));
    setWantsKeyboardFocus (true);
    lastPreset = base.pluginState.current.name;
    seenLoads = base.stateLoads.load();
    for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->sweepIn (120 + (int) (i % 6) * 60);
    startTimerHz (60);
}

void EffectEditor::setReal (const juce::String& id, float v)
{
    if (auto* p = base.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

Knob& EffectEditor::knob (juce::Component& parent, const juce::String& id, Knob::Options o)
{
    auto k = std::make_unique<Knob> (*base.apvts.getParameter (id), std::move (o));
    k->onGestureStart = [this] { begin(); };
    k->onGestureEnd = [this] { end(); };
    auto& r = *k;
    parent.addAndMakeVisible (r);
    knobs.push_back (&r);
    owned.push_back (std::move (k));
    return r;
}

Button& EffectEditor::toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help, bool small)
{
    auto& b = make<Button> (parent, text, true);
    b.help = help; b.small = small;
    b.setClickingTogglesState (false);
    auto att = std::make_unique<juce::ParameterAttachment> (*base.apvts.getParameter (id), [&b] (float v) { b.setToggleState (v > 0.5f, juce::dontSendNotification); });
    att->sendInitialUpdate();
    auto* a = att.get();
    b.onClick = [this, &b, a] { begin(); a->setValueAsCompleteGesture (b.getToggleState() ? 0.0f : 1.0f); end(); };
    atts.push_back (std::move (att));
    return b;
}

Button& EffectEditor::button (juce::Component& parent, const juce::String& text, const juce::String& hint, const juce::String& help, std::function<void()> fn)
{
    auto& b = make<Button> (parent, text);
    b.hint = hint; b.help = help; b.onClick = std::move (fn);
    return b;
}

SegControl& EffectEditor::seg (juce::Component& parent, juce::StringArray names, juce::StringArray helps, float textHeight)
{
    auto& s = make<SegControl> (parent, std::move (names), std::move (helps));
    s.textHeight = textHeight;
    return s;
}

SegControl& EffectEditor::paramSeg (juce::Component& parent, const juce::String& id, juce::StringArray names, juce::StringArray helps, float textHeight)
{
    auto& s = seg (parent, std::move (names), std::move (helps), textHeight);
    s.attach (*base.apvts.getParameter (id), [this] { begin(); }, [this] { end(); });
    return s;
}

DeckPainter& EffectEditor::painter (juce::Component& parent) { return make<DeckPainter> (parent); }

void EffectEditor::timerCallback()
{
    ++frame;
    MeterFrame fr[128];
    const int n = base.popFrames (fr, 128);
    history.insert (history.end(), fr, fr + n);
    if ((int) history.size() > historyLen) history.erase (history.begin(), history.begin() + ((long) history.size() - historyLen));
    if (frame % 15 == 0)
    {
        const int lat = base.latencyNow.load();
        if (lat != shownLatency)
        {
            shownLatency = lat;
            const double rate = base.currentRate.load();
            const auto dot = "  " + juce::String::charToString (0xb7) + "  ";
            shell->setMeta (juce::String (rate / 1000.0, 1) + " kHz" + dot + "latency " + juce::String (1000.0 * lat / rate, 1) + " ms" + dot + "v" AEMON_VERSION);
        }
    }
    tick (shell->isSimple(), frame);

    const auto& cur = base.pluginState.current;
    if (seenLoads != base.stateLoads.load()) { seenLoads = base.stateLoads.load(); lastPreset = cur.name; }
    if (cur.name != lastPreset) { lastPreset = cur.name; const Preset chosen = cur; presetChosen (chosen); }
}

bool EffectEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (k.getModifiers().isShiftDown()) base.pluginState.history.redo (base.apvts);
        else base.pluginState.history.undo (base.apvts);
        shell->refreshHeader();
        return true;
    }
    return false;
}

void drawDashed (juce::Graphics& g, juce::Line<float> l, juce::Colour c, float thickness, float on, float off)
{
    juce::Path p; p.startNewSubPath (l.getStart()); p.lineTo (l.getEnd());
    juce::Path dashed; const float d[] = { on, off };
    juce::PathStrokeType (thickness).createDashedStroke (dashed, p, d, 2);
    g.setColour (c); g.fillPath (dashed);
}

void drawDbAxis (juce::Graphics& g, juce::Rectangle<float> a, std::initializer_list<float> dbs, const std::function<float (float)>& y, bool plus)
{
    g.setFont (Fonts::mono (9.0f));
    for (float d : dbs)
    {
        g.setColour (col::bone1.withAlpha (0.055f)); g.fillRect (a.getX(), std::round (y (d)) + 0.5f, a.getWidth(), 1.0f);
        g.setColour (col::bone3);
        g.drawText ((plus && d > 0 ? "+" : "") + txt::num (d, 0), juce::Rectangle<float> (a.getX() - 36, y (d) - 6, 30, 12), juce::Justification::centredRight);
    }
}

void ReadoutPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawWell (g, b, 8);
    auto r = b.reduced (12);
    drawEngraved (g, heading, r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredLeft);
    drawEngraved (g, note, r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredRight);
    r.removeFromTop (24);
    drawEngraved (g, bigName, r.removeFromTop (14), 10.0f, col::bone3, juce::Justification::centredLeft);
    auto big = r.removeFromTop (36);
    g.setFont (Fonts::mono (30.0f)); g.setColour (col::bone1);
    g.drawText (bigValue, big, juce::Justification::centredLeft);
    drawEngraved (g, unit, big.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (Fonts::mono (30.0f), bigValue) + 8).withTrimmedTop (12), 10.5f, col::bone3, juce::Justification::centredLeft);
    r.removeFromTop (8);
    for (auto& row : rows)
    {
        auto rr = r.removeFromTop (30);
        g.setColour (col::lineSoft); g.fillRect (rr.withHeight (1));
        drawEngraved (g, row.name, rr, 10.0f, col::bone3, juce::Justification::centredLeft);
        g.setFont (Fonts::mono (12.5f)); g.setColour (row.hot ? col::emberHi : col::bone1);
        g.drawText (row.value, rr, juce::Justification::centredRight);
    }
}

} // namespace aemon
