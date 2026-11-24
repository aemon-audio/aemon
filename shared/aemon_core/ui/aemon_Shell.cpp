// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

namespace
{
    juce::Path chevron (bool left)
    {
        juce::Path p;
        if (left) { p.startNewSubPath (9, 2); p.lineTo (4, 7); p.lineTo (9, 12); }
        else { p.startNewSubPath (5, 2); p.lineTo (10, 7); p.lineTo (5, 12); }
        return p;
    }
    juce::Path undoIcon (bool redo)
    {
        juce::Path p;
        if (! redo)
        {
            p.startNewSubPath (4.5f, 5.5f); p.lineTo (9, 5.5f);
            p.addCentredArc (9, 8.5f, 3, 3, 0, 0, juce::MathConstants<float>::pi, false);
            p.lineTo (6, 11.5f);
            p.startNewSubPath (6.5f, 3); p.lineTo (4, 5.5f); p.lineTo (6.5f, 8);
        }
        else
        {
            p.startNewSubPath (9.5f, 5.5f); p.lineTo (5, 5.5f);
            p.addCentredArc (5, 8.5f, 3, 3, 0, 0, -juce::MathConstants<float>::pi, false);
            p.lineTo (8, 11.5f);
            p.startNewSubPath (7.5f, 3); p.lineTo (10, 5.5f); p.lineTo (7.5f, 8);
        }
        return p;
    }

    /** Draws the header's text buttons: preset box, A/B, size. */
    struct HeaderLnf : LookAndFeel
    {
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            const auto kind = b.getProperties()["kind"].toString();
            if (kind == "preset")
            {
                g.setColour (col::basalt0); g.fillRoundedRectangle (r, 5);
                g.setColour (over ? col::line : col::lineSoft); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
                juce::Path caret; caret.startNewSubPath (r.getRight() - 16, r.getCentreY() - 2); caret.lineTo (r.getRight() - 13, r.getCentreY() + 1); caret.lineTo (r.getRight() - 10, r.getCentreY() - 2);
                g.setColour (col::bone3); g.strokePath (caret, juce::PathStrokeType (1.6f));
            }
            else if (kind == "ab")
            {
                if (b.getToggleState()) drawRaised (g, r.reduced (0.5f), 3, false);
            }
            else if (kind == "scale")
            {
                g.setColour (over ? col::line : col::lineSoft); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
            }
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            const auto kind = b.getProperties()["kind"].toString();
            if (kind == "preset")
            {
                g.setFont (Fonts::textMedium (12.5f));
                g.setColour (col::bone1);
                auto tr = r.reduced (10, 0).withTrimmedRight (14);
                const bool edited = (bool) b.getProperties()["edited"];
                if (edited)
                {
                    auto er = tr.removeFromRight (40);
                    g.setFont (Fonts::text (11.0f)); g.setColour (col::ember);
                    g.drawText ("edited", er, juce::Justification::centredRight);
                    g.setFont (Fonts::textMedium (12.5f)); g.setColour (col::bone1);
                }
                g.drawText (b.getButtonText(), tr, juce::Justification::centredLeft, true);
            }
            else if (kind == "ab")
            {
                drawEngraved (g, b.getButtonText(), r, b.getButtonText().length() > 1 ? 9.5f : 11.0f,
                              b.getToggleState() ? col::emberHi : (over ? col::bone1 : col::bone3), juce::Justification::centred, 0.04f);
            }
            else
            {
                g.setFont (Fonts::mono (10.5f));
                g.setColour (over ? col::bone1 : col::bone3);
                g.drawText (b.getButtonText(), r, juce::Justification::centred);
            }
        }
    };
}

//==============================================================================
IconButton::IconButton (const juce::String& n, juce::Path p, juce::String h) : juce::Button (n), path (std::move (p)), help (std::move (h))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool en = isEnabled();
    if (over && en) { g.setColour (down ? col::basalt2 : col::basalt3); g.fillRoundedRectangle (r, 5); g.setColour (col::edge); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1); }
    auto p = path;
    p.applyTransform (juce::AffineTransform::translation (r.getCentreX() - 7, r.getCentreY() - 7));
    g.setColour ((over && en ? col::bone1 : col::bone2).withMultipliedAlpha (en ? 1.0f : 0.3f));
    g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

