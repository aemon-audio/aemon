// Aemon EQ. AGPL-3.0-or-later.
#include "EqEditor.h"

using namespace aemon;
using aemon::eq::Type;
using aemon::eq::Place;

namespace
{
    constexpr int baseW = 960, baseH = 600;
    const juce::String minusSign = juce::String::charToString (0x2212);
    juce::String mns (const juce::String& s) { return s.replace ("-", minusSign); }
    juce::String fmtFreq (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
    juce::String fmtDb (float v) { auto s = juce::String (v, 1); if (s == "-0.0") s = "0.0"; return mns ((v > 0.05f ? "+" : "") + s) + " dB"; }
    std::optional<float> parseFreq (const juce::String& s)
    {
        auto t = s.trim().toLowerCase();
        if (! t.containsAnyOf ("0123456789")) return {};
        const float v = t.retainCharacters ("0123456789.").getFloatValue();
        return t.contains ("k") ? v * 1000.0f : v;
    }
    juce::String noteName (float f)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float m = 69.0f + 12.0f * std::log2 (f / 440.0f);
        const int mi = juce::roundToInt (m), cents = juce::roundToInt ((m - (float) mi) * 100.0f);
        return juce::String (names[(mi % 12 + 12) % 12]) + juce::String (mi / 12 - 1) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + mns (juce::String (cents)) + "c");
    }
    const char* roleNames[] = { "Low cut", "Bass", "Mud", "Presence", "Air" };

    /** Shape icons, drawn in a 20 x 12 box. */
    juce::Path shapeIcon (int t)
    {
        juce::Path p;
        switch (t)
        {
            case 0: p.startNewSubPath (1, 9); p.cubicTo (6, 9, 7, 2, 10, 2); p.cubicTo (13, 2, 14, 9, 19, 9); break;
            case 1: p.startNewSubPath (1, 3); p.lineTo (6, 3); p.cubicTo (9, 3, 10, 9, 13, 9); p.lineTo (19, 9); break;
            case 2: p.startNewSubPath (1, 9); p.lineTo (7, 9); p.cubicTo (10, 9, 11, 3, 14, 3); p.lineTo (19, 3); break;
            case 3: p.startNewSubPath (2, 11); p.cubicTo (5, 5, 7, 3, 10, 3); p.lineTo (19, 3); break;
            case 4: p.startNewSubPath (1, 3); p.lineTo (10, 3); p.cubicTo (13, 3, 15, 5, 18, 11); break;
            case 5: p.startNewSubPath (1, 3); p.lineTo (8, 3); p.lineTo (10, 11); p.lineTo (12, 3); p.lineTo (19, 3); break;
            case 6: p.startNewSubPath (1, 11); p.cubicTo (6, 11, 7, 3, 10, 3); p.cubicTo (13, 3, 14, 11, 19, 11); break;
            default: p.startNewSubPath (1, 10); p.lineTo (19, 2); break;
        }
        return p;
    }
    const char* shapeNames[] = { "Bell", "Low shelf", "High shelf", "Low cut", "High cut", "Notch", "Band pass", "Tilt" };
}

namespace equi
{
//==============================================================================
/** Eight shape buttons bound to a band's shape parameter. */
class ShapePicker : public juce::Component, public HelpProvider
{
public:
    ShapePicker (EqEditor& e, int band) : ed (e),
        att (*e.proc.apvts.getParameter (eqp::id (band, "type")), [this] (float v) { value = juce::roundToInt (v); repaint(); })
    { att.sendInitialUpdate(); setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < 8; ++i)
        {
            auto r = cell (i);
            if (i == value) drawRaised (g, r, 4, false);
            else { g.setColour (col::basalt0); g.fillRoundedRectangle (r, 4); }
            g.setColour (i == value ? col::emberLo : col::lineSoft); g.drawRoundedRectangle (r.reduced (0.5f), 4, 1);
            auto p = shapeIcon (i);
            p.applyTransform (juce::AffineTransform::translation (r.getCentreX() - 10, r.getCentreY() - 6));
            g.setColour (i == value ? col::emberHi : (i == hover ? col::bone1 : col::bone3));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = hit (e.position);
        if (i < 0 || i == value) return;
        ed.begin(); att.setValueAsCompleteGesture ((float) i); ed.end();
    }
    void mouseMove (const juce::MouseEvent& e) override { const int i = hit (e.position); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    juce::String getHelpText() const override { return "Shape: " + juce::String (shapeNames[juce::jlimit (0, 7, hover >= 0 ? hover : value)]) + "."; }

private:
    juce::Rectangle<float> cell (int i) const
    {
        const float w = ((float) getWidth() - 12) / 4, h = ((float) getHeight() - 4) / 2;
        return { (float) (i % 4) * (w + 4), (float) (i / 4) * (h + 4), w, h };
    }
    int hit (juce::Point<float> p) const { for (int i = 0; i < 8; ++i) if (cell (i).contains (p)) return i; return -1; }
    EqEditor& ed;
    juce::ParameterAttachment att;
    int value = 0, hover = -1;
};

//==============================================================================
/** The big display: analyser, curve, draggable band nodes. */
class Display : public juce::Component, public HelpProvider
{
public:
    Display (EqEditor& e, bool simpleMode) : ed (e), simple (simpleMode) { setWantsKeyboardFocus (true); }

    juce::String getHelpText() const override
    {
        return simple ? "Drag a tone point up or down. The coloured fill is your music after the EQ."
                      : "Drag a band to move it (Shift for fine), scroll for width, double-click empty space to add one, Alt-click to remove.";
    }

