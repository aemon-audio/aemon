// Aemon Utility. AGPL-3.0-or-later.
#include "PluginEditor.h"

using namespace aemon;

namespace
{
    constexpr int baseW = 960, baseH = 600;
    const juce::String minusSign = juce::String::charToString (0x2212);
    juce::String mns (const juce::String& s) { return s.replace ("-", minusSign); }
    juce::String fmtNum (float v, int dec) { if (! std::isfinite (v)) return minusSign + juce::String::charToString (0x221E); return mns (juce::String (v, dec)); }
    juce::String fmtDbSigned (float v) { auto s = juce::String (v, 1); if (s == "-0.0") s = "0.0"; return mns ((v > 0.05f ? "+" : "") + s) + " dB"; }
    juce::String fmtPan (float v) { const int r = juce::roundToInt (v); return r == 0 ? juce::String ("Centre") : (r < 0 ? juce::String (-r) + " L" : juce::String (r) + " R"); }
    juce::String fmtLufs (float v) { return std::isfinite (v) && v > -70.0f ? mns (juce::String (v, 1)) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")); }
    std::optional<float> parsePan (const juce::String& s)
    {
        auto t = s.trim().toUpperCase();
        if (t.isEmpty()) return {};
        if (t.startsWith ("C")) return 0.0f;
        if (! t.containsAnyOf ("0123456789")) return {};
        float x = t.retainCharacters ("0123456789.").getFloatValue();
        if (t.contains ("L") || t.startsWith ("-")) x = -x;
        return x;
    }
    juce::Point<float> polarPt (juce::Point<float> c, float r, float deg)
    {
        const float a = juce::degreesToRadians (deg);
        return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
    }
}

namespace aemon::utility::ui
{

//==============================================================================
/** Control deck: groups with engraved titles and separators. */
class Deck : public juce::Component
{
public:
    struct Group
    {
        juce::String title, tag;
        int width;
        std::function<void (juce::Rectangle<int>)> layout;
        juce::Component* titleAccessory = nullptr;
    };
    std::vector<Group> groups;
    std::vector<juce::Component*> overlays;
    int pad = 15;

    void resized() override
    {
        int total = 0;
        for (auto& g : groups) total += g.width + 2 * pad;
        int x = (getWidth() - total) / 2;
        rects.clear();
        for (auto& g : groups)
        {
            juce::Rectangle<int> r (x + pad, 14, g.width, getHeight() - 26);
            rects.push_back (r);
            auto content = r.withTrimmedTop (24);
            if (g.layout) g.layout (content);
            if (g.titleAccessory)
            {
                const auto tw = (int) titleWidth (g.title);
                g.titleAccessory->setBounds (r.getCentreX() - (tw + 8 + 66) / 2 + tw + 8, r.getY() - 1, 66, 18);
            }
            x += g.width + 2 * pad;
        }
        for (auto* o : overlays) { o->setBounds (getLocalBounds()); o->toFront (false); }
    }

    void paint (juce::Graphics& g) override
    {
        for (size_t i = 0; i < groups.size(); ++i)
        {
            auto r = rects[i];
            auto& gr = groups[i];
            auto tr = r.withHeight (16).toFloat();
            const float tw = titleWidth (gr.title);
            float tagW = gr.tag.isNotEmpty() ? juce::GlyphArrangement::getStringWidth (Fonts::label (8.5f).withExtraKerningFactor (0.12f), gr.tag.toUpperCase()) + 10 : 0.0f;
            float accW = gr.titleAccessory ? 74.0f : 0.0f;
            const float full = tw + (tagW > 0 ? tagW + 8 : 0) + accW;
            float x = tr.getCentreX() - full / 2;
            drawEngraved (g, gr.title, { x, tr.getY(), tw + 2, 16 }, 10.0f, col::bone2, juce::Justification::centredLeft, 0.2f);
            x += tw + 8;
            if (tagW > 0)
            {
                auto tg = juce::Rectangle<float> (x, tr.getY() + 1, tagW, 14);
                g.setColour (col::lilac.withAlpha (0.4f)); g.drawRoundedRectangle (tg.reduced (0.5f), 3, 1);
                drawEngraved (g, gr.tag, tg, 8.5f, col::lilac, juce::Justification::centred, 0.12f);
            }
            if (i + 1 < groups.size())
            {
                const float sx = (float) r.getRight() + (float) pad;
                juce::ColourGradient sg (juce::Colour (0x000b0c0f), sx, 0, juce::Colour (0x000b0c0f), sx, (float) getHeight(), false);
                sg.addColour (0.2, juce::Colour (0xff0b0c0f)); sg.addColour (0.8, juce::Colour (0xff0b0c0f));
                g.setGradientFill (sg); g.fillRect (sx, 0.0f, 1.0f, (float) getHeight());
                g.setColour (juce::Colours::white.withAlpha (0.04f)); g.fillRect (sx + 1, 20.0f, 1.0f, (float) getHeight() - 40);
            }
        }
    }