PowerButton::PowerButton() : juce::Button ("Bypass")
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PowerButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    const bool active = ! getToggleState();
    if (active) { g.setColour (col::ember.withAlpha (0.25f)); g.fillEllipse (r.expanded (1)); }
    juce::ColourGradient grad (col::basalt4, r.getX() + r.getWidth() * 0.4f, r.getY() + r.getHeight() * 0.3f, col::basalt2, r.getRight(), r.getBottom(), true);
    g.setGradientFill (grad);
    g.fillEllipse (r);
    g.setColour (col::edge); g.drawEllipse (r.reduced (0.5f), 1);
    const auto c = r.getCentre();
    juce::Path p;
    p.addCentredArc (c.x, c.y, 4.6f, 4.6f, 0, juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
    p.startNewSubPath (c.x, c.y - 5.5f); p.lineTo (c.x, c.y - 0.5f);
    g.setColour (active ? col::ember : (over ? col::bone2 : col::bone3));
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
Shell::Shell (juce::AudioProcessorEditor& ed, PluginState& st, juce::String t, int w, int h)
    : editor (ed), state (st), title (std::move (t)), baseW (w), baseH (h),
      prevBtn ("Previous preset", chevron (true), "Previous preset."),
      nextBtn ("Next preset", chevron (false), "Next preset."),
      undoBtn ("Undo", undoIcon (false), "Undo (Ctrl/Cmd+Z)."),
      redoBtn ("Redo", undoIcon (true), "Redo (Shift+Ctrl/Cmd+Z).")
{
    headerLnf = std::make_unique<HeaderLnf>();
    addAndMakeVisible (content);
    content.setSize (baseW, baseH);
    content.setWantsKeyboardFocus (true);
    content.addMouseListener (this, true);

    for (auto* c : std::initializer_list<juce::Component*> { &modeSeg, &prevBtn, &presetBox, &nextBtn, &saveBtn, &undoBtn, &redoBtn, &abA, &abCopy, &abB, &scaleBtn, &power })
        content.addAndMakeVisible (c);
    for (auto* b : { &presetBox, &abA, &abCopy, &abB, &scaleBtn }) { b->setLookAndFeel (headerLnf.get()); b->setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    presetBox.getProperties().set ("kind", "preset");
    for (auto* b : { &abA, &abCopy, &abB }) b->getProperties().set ("kind", "ab");
    scaleBtn.getProperties().set ("kind", "scale");
    saveBtn.small = true;
    saveBtn.help = "Save the current setting as a preset you can reuse.";
    setHelp (presetBox, "Presets: click to browse. \"edited\" means you changed something since loading it.");
    setHelp (abA, "A/B: keep two settings and flip between them to compare.");
    setHelp (abB, "A/B: keep two settings and flip between them to compare.");
    setHelp (abCopy, "Copy the current setting into the other A/B slot.");
    setHelp (scaleBtn, "Window size: click to step through 75-200%, or drag the bottom-right corner.");
    modeSeg.textHeight = 10.5f;

    if (auto* bp = dynamic_cast<juce::RangedAudioParameter*> (state.apvts.getParameter ("bypass")))
        powerAtt = std::make_unique<juce::ButtonParameterAttachment> (*dynamic_cast<juce::AudioParameterBool*> (bp), power);

    modeSeg.setIndex (isSimple() ? 0 : 1, false);
    modeSeg.onChange = [this] (int i) { state.ui.setProperty ("mode", i == 0 ? "simple" : "pro", nullptr); applyMode (true); };
    prevBtn.onClick = [this] { state.stepPreset (-1); refreshHeader(); toastPreset(); };
    nextBtn.onClick = [this] { state.stepPreset (1); refreshHeader(); toastPreset(); };
    presetBox.onClick = [this] { showPresetMenu(); };
    saveBtn.onClick = [this] { startSaveAs(); };
    undoBtn.onClick = [this] { state.history.undo (state.apvts); refreshHeader(); toast ("Undo"); };
    redoBtn.onClick = [this] { state.history.redo (state.apvts); refreshHeader(); toast ("Redo"); };
    abA.onClick = [this] { state.switchAB (0); refreshHeader(); toast ("Now on A"); };
    abB.onClick = [this] { state.switchAB (1); refreshHeader(); toast ("Now on B"); };
    abCopy.onClick = [this] { state.copyAB(); toast (juce::String ("Copied ") + (state.abIndex ? "B to A" : "A to B")); };
    scaleBtn.onClick = [this]
    {
        static const float sizes[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
        const float cur = (float) getWidth() / (float) baseW;
        float next = sizes[0];
        for (float s : sizes) if (s > cur + 0.01f) { next = s; break; }
        editor.setSize (juce::roundToInt (baseW * next), juce::roundToInt (baseH * next));
    };
    state.history.onChange = [this] { refreshHeader(); };

    statusText = defaultHint = "Hover any control for help. Every value can be typed: click the number.";
    refreshHeader();
    startTimerHz (30);
}

Shell::~Shell()
{
    state.history.onChange = nullptr;
    for (auto* b : { &presetBox, &abA, &abCopy, &abB, &scaleBtn }) b->setLookAndFeel (nullptr);
}

void Shell::setModeComponents (juce::Component* sb, juce::Component* sd, juce::Component* pb, juce::Component* pd)
{
    sBody = sb; sDeck = sd; pBody = pb; pDeck = pd;
    for (auto* c : { sBody, sDeck, pBody, pDeck }) if (c) content.addChildComponent (c);
    applyMode (false);
}

void Shell::applyMode (bool animate)
{
    const bool simple = isSimple();
    if (sBody) sBody->setVisible (simple);
    if (sDeck) sDeck->setVisible (simple);
    if (pBody) pBody->setVisible (! simple);
    if (pDeck) pDeck->setVisible (! simple);
    modeSeg.setIndex (simple ? 0 : 1, false);
    if (animate) flipStart = juce::Time::getMillisecondCounterHiRes();
    layoutContent();
    if (onModeChanged) onModeChanged (simple);
    content.repaint();
}

void Shell::toastPreset()
{
    const auto& p = state.current;
    toast (p.description.isNotEmpty() ? p.name + ": " + p.description : "Preset: " + p.name);
}

void Shell::refreshHeader()
{
    presetBox.setButtonText (state.current.name);
    setHelp (presetBox, state.current.description.isNotEmpty()
                          ? "Preset \"" + state.current.name + "\": " + state.current.description + " Click to browse presets."
                          : juce::String ("Presets: click to browse. \"edited\" means you changed something since loading it."));
    presetBox.getProperties().set ("edited", state.isEdited());
    presetBox.repaint();
    undoBtn.setEnabled (state.history.canUndo());
    redoBtn.setEnabled (state.history.canRedo());
    abA.setToggleState (state.abIndex == 0, juce::dontSendNotification);
    abB.setToggleState (state.abIndex == 1, juce::dontSendNotification);
    abCopy.setButtonText (state.abIndex == 0 ? "A>B" : "B>A");
    scaleBtn.setButtonText (juce::String (juce::roundToInt (100.0f * (float) getWidth() / (float) baseW)) + "%");
}

void Shell::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    content.setTransform (juce::AffineTransform::scale (s));
    state.ui.setProperty ("scale", s, nullptr);
    refreshHeader();
    layoutContent();
}

void Shell::layoutContent()
{
    // header
    auto h = juce::Rectangle<int> (0, 0, baseW, headerH).reduced (12, 0);
    h.removeFromLeft (2);
    auto brandW = (int) juce::GlyphArrangement::getStringWidth (Fonts::display (11.5f).withExtraKerningFactor (0.22f), "AEMON")
                + (int) juce::GlyphArrangement::getStringWidth (Fonts::label (11.5f).withExtraKerningFactor (0.24f), title.toUpperCase()) + 56;
    h.removeFromLeft (brandW);
    modeSeg.setBounds (h.removeFromLeft (124).withSizeKeepingCentre (124, 28));
    auto tools = h.removeFromRight (232);
    power.setBounds (tools.removeFromRight (30).withSizeKeepingCentre (28, 28));
    tools.removeFromRight (6);
    scaleBtn.setBounds (tools.removeFromRight (46).withSizeKeepingCentre (46, 24));
    tools.removeFromRight (8);
    auto ab = tools.removeFromRight (84).withSizeKeepingCentre (84, 26);
    abB.setBounds (ab.removeFromRight (24).reduced (0, 2));
    abA.setBounds (ab.removeFromLeft (24).reduced (0, 2));
    abCopy.setBounds (ab.reduced (2, 2));
    tools.removeFromRight (6);
    redoBtn.setBounds (tools.removeFromRight (26).withSizeKeepingCentre (26, 26));
    undoBtn.setBounds (tools.removeFromRight (26).withSizeKeepingCentre (26, 26));
    auto p = h.reduced (10, 0);
    const int pw = 26 + 3 + 232 + 3 + 26 + 4 + 48;
    p = p.withSizeKeepingCentre (juce::jmin (pw, p.getWidth()), 28);
    prevBtn.setBounds (p.removeFromLeft (26)); p.removeFromLeft (3);
    saveBtn.setBounds (p.removeFromRight (48).withSizeKeepingCentre (48, 24)); p.removeFromRight (4);
    nextBtn.setBounds (p.removeFromRight (26)); p.removeFromRight (3);
    presetBox.setBounds (p);
    if (nameEditor) nameEditor->setBounds (presetBox.getBounds().reduced (2));

    const auto body = juce::Rectangle<int> (0, headerH, baseW, baseH - headerH - deckH - statusH);
    const auto deck = juce::Rectangle<int> (0, baseH - deckH - statusH, baseW, deckH);
    for (auto* c : { sBody, pBody }) if (c) c->setBounds (body);
    for (auto* c : { sDeck, pDeck }) if (c) c->setBounds (deck);
}

void Shell::paint (juce::Graphics& g) { g.fillAll (col::basalt2); }

void Shell::paintContent (juce::Graphics& g)
{
    g.fillAll (col::basalt2);
    // header
    auto hr = juce::Rectangle<float> (0, 0, (float) baseW, (float) headerH);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff23252d), 0, juce::Colour (0xff1b1d23), (float) headerH));
    g.fillRect (hr);
    g.setColour (juce::Colour (0xff0b0c0f));
    g.fillRect (0.0f, (float) headerH - 1, (float) baseW, 1.0f);
    float x = 16;
    drawMark (g, { x, 15, 18, 18 }, col::bone1);
    x += 27;
    const auto wm = Fonts::display (11.5f).withExtraKerningFactor (0.22f);
    g.setFont (wm); g.setColour (col::bone1);
    const float wmW = juce::GlyphArrangement::getStringWidth (wm, "AEMON");
    g.drawText ("AEMON", juce::Rectangle<float> (x, 0, wmW + 4, (float) headerH), juce::Justification::centredLeft);
    x += wmW + 9;
    g.setColour (col::line); g.fillRect (x, 17.0f, 1.0f, 14.0f);
    x += 9;
    drawEngraved (g, title, { x, 0, 120, (float) headerH }, 11.5f, col::ember, juce::Justification::centredLeft, 0.24f);

    // deck plate (brushed) and screws
    auto dr = juce::Rectangle<float> (0, (float) (baseH - deckH - statusH), (float) baseW, (float) deckH);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff282b34), dr.getY(), juce::Colour (0xff1f2128), dr.getBottom()));
    g.fillRect (dr);
    g.setColour (juce::Colours::white.withAlpha (0.013f));
    for (float xx = 0; xx < (float) baseW; xx += 3) g.fillRect (xx, dr.getY(), 1.0f, dr.getHeight());
    g.setColour (juce::Colour (0xff0b0c0f)); g.fillRect (dr.withHeight (1));
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRect (dr.withHeight (1).translated (0, 1));
    auto screw = [&] (float cx, float cy, float rot)
    {
        juce::ColourGradient sg (juce::Colour (0xff6b6f7b), cx - 1.5f, cy - 1.8f, juce::Colour (0xff121317), cx + 4.5f, cy + 4.5f, true);
        sg.addColour (0.6, juce::Colour (0xff2a2d34));
        g.setGradientFill (sg);
        g.fillEllipse (cx - 4.5f, cy - 4.5f, 9, 9);
        g.setColour (juce::Colour (0xff0e0f12));
        const float a = juce::degreesToRadians (rot);
        g.drawLine (cx - 3 * std::cos (a), cy - 3 * std::sin (a), cx + 3 * std::cos (a), cy + 3 * std::sin (a), 1.4f);
    };
    screw (12.5f, dr.getY() + 11.5f, 35); screw ((float) baseW - 12.5f, dr.getY() + 11.5f, -20);
    screw (12.5f, dr.getBottom() - 11.5f, 80); screw ((float) baseW - 12.5f, dr.getBottom() - 11.5f, 10);

    // status strip
    auto sr = juce::Rectangle<float> (0, (float) (baseH - statusH), (float) baseW, (float) statusH);
    g.setColour (juce::Colour (0xff121317)); g.fillRect (sr);
    g.setColour (juce::Colour (0xff0b0c0f)); g.fillRect (sr.withHeight (1));
    auto st = sr.reduced (12, 0);
    g.setFont (Fonts::mono (10.5f)); g.setColour (col::bone3);
    const float mw = juce::GlyphArrangement::getStringWidth (Fonts::mono (10.5f), meta) + 4;
    g.drawText (meta, st.removeFromRight (mw), juce::Justification::centredRight);
    g.setFont (Fonts::text (11.0f)); g.setColour (col::bone2);
    g.drawText (statusText, st.withTrimmedRight (40), juce::Justification::centredLeft, true);
}