    float xOf (float f) const { return std::log (f / 20.0f) / std::log (1000.0f) * (float) getWidth(); }
    float fOf (float x) const { return 20.0f * std::pow (1000.0f, x / (float) getWidth()); }
    float yOf (float g) const { const float h = (float) getHeight(); return h / 2 - g / 18.0f * (h / 2 - 26); }
    float gOf (float y) const { const float h = (float) getHeight(); return (h / 2 - y) / (h / 2 - 26) * 18.0f; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const float w = b.getWidth(), h = b.getHeight();
        g.setFont (Fonts::mono (9.0f));
        for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = std::round (xOf (f)) + 0.5f;
            g.setColour (col::bone1.withAlpha (0.055f)); g.fillRect (x, 0.0f, 1.0f, h);
            g.setColour (col::bone3);
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> (40, 12).withCentre ({ x, h - 10 }), juce::Justification::centred);
        }
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const float y = std::round (yOf (db)) + 0.5f;
            g.setColour (col::bone1.withAlpha (db == 0 ? 0.13f : 0.055f)); g.fillRect (0.0f, y, w, 1.0f);
            g.setColour (col::bone3);
            g.drawText (db > 0 ? "+" + juce::String ((int) db) : mns (juce::String ((int) db)), juce::Rectangle<float> (w - 34, y - 13, 28, 11), juce::Justification::centredRight);
        }

        // analyser: pre in grey, post in phosphor
        auto analyserPath = [&] (const std::vector<float>& spec)
        {
            juce::Path p;
            p.startNewSubPath (0, h);
            std::vector<float> ys;
            for (float x = 0; x <= w; x += 2)
            {
                const float f = fOf (x);
                const float d = ed.specAt (spec, fOf (x - 1), fOf (x + 1)) + 4.5f * std::log2 (f / 1000.0f);
                ys.push_back (h - 22 - (juce::jlimit (-96.0f, 0.0f, d) + 96.0f) / 96.0f * (h - 44));
            }
            for (int pass = 0; pass < 2; ++pass) for (size_t i = 1; i + 1 < ys.size(); ++i) ys[i] = (ys[i - 1] + 2 * ys[i] + ys[i + 1]) / 4;
            for (size_t i = 0; i < ys.size(); ++i) p.lineTo ((float) i * 2, ys[i]);
            p.lineTo (w, h); p.closeSubPath();
            return p;
        };
        if (! ed.preSpec.empty())
        {
            if (! simple) { g.setColour (col::bone1.withAlpha (0.06f)); g.fillPath (analyserPath (ed.preSpec)); }
            auto post = analyserPath (ed.postSpec);
            g.setGradientFill (juce::ColourGradient::vertical (col::phosphor.withAlpha (0.30f), 0, col::phosphor.withAlpha (0.02f), h));
            g.fillPath (post);
        }

        // curve
        updateCache();
        const float z = yOf (0);
        const bool byp = ed.getReal ("bypass") > 0.5f;
        juce::Path curve, fill;
        const float extra = ed.getReal ("output") + (ed.getReal ("autoGain") > 0.5f ? ed.proc.autoGainDb.load() : 0.0f);
        fill.startNewSubPath (0, z);
        for (int i = 0; i < npts; ++i)
        {
            float db = byp ? 0.0f : extra;
            if (! byp) for (auto& c : cache) if (c.p.on) db += c.db[(size_t) i];
            const float x = (float) i * 2, y = yOf (db);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (w, z); fill.closeSubPath();
        g.setColour (col::ember.withAlpha (0.09f)); g.fillPath (fill);
        g.setColour (col::ember.withAlpha (0.25f)); g.strokePath (curve, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember); g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // the selected band's own shape, dashed
        if (! simple && cache[(size_t) ed.selected].p.on)
        {
            juce::Path sp;
            for (int i = 0; i < npts; ++i) { const float y = yOf (cache[(size_t) ed.selected].db[(size_t) i]); if (i == 0) sp.startNewSubPath (0, y); else sp.lineTo ((float) i * 2, y); }
            juce::Path dashed; const float dl[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, sp, dl, 2);
            g.setColour (col::emberHi.withAlpha (0.45f)); g.fillPath (dashed);
        }

        // nodes
        for (int k = 0; k < eqp::numBands; ++k)
        {
            if (! visibleNode (k)) continue;
            const auto& p = cache[(size_t) k].p;
            const auto c = nodePos (k);
            const bool sel = k == ed.selected && ! simple, hov = k == hover;
            if (p.dynamic && aemon::eq::hasGain (p.type))
            {
                juce::Path ring; ring.addEllipse (c.x - 14, c.y - 14, 28, 28);
                juce::Path dashed; const float dl[] = { 2.0f, 3.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dl, 2);
                g.setColour (col::lilac.withAlpha (0.7f)); g.fillPath (dashed);
            }
            const float r = sel || hov ? 9.0f : 7.5f;
            g.setColour (sel ? col::ember : col::basalt0); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
            g.setColour (sel ? col::emberHi : col::ember); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 2.0f);
            juce::String lbl = simple ? juce::String (roleNames[k]).substring (0, 1)
                                      : (p.place == Place::stereo ? juce::String (k + 1) : juce::String ("SLRMS").substring ((int) p.place, (int) p.place + 1));
            g.setFont (Fonts::label (9.5f));
            g.setColour (sel ? col::basalt0 : col::emberHi);
            g.drawText (lbl, juce::Rectangle<float> (20, 14).withCentre (c), juce::Justification::centred);
        }

