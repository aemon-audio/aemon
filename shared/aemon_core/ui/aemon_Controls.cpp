// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

namespace
{
    constexpr float sweepDeg = 270.0f;
    inline juce::Point<float> polar (juce::Point<float> c, float r, float deg)
    {
        const float a = juce::degreesToRadians (deg);
        return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
    }
    inline juce::Path arc (juce::Point<float> c, float r, float d0, float d1)
    {
        juce::Path p;
        if (std::abs (d1 - d0) < 0.3f) return p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, juce::degreesToRadians (juce::jmin (d0, d1)), juce::degreesToRadians (juce::jmax (d0, d1)), true);
        return p;
    }
    inline float normToDeg (float n) { return -135.0f + sweepDeg * n; }
    inline double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

//==============================================================================
Knob::Knob (juce::RangedAudioParameter& p, Options o)
    : param (p), opt (std::move (o)),
      attachment (p, [this] (float v)
      {
          target = param.convertTo0to1 (v);
          if (dragging || inGesture) shown = target;
          if (! valueLabel.isBeingEdited()) valueLabel.setText (valueText(), juce::dontSendNotification);
          repaint();
          if (! isTimerRunning()) startTimerHz (60);
      })
{
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (true);
    target = shown = param.getValue();
    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (Fonts::mono (opt.style == Style::hero ? 11.5f : 11.0f));
    valueLabel.setEditable (true, true, false);
    valueLabel.setColour (juce::Label::textColourId, col::bone1);
    valueLabel.onEditorShow = [this]
    {
        if (auto* ed = valueLabel.getCurrentTextEditor())
        {
            ed->setJustification (juce::Justification::centred);
            ed->setFont (Fonts::mono (11.5f));
            ed->setText (param.getCurrentValueAsText(), false);
            ed->selectAll();
        }
    };
    valueLabel.onTextChange = [this]
    {
        auto txt = valueLabel.getText().replace (juce::String::charToString (0x2212), "-").trim();
        std::optional<float> v;
        if (opt.parse) v = opt.parse (txt);
        else if (txt.containsAnyOf ("0123456789")) v = txt.retainCharacters ("-+.0123456789").getFloatValue();
        if (v)
        {
            if (onGestureStart) onGestureStart();
            const float vv = juce::jlimit (param.getNormalisableRange().start, param.getNormalisableRange().end, *v);
            attachment.setValueAsCompleteGesture (vv);
            if (onUserChange) onUserChange (vv);
            if (onGestureEnd) onGestureEnd();
        }
        valueLabel.setText (valueText(), juce::dontSendNotification);
    };
    if (opt.showValue) addAndMakeVisible (valueLabel);
    attachment.sendInitialUpdate();
}

Knob::~Knob() = default;

juce::String Knob::valueText() const
{
    const float v = param.convertFrom0to1 (shown);
    if (opt.format) return opt.format (v);
    return param.getText (shown, 32) + (param.getLabel().isNotEmpty() ? " " + param.getLabel() : juce::String());
}

juce::String Knob::getHelpText() const
{
    const float v = param.convertFrom0to1 (target);
    return (opt.name.isNotEmpty() ? opt.name : param.getName (32)) + ": " + (opt.format ? opt.format (v) : param.getCurrentValueAsText())
           + ". " + opt.help;
}

void Knob::resized()
{
    auto k = knobArea();
    const int w = opt.style == Style::hero ? 66 : 56;
    valueLabel.setBounds (juce::Rectangle<int> (w, 20).withCentre ({ (int) k.getCentreX(), 0 }).withY ((int) k.getBottom() + 5));
}

juce::Rectangle<float> Knob::knobArea() const
{
    auto b = getLocalBounds().toFloat();
    const float valueH = opt.showValue ? 25.0f : 0.0f;
    const float side = juce::jmin (b.getWidth(), b.getHeight() - valueH);
    return { b.getCentreX() - side / 2, b.getY(), side, side };
}

