// Aemon Compressor. AGPL-3.0-or-later.
#include "CompEditor.h"

using namespace aemon;

namespace
{
    constexpr int baseW = 960, baseH = 600;
    const juce::String minusSign = juce::String::charToString (0x2212);
    juce::String mns (const juce::String& s) { return s.replace ("-", minusSign); }
    juce::String fmtDb (float v) { auto s = juce::String (v, 1); if (s == "-0.0") s = "0.0"; return mns ((v > 0.05f ? "+" : "") + s) + " dB"; }
    juce::String fmtMs (float v) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }
    std::optional<float> parseNum (const juce::String& s)
    {
        auto t = s.replace (minusSign, "-").trim();
        if (! t.containsAnyOf ("0123456789")) return {};
        return t.retainCharacters ("-.0123456789").getFloatValue();
    }
}

namespace compui
{
//==============================================================================
/** History of level and gain reduction (left) and the transfer curve with draggable handles (right). */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (CompEditor& e) : ed (e) {}

    juce::String getHelpText() const override
    {
        return "Left: what comes in (grey), what goes out (teal), and how much is turned down (amber, from the top). Right: drag the knee for threshold, drag above it for ratio.";
    }

    struct Layout { juce::Rectangle<float> hist, curve; };
    Layout layout() const
    {
        auto b = getLocalBounds().toFloat();
        const float side = std::min (b.getHeight() - 44, b.getWidth() * 0.36f);
        juce::Rectangle<float> curve (b.getRight() - side - 18, 26, side, side);
        juce::Rectangle<float> hist (40, 26, curve.getX() - 40 - 34, b.getHeight() - 50);
        return { hist, curve };
    }
    float cx (const Layout& L, float db) const { return L.curve.getX() + (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * L.curve.getWidth(); }
    float cy (const Layout& L, float db) const { return L.curve.getBottom() - (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * L.curve.getHeight(); }
    float gc (float x) const
    {
        return (float) aemon::comp::gainComputer (x, ed.getReal ("thr"), ed.getReal ("ratio"), ed.getReal ("knee"));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const auto L = layout();
        const auto& H = L.hist;
        auto hy = [&] (float db) { return H.getBottom() - (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * H.getHeight(); };
        auto gy = [&] (float gr) { return H.getY() + (-gr / 24.0f) * H.getHeight() * 0.5f; };
        g.setFont (Fonts::mono (9.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            g.setColour (col::bone1.withAlpha (0.055f)); g.fillRect (H.getX(), std::round (hy (db)) + 0.5f, H.getWidth(), 1.0f);
            g.setColour (col::bone3); g.drawText (db == 0 ? juce::String ("0") : mns (juce::String ((int) db)), juce::Rectangle<float> (H.getX() - 34, hy (db) - 6, 28, 12), juce::Justification::centredRight);
        }
        g.setColour (col::emberLo);
        for (float gr : { -3.0f, -6.0f, -12.0f }) g.drawText (mns (juce::String ((int) gr)), juce::Rectangle<float> (H.getRight() + 6, gy (gr) - 6, 28, 12), juce::Justification::centredLeft);

        const auto& h = ed.history;
        const int n = (int) h.size(), show = std::min (n, (int) H.getWidth());
        if (show > 1)
        {
            const int first = n - show;
            auto xAt = [&] (int i) { return H.getRight() - (float) (n - 1 - i); };
            juce::Path in, out, grFill, grLine;
            in.startNewSubPath (xAt (first), H.getBottom());
            grFill.startNewSubPath (xAt (first), H.getY());
            for (int i = first; i < n; ++i)
            {
                in.lineTo (xAt (i), hy (h[(size_t) i].inDb));
                if (i == first) out.startNewSubPath (xAt (i), hy (h[(size_t) i].outDb)); else out.lineTo (xAt (i), hy (h[(size_t) i].outDb));
                grFill.lineTo (xAt (i), gy (h[(size_t) i].grDb));
                if (i == first) grLine.startNewSubPath (xAt (i), gy (h[(size_t) i].grDb)); else grLine.lineTo (xAt (i), gy (h[(size_t) i].grDb));
            }
            in.lineTo (H.getRight(), H.getBottom()); in.closeSubPath();
            grFill.lineTo (H.getRight(), H.getY()); grFill.closeSubPath();
            g.setColour (col::bone1.withAlpha (0.10f)); g.fillPath (in);
            g.setColour (col::phosphor); g.strokePath (out, juce::PathStrokeType (1.4f));
            g.setColour (col::ember.withAlpha (0.16f)); g.fillPath (grFill);
            g.setColour (col::ember.withAlpha (0.3f)); g.strokePath (grLine, juce::PathStrokeType (5.0f));
            g.setColour (col::ember); g.strokePath (grLine, juce::PathStrokeType (2.0f));
        }
        {
            juce::Path t; t.startNewSubPath (H.getX(), hy (ed.getReal ("thr"))); t.lineTo (H.getRight(), hy (ed.getReal ("thr")));
            juce::Path dashed; const float dl[] = { 3.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, t, dl, 2);
            g.setColour (col::ember.withAlpha (0.45f)); g.fillPath (dashed);
        }
        drawEngraved (g, "Level  " + juce::String::charToString (0xb7) + "  gain reduction", { H.getX(), 6, 260, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);

        // transfer curve
        const auto& C = L.curve;
        g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (C);
        g.setColour (col::bone1.withAlpha (0.07f));
        for (float db : { -48.0f, -36.0f, -24.0f, -12.0f }) { g.fillRect (cx (L, db), C.getY(), 1.0f, C.getHeight()); g.fillRect (C.getX(), cy (L, db), C.getWidth(), 1.0f); }
        {
            juce::Path d; d.startNewSubPath (cx (L, -60), cy (L, -60)); d.lineTo (cx (L, 0), cy (L, 0));
            juce::Path dashed; const float dl[] = { 2.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, d, dl, 2);
            g.setColour (col::bone1.withAlpha (0.18f)); g.fillPath (dashed);
        }
        const bool byp = ed.getReal ("bypass") > 0.5f;
        juce::Path curve;
        for (float x = -60; x <= 0.001f; x += 0.25f)
        {
            const float y = x + (byp ? 0.0f : gc (x));
            if (x <= -60) curve.startNewSubPath (cx (L, x), cy (L, y)); else curve.lineTo (cx (L, x), cy (L, y));
        }
        g.setColour (col::ember.withAlpha (0.25f)); g.strokePath (curve, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember); g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float thr = ed.getReal ("thr");
        const juce::Point<float> kn (cx (L, thr), cy (L, thr)), ra (cx (L, 0), cy (L, gc (0)));
        for (auto [p, nm] : { std::pair { kn, 1 }, std::pair { ra, 2 } })
        {
            const float r = hover == nm || dragWhat == nm ? 7.0f : 5.5f;
            g.setColour (col::basalt0); g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour (col::ember); g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);
        }
        if (! h.empty())
        {
            const float li = juce::jlimit (-60.0f, 0.0f, h.back().inDb), lo = li + gc (li);
            g.setColour (col::phosphor.withAlpha (0.35f)); g.fillEllipse (cx (L, li) - 7, cy (L, lo) - 7, 14, 14);
            g.setColour (col::bone1); g.fillEllipse (cx (L, li) - 4, cy (L, lo) - 4, 8, 8);
        }
        const float gNow = h.empty() ? 0.0f : h.back().grDb;
        g.setFont (Fonts::mono (22.0f)); g.setColour (col::emberHi);
        g.drawText ((gNow > -0.05f ? juce::String ("0.0") : mns (juce::String (gNow, 1))) + " dB", juce::Rectangle<float> (C.getX() + 10, C.getY() + 6, 160, 26), juce::Justification::centredLeft);
        drawEngraved (g, "Reduction", { C.getX() + 10, C.getY() + 32, 120, 12 }, 9.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "Transfer", { C.getRight() - 120, 6, 120, 14 }, 9.5f, col::bone3, juce::Justification::centredRight);
        drawEngraved (g, juce::String ("In  ") + juce::String::charToString (0x2192), { C.getX(), C.getBottom() + 4, 80, 12 }, 9.5f, col::bone3, juce::Justification::centredLeft);
        if (ed.getReal ("extSc") > 0.5f)
            drawEngraved (g, ed.proc.sidechainConnected() ? "External sidechain" : "External sidechain: not connected", { H.getX(), H.getBottom() - 16, 300, 14 }, 9.5f, ed.proc.sidechainConnected() ? col::lilac : col::sigHot, juce::Justification::centredLeft);
    }

    int hit (juce::Point<float> p, const Layout& L) const
    {
        const float thr = ed.getReal ("thr");
        const juce::Point<float> kn (cx (L, thr), cy (L, thr)), ra (cx (L, 0), cy (L, gc (0)));
        if (kn.getDistanceFrom (p) < 14) return 1;
        if (ra.getDistanceFrom (p) < 14) return 2;
        if (L.curve.contains (p)) return p.x < kn.x ? 1 : 2;
        return 0;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hit (e.position, layout());
        if (h != hover) { hover = h; repaint(); }
        setMouseCursor (h ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hover = 0; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragWhat = hit (e.position, layout());
        if (! dragWhat) return;
        startThr = ed.getReal ("thr");
        startOut0 = gc (0);
        ed.begin();
        ed.proc.apvts.getParameter (dragWhat == 1 ? "thr" : "ratio")->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragWhat) return;
        const auto L = layout();
        const float f = e.mods.isShiftDown() ? 0.25f : 1.0f;
        if (dragWhat == 1)
        {
            auto* p = ed.proc.apvts.getParameter ("thr");
            const float v = juce::jlimit (-60.0f, 0.0f, startThr + (float) e.getDistanceFromDragStartX() / L.curve.getWidth() * 60.0f * f);
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
        else
        {
            auto* p = ed.proc.apvts.getParameter ("ratio");
            const float thr = ed.getReal ("thr");
            const float out0 = startOut0 - (float) e.getDistanceFromDragStartY() / L.curve.getHeight() * 60.0f * f;
            const float r = juce::jlimit (1.0f, 20.0f, (0.0f - thr) / std::max (0.05f, out0 - thr));
            p->setValueNotifyingHost (p->convertTo0to1 (r));
        }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragWhat) return;
        ed.proc.apvts.getParameter (dragWhat == 1 ? "thr" : "ratio")->endChangeGesture();
        dragWhat = 0;
        ed.end();
    }

private:
    CompEditor& ed;
    int hover = 0, dragWhat = 0;
    float startThr = 0, startOut0 = 0;
};

//==============================================================================
class GrMeter : public juce::Component, public HelpProvider
{
public:
    float gr = 0;
    juce::String getHelpText() const override { return "Gain reduction: how much the compressor is turning the signal down right now."; }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedTop (9);
        g.setColour (col::groove); g.fillRoundedRectangle (r, 2);
        const float h = std::min (1.0f, -gr / 24.0f) * r.getHeight();
        g.setGradientFill (juce::ColourGradient::vertical (col::emberHi, r.getY(), col::emberLo, r.getBottom()));
        g.fillRect (r.getX() + 2, r.getY(), r.getWidth() - 4, h);
        g.setColour (col::groove.withAlpha (0.55f));
        for (float y = r.getY() + 2; y < r.getBottom(); y += 3) g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
};

//==============================================================================
/** Simple: plain-language compression check with fixes. */
class Check : public CheckCards
{
public:
    explicit Check (CompEditor& e)
        : CheckCards ("Compression check", "Compression check: what the compressor is doing, in plain words, with a fix when something is off.", { 26, 60, 84, 6, 8, 30, false }), ed (e)
    {
        rows[0] = { S::ok, "Amount", "Listening..." }; rows[1] = { S::ok, "Punch", "Listening..." };
        rows[2] = { S::ok, "Pumping", "Listening..." }; rows[3] = { S::ok, "Level", "Listening..." };
    }
    void update()
    {
        const bool byp = ed.getReal ("bypass") > 0.5f;
        const float avg = -ed.avgGr;
        if (byp) rows[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
        else if (avg < 0.5f) rows[0] = { S::warn, "Barely working", "Less than 0.5 dB of gain reduction. Turn Amount up to hear it.", "Set it for me", [this] { ed.setAmountForMe(); } };
        else if (avg < 6.0f) rows[0] = { S::ok, avg < 3 ? "Gentle" : "Moderate", "About " + juce::String (avg, 1) + " dB of gain reduction on the loud parts." };
        else rows[0] = { avg > 10 ? S::bad : S::warn, "Heavy", "About " + juce::String (avg, 1) + " dB of gain reduction. Great as an effect, too much for gentle control.", "Back off",
                         [this] { ed.begin(); ed.setReal ("thr", std::min (0.0f, ed.getReal ("thr") + 6.0f)); ed.end(); } };
        const float lost = ed.crestIn - ed.crestOut;
        if (lost > 6) rows[1] = { S::warn, "Punch flattened", "Peaks stick out " + juce::String (lost, 1) + " dB less than before. A slower attack keeps more punch.", "Keep the punch",
                                  [this] { ed.begin(); ed.setReal ("atk", juce::jlimit (20.0f, 60.0f, ed.getReal ("atk") * 2.0f)); ed.end(); } };
        else rows[1] = { S::ok, "Punch kept", "Peaks still stick out " + juce::String (std::max (0.0f, ed.crestOut), 1) + " dB above the average." };
        const bool pump = ed.grSpread > 2.2f && avg > 3 && ed.getReal ("rel") < 120 && ed.getReal ("autoRel") < 0.5f;
        if (pump) rows[2] = { S::warn, "Pumping likely", "The level rises and falls with every beat. Good for EDM, distracting elsewhere.", "Smooth it", [this] { ed.lessPumping(); } };
        else rows[2] = { S::ok, "No pumping", "The release is smooth for this material." };
        const float dl = ed.levelDiff;
        if (std::abs (dl) < 1.0f) rows[3] = { S::ok, "Level matched", "Output is as loud as the input, so you can compare fairly." };
        else rows[3] = { S::warn, dl > 0 ? "Louder than the input" : "Quieter than the input",
                         juce::String (std::abs (dl), 1) + " dB " + (dl > 0 ? "louder" : "quieter") + ". Louder often sounds better without being better.",
                         ed.getReal ("autoLevel") > 0.5f ? juce::String() : juce::String ("Match level"),
                         ed.getReal ("autoLevel") > 0.5f ? std::function<void()>() : std::function<void()> ([this] { ed.begin(); ed.setReal ("autoLevel", 1); ed.end(); }) };
        refresh();
    }
private:
    CompEditor& ed;
};
} // namespace compui

using namespace compui;

//==============================================================================
CompEditor::CompEditor (CompProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    shell = std::make_unique<Shell> (*this, proc.pluginState, "Compressor", baseW, baseH);
    addAndMakeVisible (*shell);
    buildSimple();
    buildPro();
    shell->setModeComponents (simpleBody.get(), simpleDeck.get(), proBody.get(), proDeck.get());
    shell->setMeta (juce::String (proc.currentRate.load() / 1000.0, 1) + " kHz  " + juce::String::charToString (0xb7) + "  v" AEMON_VERSION);

    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    const double s = (double) proc.pluginState.ui.getProperty ("scale", 1.0);
    setSize (juce::roundToInt (baseW * juce::jlimit (0.6, 2.0, s)), juce::roundToInt (baseH * juce::jlimit (0.6, 2.0, s)));
    setWantsKeyboardFocus (true);
    for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->sweepIn (120 + (int) (i % 6) * 60);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

CompEditor::~CompEditor()
{
    stopTimer();
    atts.clear();
    shell.reset();
    setLookAndFeel (nullptr);
}

void CompEditor::resized() { shell->setBounds (getLocalBounds()); }

void CompEditor::setReal (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

aemon::Knob& CompEditor::knob (juce::Component& parent, const juce::String& id, Knob::Options o)
{
    auto k = std::make_unique<Knob> (*proc.apvts.getParameter (id), std::move (o));
    k->onGestureStart = [this] { begin(); };
    k->onGestureEnd = [this] { end(); };
    auto& ref = *k;
    parent.addAndMakeVisible (ref);
    knobs.push_back (&ref);
    owned.push_back (std::move (k));
    return ref;
}

aemon::Button& CompEditor::toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto b = std::make_unique<Button> (text, true);
    b->help = help;
    b->setClickingTogglesState (false);
    auto& ref = *b;
    parent.addAndMakeVisible (ref);
    auto att = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (id), [&ref] (float v) { ref.setToggleState (v > 0.5f, juce::dontSendNotification); });
    att->sendInitialUpdate();
    auto* a = att.get();
    ref.onClick = [this, &ref, a] { begin(); a->setValueAsCompleteGesture (ref.getToggleState() ? 0.0f : 1.0f); end(); };
    atts.push_back (std::move (att));
    owned.push_back (std::move (b));
    return ref;
}

void CompEditor::applyStyle (int i)
{
    const auto& s = compp::styles[i];
    begin();
    setReal ("ratio", s.ratio); setReal ("atk", s.attack); setReal ("rel", s.release); setReal ("knee", s.knee);
    setReal ("autoRel", s.autoRelease ? 1.0f : 0.0f); setReal ("mix", s.mix);
    end();
    updateStyle();
}

void CompEditor::updateStyle()
{
    const int i = compp::matchingStyle (proc.apvts);
    if (i == shownStyle && styleSeg->getIndex() == i) return;
    shownStyle = i;
    styleSeg->setIndex (i, false);
    if (simpleDeck) { simpleDeck->resized(); simpleDeck->repaint(); }
}

void CompEditor::setAmountForMe()
{
    std::vector<float> lv;
    for (size_t i = history.size() > 400 ? history.size() - 400 : 0; i < history.size(); ++i) if (history[i].inDb > -70) lv.push_back (history[i].inDb);
    if (lv.size() < 20) { shell->toast ("No signal yet."); return; }
    std::sort (lv.begin(), lv.end());
    const float p90 = lv[(size_t) ((float) lv.size() * 0.9f)];
    const float r = getReal ("ratio");
    const float slope = r >= 19.95f ? 1.0f : 1.0f - 1.0f / r;
    begin(); setReal ("thr", juce::jlimit (-60.0f, -1.0f, p90 - 3.0f / slope)); end();
    shell->toast ("Amount set for about 3 dB of reduction on the loud parts.");
}

void CompEditor::lessPumping()
{
    begin(); setReal ("rel", std::max (getReal ("rel") * 1.8f, 200.0f)); setReal ("autoRel", 1); end();
    shell->toast ("Release slowed down and set to auto: smoother, less pumping.");
}

void CompEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    auto disp = std::make_unique<Display> (*this);
    auto mi = std::make_unique<LevelMeterView> (LevelMeterView::ScaleSide::none);
    auto mo = std::make_unique<LevelMeterView> (LevelMeterView::ScaleSide::none);
    auto gm = std::make_unique<GrMeter>();
    displayPro = disp.get(); meterIn = mi.get(); meterOut = mo.get(); grMeter = gm.get();
    for (auto* c : std::initializer_list<juce::Component*> { displayPro, meterIn, meterOut, grMeter }) body->addAndMakeVisible (c);
    struct Labels : juce::Component
    {
        Labels() { setInterceptsMouseClicks (false, false); }
        juce::Rectangle<int> a, b, c;
        void paint (juce::Graphics& g) override
        {
            drawEngraved (g, "In", a.toFloat(), 10.0f, col::bone3); drawEngraved (g, "Out", b.toFloat(), 10.0f, col::bone3); drawEngraved (g, "GR", c.toFloat(), 10.0f, col::bone3);
        }
    };
    auto labels = std::make_unique<Labels>();
    auto* lab = labels.get();
    body->addAndMakeVisible (*labels);
    body->layout = [this, lab] (juce::Rectangle<int> r)
    {
        lab->setBounds (r);
        r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10);
        auto col = r.removeFromRight (100);
        r.removeFromRight (10);
        displayPro->setBounds (r);
        auto top = col.removeFromTop (14);
        lab->a = top.withWidth (38); lab->b = top.withX (top.getX() + 42).withWidth (38); lab->c = top.withX (top.getX() + 82).withWidth (18);
        col.removeFromTop (4);
        meterIn->setBounds (col.withWidth (38)); meterOut->setBounds (col.withX (col.getX() + 42).withWidth (38)); grMeter->setBounds (col.withX (col.getX() + 84).withWidth (16));
    };
    owned.push_back (std::move (disp)); owned.push_back (std::move (mi)); owned.push_back (std::move (mo)); owned.push_back (std::move (gm)); owned.push_back (std::move (labels));
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto painter = std::make_unique<DeckPainter>();
    auto* pnt = painter.get();
    auto hero = [] (const char* name, const char* help, std::function<juce::String (float)> fmt, std::function<std::optional<float> (const juce::String&)> parse = nullptr)
    {
        Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = std::move (parse); return o;
    };
    auto thrO = hero ("Threshold", "Compression starts above this level.", [] (float v) { return mns (juce::String (v, 1)) + " dB"; }, parseNum);
    thrO.ticks = 13; thrO.minLabel = minusSign + "60"; thrO.maxLabel = "0";
    auto& thr = knob (*deck, "thr", thrO);
    auto ratO = hero ("Ratio", "How strongly level above the threshold is turned down. 2:1 is gentle, 10:1 and up is limiting.",
                      [] (float v) { return v >= 19.95f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e : 1")) : juce::String (v, v < 10 ? 1 : 0) + " : 1"; },
                      [] (const juce::String& s) -> std::optional<float> { if (s.containsIgnoreCase ("inf") || s.contains (juce::CharPointer_UTF8 ("\xe2\x88\x9e"))) return 20.0f; return parseNum (s); });
    ratO.ticks = 9; ratO.minLabel = "1"; ratO.maxLabel = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"));
    auto& rat = knob (*deck, "ratio", ratO);
    auto kneeO = hero ("Knee", "Soft knee: compression fades in around the threshold instead of switching on.", [] (float v) { return juce::String (v, 1) + " dB"; }, parseNum);
    kneeO.style = Knob::Style::compact;
    auto& knee = knob (*deck, "knee", kneeO);
    auto atkO = hero ("Attack", "How fast it reacts. A slower attack lets the punch of transients through.", fmtMs, parseNum);
    auto& atk = knob (*deck, "atk", atkO);
    auto relO = hero ("Release", "How fast it lets go. Too fast pumps, too slow squashes.", fmtMs, parseNum);
    auto& rel = knob (*deck, "rel", relO);
    auto& autoRel = toggle (*deck, "autoRel", "Auto rel", "Auto release: lets go faster after short peaks and slower during sustained loud parts. Avoids pumping.");
    autoRel.small = true;
    auto lookO = hero ("Lookahead", "Sees peaks coming so it can catch them cleanly. Adds this much latency.", fmtMs, parseNum);
    lookO.style = Knob::Style::compact;
    auto& look = knob (*deck, "look", lookO);
    auto det = std::make_unique<SegControl> (juce::StringArray { "Peak", "RMS" }, juce::StringArray { "Peak: reacts to every transient. Tight control.", "RMS: reacts to the average level. Smoother, more musical." });
    det->textHeight = 9.5f;
    det->attach (*proc.apvts.getParameter ("det"), [this] { begin(); }, [this] { end(); });
    auto* detRaw = det.get();
    deck->addAndMakeVisible (*det); owned.push_back (std::move (det));
    auto hpfO = hero ("SC high-pass", "Stops the low end (kick, bass) from driving the compression. It does not filter what you hear.",
                      [] (float v) { return v <= 20.5f ? juce::String ("Off") : juce::String (juce::roundToInt (v)) + " Hz"; }, parseNum);
    hpfO.style = Knob::Style::compact;
    auto& hpf = knob (*deck, "hpf", hpfO);
    auto& ext = toggle (*deck, "extSc", "Ext SC", "External sidechain: another track drives the compression (for example the kick ducking the bass). Route that track into this plugin's sidechain input in your DAW.");
    ext.small = true;
    auto mixO = hero ("Mix", "Blend with the uncompressed signal (parallel compression).", [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; }, parseNum);
    mixO.ticks = 11; mixO.minLabel = "dry"; mixO.maxLabel = "wet";
    auto& mix = knob (*deck, "mix", mixO);
    auto outO = hero ("Output", "Output level, on top of Auto level.", fmtDb, parseNum);
    outO.bipolar = true; outO.detent = true; outO.ticks = 13; outO.minLabel = minusSign + "24"; outO.maxLabel = "+24";
    auto& out = knob (*deck, "out", outO);
    auto& al = toggle (*deck, "autoLevel", "Auto level", "Auto level: keeps the output as loud as the input, so you judge the compression, not the volume.");
    al.small = true;
    deck->addAndMakeVisible (*painter);
    owned.push_back (std::move (painter));

    deck->layout = [pnt, &thr, &rat, &knee, &atk, &rel, &autoRel, &look, detRaw, &hpf, &ext, &mix, &out, &al] (juce::Rectangle<int> r)
    {
        pnt->setBounds (r);
        const int top = 32, kh = 70 + 25;
        auto heroAt = [&] (Knob& k, int x) { k.setBounds (x, top, 76, kh); };
        auto compactAt = [&] (Knob& k, int x, int y) { k.setBounds (x, y, 64, 42 + 25); };
        pnt->clear();
        int x = 18;
        pnt->groups.push_back ({ { x, 14, 220, 20 }, "Compression" });
        heroAt (thr, x); heroAt (rat, x + 80); compactAt (knee, x + 158, top + 8);
        x += 236;
        pnt->groups.push_back ({ { x, 14, 238, 20 }, "Timing" });
        heroAt (atk, x); heroAt (rel, x + 80);
        autoRel.setBounds (x + 160, top + 2, 78, 22); compactAt (look, x + 167, top + 28);
        x += 252;
        pnt->groups.push_back ({ { x, 14, 104, 20 }, "Detector" });
        detRaw->setBounds (x + 4, top + 2, 96, 24);
        compactAt (hpf, x + 20, top + 30);
        x += 118;
        pnt->groups.push_back ({ { x, 14, 330, 20 }, "Output" });
        heroAt (mix, x); heroAt (out, x + 80);
        al.setBounds (x + 166, top + 20, 160, 24);
        ext.setBounds (x + 166, top + 54, 160, 24);
        auto under = [&] (juce::Component& c, const char* t) { pnt->under (c, t); };
        under (thr, "Threshold"); under (rat, "Ratio"); under (knee, "Knee"); under (atk, "Attack"); under (rel, "Release");
        under (look, "Lookahead"); under (hpf, "SC high-pass"); under (mix, "Mix"); under (out, "Output");
        pnt->seps = { 18 + 236 - 9, 18 + 236 + 252 - 9, 18 + 236 + 252 + 118 - 9 };
        pnt->toFront (false);
    };
    proDeck = std::move (deck);
}

void CompEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    auto disp = std::make_unique<Display> (*this);
    auto ck = std::make_unique<Check> (*this);
    displaySimple = disp.get(); check = ck.get();
    body->addAndMakeVisible (*disp); body->addAndMakeVisible (*ck);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10);
        check->setBounds (r.removeFromRight (236));
        r.removeFromRight (10);
        displaySimple->setBounds (r);
    };
    owned.push_back (std::move (disp)); owned.push_back (std::move (ck));
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto painter = std::make_unique<DeckPainter>();
    auto* pnt = painter.get();
    Knob::Options ao; ao.name = "Amount"; ao.inverted = true; ao.ticks = 11; ao.minLabel = "0"; ao.maxLabel = "100";
    ao.help = "How much compression. Turn up until it sounds controlled, back off if it sounds squashed.";
    ao.format = [] (float thr) { return juce::String (juce::roundToInt (-thr / 60.0f * 100.0f)) + " %"; };
    ao.parse = [] (const juce::String& s) -> std::optional<float> { auto v = parseNum (s); if (! v) return {}; return -juce::jlimit (0.0f, 100.0f, *v) * 0.6f; };
    auto& amount = knob (*deck, "thr", ao);
    Knob::Options oo; oo.name = "Output"; oo.bipolar = true; oo.detent = true; oo.ticks = 13; oo.minLabel = minusSign + "24"; oo.maxLabel = "+24"; oo.format = fmtDb; oo.parse = parseNum;
    oo.help = "Output level, on top of Auto level.";
    auto& out = knob (*deck, "out", oo);
    juce::StringArray names, helps;
    for (auto& s : compp::styles) { names.add (s.name); helps.add (juce::String (s.name) + ": " + juce::String (s.sub).replace ("  ", ", ") + "."); }
    auto seg = std::make_unique<SegControl> (names, helps);
    styleSeg = seg.get();
    shownStyle = compp::matchingStyle (proc.apvts);
    styleSeg->setIndex (shownStyle, false);
    styleSeg->onChange = [this] (int i) { applyStyle (i); };
    deck->addAndMakeVisible (*seg); owned.push_back (std::move (seg));
    auto mkBtn = [&] (const juce::String& t, const juce::String& hint, const juce::String& help, std::function<void()> fn) -> Button&
    {
        auto b = std::make_unique<Button> (t); b->hint = hint; b->help = help; b->onClick = std::move (fn);
        auto& ref = *b; deck->addAndMakeVisible (ref); owned.push_back (std::move (b)); return ref;
    };
    auto& setAmt = mkBtn ("Set amount for me", "about 3 dB", "Measures what is playing and sets Amount for about 3 dB of reduction on the loud parts.", [this] { setAmountForMe(); });
    auto& less = mkBtn ("Less pumping", "smoother release", "Slows the release and switches on auto release, so the level does not bob with the beat.", [this] { lessPumping(); });
    auto& al = toggle (*deck, "autoLevel", "Auto level", "Auto level: keeps the output as loud as the input, so you judge the compression, not the volume.");
    al.hint = "same loudness";
    deck->addAndMakeVisible (*painter);
    owned.push_back (std::move (painter));
    deck->layout = [this, pnt, &amount, &out, &setAmt, &less, &al] (juce::Rectangle<int> r)
    {
        pnt->setBounds (r);
        pnt->clear();
        const int top = 38;
        pnt->groups.push_back ({ { 30, 14, 120, 20 }, "Amount" });
        amount.setBounds (34, top - 4, 112, 98 + 25);
        pnt->groups.push_back ({ { 166, 14, 330, 20 }, "Style" });
        styleSeg->setBounds (176, top + 22, 310, 30);
        const int si = shownStyle;
        pnt->subs.push_back ({ { 176, top + 60, 310, 12 }, juce::isPositiveAndBelow (si, compp::numStyles) ? juce::String (compp::styles[si].sub) : juce::String ("custom settings") });
        pnt->groups.push_back ({ { 510, 14, 120, 20 }, "Output" });
        out.setBounds (514, top - 4, 112, 98 + 25);
        pnt->groups.push_back ({ { 650, 14, 280, 20 }, "One-click helpers" });
        setAmt.setBounds (664, top + 2, 260, 30); less.setBounds (664, top + 38, 260, 30); al.setBounds (664, top + 74, 260, 30);
        pnt->seps = { 158, 502, 642 };
        pnt->toFront (false);
    };
    simpleDeck = std::move (deck);
}

void CompEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.001, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;
    ++frame;
    compp::Frame fr[256];
    const int n = proc.popFrames (fr, 256);
    for (int i = 0; i < n; ++i) history.push_back (fr[i]);
    if (history.size() > 900) history.erase (history.begin(), history.begin() + (long) (history.size() - 900));

    updateStyle();
    const bool simple = shell->isSimple();
    (simple ? displaySimple : displayPro)->repaint();
    if (! simple)
    {
        const auto in = proc.inTap.take(), out = proc.outTap.take();
        if (out.count > 0)
        {
            const float a = 1.0f - std::exp (-dt / 0.3f);
            for (int c = 0; c < 2; ++c) { msIn[c] += (in.meanSquare (c) - msIn[c]) * a; msOut[c] += (out.meanSquare (c) - msOut[c]) * a; }
            auto db = [] (float g) { return juce::Decibels::gainToDecibels (g, -120.0f); };
            const float pkI[2] { db (in.peak[0]), db (in.peak[1]) }, pkO[2] { db (out.peak[0]), db (out.peak[1]) };
            const float rI[2] { 10 * std::log10 (std::max (msIn[0], 1e-12f)), 10 * std::log10 (std::max (msIn[1], 1e-12f)) };
            const float rO[2] { 10 * std::log10 (std::max (msOut[0], 1e-12f)), 10 * std::log10 (std::max (msOut[1], 1e-12f)) };
            meterIn->push (pkI, rI, dt); meterOut->push (pkO, rO, dt);
        }
        grMeter->gr = history.empty() ? 0.0f : history.back().grDb;
        grMeter->repaint();
    }
    if (frame % 15 == 0 && history.size() > 100)
    {
        // statistics over the last ~2 s (400 frames of 5 ms)
        const size_t from = history.size() > 400 ? history.size() - 400 : 0;
        double gsum = 0, gsq = 0, inSq = 0, outSq = 0; int gn = 0; float pkIn = -120, pkOut = -120;
        for (size_t i = from; i < history.size(); ++i)
        {
            const auto& f = history[i];
            if (f.inDb > -60) { gsum += f.grDb; gsq += (double) f.grDb * f.grDb; ++gn; }
            inSq += std::pow (10.0, f.inRms / 10.0); outSq += std::pow (10.0, f.outRms / 10.0);
            pkIn = std::max (pkIn, f.inDb); pkOut = std::max (pkOut, f.outDb);
        }
        const double cnt = (double) (history.size() - from);
        avgGr = gn ? (float) (gsum / gn) : 0.0f;
        grSpread = gn ? (float) std::sqrt (std::max (0.0, gsq / gn - (gsum / gn) * (gsum / gn))) : 0.0f;
        const float rIn = (float) (10 * std::log10 (inSq / cnt + 1e-12)), rOut = (float) (10 * std::log10 (outSq / cnt + 1e-12));
        crestIn = pkIn - rIn; crestOut = pkOut - rOut;
        levelDiff = rOut - rIn;
        if (simple) check->update();
    }
}

bool CompEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (k.getModifiers().isShiftDown()) proc.pluginState.history.redo (proc.apvts);
        else proc.pluginState.history.undo (proc.apvts);
        shell->refreshHeader();
        return true;
    }
    return false;
}