        // value tag for the band under the mouse, or frequency + note under the cursor
        const int tagBand = dragBand >= 0 ? dragBand : hover;
        if (tagBand >= 0)
        {
            const auto& p = cache[(size_t) tagBand].p;
            juce::String t = (simple ? juce::String (roleNames[tagBand]) + "  " : juce::String()) + fmtFreq ((float) p.freq);
            if (aemon::eq::hasGain (p.type)) t += "  " + fmtDb ((float) p.gainDb);
            if (! simple) t += "  Q " + juce::String (p.q, 2);
            if (p.dynamic) t += "  dyn " + mns (juce::String (p.thresholdDb, 0)) + " dB";
            drawTag (g, t, nodePos (tagBand).translated (0, -24), true);
        }
        else if (cursorX >= 0)
            drawTag (g, fmtFreq (fOf (cursorX)) + "  " + juce::String::charToString (0xb7) + "  " + noteName (fOf (cursorX)), { cursorX, h - 34 }, false);

        drawEngraved (g, simple ? "Spectrum and tone" : "Spectrum  " + juce::String::charToString (0xb7) + "  curve", { 12, 8, 200, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        if (! simple && ! anyOn())
            drawEngraved (g, "Double-click to add a band  " + juce::String::charToString (0xb7) + "  drag to move  " + juce::String::charToString (0xb7) + "  scroll for width",
                          { 0, h - 54, w, 14 }, 10.0f, col::bone3.withAlpha (0.8f), juce::Justification::centred, 0.1f, true);
        if (ed.proc.phase() != eqp::Phase::zeroLatency)
            drawEngraved (g, "Linear phase", { w - 160, 8, 148, 14 }, 9.5f, col::lilac, juce::Justification::centredRight);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int hb = hit (e.position);
        cursorX = hb < 0 ? e.position.x : -1.0f;
        hover = hb;
        setMouseCursor (hb >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; cursorX = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hb = hit (e.position);
        if (hb < 0) return;
        if (e.mods.isAltDown() && ! simple) { ed.removeBand (hb); return; }
        ed.select (hb);
        dragBand = hb;
        startPos = e.position;
        startF = (float) cache[(size_t) hb].p.freq;
        startG = (float) cache[(size_t) hb].p.gainDb;
        ed.begin();
        for (auto* w : { "freq", "gain" }) ed.proc.apvts.getParameter (eqp::id (hb, w))->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0) return;
        const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
        const auto d = (e.position - startPos) * fine;
        auto* pf = ed.proc.apvts.getParameter (eqp::id (dragBand, "freq"));
        auto* pg = ed.proc.apvts.getParameter (eqp::id (dragBand, "gain"));
        if (! simple) pf->setValueNotifyingHost (pf->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, fOf (xOf (startF) + d.x))));
        if (aemon::eq::hasGain (cache[(size_t) dragBand].p.type))
            pg->setValueNotifyingHost (pg->convertTo0to1 (juce::jlimit (-24.0f, 24.0f, std::round ((startG + gOf (yOf (0) + d.y)) * 10.0f) / 10.0f)));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand < 0) return;
        for (auto* w : { "freq", "gain" }) ed.proc.apvts.getParameter (eqp::id (dragBand, w))->endChangeGesture();
        dragBand = -1;
        ed.end();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hb = hit (e.position);
        if (hb >= 0) { ed.begin(); ed.setReal (eqp::id (hb, "gain"), 0.0f); ed.end(); return; }
        if (simple) return;
        const float f = fOf (e.position.x), gn = juce::jlimit (-18.0f, 18.0f, std::round (gOf (e.position.y) * 10.0f) / 10.0f);
        const Type t = f < 40 ? Type::lowCut : (f > 16000 ? Type::highCut : Type::bell);
        const int nb = ed.addBand (t, f, t == Type::bell ? gn : 0.0f, t == Type::bell ? 1.0f : 0.71f);
        if (nb < 0) ed.getShell().toast ("All 16 bands are in use.");
        else ed.getShell().toast ("Band " + juce::String (nb + 1) + " added at " + fmtFreq (f));
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        const int hb = hit (e.position);
        if (hb < 0 || simple) return;
        const float dir = wd.deltaY * (wd.isReversed ? -1.0f : 1.0f);
        if (dir == 0) return;
        ed.select (hb);
        ed.begin();
        ed.setReal (eqp::id (hb, "q"), juce::jlimit (0.1f, 18.0f, ed.getReal (eqp::id (hb, "q")) * (dir > 0 ? 1.08f : 1.0f / 1.08f)));
        ed.end();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! simple && (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)) { ed.removeBand (ed.selected); return true; }
        return false;
    }