void Shell::paintContentOver (juce::Graphics& g)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto bodyDeck = juce::Rectangle<float> (0, (float) headerH, (float) baseW, (float) (baseH - headerH - statusH));
    if (bypassed) { g.setColour (col::basalt2.withAlpha (0.5f)); g.fillRect (bodyDeck); }
    if (flipStart > 0)
    {
        const float k = (float) ((now - flipStart) / 320.0);
        if (k < 1.0f) { g.setColour (col::basalt2.withAlpha (0.85f * (1.0f - k) * (1.0f - k))); g.fillRect (bodyDeck); }
        else flipStart = 0;
    }
    if (toastAlpha > 0.01f)
    {
        g.setFont (Fonts::textMedium (12.0f));
        const float tw = juce::jmin (520.0f, juce::GlyphArrangement::getStringWidth (Fonts::textMedium (12.0f), toastText) + 30);
        const int lines = tw >= 520.0f ? 2 : 1;
        auto r = juce::Rectangle<float> (tw, 18.0f + 16.0f * (float) lines).withCentre ({ baseW / 2.0f, (float) (baseH - statusH - 30) - 8.0f * (1.0f - toastAlpha) });
        g.setColour (juce::Colours::black.withAlpha (0.4f * toastAlpha));
        g.fillRoundedRectangle (r.translated (0, 4).expanded (4), 8);
        g.setColour (juce::Colour (0xff26292f).withAlpha (toastAlpha));
        g.fillRoundedRectangle (r, 6);
        g.setColour (col::edge.withAlpha (toastAlpha)); g.drawRoundedRectangle (r, 6, 1);
        g.setColour (col::bone1.withAlpha (toastAlpha));
        g.drawFittedText (toastText, r.reduced (14, 6).toNearestInt(), juce::Justification::centred, lines);
    }
}