    void paintSub (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r)
    { drawEngraved (g, t, r.toFloat(), 9.0f, col::bone3, juce::Justification::centred, 0.16f, true); }

private:
    static float titleWidth (const juce::String& t) { return juce::GlyphArrangement::getStringWidth (Fonts::label (10.0f).withExtraKerningFactor (0.2f), t.toUpperCase()); }
    std::vector<juce::Rectangle<int>> rects;
};

//==============================================================================
/** The stereo field: vectorscope dots plus the amber arc you drag (width and position). */
class StereoField : public juce::Component, public HelpProvider
{
public:
    explicit StereoField (UtilityEditor& e)
        : ed (e),
          panAtt (*e.proc.apvts.getParameter ("pan"), [this] (float v) { pan = v; repaint(); }),
          widthAtt (*e.proc.apvts.getParameter ("width"), [this] (float v) { width = v; repaint(); })
    {
        panAtt.sendInitialUpdate(); widthAtt.sendInitialUpdate();
        addAndMakeVisible (corr);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    CorrelationView corr;

    void addPoints (const juce::Point<float>* p, int n, float dt)
    {
        auto& slot = trail[(size_t) (trailPos++ % trail.size())];
        slot.clear();
        const int step = juce::jmax (1, n / 700);
        float mx = 0;
        for (int i = 0; i < n; i += step) { slot.push_back (p[i]); mx = std::max ({ mx, std::abs (p[i].x), std::abs (p[i].y) }); }
        env = std::max ({ mx, env * std::exp (-dt / 1.5f), 0.02f });
        if (isVisible()) repaint();
    }

    juce::String getHelpText() const override
    {
        return "Stereo field: the dots are your audio (up = centre, diagonals = left and right). The amber arc is the width and position you set. Drag sideways to pan, up and down for width.";
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (14, 0);
        corr.setBounds (b.getX() + 18, getHeight() - 40, b.getWidth() - 36, 10);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8.0f);
        const juce::Point<float> c (b.getCentreX(), b.getHeight() * 0.45f);
        const float R = std::min (b.getWidth() * 0.4f, b.getHeight() * 0.36f);

        // grid
        g.setColour (col::bone1.withAlpha (0.07f));
        g.drawEllipse (c.x - R, c.y - R, 2 * R, 2 * R, 1.0f);
        g.drawEllipse (c.x - R / 2, c.y - R / 2, R, R, 1.0f);
        const float dash[] = { 2.0f, 4.0f };
        for (float d : { 0.0f, 45.0f, 90.0f, 135.0f })
            g.drawDashedLine ({ polarPt (c, R * 1.06f, d), polarPt (c, R * 1.06f, d + 180.0f) }, dash, 2, 1.0f);
        auto lab = [&] (const juce::String& t, float d, float r)
        { drawEngraved (g, t, juce::Rectangle<float> (24, 12).withCentre (polarPt (c, r, d)), 10.0f, col::bone3, juce::Justification::centred, 0.0f); };
        lab ("M", 0, R + 14); lab ("L", -45, R + 14); lab ("R", 45, R + 14); lab ("+S", -90, R + 16); lab (minusSign + "S", 90, R + 16);

        // dots with a short phosphor trail
        const float k = R / (juce::MathConstants<float>::sqrt2 * env) * 0.95f;
        for (size_t f = 0; f < trail.size(); ++f)
        {
            const auto& pts = trail[(trailPos + f) % trail.size()];
            const float age = (float) (f + 1) / (float) trail.size();
            for (size_t i = 0; i < pts.size(); ++i)
            {
                const auto& p = pts[i];
                const float x = c.x + (p.y - p.x) / juce::MathConstants<float>::sqrt2 * k;
                const float y = c.y - (p.x + p.y) / juce::MathConstants<float>::sqrt2 * k;
                g.setColour ((i & 1 ? col::phosphor : col::lilac).withAlpha (0.32f * age));
                g.fillRect (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
            }
        }

        // the field you set
        const bool mono = ed.getReal ("mono") > 0.5f, byp = ed.getReal ("bypass") > 0.5f;
        const float span = (mono ? 0.0f : std::min (width, 200.0f) / 100.0f) * 45.0f;
        const float a0 = byp ? 0.0f : pan / 100.0f * 45.0f;
        juce::Path fan, arcP;
        if (span > 0.5f)
        {
            fan.startNewSubPath (c);
            fan.addCentredArc (c.x, c.y, R, R, 0, juce::degreesToRadians (a0 - span), juce::degreesToRadians (a0 + span));
            fan.closeSubPath();
            arcP.addCentredArc (c.x, c.y, R, R, 0, juce::degreesToRadians (a0 - span), juce::degreesToRadians (a0 + span), true);
        }
        else { arcP.startNewSubPath (c); arcP.lineTo (polarPt (c, R, a0)); }
        juce::ColourGradient fg (col::ember.withAlpha (0.0f), c.x, c.y, col::ember.withAlpha (0.13f), c.x + R, c.y, true);
        g.setGradientFill (fg); g.fillPath (fan);
        g.setColour (col::ember.withAlpha (0.25f));
        g.strokePath (arcP, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember);
        g.strokePath (arcP, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (float d : { a0 - span, a0 + span })
        {
            const auto p = polarPt (c, R, d);
            const float r = hot ? 6.0f : 4.5f;
            g.setColour (col::basalt0); g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
            g.setColour (col::ember); g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);
        }
        g.fillEllipse (c.x - 3, c.y - 3, 6, 6);

        // HUD
        drawEngraved (g, "Stereo field", { 12, 8, 140, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        g.setFont (Fonts::mono (11.0f));
        auto hud = juce::Rectangle<float> (b.getRight() - 172, 8, 160, 15);
        auto kv = [&] (const juce::String& key, const juce::String& v, juce::Rectangle<float> r)
        {
            g.setColour (col::emberHi); g.drawText (v, r, juce::Justification::centredRight);
            const float vw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), v) + 6;
            g.setColour (col::bone2); g.drawText (key, r.withTrimmedRight (vw), juce::Justification::centredRight);
        };
        kv ("WIDTH", mono ? "mono" : juce::String (juce::roundToInt (width)) + "%", hud);
        kv ("PAN", fmtPan (pan), hud.translated (0, 15));

        juce::StringArray chips;
        juce::Array<bool> warn;
        auto chip = [&] (const juce::String& s, bool w) { chips.add (s); warn.add (w); };
        if (byp) chip ("Bypassed", true);
        if (mono) chip ("Mono", false);
        if (ed.getReal ("swap") > 0.5f) chip ("L <> R", false);
        if (ed.getReal ("polL") > 0.5f) chip (juce::CharPointer_UTF8 ("\xc3\x98 L"), false);
        if (ed.getReal ("polR") > 0.5f) chip (juce::CharPointer_UTF8 ("\xc3\x98 R"), false);
        if (const int m = (int) ed.getReal ("monitor"); m > 0) chip (m == 1 ? "Mid solo" : "Side solo", false);
        if (const int m = (int) ed.getReal ("matrix"); m > 0) chip (m == 1 ? "M/S encode" : "M/S decode", false);
        if (ed.getReal ("bassMono") > 0.5f) chip ("Bass mono " + juce::String (juce::roundToInt (ed.getReal ("bassFreq"))) + " Hz", false);
        float cx = 12;
        for (int i = 0; i < chips.size(); ++i)
        {
            const float w = juce::GlyphArrangement::getStringWidth (Fonts::label (9.5f).withExtraKerningFactor (0.14f), chips[i].toUpperCase()) + 12;
            auto r = juce::Rectangle<float> (cx, 28, w, 17);
            const auto cc = warn[i] ? col::sigClip : col::ember;
            g.setColour (cc.withAlpha (0.3f)); g.fillRoundedRectangle (r.expanded (2), 4);
            g.setColour (cc); g.fillRoundedRectangle (r, 3);
            drawEngraved (g, chips[i], r, 9.5f, col::basalt0, juce::Justification::centred, 0.14f);
            cx += w + 4;
        }

        if (! touched)
            drawEngraved (g, juce::CharPointer_UTF8 ("Drag \xe2\x86\x94 pan  \xc2\xb7  \xe2\x86\x95 width  \xc2\xb7  double-click resets"),
                          { 0, b.getHeight() - 62, b.getWidth(), 14 }, 10.0f, col::bone3.withAlpha (0.8f), juce::Justification::centred, 0.1f, true);
        g.setFont (Fonts::mono (9.5f)); g.setColour (col::bone3);
        g.drawText (minusSign + "1", juce::Rectangle<float> (14, b.getHeight() - 42, 18, 14), juce::Justification::centredLeft);
        g.drawText ("+1", juce::Rectangle<float> (b.getRight() - 32, b.getHeight() - 42, 18, 14), juce::Justification::centredRight);
        drawEngraved (g, "Phase correlation", { 0, b.getHeight() - 24, b.getWidth(), 12 }, 9.0f, col::bone3, juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent&) override { if (! hot) { hot = true; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override
    {
        touched = true;
        ed.begin();
        panAtt.beginGesture(); widthAtt.beginGesture();
        startPan = pan; startWidth = width; dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float f = e.mods.isShiftDown() ? 0.25f : 1.0f;
        panAtt.setValueAsPartOfGesture (juce::jlimit (-100.0f, 100.0f, (float) juce::roundToInt (startPan + (float) e.getDistanceFromDragStartX() * 0.6f * f)));
        widthAtt.setValueAsPartOfGesture (juce::jlimit (0.0f, 200.0f, (float) juce::roundToInt (startWidth - (float) e.getDistanceFromDragStartY() * 0.9f * f)));
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return;
        dragging = false;
        panAtt.endGesture(); widthAtt.endGesture();
        ed.end();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        ed.begin();
        panAtt.setValueAsCompleteGesture (0.0f);
        widthAtt.setValueAsCompleteGesture (100.0f);
        ed.end();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float dir = w.deltaY * (w.isReversed ? -1.0f : 1.0f);
        if (dir == 0) return;
        ed.begin();
        widthAtt.setValueAsCompleteGesture (juce::jlimit (0.0f, 200.0f, width + (dir > 0 ? 2.0f : -2.0f)));
        ed.end();
    }

private:
    UtilityEditor& ed;
    juce::ParameterAttachment panAtt, widthAtt;
    float pan = 0, width = 100, startPan = 0, startWidth = 100, env = 0.2f;
    std::array<std::vector<juce::Point<float>>, 4> trail;
    size_t trailPos = 0;
    bool hot = false, touched = false, dragging = false;
};

//==============================================================================
class MeterColumn : public juce::Component, public HelpProvider
{
public:
    MeterColumn (juce::String t, LevelMeterView::ScaleSide s) : title (std::move (t)), meter (s) { addAndMakeVisible (meter); }
    LevelMeterView meter;
    float maxPk[2] { -120, -120 };

    void resized() override { meter.setBounds (getLocalBounds().withTrimmedTop (18).withTrimmedBottom (36)); }
    void paint (juce::Graphics& g) override
    {
        drawEngraved (g, title, getLocalBounds().withHeight (12).toFloat(), 10.0f, col::bone3);
        auto rr = getLocalBounds().removeFromBottom (30).toFloat();
        for (int i = 0; i < 2; ++i)
        {
            auto r = (i == 0 ? rr.withWidth (rr.getWidth() / 2 - 1.5f) : rr.withTrimmedLeft (rr.getWidth() / 2 + 1.5f));
            g.setColour (col::basalt0); g.fillRoundedRectangle (r, 3);
            g.setColour (col::lineSoft); g.drawRoundedRectangle (r.reduced (0.5f), 3, 1);
            const float v = maxPk[i];
            g.setColour (v > -0.1f ? col::sigClip : (v > -1.0f ? col::sigHot : col::bone2));
            g.setFont (Fonts::mono (10.0f));
            g.drawText (v > -100 ? fmtNum (v, 1) : minusSign + juce::String::charToString (0x221E), r, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.y > getHeight() - 32) { maxPk[0] = maxPk[1] = -120; meter.reset(); repaint(); }
    }
    juce::String getHelpText() const override { return "Highest peak so far, left and right. Click the numbers to reset."; }

private:
    juce::String title;
};

//==============================================================================
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel (UtilityEditor& e) : ed (e)
    {
        addAndMakeVisible (source);
        addAndMakeVisible (reset);
        reset.small = true;
        reset.help = "Start the loudness measurement again.";
        source.textHeight = 9.5f;
        source.setIndex ((int) ed.proc.pluginState.ui.getProperty ("loudFrom", 1), false);
        source.onChange = [this] (int i) { ed.proc.pluginState.ui.setProperty ("loudFrom", i, nullptr); repaint(); };
        reset.onClick = [this] { ed.proc.loudIn.requestReset(); ed.proc.loudOut.requestReset(); hist[0].clear(); hist[1].clear(); };
        setHelp (*this, "Loudness (ITU-R BS.1770 / EBU R128). Short-term is the last 3 seconds, momentary the last 0.4 s, integrated everything since reset.");
    }
    SegControl source { { "In", "Out" }, { "Measure the signal coming in.", "Measure the signal going out." } };
    Button reset { "Reset" };
    std::array<std::vector<float>, 2> hist;

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        source.setBounds (r.removeFromTop (22).removeFromRight (78));
        reset.setBounds (r.removeFromBottom (22).removeFromRight (58));
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        auto r = b.reduced (12);
        drawEngraved (g, "Loudness", r.withHeight (22), 10.0f, col::bone3, juce::Justification::centredLeft);
        r.removeFromTop (30);
        const int sel = source.getIndex();
        auto& m = sel == 0 ? ed.proc.loudIn : ed.proc.loudOut;
        drawEngraved (g, "Short-term", r.removeFromTop (14), 10.0f, col::bone3, juce::Justification::centredLeft);
        auto big = r.removeFromTop (36);
        g.setFont (Fonts::mono (30.0f)); g.setColour (col::bone1);
        const auto st = fmtLufs (m.getShortTerm());
        g.drawText (st, big, juce::Justification::centredLeft);
        drawEngraved (g, "LUFS", big.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (Fonts::mono (30.0f), st) + 6).withTrimmedTop (12), 11.0f, col::bone3, juce::Justification::centredLeft, 0.12f);
        r.removeFromTop (6);
        auto row = [&] (const juce::String& k, const juce::String& v)
        {
            auto rr = r.removeFromTop (26);
            g.setColour (col::lineSoft); g.fillRect (rr.withHeight (1));
            drawEngraved (g, k, rr.withTrimmedTop (4), 10.0f, col::bone3, juce::Justification::centredLeft);
            g.setFont (Fonts::mono (12.5f)); g.setColour (col::bone1);
            g.drawText (v, rr.withTrimmedTop (4), juce::Justification::centredRight);
        };
        row ("Momentary", fmtLufs (m.getMomentary()));
        row ("Integrated", fmtLufs (m.getIntegrated()));
        const float tp = m.getTruePeakMaxDb();
        row ("True peak", std::isfinite (tp) ? fmtNum (tp, 1) + " dB" : fmtLufs (tp));
        r.removeFromTop (8);
        auto gr = r.removeFromTop (64);
        // history, last 30 s
        auto y = [&] (float v) { return gr.getBottom() - (juce::jlimit (-40.0f, 0.0f, v) + 40.0f) / 40.0f * gr.getHeight(); };
        g.setFont (Fonts::mono (8.0f));
        for (float v : { -10.0f, -20.0f, -30.0f })
        {
            g.setColour (col::bone1.withAlpha (0.07f)); g.fillRect (gr.getX(), y (v), gr.getWidth(), 1.0f);
            g.setColour (col::bone3); g.drawText (fmtNum (v, 0), juce::Rectangle<float> (gr.getX() + 1, y (v) - 10, 30, 10), juce::Justification::centredLeft);
        }
        auto& h = hist[(size_t) sel];
        if (h.size() > 1)
        {
            juce::Path line, area;
            const float dx = gr.getWidth() / 299.0f;
            for (size_t i = 0; i < h.size(); ++i)
            {
                const float x = gr.getRight() - (float) (h.size() - 1 - i) * dx;
                const float yy = y (std::isfinite (h[i]) ? h[i] : -40.0f);
                if (i == 0) { line.startNewSubPath (x, yy); area.startNewSubPath (x, gr.getBottom()); }
                else line.lineTo (x, yy);
                area.lineTo (x, yy);
            }
            area.lineTo (gr.getRight(), gr.getBottom()); area.closeSubPath();
            g.setGradientFill (juce::ColourGradient::vertical (col::phosphor.withAlpha (0.35f), gr.getY(), col::phosphor.withAlpha (0.0f), gr.getBottom()));
            g.fillPath (area);
            g.setColour (col::phosphor); g.strokePath (line, juce::PathStrokeType (1.5f));
        }
        g.setFont (Fonts::text (9.5f)); g.setColour (col::bone3);
        g.drawText ("Last 30 s", getLocalBounds().reduced (12).removeFromBottom (22).toFloat(), juce::Justification::centredLeft);
    }
    void push (int which, float v) { auto& h = hist[(size_t) which]; h.push_back (v); if (h.size() > 300) h.erase (h.begin()); }

private:
    UtilityEditor& ed;
};

//==============================================================================
class Gauge : public juce::Component
{
public:
    explicit Gauge (UtilityEditor& e) : ed (e)
    {
        addAndMakeVisible (targets);
        targets.textHeight = 9.5f;
        const float t = ed.target();
        targets.setIndex (t > -12 ? 1 : (t < -18 ? 2 : 0), false);
        targets.onChange = [this] (int i) { ed.proc.pluginState.ui.setProperty ("target", i == 0 ? -14.0 : (i == 1 ? -9.0 : -23.0), nullptr); repaint(); };
        setHelp (*this, "Loudness compared with your target. The green band is the target, plus or minus 1 LU.");
    }
    SegControl targets { { "Streaming", "Club", "Broadcast" },
                         { "Streaming services normalise to about -14 LUFS.", "Loud club and EDM masters often sit near -9 LUFS.", "EBU R128 broadcast target: -23 LUFS." } };
    float shown = -40.0f;

    void resized() override { targets.setBounds (getLocalBounds().reduced (12).removeFromBottom (24)); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        auto r = b.reduced (12);
        drawEngraved (g, "Loudness", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "target " + fmtNum (ed.target(), 0), r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredRight);
        const float tgt = ed.target();
        const juce::Point<float> c (b.getCentreX(), 128);
        const float R = std::min (b.getWidth() * 0.4f, 78.0f);
        auto ang = [&] (float v) { return -126.0f + (juce::jlimit (-36.0f, 0.0f, v) + 36.0f) / 36.0f * 252.0f; };
        auto arcPath = [&] (float d0, float d1) { juce::Path p; p.addCentredArc (c.x, c.y, R, R, 0, juce::degreesToRadians (d0), juce::degreesToRadians (d1), true); return p; };
        g.setColour (col::groove);
        g.strokePath (arcPath (-126, 126), juce::PathStrokeType (12, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::phosphor.withAlpha (0.28f));
        g.strokePath (arcPath (ang (tgt - 1), ang (tgt + 1)), juce::PathStrokeType (12));
        if (shown > -36.0f)
        {
            const bool ok = std::abs (shown - tgt) <= 1.0f;
            const auto cc = ok ? col::phosphor : col::ember;
            auto p = arcPath (-126, ang (shown));
            g.setColour (cc.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (12, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (cc); g.strokePath (p, juce::PathStrokeType (6, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto dot = polarPt (c, R, ang (shown));
            g.setColour (col::bone1); g.fillEllipse (dot.x - 6, dot.y - 6, 12, 12);
        }
        g.setFont (Fonts::mono (8.5f)); g.setColour (col::bone3);
        for (float v : { -36.0f, -24.0f, -14.0f, -9.0f, 0.0f })
            g.drawText (v == 0 ? juce::String ("0") : fmtNum (v, 0), juce::Rectangle<float> (30, 10).withCentre (polarPt (c, R + 17, ang (v))), juce::Justification::centred);
        {
            const float ta = ang (tgt);
            juce::Path tri;
            tri.addTriangle (polarPt (c, R - 10, ta), polarPt (c, R - 18, ta - 3), polarPt (c, R - 18, ta + 3));
            g.setColour (col::phosphor); g.fillPath (tri);
        }
        const float st = ed.stOut;
        g.setFont (Fonts::mono (30.0f)); g.setColour (col::bone1);
        g.drawText (fmtLufs (st), juce::Rectangle<float> (b.getWidth(), 34).withCentre ({ c.x, c.y + 4 }), juce::Justification::centred);
        drawEngraved (g, "LUFS short-term", juce::Rectangle<float> (b.getWidth(), 12).withCentre ({ c.x, c.y + 28 }), 9.5f, col::bone3);
        juce::String verdict = "No signal.";
        if (std::isfinite (st) && st > -60.0f)
        {
            const float d = st - tgt;
            verdict = std::abs (d) <= 1.0f ? "On target for " + ed.targetName() + "."
                                           : juce::String (std::abs (d), 1) + " dB " + (d < 0 ? "quieter" : "louder") + " than " + ed.targetName() + ".";
        }
        g.setFont (Fonts::textMedium (12.0f)); g.setColour (col::bone2);
        g.drawFittedText (verdict, juce::Rectangle<int> ((int) b.getWidth() - 20, 34).withCentre ({ (int) c.x, (int) b.getBottom() - 62 }), juce::Justification::centred, 2);
    }

private:
    UtilityEditor& ed;
};

//==============================================================================
class Health : public juce::Component
{
public:
    enum class State { ok, warn, bad };
    struct Row { State s = State::ok; juce::String title, desc, fixLabel; std::function<void()> fix; };

    explicit Health (UtilityEditor& e) : ed (e)
    {
        rows[0] = { State::ok, "Mono-safe", "Waiting for signal." };
        rows[1] = { State::ok, "Headroom", "Waiting for signal." };
        rows[2] = { State::ok, "Balance", "Waiting for signal." };
        rows[3] = { State::ok, "Loudness", "Waiting for signal." };
        for (auto& b : fixes) { b = std::make_unique<Button> ("Fix"); b->small = true; addChildComponent (*b); }
        setHelp (*this, "Mix check: live, plain-language checks on what comes out of the plugin.");
    }
    std::array<Row, 4> rows;

    void update()
    {
        const float st = ed.stOut;
        const bool silent = ! std::isfinite (st) || st < -60.0f;
        if (silent)
            for (auto& r : rows) { r.s = State::ok; r.desc = "Waiting for signal."; r.fix = nullptr; }
        else
        {
            auto& m = rows[0];
            if (ed.corrAvg < -0.05f) m = { State::bad, "Phase problem", "Left and right cancel each other. In mono, parts of the mix will disappear.", "Fix it", [this] { ed.fixPhase(); } };
            else if (ed.corrAvg < 0.25f) m = { State::warn, "Very wide", "Fine on headphones, but check it in mono before you bounce.", "Check in mono", [this] { ed.begin(); ed.setReal ("mono", 1); ed.end(); } };
            else m = { State::ok, "Mono-safe", "Your stereo holds up when summed to mono." };

            auto& p = rows[1];
            const float ph = ed.peakHold;
            if (ph > -0.1f) p = { State::bad, "Clipping", "Peaks reach " + fmtNum (ph, 1) + " dBFS and will distort.", "Lower " + juce::String ((int) std::ceil (ph + 1.0f)) + " dB", [this] { ed.lowerForHeadroom(); } };
            else if (ph > -1.0f) p = { State::warn, "Close to clipping", "Peaks at " + fmtNum (ph, 1) + " dBFS. Leave about 1 dB for converters and streaming." };
            else p = { State::ok, "Headroom", "Peaks stay at " + fmtNum (ph, 1) + " dBFS, safely below " + minusSign + "1." };

            auto& b = rows[2];
            const float d = ed.balanceDb;
            if (ed.getReal ("monitor") > 0.5f || ed.getReal ("matrix") > 0.5f) b = { State::ok, "Balance", "Not measured while M/S tools are on." };
            else if (std::abs (d) < 1.5f) b = { State::ok, "Balance", "Left and right are even (" + juce::String (std::abs (d), 1) + " dB apart)." };
            else b = { std::abs (d) > 4.0f ? State::bad : State::warn, d > 0 ? "Leaning left" : "Leaning right",
                       "One side is " + juce::String (std::abs (d), 1) + " dB louder.", "Centre it", [this] { ed.centreBalance(); } };

            auto& l = rows[3];
            const float dl = st - ed.target();
            if (std::abs (dl) <= 1.0f) l = { State::ok, "On target", fmtNum (st, 1) + " LUFS, right where " + ed.targetName() + " wants it." };
            else l = { State::warn, dl < 0 ? "Quieter than target" : "Louder than target",
                       fmtNum (st, 1) + " LUFS, " + juce::String (std::abs (dl), 1) + " dB " + (dl < 0 ? "under " : "over ") + ed.targetName() + ".",
                       "Match", [this] { ed.matchLoudness(); } };
        }
        for (size_t i = 0; i < 4; ++i)
        {
            auto& b = *fixes[i];
            b.setVisible (rows[i].fix != nullptr);
            b.setButtonText (rows[i].fixLabel);
            b.onClick = rows[i].fix;
        }
        resized();
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12).withTrimmedTop (26);
        for (size_t i = 0; i < 4; ++i)
        {
            auto rr = r.removeFromTop (rowH (i)); r.removeFromTop (7);
            if (fixes[i]->isVisible())
            {
                const int w = (int) juce::GlyphArrangement::getStringWidth (Fonts::label (10.0f).withExtraKerningFactor (0.12f), rows[i].fixLabel.toUpperCase()) + 20;
                fixes[i]->setBounds (rr.getX() + 28, rr.getBottom() - 30, w, 22);
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        auto r = b.reduced (12);
        drawEngraved (g, "Mix check", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "live", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredRight);
        r.removeFromTop (26);
        for (size_t i = 0; i < 4; ++i)
        {
            auto rr = r.removeFromTop ((float) rowH (i)); r.removeFromTop (7);
            const auto& row = rows[i];
            const auto cc = row.s == State::ok ? col::sigOk : (row.s == State::warn ? col::sigHot : col::sigClip);
            g.setColour (row.s == State::bad ? col::sigClip.withAlpha (0.05f) : juce::Colours::white.withAlpha (0.02f));
            g.fillRoundedRectangle (rr, 5);
            g.setColour (row.s == State::bad ? col::sigClip.withAlpha (0.35f) : col::lineSoft);
            g.drawRoundedRectangle (rr.reduced (0.5f), 5, 1);
            auto in = rr.reduced (10, 9);
            auto dot = juce::Rectangle<float> (8, 8).withPosition (in.getX(), in.getY() + 3);
            g.setColour (cc.withAlpha (0.4f)); g.fillEllipse (dot.expanded (3));
            g.setColour (cc); g.fillEllipse (dot);
            in.removeFromLeft (18);
            g.setFont (Fonts::textMedium (12.5f)); g.setColour (col::bone1);
            g.drawText (row.title, in.removeFromTop (16), juce::Justification::centredLeft);
            g.setFont (Fonts::text (11.5f)); g.setColour (col::bone2);
            g.drawFittedText (row.desc, in.removeFromTop (32).toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
        }
    }

private:
    int rowH (size_t i) const { return rows[i].fix ? 88 : 62; }
    UtilityEditor& ed;
    std::array<std::unique_ptr<Button>, 4> fixes;
};

} // namespace aemon::utility::ui

using namespace aemon::utility::ui;

//==============================================================================
namespace
{
    /** A body that lays its children out with a lambda. */
    struct Body : juce::Component
    {
        std::function<void (juce::Rectangle<int>)> layout;
        void resized() override { if (layout) layout (getLocalBounds().reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10)); }
    };

    juce::Path matchIcon() { juce::Path p; p.startNewSubPath (1, 6); p.lineTo (4, 6); p.lineTo (5.5f, 2); p.lineTo (7.5f, 10); p.lineTo (9, 6); p.lineTo (11, 6); return p; }
    juce::Path checkIcon() { juce::Path p; p.startNewSubPath (2, 6.5f); p.lineTo (4.5f, 9); p.lineTo (10, 3); return p; }
}

UtilityEditor::UtilityEditor (UtilityProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    shell = std::make_unique<Shell> (*this, proc.pluginState, "Utility", baseW, baseH);
    addAndMakeVisible (*shell);

    buildSimple();
    buildPro();
    shell->setModeComponents (simpleBody.get(), simpleDeck.get(), proBody.get(), proDeck.get());
    shell->setMeta (juce::String (proc.currentRate.load() / 1000.0, 1) + " kHz  " + juce::String::charToString (0xb7) + "  latency 0 samples  " + juce::String::charToString (0xb7) + "  v" AEMON_VERSION);

    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    const double s = (double) proc.pluginState.ui.getProperty ("scale", 1.0);
    setSize (juce::roundToInt (baseW * juce::jlimit (0.6, 2.0, s)), juce::roundToInt (baseH * juce::jlimit (0.6, 2.0, s)));
    setWantsKeyboardFocus (true);

    for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->sweepIn (120 + (int) (i % 4) * 70);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

UtilityEditor::~UtilityEditor()
{
    stopTimer();
    paramAtts.clear();
    shell.reset();
    setLookAndFeel (nullptr);
}

void UtilityEditor::resized() { shell->setBounds (getLocalBounds()); }

float UtilityEditor::getReal (const juce::String& id) const
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void UtilityEditor::setReal (const juce::String& id, float value)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

juce::String UtilityEditor::targetName() const
{
    const float t = target();
    return t > -12 ? "a club master" : (t < -18 ? "broadcast" : "streaming");
}

aemon::Knob& UtilityEditor::addKnob (juce::Component& parent, const juce::String& id, Knob::Options o)
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

aemon::Button& UtilityEditor::addToggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto b = std::make_unique<Button> (text, true);
    b->help = help;
    b->setClickingTogglesState (false);
    auto& ref = *b;
    parent.addAndMakeVisible (ref);
    auto att = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (id),
        [&ref] (float v) { ref.setToggleState (v > 0.5f, juce::dontSendNotification); });
    att->sendInitialUpdate();
    auto* a = att.get();
    ref.onClick = [this, &ref, a]
    {
        begin();
        a->setValueAsCompleteGesture (ref.getToggleState() ? 0.0f : 1.0f);
        end();
    };
    paramAtts.push_back (std::move (att));
    owned.push_back (std::move (b));
    return ref;
}

void UtilityEditor::buildSimple()
{
    auto body = std::make_unique<Body>();
    auto h = std::make_unique<Health> (*this);
    auto f = std::make_unique<StereoField> (*this);
    auto gg = std::make_unique<Gauge> (*this);
    health = h.get(); fieldSimple = f.get(); gauge = gg.get();
    body->addAndMakeVisible (*h); body->addAndMakeVisible (*f); body->addAndMakeVisible (*gg);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        health->setBounds (r.removeFromLeft (244)); r.removeFromLeft (10);
        gauge->setBounds (r.removeFromRight (214)); r.removeFromRight (10);
        fieldSimple->setBounds (r);
    };
    owned.push_back (std::move (h)); owned.push_back (std::move (f)); owned.push_back (std::move (gg));
    simpleBody = std::move (body);

    simpleDeck = std::make_unique<Deck>();
    auto& d = *simpleDeck;
    Knob::Options gainO;
    gainO.bipolar = true; gainO.detent = true; gainO.ticks = 13; gainO.minLabel = minusSign + "36"; gainO.maxLabel = "+36";
    gainO.format = fmtDbSigned; gainO.name = "Volume"; gainO.help = "Make it louder or quieter.";
    auto& vol = addKnob (d, "gain", gainO);
    Knob::Options wO;
    wO.bipolar = true; wO.centre = 100; wO.detent = true; wO.ticks = 9; wO.minLabel = "0"; wO.maxLabel = "200"; wO.centreLabel = "100";
    wO.format = [] (float v) { const int r = juce::roundToInt (v); return r == 0 ? juce::String ("Mono") : (r == 100 ? juce::String ("Original") : juce::String (r) + "%"); };
    wO.name = "Stereo width"; wO.help = "Turn left to narrow towards mono, right to widen.";
    auto& wid = addKnob (d, "width", wO);
    Knob::Options pO;
    pO.bipolar = true; pO.detent = true; pO.ticks = 11; pO.minLabel = "L"; pO.maxLabel = "R";
    pO.format = fmtPan; pO.parse = parsePan; pO.name = "Balance"; pO.help = "Lean the sound left or right. Type 30L or 20R.";
    auto& bal = addKnob (d, "pan", pO);

    auto mb = std::make_unique<Button> ("Match target loudness");
    mb->icon = matchIcon();
    mb->help = "Measures the output and sets Volume so it lands on your loudness target, without letting peaks pass -1 dBFS.";
    mb->onClick = [this] { matchLoudness(); };
    matchBtn = mb.get();
    d.addAndMakeVisible (*mb); owned.push_back (std::move (mb));
    auto& monoB = addToggle (d, "mono", "Check in mono", "Check in mono: hear your mix the way a phone speaker or a mono club system hears it. Click again to go back.");
    monoB.hint = "hear what a phone hears";
    auto fb = std::make_unique<Button> ("Fix phase problem");
    fb->icon = checkIcon();
    fb->help = "Lights up when left and right work against each other. One click flips one side back.";
    fb->onClick = [this] { fixPhase(); };
    fb->setEnabled (false);
    fixBtn = fb.get();
    d.addAndMakeVisible (*fb); owned.push_back (std::move (fb));

    auto place = [] (juce::Component& c, juce::Rectangle<int> r) { c.setBounds (r.withSizeKeepingCentre (110, 98 + 25)); };
    d.groups = {
        { "Volume", "", 110, [&vol, place] (auto r) { place (vol, r); } },
        { "Stereo width", "", 110, [&wid, place] (auto r) { place (wid, r); } },
        { "Balance", "", 110, [&bal, place] (auto r) { place (bal, r); } },
        { "One-click helpers", "", 250, [this, &monoB] (juce::Rectangle<int> r)
          {
              auto c = r.withSizeKeepingCentre (250, 30 * 3 + 12);
              matchBtn->setBounds (c.removeFromTop (30)); c.removeFromTop (6);
              monoB.setBounds (c.removeFromTop (30)); c.removeFromTop (6);
              fixBtn->setBounds (c.removeFromTop (30));
          } },
    };
}

void UtilityEditor::buildPro()
{
    auto body = std::make_unique<Body>();
    auto ic = std::make_unique<MeterColumn> ("In", LevelMeterView::ScaleSide::left);
    auto oc = std::make_unique<MeterColumn> ("Out", LevelMeterView::ScaleSide::right);
    auto f = std::make_unique<StereoField> (*this);
    auto lp = std::make_unique<LoudnessPanel> (*this);
    inCol = ic.get(); outCol = oc.get(); fieldPro = f.get(); loudPanel = lp.get();
    for (auto* c : std::initializer_list<juce::Component*> { inCol, fieldPro, outCol, loudPanel }) body->addAndMakeVisible (c);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        inCol->setBounds (r.removeFromLeft (70)); r.removeFromLeft (10);
        loudPanel->setBounds (r.removeFromRight (182)); r.removeFromRight (10);
        outCol->setBounds (r.removeFromRight (70)); r.removeFromRight (10);
        fieldPro->setBounds (r);
    };
    owned.push_back (std::move (ic)); owned.push_back (std::move (oc)); owned.push_back (std::move (f)); owned.push_back (std::move (lp));
    proBody = std::move (body);

    proDeck = std::make_unique<Deck>();
    auto& d = *proDeck;
    d.pad = 14;
    Knob::Options gO;
    gO.bipolar = true; gO.detent = true; gO.ticks = 13; gO.minLabel = minusSign + "36"; gO.maxLabel = "+36";
    gO.format = fmtDbSigned; gO.name = "Gain"; gO.help = "Level change from -36 to +36 dB.";
    auto& gain = addKnob (d, "gain", gO);
    Knob::Options pO;
    pO.bipolar = true; pO.detent = true; pO.ticks = 11; pO.minLabel = "L"; pO.maxLabel = "R";
    pO.format = fmtPan; pO.parse = parsePan; pO.name = "Pan"; pO.help = "Balance between left and right. Type 30L or 20R.";
    auto& pan = addKnob (d, "pan", pO);
    Knob::Options wO;
    wO.bipolar = true; wO.centre = 100; wO.detent = true; wO.ticks = 9; wO.minLabel = "0"; wO.maxLabel = "200"; wO.centreLabel = "100";
    wO.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    wO.name = "Width"; wO.help = "0% is mono, 100% is unchanged, 200% doubles the side signal.";
    auto& wid = addKnob (d, "width", wO);
    Knob::Options bO;
    bO.style = Knob::Style::compact;
    bO.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; };
    bO.name = "Bass mono below"; bO.help = "Everything below this frequency is made mono; the mid is never touched.";
    auto& bf = addKnob (d, "bassFreq", bO);

    auto& mono = addToggle (d, "mono", "Mono", "Mono: sums left and right. Same as width 0%, but keeps your width setting for when you switch back.");
    auto& swap = addToggle (d, "swap", "Swap", "Swap: left goes right, right goes left.");
    auto& pl = addToggle (d, "polL", juce::CharPointer_UTF8 ("\xc3\x98 Left"), "Polarity left: flips the left channel upside down (often called phase invert).");
    auto& pr = addToggle (d, "polR", juce::CharPointer_UTF8 ("\xc3\x98 Right"), "Polarity right: flips the right channel upside down.");
    auto& bm = addToggle (d, "bassMono", "On", "Bass mono: keeps the low end mono and tight while the top stays wide. Only the side signal is filtered.");

    auto law = std::make_unique<juce::TextButton>();
    auto* lawRaw = law.get();
    auto lawAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("panLaw"),
        [lawRaw, this] (float) { lawRaw->setButtonText ("law " + mns (proc.apvts.getParameter ("panLaw")->getCurrentValueAsText())); });
    lawAtt->sendInitialUpdate();
    law->onClick = [this, a = lawAtt.get()]
    {
        begin();
        a->setValueAsCompleteGesture ((float) (((int) getReal ("panLaw") + 1) % 4));
        end();
    };
    paramAtts.push_back (std::move (lawAtt));
    setHelp (*law, "Pan law: how much louder a hard-panned side gets compared with centre (centre stays at unity). Click to change.");
    law->getProperties().set ("kind", "chip");
    law->setMouseCursor (juce::MouseCursor::PointingHandCursor);
    d.addAndMakeVisible (*law);
    owned.push_back (std::move (law));

    auto mon = std::make_unique<SegControl> (juce::StringArray { "Stereo", "Mid", "Side" },
        juce::StringArray { "Normal stereo output.", "Solo the Mid (L+R): what both speakers share.", "Solo the Side (L-R): only the difference between speakers." });
    mon->attach (*proc.apvts.getParameter ("monitor"), [this] { begin(); }, [this] { end(); });
    auto mx = std::make_unique<SegControl> (juce::StringArray { "Off", "Encode", "Decode" },
        juce::StringArray { "No conversion.", "Encode: L/R in, Mid on the left channel and Side on the right out.", "Decode: M/S in (Mid left, Side right), normal L/R out." });
    mx->attach (*proc.apvts.getParameter ("matrix"), [this] { begin(); }, [this] { end(); });
    auto* monRaw = mon.get(); auto* mxRaw = mx.get();
    d.addAndMakeVisible (*mon); d.addAndMakeVisible (*mx);
    owned.push_back (std::move (mon)); owned.push_back (std::move (mx));

    auto knobBox = [] (juce::Component& c, juce::Rectangle<int> r) { c.setBounds (r.withSizeKeepingCentre (96, 86 + 25)); };
    d.groups = {
        { "Gain", "", 96, [&gain, knobBox] (auto r) { knobBox (gain, r); } },
        { "Pan", "", 112, [&pan, knobBox] (auto r) { knobBox (pan, r); }, lawRaw },
        { "Stereo", "", 272, [&wid, &mono, &swap, &pl, &pr] (juce::Rectangle<int> r)
          {
              wid.setBounds (r.removeFromLeft (96).withSizeKeepingCentre (96, 86 + 25));
              r.removeFromLeft (14);
              auto grid = r.withSizeKeepingCentre (162, 26 * 2 + 6);
              auto top = grid.removeFromTop (26); grid.removeFromTop (6);
              mono.setBounds (top.removeFromLeft (78)); swap.setBounds (top.removeFromRight (78));
              pl.setBounds (grid.removeFromLeft (78)); pr.setBounds (grid.removeFromRight (78));
          } },
        { "Mid / Side", "", 196, [monRaw, mxRaw] (juce::Rectangle<int> r)
          {
              auto c = r.withSizeKeepingCentre (196, 100);
              c.removeFromTop (14); monRaw->setBounds (c.removeFromTop (28));
              c.removeFromTop (16); mxRaw->setBounds (c.removeFromTop (28));
          } },
        { "Bass mono", "", 104, [&bm, &bf] (juce::Rectangle<int> r)
          {
              auto c = r.withSizeKeepingCentre (104, 110);
              bm.setBounds (c.removeFromTop (26).withSizeKeepingCentre (64, 26));
              c.removeFromTop (6);
              bf.setBounds (c.withSizeKeepingCentre (70, 46 + 25));
          } },
    };

    // sub-labels for the M/S group, painted by a tiny overlay
    struct Sub : juce::Component
    {
        juce::Component *a, *b;
        Sub (juce::Component* x, juce::Component* y) : a (x), b (y) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            auto pa = a->getBounds().translated (-getX(), -getY()), pb = b->getBounds().translated (-getX(), -getY());
            drawEngraved (g, "Listen to", pa.withY (pa.getY() - 13).withHeight (11).toFloat(), 9.0f, col::bone3, juce::Justification::centred, 0.16f, true);
            drawEngraved (g, "Matrix", pb.withY (pb.getY() - 13).withHeight (11).toFloat(), 9.0f, col::bone3, juce::Justification::centred, 0.16f, true);
        }
    };
    auto sub = std::make_unique<Sub> (monRaw, mxRaw);
    d.addAndMakeVisible (*sub);
    d.overlays.push_back (sub.get());
    owned.push_back (std::move (sub));
}

void UtilityEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.001, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;
    ++frame;