private:
    struct Cache { aemon::eq::BandParams p; std::vector<float> db; bool valid = false; };
    static bool same (const aemon::eq::BandParams& a, const aemon::eq::BandParams& b)
    {
        return a.on == b.on && a.type == b.type && a.freq == b.freq && a.gainDb == b.gainDb && a.q == b.q && a.slope == b.slope && a.place == b.place && a.dynamic == b.dynamic && a.thresholdDb == b.thresholdDb;
    }
    void updateCache()
    {
        const int n = getWidth() / 2 + 1;
        const bool linear = ed.proc.phase() != eqp::Phase::zeroLatency;
        const double fs = ed.proc.currentRate.load();
        const bool resized = n != npts || linear != cacheLinear;
        npts = n; cacheLinear = linear;
        for (int k = 0; k < eqp::numBands; ++k)
        {
            auto& c = cache[(size_t) k];
            const auto p = ed.proc.bandParams (k);
            if (c.valid && ! resized && same (c.p, p)) continue;
            c.p = p; c.valid = true;
            c.db.assign ((size_t) npts, 0.0f);
            if (! p.on) continue;
            const auto secs = aemon::eq::design (p, fs);
            for (int i = 0; i < npts; ++i)
            {
                const double f = fOf ((float) i * 2);
                c.db[(size_t) i] = (float) (linear ? aemon::eq::analogDb (p, f) : aemon::eq::digitalDb (secs, f, fs));
            }
        }
    }
    bool visibleNode (int k) const { return cache[(size_t) k].p.on && (! simple || k < eqp::numRoles); }
    bool anyOn() const { for (auto& c : cache) if (c.p.on) return true; return false; }
    juce::Point<float> nodePos (int k) const
    {
        const auto& p = cache[(size_t) k].p;
        return { xOf ((float) p.freq), aemon::eq::hasGain (p.type) ? yOf ((float) p.gainDb) : yOf (0) };
    }
    int hit (juce::Point<float> pos) const
    {
        int best = -1; float bd = 16.0f;
        for (int k = 0; k < eqp::numBands; ++k)
            if (visibleNode (k)) { const float d = nodePos (k).getDistanceFrom (pos); if (d < bd) { bd = d; best = k; } }
        return best;
    }
    void drawTag (juce::Graphics& g, const juce::String& t, juce::Point<float> at, bool emberBorder)
    {
        const auto f = Fonts::mono (11.0f);
        const float tw = juce::GlyphArrangement::getStringWidth (f, t) + 16;
        auto r = juce::Rectangle<float> (tw, 22).withCentre (at);
        r = r.withX (juce::jlimit (4.0f, (float) getWidth() - tw - 4, r.getX()));
        g.setColour (col::basalt0.withAlpha (0.92f)); g.fillRoundedRectangle (r, 4);
        g.setColour (emberBorder ? col::emberLo : col::line); g.drawRoundedRectangle (r, 4, 1);
        g.setFont (f); g.setColour (col::bone1); g.drawText (t, r, juce::Justification::centred);
    }

    EqEditor& ed;
    bool simple;
    std::array<Cache, eqp::numBands> cache;
    int npts = 0;
    bool cacheLinear = false;
    int hover = -1, dragBand = -1;
    float cursorX = -1, startF = 1000, startG = 0;
    juce::Point<float> startPos;
};

//==============================================================================
/** Pro: every control of one band. One panel per band; only the selected one is visible. */
class BandPanel : public juce::Component
{
public:
    BandPanel (EqEditor& e, int b, std::function<Knob& (juce::Component&, const juce::String&, Knob::Options)> mkKnob,
               std::function<Button& (juce::Component&, const juce::String&, const juce::String&, const juce::String&)> mkToggle)
        : band (b), shapes (e, b),
          slope ({ "12", "24", "48", "96" }, { "12 dB per octave", "24 dB per octave", "48 dB per octave", "96 dB per octave" }),
          place ({ "St", "L", "R", "M", "S" }, { "Both channels.", "Left only.", "Right only.", "Mid only: what both speakers share.", "Side only: the stereo width part." }),
          ed (e)
    {
        addAndMakeVisible (shapes);
        addAndMakeVisible (slope); addAndMakeVisible (place);
        slope.textHeight = 9.5f; place.textHeight = 9.5f;
        slope.attach (*e.proc.apvts.getParameter (eqp::id (b, "slope")), [&e] { e.begin(); }, [&e] { e.end(); });
        place.attach (*e.proc.apvts.getParameter (eqp::id (b, "place")), [&e] { e.begin(); }, [&e] { e.end(); });
        Knob::Options fo; fo.style = Knob::Style::compact; fo.format = fmtFreq; fo.parse = parseFreq; fo.name = "Frequency"; fo.help = "Type 1.2k or 250.";
        freq = &mkKnob (*this, eqp::id (b, "freq"), fo);
        Knob::Options go; go.bipolar = true; go.detent = true; go.ticks = 13; go.minLabel = minusSign + "24"; go.maxLabel = "+24"; go.format = fmtDb; go.name = "Gain"; go.help = "Boost or cut.";
        gain = &mkKnob (*this, eqp::id (b, "gain"), go);
        Knob::Options qo; qo.style = Knob::Style::compact; qo.format = [] (float v) { return juce::String (v, 2); }; qo.name = "Q"; qo.help = "Width: low Q is broad, high Q is narrow.";
        q = &mkKnob (*this, eqp::id (b, "q"), qo);
        Knob::Options to; to.style = Knob::Style::compact; to.format = [] (float v) { return mns (juce::String (v, 1)) + " dB"; }; to.name = "Threshold";
        to.help = "Dynamic: the band's gain only applies when this frequency range gets louder than the threshold.";
        thr = &mkKnob (*this, eqp::id (b, "thr"), to);
        on = &mkToggle (*this, eqp::id (b, "on"), "On", "Switch this band on or off.");
        dyn = &mkToggle (*this, eqp::id (b, "dyn"), "Dyn", "Dynamic: the band only acts when its frequency range gets too loud. Great for harsh vocals or boomy notes.");
        on->small = dyn->small = true;
        dynAtt = std::make_unique<juce::ParameterAttachment> (*e.proc.apvts.getParameter (eqp::id (b, "dyn")), [this] (float v) { thr->setVisible (v > 0.5f); repaint(); });
        dynAtt->sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto col1 = r.removeFromLeft (136);
        shapes.setBounds (col1.removeFromTop (60).withTrimmedTop (2));
        col1.removeFromTop (16);
        slope.setBounds (col1.removeFromTop (24));
        r.removeFromLeft (10);
        freq->setBounds (r.removeFromLeft (76).withSizeKeepingCentre (76, 60 + 25).translated (0, -6));
        r.removeFromLeft (4);
        gain->setBounds (r.removeFromLeft (92).withSizeKeepingCentre (92, 80 + 25).translated (0, -6));
        r.removeFromLeft (4);
        q->setBounds (r.removeFromLeft (76).withSizeKeepingCentre (76, 60 + 25).translated (0, -6));
        r.removeFromLeft (12);
        auto col2 = r.removeFromLeft (156);
        col2.removeFromTop (14);
        place.setBounds (col2.removeFromTop (24));
        col2.removeFromTop (10);
        auto row = col2.removeFromTop (24);
        on->setBounds (row.removeFromLeft (74)); row.removeFromLeft (8); dyn->setBounds (row.removeFromLeft (74));
        r.removeFromLeft (8);
        thr->setBounds (r.removeFromLeft (70).withSizeKeepingCentre (70, 52 + 25).translated (0, -6));
    }
    void paint (juce::Graphics& g) override
    {
        drawEngraved (g, "Slope", { 0, 62, 136, 14 }, 9.0f, col::bone3, juce::Justification::centred, 0.16f, true);
        auto under = [&] (Knob* k, const char* t) { if (k->isVisible()) drawEngraved (g, t, k->getBounds().toFloat().withY ((float) k->getBottom() + 1).withHeight (11), 9.0f, col::bone3, juce::Justification::centred, 0.16f, true); };
        under (freq, "Freq"); under (gain, "Gain"); under (q, "Q"); under (thr, "Threshold");
        drawEngraved (g, "Placement", place.getBounds().toFloat().translated (0, -14).withHeight (12), 9.0f, col::bone3, juce::Justification::centred, 0.16f, true);
        const int t = juce::roundToInt (ed.getReal (eqp::id (band, "type")));
        slope.setAlpha (t == 3 || t == 4 ? 1.0f : 0.35f);
        gain->setAlpha (t == 3 || t == 4 || t == 5 || t == 6 ? 0.35f : 1.0f);
    }

private:
    int band;
    ShapePicker shapes;
    SegControl slope, place;
    Knob *freq = nullptr, *gain = nullptr, *q = nullptr, *thr = nullptr;
    Button *on = nullptr, *dyn = nullptr;
    std::unique_ptr<juce::ParameterAttachment> dynAtt;
    EqEditor& ed;
};