void Shell::paintOverChildren (juce::Graphics&) {}

void Shell::toast (const juce::String& t)
{
    toastText = t;
    toastUntil = juce::Time::getMillisecondCounterHiRes() + 2200.0 + 45.0 * t.length();   // longer messages stay longer
}

void Shell::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float want = now < toastUntil ? 1.0f : 0.0f;
    if (std::abs (want - toastAlpha) > 0.01f || flipStart > 0)
    {
        toastAlpha += (want - toastAlpha) * 0.3f;
        content.repaint();
    }
    if (auto* bp = state.apvts.getRawParameterValue ("bypass"))
    {
        const bool b = bp->load() > 0.5f;
        if (b != bypassed) { bypassed = b; content.repaint(); }
    }
    const bool edited = state.isEdited();
    if ((bool) presetBox.getProperties()["edited"] != edited) refreshHeader();
}

void Shell::updateHintFrom (juce::Component* c)
{
    for (auto* x = c; x != nullptr && x != &content; x = x->getParentComponent())
    {
        if (auto* hp = dynamic_cast<HelpProvider*> (x)) { auto t = hp->getHelpText(); if (t.isNotEmpty()) { setHint (t); return; } }
        if (x->getProperties().contains ("aemonHelp")) { setHint (x->getProperties()["aemonHelp"].toString()); return; }
    }
    setHint (defaultHint);
}