void Knob::paint (juce::Graphics& g)
{
    const float n = ui (juce::jlimit (0.0f, 1.0f, shown));
    auto k = knobArea();
    if (opt.style == Style::hero) paintHero (g, k, n);
    else paintCompact (g, k, n);

    if (opt.showValue)
    {
        auto vb = valueLabel.getBounds().toFloat();
        g.setColour (col::basalt0);
        g.fillRoundedRectangle (vb, 3.0f);
        const bool active = dragging || hasKeyboardFocus (true);
        g.setColour (active ? col::emberLo : (isMouseOver (true) ? col::line : col::lineSoft));
        g.drawRoundedRectangle (vb.reduced (0.5f), 3.0f, 1.0f);
        valueLabel.setColour (juce::Label::textColourId, active ? col::emberHi : col::bone1);
    }
}

void Knob::paintHero (juce::Graphics& g, juce::Rectangle<float> area, float n)
{
    const float u = area.getWidth() / 126.0f;
    const juce::Point<float> c (area.getX() + 63 * u, area.getY() + 63 * u);
    const float d = normToDeg (n);
    const float startDeg = opt.bipolar ? normToDeg (centreNorm()) : -135.0f;
    const bool active = dragging;

    g.setColour (col::groove);
    g.strokePath (arc (c, 45 * u, -135, 135), juce::PathStrokeType (7.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::basalt3);
    g.strokePath (arc (c, 45 * u, -135, 135), juce::PathStrokeType (4.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto va = arc (c, 45 * u, startDeg, d);
    if (! va.isEmpty())
    {
        g.setColour (col::ember.withAlpha (0.22f));
        g.strokePath (va, juce::PathStrokeType (10.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (active ? col::emberHi : col::ember);
        g.strokePath (va, juce::PathStrokeType (4.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    for (int i = 0; i < opt.ticks; ++i)
    {
        const float td = -135.0f + sweepDeg * (float) i / (float) (opt.ticks - 1);
        const bool major = i == 0 || i == opt.ticks - 1 || (opt.bipolar && std::abs (td - startDeg) < 0.5f);
        const auto p = polar (c, 54 * u, td);
        const float r = (major ? 1.8f : 1.05f) * u;
        g.setColour (major ? col::bone2 : col::bone3.withAlpha (0.7f));
        g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
    }
    g.setFont (Fonts::mono (8.0f * u));
    g.setColour (col::bone3);
    auto lbl = [&] (const juce::String& s, juce::Point<float> p)
    { g.drawText (s, juce::Rectangle<float> (40 * u, 10 * u).withCentre (p), juce::Justification::centred, false); };
    if (opt.minLabel.isNotEmpty()) lbl (opt.minLabel, polar (c, 57 * u, -135) + juce::Point<float> (-3 * u, 8 * u));
    if (opt.maxLabel.isNotEmpty()) lbl (opt.maxLabel, polar (c, 57 * u, 135) + juce::Point<float> (3 * u, 8 * u));
    if (opt.centreLabel.isNotEmpty()) lbl (opt.centreLabel, polar (c, 61 * u, startDeg) + juce::Point<float> (0, -2 * u));

    // cap: shadow, rim, knurl (rotates), lit face (static light), pointer
    juce::Path capShape; capShape.addEllipse (c.x - 36 * u, c.y - 36 * u, 72 * u, 72 * u);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (6 * u), { 0, (int) std::round (3 * u) }).drawForPath (g, capShape);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff707584), c.y - 35 * u, juce::Colour (0xff0c0d10), c.y + 35 * u));
    g.fillEllipse (c.x - 35 * u, c.y - 35 * u, 70 * u, 70 * u);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    for (int i = 0; i < 72; ++i)
    {
        const float kd = d + (float) i * 5.0f;
        const auto a = polar (c, 31.5f * u, kd), b = polar (c, 35 * u, kd);
        g.drawLine ({ a, b }, 1.1f * u);
    }
    {
        juce::ColourGradient face (juce::Colour (0xff5b606e), c.x - 31.5f * u + 0.38f * 63 * u, c.y - 31.5f * u + 0.28f * 63 * u,
                                   juce::Colour (0xff16171c), c.x + 30 * u, c.y + 34 * u, true);
        face.addColour (0.42, juce::Colour (0xff2e313a));
        g.setGradientFill (face);
        g.fillEllipse (c.x - 31.5f * u, c.y - 31.5f * u, 63 * u, 63 * u);
        juce::ColourGradient vig (juce::Colours::transparentBlack, c.x, c.y, juce::Colours::black.withAlpha (0.35f), c.x + 31.5f * u, c.y, true);
        vig.addColour (0.55, juce::Colours::transparentBlack);
        g.setGradientFill (vig);
        g.fillEllipse (c.x - 31.5f * u, c.y - 31.5f * u, 63 * u, 63 * u);
    }
    if (hasKeyboardFocus (true))
    {
        g.setColour (col::ember);
        g.drawEllipse (c.x - 37.5f * u, c.y - 37.5f * u, 75 * u, 75 * u, 1.5f * u);
    }
    const auto p0 = polar (c, 14 * u, d), p1 = polar (c, 27.5f * u, d);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawLine ({ p0, p1 }, 5.2f * u);
    g.setColour (col::bone1);
    juce::Path ptr; ptr.startNewSubPath (p0); ptr.lineTo (p1);
    g.strokePath (ptr, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Knob::paintCompact (juce::Graphics& g, juce::Rectangle<float> area, float n)
{
    const float u = area.getWidth() / 100.0f;
    const juce::Point<float> c = area.getCentre();
    const float d = normToDeg (n);
    const float startDeg = opt.bipolar ? normToDeg (centreNorm()) : -135.0f;
    g.setColour (col::groove);
    g.strokePath (arc (c, 41 * u, -135, 135), juce::PathStrokeType (10.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::basalt3);
    g.strokePath (arc (c, 41 * u, -135, 135), juce::PathStrokeType (7.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto va = arc (c, 41 * u, startDeg, d);
    if (! va.isEmpty())
    {
        g.setColour (col::ember.withAlpha (0.2f));
        g.strokePath (va, juce::PathStrokeType (12 * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (dragging ? col::emberHi : col::ember);
        g.strokePath (va, juce::PathStrokeType (7.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Path cap; cap.addEllipse (c.x - 28 * u, c.y - 28 * u, 56 * u, 56 * u);
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), (int) (5 * u), { 0, (int) std::round (2.5f * u) }).drawForPath (g, cap);
    juce::ColourGradient face (juce::Colour (0xff5b606e), c.x - 8 * u, c.y - 14 * u, juce::Colour (0xff16171c), c.x + 24 * u, c.y + 26 * u, true);
    face.addColour (0.42, juce::Colour (0xff2e313a));
    g.setGradientFill (face);
    g.fillPath (cap);
    if (hasKeyboardFocus (true)) { g.setColour (col::ember); g.drawEllipse (c.x - 31 * u, c.y - 31 * u, 62 * u, 62 * u, 1.5f * u); }
    const auto dot = polar (c, 19 * u, d);
    g.setColour (col::bone1);
    g.fillEllipse (dot.x - 3.6f * u, dot.y - 3.6f * u, 7.2f * u, 7.2f * u);
}

void Knob::beginGesture()
{
    if (inGesture) return;
    inGesture = true;
    if (onGestureStart) onGestureStart();
    attachment.beginGesture();
}

void Knob::endGesture()
{
    if (! inGesture) return;
    inGesture = false;
    attachment.endGesture();
    if (onGestureEnd) onGestureEnd();
}

void Knob::setNormFromUi (float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    const float v = param.convertFrom0to1 (n);
    shown = target = param.convertTo0to1 (param.getNormalisableRange().snapToLegalValue (v));
    attachment.setValueAsPartOfGesture (v);
    if (onUserChange) onUserChange (v);
    valueLabel.setText (valueText(), juce::dontSendNotification);
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! knobArea().expanded (6).contains (e.position)) return;
    grabKeyboardFocus();
    dragging = true;
    dragNorm = ui (target);
    beginGesture();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dy = (float) (e.getDistanceFromDragStartY() - (lastDragY.has_value() ? *lastDragY : 0));
    lastDragY = e.getDistanceFromDragStartY();
    const bool fine = e.mods.isShiftDown();
    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm - dy * (fine ? 1.0f / 1100.0f : 1.0f / 230.0f));
    float n = dragNorm;
    if (opt.detent && ! fine && std::abs (n - centreNorm()) < 0.014f) n = centreNorm();
    setNormFromUi (ui (n));
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    lastDragY.reset();
    if (! dragging) return;
    dragging = false;
    endGesture();
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! knobArea().contains (e.position)) return;
    if (onGestureStart) onGestureStart();
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    if (onUserChange) onUserChange (param.convertFrom0to1 (param.getDefaultValue()));
    if (onGestureEnd) onGestureEnd();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float dir = (w.deltaY != 0 ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    if (dir == 0) return;
    beginGesture();
    setNormFromUi (ui (ui (target) + (dir > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.004f : 0.02f)));
    wheelEndAt = nowMs() + 450.0;
    startTimerHz (60);
}

bool Knob::keyPressed (const juce::KeyPress& k)
{
    const float st = k.getModifiers().isShiftDown() ? 0.005f : 0.03f;
    float n = ui (target);
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) n += st;
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) n -= st;
    else if (code == juce::KeyPress::homeKey) n = 0;
    else if (code == juce::KeyPress::endKey) n = 1;
    else if (code == juce::KeyPress::returnKey) { valueLabel.showEditor(); return true; }
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) n = ui (param.getDefaultValue());
    else return false;
    beginGesture();
    setNormFromUi (ui (n));
    endGesture();
    return true;
}

void Knob::sweepIn (int delayMs)
{
    shown = ui (0.0f);
    sweepAt = nowMs() + delayMs;
    startTimerHz (60);
    repaint();
}

void Knob::timerCallback()
{
    const double now = nowMs();
    if (wheelEndAt > 0 && now >= wheelEndAt) { wheelEndAt = 0; endGesture(); }
    if (sweepAt > 0 && now < sweepAt) return;
    sweepAt = 0;
    if (! dragging && std::abs (shown - target) > 1.0e-4f)
    {
        shown += (target - shown) * 0.2f;       // ~200 ms glide at 60 Hz
        if (std::abs (shown - target) < 1.0e-4f) shown = target;
        if (! valueLabel.isBeingEdited()) valueLabel.setText (valueText(), juce::dontSendNotification);
        repaint();
    }
    else if (wheelEndAt == 0)
    {
        shown = target;
        if (! valueLabel.isBeingEdited()) valueLabel.setText (valueText(), juce::dontSendNotification);
        stopTimer();
    }
}

//==============================================================================
Button::Button (const juce::String& text, bool withLamp) : juce::Button (text), lamp (withLamp)
{
    setClickingTogglesState (withLamp);
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Button::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 1.0f);
    const bool en = isEnabled();
    g.setOpacity (en ? 1.0f : 0.35f);
    drawRaised (g, r, 5.0f, down && en);
    if (attention && en)
    {
        const float t = (float) (0.5 + 0.5 * std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0045));
        g.setColour (col::sigClip.withAlpha (0.35f + 0.4f * t));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.2f);
    }
    if (hasKeyboardFocus (false)) { g.setColour (col::ember); g.drawRoundedRectangle (r.expanded (1.5f), 6.0f, 1.0f); }

    auto inner = r.reduced (small ? 8.0f : 10.0f, 0);
    const bool on = getToggleState();
    if (lamp)
    {
        auto l = inner.removeFromLeft (6).withSizeKeepingCentre (6, 6);
        if (on)
        {
            g.setColour (col::ember.withAlpha (0.35f));
            g.fillEllipse (l.expanded (3));
            g.setColour (col::ember);
        }
        else g.setColour (col::basalt0);
        g.fillEllipse (l);
        inner.removeFromLeft (7);
    }
    if (! icon.isEmpty())
    {
        auto ib = inner.removeFromLeft (12).withSizeKeepingCentre (12, 12);
        auto p = icon;
        p.applyTransform (juce::AffineTransform::translation (ib.getX(), ib.getY()));
        g.setColour (attention ? col::sigClip : (over ? col::bone1 : col::bone2));
        g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        inner.removeFromLeft (7);
    }
    const auto textCol = attention && en ? col::sigClip : (on || over ? col::bone1 : col::bone2);
    if (hint.isNotEmpty())
    {
        g.setFont (Fonts::mono (10.0f));
        g.setColour (col::bone3);
        g.drawText (hint, inner, juce::Justification::centredRight, true);
    }
    drawEngraved (g, getButtonText(), inner, small ? 10.0f : 10.5f, textCol,
                  lamp || ! icon.isEmpty() || hint.isNotEmpty() ? juce::Justification::centredLeft : juce::Justification::centred, 0.12f);
}

//==============================================================================
SegControl::SegControl (juce::StringArray l, juce::StringArray h) : labels (std::move (l)), helps (std::move (h))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SegControl::attach (juce::RangedAudioParameter& p, std::function<void()> begin, std::function<void()> end)
{
    beginFn = std::move (begin); endFn = std::move (end);
    att = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { index = juce::roundToInt (v); repaint(); });
    att->sendInitialUpdate();
}

void SegControl::setIndex (int i, bool notify)
{
    if (i == index) return;
    index = i;
    repaint();
    if (notify)
    {
        if (att) { if (beginFn) beginFn(); att->setValueAsCompleteGesture ((float) i); if (endFn) endFn(); }
        if (onChange) onChange (i);
    }
}

juce::Rectangle<float> SegControl::segBounds (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    float total = 0;
    juce::Array<float> w;
    const auto f = Fonts::label (textHeight).withExtraKerningFactor (0.1f);
    for (auto& s : labels) { const float tw = juce::GlyphArrangement::getStringWidth (f, s.toUpperCase()) + 18.0f; w.add (tw); total += tw; }
    const float extra = (r.getWidth() - total - 2.0f * (float) (labels.size() - 1)) / (float) labels.size();
    float x = r.getX();
    for (int k = 0; k < i; ++k) x += w[k] + extra + 2.0f;
    return { x, r.getY(), w[i] + extra, r.getHeight() };
}

int SegControl::hitIndex (juce::Point<float> p) const
{
    for (int i = 0; i < labels.size(); ++i) if (segBounds (i).expanded (1.0f).contains (p)) return i;
    return -1;
}

void SegControl::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::basalt0);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::lineSoft);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto s = segBounds (i);
        if (i == index) drawRaised (g, s, 3.0f, false);
        drawEngraved (g, labels[i], s, textHeight, i == index ? col::emberHi : (i == hover ? col::bone1 : col::bone3),
                      juce::Justification::centred, 0.1f);
    }
}

void SegControl::mouseDown (const juce::MouseEvent& e)
{
    const int i = hitIndex (e.position);
    if (i >= 0) setIndex (i, true);
}

void SegControl::mouseMove (const juce::MouseEvent& e)
{
    const int i = hitIndex (e.position);
    if (i != hover) { hover = i; repaint(); }
}

juce::String SegControl::getHelpText() const
{
    const int i = hover >= 0 ? hover : index;
    return juce::isPositiveAndBelow (i, helps.size()) ? helps[i] : juce::String();
}

} // namespace aemon