//==============================================================================
/** Simple: how the tone compares with a typical mix, vocal or bass. */
class ToneBalance : public juce::Component
{
public:
    explicit ToneBalance (EqEditor& e) : ed (e)
    {
        addAndMakeVisible (ref);
        ref.textHeight = 9.5f;
        setHelp (*this, "Tone balance: compares the low, low-mid, mid and high energy of your sound (after the EQ) with a typical reference.");
    }
    SegControl ref { { "Mix", "Vocal", "Bass" }, { "Compare with the average tonal balance of modern mixes.", "Compare with a typical lead vocal.", "Compare with a typical bass or 808." } };
    void resized() override { ref.setBounds (getLocalBounds().reduced (12).removeFromBottom (24)); }
    void update()
    {
        static const float refs[3][4] = { { 0, 0, 0, 0 }, { -10, 2, 3, -2 }, { 6, -2, -8, -14 } };
        static const float edges[5] = { 30, 150, 600, 4000, 16000 };
        if (ed.postSpec.empty()) return;
        float v[4], mean = 0;
        for (int i = 0; i < 4; ++i)
        {
            float s = 0; int n = 0;
            for (float f = edges[i]; f < edges[i + 1]; f *= 1.06f) { s += ed.specAt (ed.postSpec, f / 1.03f, f * 1.03f) + 4.5f * std::log2 (f / 1000.0f); ++n; }
            v[i] = s / (float) n; mean += v[i] / 4;
        }
        juce::StringArray words;
        static const char* names[] = { "lows", "low mids", "mids", "highs" };
        for (int i = 0; i < 4; ++i)
        {
            dev[i] = v[i] - mean - refs[ref.getIndex()][i];
            if (std::abs (dev[i]) >= 3) words.add (juce::String (names[i]) + (dev[i] > 0 ? " heavy" : " light"));
        }
        const char* refName = ref.getIndex() == 0 ? "mix" : (ref.getIndex() == 1 ? "vocal" : "bass");
        silent = ed.specAt (ed.postSpec, 30, 16000) < -100;
        verdict = silent ? "No signal." : (words.isEmpty() ? juce::String ("Balanced compared with a typical ") + refName + "."
                                                           : juce::String ("Compared with a typical ") + refName + ": " + words.joinIntoString (", ") + ".");
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        auto r = b.reduced (12);
        drawEngraved (g, "Tone balance", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "live", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredRight);
        r.removeFromTop (26);
        static const char* rows[] = { "Lows", "Low mids", "Mids", "Highs" };
        for (int i = 0; i < 4; ++i)
        {
            auto row = r.removeFromTop (20); r.removeFromTop (4);
            drawEngraved (g, rows[i], row.removeFromLeft (74), 9.5f, col::bone3, juce::Justification::centredLeft, 0.14f);
            auto bar = row.withSizeKeepingCentre (row.getWidth(), 12);
            g.setColour (col::groove); g.fillRoundedRectangle (bar, 6);
            const float d = juce::jlimit (-12.0f, 12.0f, silent ? 0.0f : dev[i]), half = bar.getWidth() / 2;
            auto fillR = d < 0 ? juce::Rectangle<float> (bar.getCentreX() + d / 12 * half, bar.getY() + 2, -d / 12 * half, 8)
                               : juce::Rectangle<float> (bar.getCentreX(), bar.getY() + 2, d / 12 * half, 8);
            g.setColour (std::abs (d) < 3 ? col::phosphor : col::ember); g.fillRoundedRectangle (fillR, 4);
            g.setColour (col::bone1.withAlpha (0.25f)); g.fillRect (bar.getCentreX(), bar.getY(), 1.0f, bar.getHeight());
        }
        r.removeFromTop (8);
        g.setFont (Fonts::textMedium (12.0f)); g.setColour (col::bone2);
        g.drawFittedText (verdict, r.removeFromTop (60).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }

private:
    EqEditor& ed;
    float dev[4] {};
    bool silent = true;
    juce::String verdict = "Listening...";
};

} // namespace equi