    const auto in = proc.inTap.take(), out = proc.outTap.take();
    auto db = [] (float g) { return juce::Decibels::gainToDecibels (g, -120.0f); };
    const float a = 1.0f - std::exp (-dt / 0.3f), a15 = 1.0f - std::exp (-dt / 1.5f);
    if (out.count > 0)
    {
        for (int c = 0; c < 2; ++c) { msIn[c] += (in.meanSquare (c) - msIn[c]) * a; msOut[c] += (out.meanSquare (c) - msOut[c]) * a; }
        balL += (out.meanSquare (0) - balL) * a15; balR += (out.meanSquare (1) - balR) * a15;
        corrAvg += (out.correlation() - corrAvg) * a15;
    }
    const float pkI[2] { db (in.peak[0]), db (in.peak[1]) }, pkO[2] { db (out.peak[0]), db (out.peak[1]) };
    const float rI[2] { 10.0f * std::log10 (std::max (msIn[0], 1e-12f)), 10.0f * std::log10 (std::max (msIn[1], 1e-12f)) };
    const float rO[2] { 10.0f * std::log10 (std::max (msOut[0], 1e-12f)), 10.0f * std::log10 (std::max (msOut[1], 1e-12f)) };
    peakHold = std::max ({ pkO[0], pkO[1], peakHold - dt * 1.5f });
    balanceDb = 10.0f * std::log10 (std::max (balL, 1e-12f) / std::max (balR, 1e-12f));
    stOut = proc.loudOut.getShortTerm();