void Shell::mouseEnter (const juce::MouseEvent& e) { updateHintFrom (e.eventComponent); }
void Shell::mouseMove (const juce::MouseEvent& e)  { updateHintFrom (e.eventComponent); }
void Shell::mouseDrag (const juce::MouseEvent& e)  { updateHintFrom (e.eventComponent); }
void Shell::mouseExit (const juce::MouseEvent&)    { setHint (defaultHint); }

void Shell::showPresetMenu()
{
    juce::PopupMenu m;
    m.setLookAndFeel (headerLnf.get());
    auto all = state.allPresets();
    auto isCur = [this] (const Preset* p) { return p->name == state.current.name && p->user == state.current.user; };
    // factory presets, grouped by category (in order of first appearance)
    juce::StringArray cats;
    for (auto* p : all) if (! p->user && p->category.isNotEmpty()) cats.addIfNotAlreadyThere (p->category);
    m.addSectionHeader ("Factory");
    for (int i = 0; i < (int) all.size(); ++i)
        if (! all[(size_t) i]->user && all[(size_t) i]->category.isEmpty())
            m.addItem (i + 1, all[(size_t) i]->name, true, isCur (all[(size_t) i]));
    for (auto& c : cats)
    {
        juce::PopupMenu sub;
        bool hasCur = false;
        for (int i = 0; i < (int) all.size(); ++i)
            if (! all[(size_t) i]->user && all[(size_t) i]->category == c)
            {
                sub.addItem (i + 1, all[(size_t) i]->name, true, isCur (all[(size_t) i]));
                hasCur |= isCur (all[(size_t) i]);
            }
        m.addSubMenu (c, sub, true, nullptr, hasCur);
    }
    bool anyUser = false;
    for (int i = 0; i < (int) all.size(); ++i)
        if (all[(size_t) i]->user)
        {
            if (! anyUser) { m.addSectionHeader ("Yours"); anyUser = true; }
            m.addItem (i + 1, all[(size_t) i]->name, true, isCur (all[(size_t) i]));
        }
    m.addSeparator();
    m.addItem (1000, "Save current as...");
    m.addItem (1001, "Show presets folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBox).withMinimumWidth (presetBox.getWidth()),
        [this, all] (int r)
        {
            if (r == 1000) startSaveAs();
            else if (r == 1001) { state.userPresetFolder().createDirectory(); state.userPresetFolder().revealToUser(); }
            else if (r > 0 && r <= (int) all.size()) { state.loadPreset (*all[(size_t) r - 1]); refreshHeader(); toastPreset(); }
        });
}

void Shell::startSaveAs()
{
    nameEditor = std::make_unique<juce::TextEditor>();
    nameEditor->setFont (Fonts::textMedium (12.5f));
    nameEditor->setIndents (8, 5);
    nameEditor->setText ("My setting " + juce::String (state.userPresets.size() + 1));
    nameEditor->selectAll();
    content.addAndMakeVisible (*nameEditor);
    nameEditor->setBounds (presetBox.getBounds().reduced (2));
    nameEditor->grabKeyboardFocus();
    auto finish = [this] (bool ok)
    {
        if (! nameEditor) return;
        const auto txt = nameEditor->getText().trim();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Shell> (this)] { if (safe) safe->nameEditor.reset(); });
        nameEditor->setVisible (false);
        if (ok && txt.isNotEmpty())
        {
            if (state.saveUserPreset (txt)) toast ("Saved \"" + txt + "\" to Documents/Aemon/" + state.name + " Presets.");
            else toast ("Could not save the preset. Check that your Documents folder is writable.");
            refreshHeader();
        }
    };
    nameEditor->onReturnKey = [finish] { finish (true); };
    nameEditor->onEscapeKey = [finish] { finish (false); };
    nameEditor->onFocusLost = [finish] { finish (true); };
}

} // namespace aemon