using namespace equi;

//==============================================================================
EqEditor::EqEditor (EqProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    preSpec.assign (fftSize / 2, -140.0f);
    postSpec.assign (fftSize / 2, -140.0f);
    fftBuf.assign ((size_t) fftSize * 2, 0.0f);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));

    shell = std::make_unique<Shell> (*this, proc.pluginState, "EQ", baseW, baseH);
    addAndMakeVisible (*shell);
    buildSimple();
    buildPro();
    shell->setModeComponents (simpleBody.get(), simpleDeck.get(), proBody.get(), proDeck.get());
    shell->setDefaultHint ("Hover any control for help. Double-click the display to add a band.");
    shell->setMeta (juce::String (proc.currentRate.load() / 1000.0, 1) + " kHz  " + juce::String::charToString (0xb7) + "  v" AEMON_VERSION);

    // select the first band that is on
    for (int b = 0; b < eqp::numBands; ++b) if (getReal (eqp::id (b, "on")) > 0.5f) { select (b); break; }

    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    const double s = (double) proc.pluginState.ui.getProperty ("scale", 1.0);
    setSize (juce::roundToInt (baseW * juce::jlimit (0.6, 2.0, s)), juce::roundToInt (baseH * juce::jlimit (0.6, 2.0, s)));
    setWantsKeyboardFocus (true);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

EqEditor::~EqEditor()
{
    stopTimer();
    atts.clear();
    shell.reset();
    setLookAndFeel (nullptr);
}

void EqEditor::resized() { shell->setBounds (getLocalBounds()); }

void EqEditor::setReal (const juce::String& id, float v, bool gesture)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        if (gesture) p->endChangeGesture();
    }
}

void EqEditor::select (int band)
{
    selected = juce::jlimit (0, eqp::numBands - 1, band);
    for (int b = 0; b < (int) panels.size(); ++b) panels[(size_t) b]->setVisible (b == selected);
    if (proDeck) proDeck->repaint();
    if (displayPro) displayPro->repaint();
}

int EqEditor::addBand (Type type, float freq, float gain, float q)
{
    int slot = -1;
    for (int b = eqp::numRoles; b < eqp::numBands && slot < 0; ++b) if (getReal (eqp::id (b, "on")) < 0.5f) slot = b;
    for (int b = 0; b < eqp::numRoles && slot < 0; ++b) if (getReal (eqp::id (b, "on")) < 0.5f) slot = b;
    if (slot < 0) return -1;
    begin();
    setReal (eqp::id (slot, "type"), (float) (int) type);
    setReal (eqp::id (slot, "freq"), freq);
    setReal (eqp::id (slot, "gain"), gain);
    setReal (eqp::id (slot, "q"), q);
    setReal (eqp::id (slot, "place"), 0);
    setReal (eqp::id (slot, "dyn"), 0);
    setReal (eqp::id (slot, "on"), 1);
    end();
    select (slot);
    return slot;
}

void EqEditor::removeBand (int band)
{
    if (getReal (eqp::id (band, "on")) < 0.5f) return;
    begin(); setReal (eqp::id (band, "on"), 0); end();
    shell->toast ("Band " + juce::String (band + 1) + " removed");
}

void EqEditor::ensureRoleBand (int role)
{
    if (getReal (eqp::id (role, "on")) > 0.5f) return;
    const auto& d = eqp::roleDefaults[role];
    setReal (eqp::id (role, "type"), (float) (int) d.type);
    if (role != eqp::lowCut) setReal (eqp::id (role, "freq"), d.freq);
    setReal (eqp::id (role, "q"), d.q);
    setReal (eqp::id (role, "slope"), (float) d.slopeIndex);
    setReal (eqp::id (role, "place"), 0);
    setReal (eqp::id (role, "on"), 1);
}

aemon::Knob& EqEditor::knob (juce::Component& parent, const juce::String& id, Knob::Options o)
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

aemon::Button& EqEditor::toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help)
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

void EqEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    auto disp = std::make_unique<Display> (*this, false);
    auto m = std::make_unique<LevelMeterView> (LevelMeterView::ScaleSide::right);
    displayPro = disp.get(); meter = m.get();
    body->addAndMakeVisible (*disp); body->addAndMakeVisible (*m);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10);
        meter->setBounds (r.removeFromRight (58).withTrimmedTop (18).withTrimmedBottom (4));
        r.removeFromRight (10);
        displayPro->setBounds (r);
    };
    owned.push_back (std::move (disp)); owned.push_back (std::move (m));
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto painter = std::make_unique<DeckPainter>();
    auto* pnt = painter.get();
    for (int b = 0; b < eqp::numBands; ++b)
    {
        auto panel = std::make_unique<BandPanel> (*this, b,
            [this] (juce::Component& parent, const juce::String& id, Knob::Options o) -> Knob& { return knob (parent, id, std::move (o)); },
            [this] (juce::Component& parent, const juce::String& id, const juce::String& t, const juce::String& h) -> Button& { return toggle (parent, id, t, h); });
        panels.push_back (panel.get());
        deck->addChildComponent (*panel);
        owned.push_back (std::move (panel));
    }
    Knob::Options oo; oo.bipolar = true; oo.detent = true; oo.ticks = 13; oo.minLabel = minusSign + "24"; oo.maxLabel = "+24"; oo.format = fmtDb; oo.name = "Output"; oo.help = "Output level.";
    auto& out = knob (*deck, "output", oo);
    auto& ag = toggle (*deck, "autoGain", "Auto gain", "Auto gain: keeps the overall loudness the same so you judge the tone, not the volume.");
    ag.small = true;
    auto ph = std::make_unique<SegControl> (juce::StringArray { "Zero", "Lin M", "Lin H" },
        juce::StringArray { "Zero latency: analogue-matched filters, no cramping near 20 kHz. Best for tracking and most mixing.",
                            "Linear phase, Medium: no phase shift at all, about 90 ms delay (your DAW compensates it automatically).",
                            "Linear phase, High: no phase shift, keeps steep low cuts exact, about 180 ms delay. Best for mastering." });
    ph->textHeight = 9.5f;
    ph->attach (*proc.apvts.getParameter ("phase"), [this] { begin(); }, [this] { end(); });
    auto* phRaw = ph.get();
    deck->addAndMakeVisible (*ph); owned.push_back (std::move (ph));
    deck->addAndMakeVisible (*painter);
    owned.push_back (std::move (painter));
    deck->layout = [this, pnt, &out, &ag, phRaw] (juce::Rectangle<int> r)
    {
        pnt->setBounds (r);
        auto band = juce::Rectangle<int> (22, 14, 640, r.getHeight() - 26);
        auto outG = juce::Rectangle<int> (690, 14, 250, r.getHeight() - 26);
        pnt->groups = { { band, [this] {
                            const bool dyn = getReal (eqp::id (selected, "dyn")) > 0.5f, on = getReal (eqp::id (selected, "on")) > 0.5f;
                            return "Band " + juce::String (selected + 1) + (on ? juce::String() : "  (off)") + (dyn ? "  dynamic" : ""); } },
                        { outG, [] { return juce::String ("Output"); } } };
        pnt->seps = { 676 };
        for (auto* p : panels) p->setBounds (band.withTrimmedTop (24));
        auto o = outG.withTrimmedTop (24);
        out.setBounds (o.removeFromLeft (92).withSizeKeepingCentre (92, 80 + 25));
        o.removeFromLeft (10);
        ag.setBounds (o.getX(), o.getY() + 6, 140, 24);
        phRaw->setBounds (o.getX(), o.getY() + 52, 146, 26);
        pnt->toFront (false);
    };
    proDeck = std::move (deck);
    select (selected);
}

void EqEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    auto disp = std::make_unique<Display> (*this, true);
    auto tb = std::make_unique<ToneBalance> (*this);
    displaySimple = disp.get(); tone = tb.get();
    body->addAndMakeVisible (*disp); body->addAndMakeVisible (*tb);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10);
        tone->setBounds (r.removeFromRight (220));
        r.removeFromRight (10);
        displaySimple->setBounds (r);
    };
    owned.push_back (std::move (disp)); owned.push_back (std::move (tb));
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto painter = std::make_unique<DeckPainter>();
    auto* pnt = painter.get();
    std::vector<Knob*> roleKnobs;
    for (int r = 0; r < eqp::numRoles; ++r)
    {
        Knob::Options o;
        o.name = roleNames[r];
        if (r == eqp::lowCut)
        {
            o.ticks = 9; o.minLabel = "off"; o.maxLabel = "300";
            o.format = [] (float v) { return v <= 20.5f ? juce::String ("Off") : fmtFreq (v); };
            o.parse = parseFreq;
            o.help = "Removes rumble below this frequency.";
        }
        else
        {
            o.bipolar = true; o.detent = true; o.ticks = 13; o.minLabel = minusSign + "12"; o.maxLabel = "+12"; o.format = fmtDb;
            static const char* helps[] = { "", "Weight and warmth around 110 Hz.", "Cut to clear boxy, muddy low mids around 300 Hz.", "Forwardness and clarity around 3 kHz.", "Sparkle and openness above 10 kHz." };
            o.help = helps[r];
        }
        auto& k = knob (*deck, eqp::id (r, r == eqp::lowCut ? "freq" : "gain"), o);
        k.onUserChange = [this, r] (float v)
        {
            if (r == eqp::lowCut)
            {
                const bool want = v > 20.5f;
                if (want) ensureRoleBand (r);
                else if (getReal (eqp::id (r, "on")) > 0.5f) setReal (eqp::id (r, "on"), 0);
            }
            else if (std::abs (v) > 0.05f) ensureRoleBand (r);
        };
        roleKnobs.push_back (&k);
    }
    auto mkBtn = [&] (const juce::String& t, const juce::String& hint, const juce::String& help, std::function<void()> fn) -> Button&
    {
        auto b = std::make_unique<Button> (t);
        b->hint = hint; b->help = help; b->onClick = std::move (fn);
        auto& ref = *b;
        deck->addAndMakeVisible (ref); owned.push_back (std::move (b));
        return ref;
    };
    auto& harsh = mkBtn ("Tame harsh spots", "finds resonances", "Looks for narrow peaks between 2 and 9 kHz and places gentle dynamic cuts on them.", [this] { tameHarsh(); });
    auto& mudB = mkBtn ("Clean up mud", "150-500 Hz", "Finds where the low mids pile up and cuts there.", [this] { cleanMud(); });
    auto& ag = toggle (*deck, "autoGain", "Auto gain", "Auto gain: keeps the loudness the same so you hear tone, not volume.");
    ag.hint = "same loudness";
    deck->addAndMakeVisible (*painter);
    owned.push_back (std::move (painter));
    deck->layout = [pnt, roleKnobs, &harsh, &mudB, &ag] (juce::Rectangle<int> r)
    {
        pnt->setBounds (r);
        pnt->groups.clear();
        int x = 24;
        for (int i = 0; i < eqp::numRoles; ++i)
        {
            auto g = juce::Rectangle<int> (x, 14, 108, r.getHeight() - 26);
            pnt->groups.push_back ({ g, [i] { return juce::String (roleNames[i]); } });
            roleKnobs[(size_t) i]->setBounds (g.withTrimmedTop (24).withSizeKeepingCentre (100, 84 + 25));
            x += 112;
        }
        pnt->seps = { x + 6 };
        auto hg = juce::Rectangle<int> (x + 22, 14, 250, r.getHeight() - 26);
        pnt->groups.push_back ({ hg, [] { return juce::String ("One-click helpers"); } });
        auto c = hg.withTrimmedTop (24).withSizeKeepingCentre (250, 30 * 3 + 12);
        harsh.setBounds (c.removeFromTop (30)); c.removeFromTop (6);
        mudB.setBounds (c.removeFromTop (30)); c.removeFromTop (6);
        ag.setBounds (c.removeFromTop (30));
        pnt->toFront (false);
    };
    simpleDeck = std::move (deck);
}