    const bool simple = shell->isSimple();
    pts.resize (4096);
    const int n = proc.scope.pop (pts.data(), 4096);
    auto* field = simple ? fieldSimple : fieldPro;
    field->addPoints (pts.data(), n, dt);
    if (out.count > 0) field->corr.push (out.correlation(), dt);

    float h[64];
    const int nIn = proc.loudIn.popShortTermHistory (h, 64);
    for (int i = 0; i < nIn; ++i) loudPanel->push (0, h[i]);
    const int nOut = proc.loudOut.popShortTermHistory (h, 64);
    for (int i = 0; i < nOut; ++i) loudPanel->push (1, h[i]);

    if (simple)
    {
        const float tgt = std::isfinite (stOut) ? std::max (stOut, -40.0f) : -40.0f;
        gauge->shown += (tgt - gauge->shown) * (1.0f - std::exp (-dt / 0.25f));
        if (frame % 2 == 0) gauge->repaint();
        if (frame % 18 == 0)
        {
            health->update();
            const bool bad = corrAvg < -0.05f;
            fixBtn->setEnabled (bad);
            fixBtn->attention = bad;
            fixBtn->hint = bad ? "one click" : "nothing to fix";
            matchBtn->hint = std::isfinite (stOut) && stOut > -60.0f ? fmtNum (stOut, 1) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + fmtNum (target(), 0) : juce::String();
            matchBtn->repaint();
        }
        if (fixBtn->attention) fixBtn->repaint();
    }
    else
    {
        inCol->meter.push (pkI, rI, dt);
        outCol->meter.push (pkO, rO, dt);
        for (int c = 0; c < 2; ++c) { inCol->maxPk[c] = std::max (inCol->maxPk[c], pkI[c]); outCol->maxPk[c] = std::max (outCol->maxPk[c], pkO[c]); }
        if (frame % 6 == 0) { inCol->repaint(); outCol->repaint(); loudPanel->repaint(); }
    }
}

void UtilityEditor::matchLoudness()
{
    if (! std::isfinite (stOut) || stOut < -60.0f) { shell->toast ("No signal to measure yet."); return; }
    float delta = target() - stOut;
    const float headroom = -1.0f - peakHold;
    bool capped = false;
    if (delta > headroom) { delta = std::max (0.0f, headroom); capped = true; }
    const float ng = juce::jlimit (-36.0f, 36.0f, std::round ((getReal ("gain") + delta) * 10.0f) / 10.0f);
    begin(); setReal ("gain", ng); end();
    shell->toast (capped ? "Raised by " + juce::String (delta, 1) + " dB, then stopped so peaks stay under -1 dBFS. Louder than that needs a limiter (Aemon Limiter, coming later)."
                         : "Volume set to " + fmtDbSigned (ng) + " to hit " + fmtNum (target(), 0) + " LUFS.");
}

void UtilityEditor::fixPhase()
{
    begin(); setReal ("polR", getReal ("polR") > 0.5f ? 0.0f : 1.0f); end();
    shell->toast ("Flipped the right channel's polarity. The two sides now add up instead of cancelling.");
}

void UtilityEditor::lowerForHeadroom()
{
    const float by = std::ceil (peakHold + 1.0f);
    begin(); setReal ("gain", juce::jlimit (-36.0f, 36.0f, getReal ("gain") - by)); end();
    peakHold = -120.0f;
    shell->toast ("Lowered by " + juce::String ((int) by) + " dB.");
}

void UtilityEditor::centreBalance()
{
    const int law = (int) getReal ("panLaw");
    const auto cur = aemon::utility::panGains (getReal ("pan") / 100.0f, law);
    const float want = 20.0f * std::log10 (cur[0] / cur[1]) - balanceDb;
    int best = 0; float bestErr = 1e9f;
    for (int p = -100; p <= 100; ++p)
    {
        const auto g = aemon::utility::panGains ((float) p / 100.0f, law);
        const float e = std::abs (20.0f * std::log10 (std::max (g[0], 1e-6f) / std::max (g[1], 1e-6f)) - want);
        if (e < bestErr) { bestErr = e; best = p; }
    }
    begin(); setReal ("pan", (float) best); end();
    shell->toast ("Balance set to " + fmtPan ((float) best) + " to even out left and right.");
}

bool UtilityEditor::keyPressed (const juce::KeyPress& k)
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