float EqEditor::specAt (const std::vector<float>& spec, float f0, float f1) const
{
    const double fs = proc.currentRate.load();
    const int nb = (int) spec.size();
    const int b0 = juce::jlimit (1, nb - 1, (int) std::floor (f0 / fs * fftSize)), b1 = juce::jlimit (1, nb - 1, (int) std::ceil (f1 / fs * fftSize));
    float m = -140.0f;
    for (int b = b0; b <= b1; ++b) m = std::max (m, spec[(size_t) b]);
    return m;
}

void EqEditor::analyse (aemon::MonoRing& ring, std::vector<float>& spec)
{
    ring.latest (fftBuf.data(), fftSize);
    for (int i = 0; i < fftSize; ++i) fftBuf[(size_t) i] *= window[(size_t) i];
    std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    const float norm = 2.0f / (fftSize * 0.5f);
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const float d = juce::Decibels::gainToDecibels (fftBuf[(size_t) k] * norm, -140.0f);
        auto& s = spec[(size_t) k];
        s += (d - s) * (d > s ? 0.5f : 0.08f);
    }
}

void EqEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.001, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;
    ++frame;
    analyse (proc.preRing, preSpec);
    analyse (proc.postRing, postSpec);
    const bool simple = shell->isSimple();
    (simple ? displaySimple : displayPro)->repaint();
    const auto out = proc.outTap.take();
    if (out.count > 0 && ! simple)
    {
        const float a = 1.0f - std::exp (-dt / 0.3f);
        for (int c = 0; c < 2; ++c) msOut[c] += (out.meanSquare (c) - msOut[c]) * a;
        const float pk[2] { juce::Decibels::gainToDecibels (out.peak[0], -120.0f), juce::Decibels::gainToDecibels (out.peak[1], -120.0f) };
        const float rm[2] { 10.0f * std::log10 (std::max (msOut[0], 1e-12f)), 10.0f * std::log10 (std::max (msOut[1], 1e-12f)) };
        meter->push (pk, rm, dt);
    }
    if (simple && frame % 10 == 0) tone->update();
    if (! simple && frame % 6 == 0) proDeck->repaint (0, 0, 680, 30);
    for (auto* p : panels) if (p->isVisible() && frame % 6 == 0) p->repaint();
}

void EqEditor::tameHarsh()
{
    juce::Array<float> found;
    for (float f = 2000; f <= 9000; f *= 1.03f)
    {
        const float v = specAt (postSpec, f / 1.01f, f * 1.01f);
        const float around = 0.5f * (specAt (postSpec, f / 1.35f, f / 1.25f) + specAt (postSpec, f * 1.25f, f * 1.35f));
        bool near = false;
        for (float g : found) near |= std::abs (std::log2 (g / f)) < 0.3f;
        if (v - around > 5.0f && ! near && v > -90) found.add (f);
    }
    if (found.isEmpty()) { shell->toast ("No harsh resonances found between 2 and 9 kHz right now."); return; }
    int placed = 0;
    begin();
    for (float f : found)
    {
        if (placed == 3) break;
        const float level = specAt (postSpec, f / 1.01f, f * 1.01f);
        const int b = addBand (Type::bell, std::round (f), -4.0f, 6.0f);
        if (b < 0) break;
        setReal (eqp::id (b, "dyn"), 1);
        setReal (eqp::id (b, "thr"), juce::jlimit (-60.0f, 0.0f, std::round (level - 3.0f)));
        ++placed;
    }
    end();
    shell->toast ("Placed " + juce::String (placed) + " narrow dynamic cut" + (placed > 1 ? "s" : "") + " on the harsh spots. You can see them in Pro.");
}

void EqEditor::cleanMud()
{
    float best = 320, bv = -1e9f;
    for (float f = 150; f <= 500; f *= 1.05f)
    {
        const float v = specAt (postSpec, f / 1.03f, f * 1.03f) + 4.5f * std::log2 (f / 1000.0f);
        if (v > bv) { bv = v; best = f; }
    }
    begin();
    ensureRoleBand (eqp::mud);
    setReal (eqp::id (eqp::mud, "freq"), std::round (best));
    setReal (eqp::id (eqp::mud, "gain"), -3.0f);
    setReal (eqp::id (eqp::mud, "q"), 1.4f);
    end();
    shell->toast ("Found the build-up at " + fmtFreq (best) + " and cut it by 3 dB.");
}

bool EqEditor::keyPressed (const juce::KeyPress& k)
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
