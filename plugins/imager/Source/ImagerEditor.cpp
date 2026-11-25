// Aemon Imager. AGPL-3.0-or-later.
#include "ImagerEditor.h"

using namespace aemon;

namespace
{
    juce::String hzText (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, v >= 9950.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String lowText (float v) { return v <= 20.5f ? juce::String ("off") : "< " + juce::String (juce::roundToInt (v)) + " Hz"; }
    float leanDb (float c) { return 20.0f * std::log10 ((1.0f + c / 100.0f) / (1.0f - c / 100.0f)); }
    juce::String centreText (float v) { const float d = leanDb (v); return std::abs (d) < 0.05f ? juce::String ("as is") : juce::String (std::abs (d), 1) + (d > 0 ? " dB R" : " dB L"); }
    std::optional<float> parseCentre (const juce::String& s)
    {
        auto v = txt::parse (s); if (! v) return {};
        const float g = std::pow (10.0f, std::min (9.5f, std::abs (*v)) / 20.0f), c = 100.0f * (g - 1.0f) / (g + 1.0f);
        return s.containsIgnoreCase ("L") || *v < 0 ? -c : c;
    }
    constexpr const char* widthIds[] = { "w1", "w2", "w3", "w4" };
    constexpr const char* splitIds[] = { "f1", "f2", "f3" };
    constexpr float splitDefaults[] = { 150, 1500, 6000 };
    juce::String lossText (double v) { return (v < 0.05 ? juce::String ("0.0") : txt::num ((float) -v, 1)) + " dB"; }
}

namespace imgui
{
/** The image (left) and the stereo map: every frequency where it sits between left and right. */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (ImagerEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return ed.pro ? "Left: the image, the middle upwards. Right: the stereo map. Drag the amber handles for the width of a band, the dividers to move its edges. Double-click resets." : "Left: the image, left and right, the middle upwards. Right: the stereo map, every frequency where it sits between left and right."; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const juce::Rectangle<float> V (16, 28, std::round (b.getWidth() * 0.33f), b.getHeight() - 84);
        const float rad = std::min (V.getWidth() / 2.0f, V.getHeight() - 14.0f); const juce::Point<float> c (V.getCentreX(), V.getBottom() - 6.0f - std::max (0.0f, (V.getHeight() - 14.0f - rad) / 2.0f));
        drawEngraved (g, "Image", { V.getX(), 7, 100, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        g.setColour (col::bone1.withAlpha (0.09f));
        for (float q : { 1.0f, 0.66f, 0.33f }) { juce::Path p; p.addCentredArc (c.x, c.y, rad * q, rad * q, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true); g.strokePath (p, juce::PathStrokeType (1.0f)); }
        for (float a : { -45.0f, 0.0f, 45.0f }) g.drawLine (juce::Line<float> (c, c.getPointOnCircumference (rad, juce::degreesToRadians (a))), 1.0f);
        g.drawLine (c.x - rad, c.y, c.x + rad, c.y, 1.0f);
        g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3);
        auto mark = [&g] (const juce::String& s, juce::Point<float> p) { g.drawText (s, juce::Rectangle<float> (60, 12).withCentre (p), juce::Justification::centred); };
        mark ("L", c + juce::Point<float> (-rad * 0.75f, -rad * 0.77f)); mark ("R", c + juce::Point<float> (rad * 0.75f, -rad * 0.77f)); mark ("middle", c + juce::Point<float> (0, -rad - 8));
        auto dots = [&] (const std::vector<float>& L, const std::vector<float>& R, juce::Colour colour, float size)
        {
            g.setColour (colour);
            for (size_t i = 0; i < L.size() && i < R.size(); ++i)
            {
                const float m = 0.5f * (L[i] + R[i]) * ed.zoom, s = 0.5f * (L[i] - R[i]) * ed.zoom;
                juce::Point<float> p (-(m < 0 ? -1.0f : 1.0f) * s * rad, -std::abs (m) * rad);
                if (p.getDistanceFromOrigin() > rad) p *= rad / p.getDistanceFromOrigin();
                g.fillRect (c.x + p.x - size / 2, c.y + p.y - size / 2, size, size);
            }
        };
        dots (ed.dotsInL, ed.dotsInR, col::bone1.withAlpha (0.20f), 1.2f);
        dots (ed.dotsL, ed.dotsR, col::phosphor.withAlpha (0.75f), 1.4f);
        const juce::Rectangle<float> bar (V.getX(), b.getHeight() - 40, V.getWidth(), 8);
        juce::ColourGradient gr (col::sigClip.withAlpha (0.6f), bar.getX(), 0, col::sigOk.withAlpha (0.6f), bar.getRight(), 0, false); gr.addColour (0.5, col::sigHot.withAlpha (0.5f)); gr.addColour (0.75, col::sigOk.withAlpha (0.5f));
        g.setGradientFill (gr); g.fillRoundedRectangle (bar, 4);
        g.setColour (col::bone1); g.fillRoundedRectangle (juce::jlimit (bar.getX(), bar.getRight() - 4, bar.getX() + (ed.correlation + 1) / 2 * bar.getWidth() - 2), bar.getY() - 3, 4, 14, 2);
        g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3);
        g.drawText ("vanishes in mono", bar.withY (bar.getBottom() + 6).withHeight (12), juce::Justification::centredLeft);
        g.drawText ("mono", bar.withY (bar.getBottom() + 6).withHeight (12), juce::Justification::centredRight);

        // the map
        const auto B = box();
        drawEngraved (g, "Stereo map", { B.getX(), 7, 120, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "now", { B.getRight() - 40, 7, 40, 14 }, 10.0f, col::phosphor, juce::Justification::centredRight);
        drawEngraved (g, "before", { B.getRight() - 104, 7, 66, 14 }, 10.0f, col::bone2, juce::Justification::centredRight);
        drawEngraved (g, "lost in mono", { b.getRight() - 120, 7, 108, 14 }, 10.0f, col::bone3, juce::Justification::centredRight);
        for (auto [f, t] : { std::pair { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } }) { g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3); g.drawText (t, juce::Rectangle<float> (B.getX() - 36, Y (f) - 6, 30, 12), juce::Justification::centredRight); }
        for (auto [x, t] : { std::pair { B.getX() + 16, "left" }, { B.getCentreX(), "middle" }, { B.getRight() - 16, "right" } }) mark (t, { x, B.getBottom() + 12 });
        g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (B);
        {
            juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (B.toNearestInt());
            g.setColour (col::bone1.withAlpha (0.08f)); g.fillRect (B.getCentreX() - 0.5f, B.getY(), 1.0f, B.getHeight());
            double top = 1e-12; for (size_t i = 0; i < imgp::cells; ++i) top = std::max ({ top, ed.out.m[i] + ed.out.s[i], ed.in.m[i] + ed.in.s[i] });
            auto cell = [&] (const imgp::Cells& q, size_t i, float& lv, float& a, float& e)
            {
                const double p = q.m[i] + q.s[i], w = std::sqrt (q.s[i] / (p + 1e-18)), lean = (q.r[i] - q.l[i]) / (q.l[i] + q.r[i] + 1e-18);
                lv = (float) juce::jlimit (0.0, 1.0, (10 * std::log10 (p / top + 1e-9) + 48) / 48);
                const float half = B.getWidth() * 0.48f; a = B.getCentreX() + (float) (lean - w * (1 - std::abs (lean))) * half; e = B.getCentreX() + (float) (lean + w * (1 - std::abs (lean))) * half;
            };
            for (size_t i = 0; i < imgp::cells; ++i)
            {
                const float y1 = Y (imgp::cellHz ((double) i)), y0 = Y (imgp::cellHz ((double) i + 1)); float lv, a, e;
                cell (ed.in, i, lv, a, e); if (lv > 0.02f) { g.setColour (col::bone1.withAlpha (0.05f + 0.13f * lv)); g.fillRect (a - 1, y0 + 1, std::max (2.0f, e - a + 2), y1 - y0 - 2); }
                cell (ed.out, i, lv, a, e); if (lv > 0.02f) { g.setColour (col::phosphor.withAlpha (0.12f + 0.6f * lv)); g.fillRoundedRectangle (a - 1.5f, y0 + 2.5f, std::max (3.0f, e - a + 3), y1 - y0 - 5, 2.5f); }
            }
            if (const float mono = ed.real ("mono"); mono > 20.5f)
            {
                const float y = Y (mono); g.setColour (col::ember.withAlpha (0.06f)); g.fillRect (B.getX(), y, B.getWidth(), B.getBottom() - y);
                drawDashed (g, { B.getX(), y, B.getRight(), y }, col::ember.withAlpha (0.7f));
                g.setFont (Fonts::mono (9.0f)); g.setColour (col::emberHi); g.drawText ("mono below " + hzText (mono), juce::Rectangle<float> (B.getX() + 6, std::min (y + 4, B.getBottom() - 14), 140, 12), juce::Justification::centredLeft);
            }
            for (int k = 0; k < img::bands; ++k)
            {
                const float y0 = top0 (k), y1 = bottom0 (k), set = ed.real (widthIds[k]) * (ed.pro ? 1.0f : ed.real ("width") / 100.0f), x = juce::jlimit (0.0f, 200.0f, set) / 200.0f * B.getWidth() / 2;
                if (k < 3) { const bool h = ed.hot == splitIds[k]; g.setColour (h ? col::emberHi.withAlpha (0.8f) : col::bone1.withAlpha (0.22f)); g.fillRect (B.getX(), y0 - 0.5f, B.getWidth(), h ? 1.6f : 1.0f); }
                g.setColour ((ed.hot == widthIds[k] ? col::emberHi : col::ember).withAlpha (ed.pro ? 0.95f : 0.55f));
                for (float sg : { -1.0f, 1.0f })
                {
                    const float px = juce::jlimit (B.getX() + 1.5f, B.getRight() - 1.5f, B.getCentreX() + sg * x);
                    juce::Path p; p.startNewSubPath (px - sg * 5, y0 + 4); p.lineTo (px, y0 + 4); p.lineTo (px, y1 - 4); p.lineTo (px - sg * 5, y1 - 4);
                    g.strokePath (p, juce::PathStrokeType (ed.pro ? 2.2f : 1.4f));
                }
            }
        }
        for (int k = 0; k < img::bands; ++k)
        {
            bool quiet = false; const double v = ed.bandLoss (k, quiet); const float y = (top0 (k) + bottom0 (k)) / 2;
            drawEngraved (g, imgp::bandNames[k], { B.getRight() + 12, y - 15, 90, 14 }, 9.5f, col::bone2, juce::Justification::centredLeft);
            g.setFont (Fonts::mono (12.0f)); g.setColour (quiet ? col::bone3 : (v > 6 ? col::sigClip : (v > 3.2 ? col::sigHot : col::sigOk)));
            g.drawText (quiet ? juce::String::charToString (0x2014) : lossText (v), juce::Rectangle<float> (B.getRight() + 12, y, 90, 14), juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const auto h = hit (e.position); if (h != ed.hot) { ed.hot = h; setMouseCursor (h.isEmpty() ? juce::MouseCursor::NormalCursor : (h[0] == 'f' ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor)); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { ed.hot.clear(); repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { drag = hit (e.position); if (drag.isNotEmpty()) { ed.begin(); apply (e.position); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (drag.isNotEmpty()) apply (e.position); }
    void mouseUp (const juce::MouseEvent&) override { if (drag.isNotEmpty()) ed.end(); drag.clear(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { const auto h = hit (e.position); if (h.isEmpty()) return; ed.change (h, h[0] == 'w' ? 100.0f : splitDefaults[h[1] - '1']); }

private:
    juce::Rectangle<float> box() const { const float x = 16 + std::round ((float) getWidth() * 0.33f) + 62; return { x, 28, (float) getWidth() - x - 104, (float) getHeight() - 52 }; }
    float Y (double f) const { const auto B = box(); return B.getBottom() - (float) (std::log (f / 30.0) / std::log (600.0)) * B.getHeight(); }
    double F (float y) const { const auto B = box(); return 30.0 * std::pow (600.0, (B.getBottom() - y) / B.getHeight()); }
    float top0 (int k) const { return k < 3 ? Y (ed.splitHz (k)) : box().getY(); }
    float bottom0 (int k) const { return k > 0 ? Y (ed.splitHz (k - 1)) : box().getBottom(); }
    juce::String hit (juce::Point<float> p) const
    {
        if (! ed.pro || ! box().contains (p)) return {};
        for (int k = 0; k < 3; ++k) if (std::abs (p.y - Y (ed.splitHz (k))) < 6.0f) return splitIds[k];
        for (int k = 0; k < img::bands; ++k) if (p.y >= top0 (k)) return widthIds[k];
        return widthIds[0];
    }
    void apply (juce::Point<float> p)
    {
        const auto B = box();
        if (drag[0] == 'w') { ed.setReal (drag, juce::jlimit (0.0f, 200.0f, std::abs (p.x - B.getCentreX()) / (B.getWidth() / 2) * 200.0f)); return; }
        const auto range = ed.proc.apvts.getParameterRange (drag);
        ed.setReal (drag, juce::jlimit (range.start, range.end, (float) F (p.y)));
    }
    ImagerEditor& ed;
    juce::String drag;
};
} // namespace imgui

using namespace imgui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {})
    {
        Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o;
    }
    Knob::Options widthO (const char* name, const char* help) { auto o = opt (name, help, txt::percent, 9, "mono", "200"); o.bipolar = true; o.centre = 100.0f; o.detent = true; return o; }
    Knob::Options monoO() { return opt ("Low end", "Everything below this frequency is brought to the middle. Tight bass on big systems, and safe on vinyl.", lowText, 9, "off", "400"); }
    Knob::Options makeO() { return opt ("Make stereo", "Adds width to a mono sound. What is added disappears completely in mono.", txt::percent, 11, "off", "full"); }
    const char* widthHelp = "How wide the image is. 100 % is untouched, 0 % is mono. The mono sum stays the same throughout.";
    const char* safeHelp = "Keep it mono-safe: turns the sides down, band by band, only while mono would lose more than the limit.";
    const char* monoHelp = "Hear it in mono: what a phone, a club or a small speaker plays. It does not change when you turn Width.";
    const char* sideHelp = "Only the sides: what disappears in mono.";
    const char* matchHelp = "Equal loudness: wider is also louder. This turns the result down to the original while you listen, so you judge the width. Never in an export.";
}

ImagerEditor::ImagerEditor (ImagerProcessor& p) : EffectEditor (p), proc (p)
{
    buildSimple();
    buildPro();
    start();
}

double ImagerEditor::bandLoss (int k, bool& quiet) const
{
    const auto p = proc.readParams();
    const double lo = k > 0 ? p.split[(size_t) k - 1] : 0.0, hi = k < 3 ? p.split[(size_t) k] : 1e9, m = outSlow.middle (lo, hi), s = outSlow.side (lo, hi);
    quiet = m + s < 1e-6 * outSlow.total() || m + s < 1e-12;
    return imgp::lossDb (m, s);
}

void ImagerEditor::centreIt()
{
    const auto c = proc.centring();
    if (! c) { toast ("No signal yet. Play a few seconds, then press again."); return; }
    change ("centre", *c);
    toast ("Centred. The mono sum is the same as before.");
}

void ImagerEditor::makeItStereo()
{
    begin(); setReal ("make", 40); if (real ("mono") < 150) setReal ("mono", 150); end();
    toast ("Stereo made from the mono sound. In mono it is exactly what it was.");
}

void ImagerEditor::tighten() { change ("mono", std::max (150.0f, real ("mono"))); toast ("Everything below " + hzText (real ("mono")) + " is in the middle now."); }

void ImagerEditor::listenToggle (Button& b, const char* me, const char* other)
{
    auto f = b.onClick; const juce::String m (me), o (other);
    b.onClick = [this, f, m, o] { f(); if (base.apvts.getRawParameterValue (m)->load() > 0.5f) setReal (o, 0); };
}

void ImagerEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "What mono loses, in numbers, over the last seconds.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (200)); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& width = knob (*deck, "width", widthO ("Width", widthHelp));
    auto& make = knob (*deck, "make", makeO());
    auto co = opt ("Balance", "Moves the middle of the image left or right, for a recording that leans to one side. The mono sum stays the same.", centreText, 9, "L", "R"); co.bipolar = true; co.detent = true; co.parse = parseCentre;
    auto& centre = knob (*deck, "centre", co);
    auto& mono = knob (*deck, "mono", monoO());
    const char* bandHelp[] = { "Width of the lows.", "Width of the low mids.", "Width of the high mids.", "Width of the highs." };
    std::array<Knob*, 4> w {}; for (int k = 0; k < 4; ++k) w[(size_t) k] = &knob (*deck, widthIds[k], widthO (imgp::bandNames[k], bandHelp[k]));
    auto& limit = knob (*deck, "limit", opt ("Limit", "The most each band may lose when the sound is played in mono.", [] (float v) { return juce::String (v, 1) + " dB"; }, 11, "1", "6"));
    auto& safe = toggle (*deck, "safe", "Mono-safe", safeHelp, true);
    auto& hm = toggle (*deck, "hearMono", "In mono", monoHelp, true); auto& hs = toggle (*deck, "hearSide", "Sides only", sideHelp, true);
    listenToggle (hm, "hearMono", "hearSide"); listenToggle (hs, "hearSide", "hearMono");
    auto& delay = paramSeg (*deck, "delay", { "Straight", "Zero" }, { "Straight phase: clean separation between the bands. 48 ms of delay, the DAW lines it up.", "Zero delay: gentler slopes between the bands, for recording and live playing." });
    auto& match = toggle (*deck, "match", "Equal loud", matchHelp, true);
    auto& pnt = painter (*deck);
    deck->layout = [&, w] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kw = 66, kh = 60 + 25;
        auto hero = [&] (Knob& k, int x, const char* t) { k.setBounds (x, top + 4, kw, kh); pnt.under (k, t); };
        int x = 16;
        pnt.groups.push_back ({ { x, 14, 3 * kw, 20 }, "Image" }); hero (width, x, "Width"); hero (make, x + kw, "Make stereo"); hero (centre, x + 2 * kw, "Balance"); x += 3 * kw + 20;
        pnt.seps.push_back (x - 10);
        pnt.groups.push_back ({ { x - 4, 14, kw + 8, 20 }, "Low end" }); hero (mono, x, "Mono below"); x += kw + 20;
        pnt.seps.push_back (x - 10);
        pnt.groups.push_back ({ { x, 14, 4 * kw, 20 }, "Width per band" }); for (int k = 0; k < 4; ++k) hero (*w[(size_t) k], x + k * kw, imgp::bandNames[k]); x += 4 * kw + 20;
        pnt.seps.push_back (x - 10);
        pnt.groups.push_back ({ { x, 14, kw + 98, 20 }, "Mono check" }); hero (limit, x, "Lose at most");
        safe.setBounds (x + kw + 4, top + 8, 96, 22); hm.setBounds (x + kw + 4, top + 36, 96, 22); hs.setBounds (x + kw + 4, top + 64, 96, 22); x += kw + 104 + 16;
        pnt.seps.push_back (x - 8);
        pnt.groups.push_back ({ { x, 14, 116, 20 }, "Quality" }); delay.setBounds (x, top + 18, 116, 24); match.setBounds (x, top + 52, 116, 24);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void ImagerEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    check = &make<CheckCards> (*body, "Stereo check", "Stereo check: the image in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "In mono", "Low end", "Balance", "Width" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& width = knob (*deck, "width", widthO ("Width", widthHelp)); auto& mono = knob (*deck, "mono", monoO()); auto& make = knob (*deck, "make", makeO());
    auto& safe = toggle (*deck, "safe", "Keep it mono-safe", safeHelp); safe.hint = "loses little";
    auto& hm = toggle (*deck, "hearMono", "Hear it in mono", monoHelp); auto& hs = toggle (*deck, "hearSide", "Only the sides", sideHelp);
    listenToggle (hm, "hearMono", "hearSide"); listenToggle (hs, "hearSide", "hearMono");
    auto& match = toggle (*deck, "match", "Compare at equal loudness", matchHelp); match.hint = "honest A/B";
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38; int x = 70;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &width, "Width" }, { &mono, "Low end" }, { &make, "Make stereo" } }) { pnt.groups.push_back ({ { x, 14, 112, 20 }, t }); k->setBounds (x, top - 4, 112, 98 + 25); x += 136; pnt.seps.push_back (x - 12); }
        pnt.groups.push_back ({ { x + 14, 14, 330, 20 }, "One-click helpers" });
        safe.setBounds (x + 16, top + 2, 326, 30); hm.setBounds (x + 16, top + 38, 160, 30); hs.setBounds (x + 182, top + 38, 160, 30); match.setBounds (x + 16, top + 74, 326, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void ImagerEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const double tot = outSlow.total();
    if (real ("bypass") > 0.5f) { r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." }; const char* n[] = { "Low end", "Balance", "Width" }; for (size_t i = 0; i < 3; ++i) r[i + 1] = { S::ok, n[i], "Nothing while bypassed." }; check->refresh(); return; }
    if (proc.monoTrack()) { r[0] = { S::warn, "This track is mono", "There is no second side to widen. Put it on a stereo track." }; const char* n[] = { "Low end", "Balance", "Width" }; for (size_t i = 0; i < 3; ++i) r[i + 1] = { S::ok, n[i], "Passes through untouched." }; check->refresh(); return; }
    if (tot < 1e-9) { const char* n[] = { "In mono", "Low end", "Balance", "Width" }; for (size_t i = 0; i < 4; ++i) r[i] = { S::ok, n[i], "Waiting for signal." }; check->refresh(); return; }

    int worstBand = 0; double worst = 0;
    for (int k = 0; k < img::bands; ++k) { bool quiet; const double v = bandLoss (k, quiet); const auto p = proc.readParams(); const double lo = k > 0 ? p.split[(size_t) k - 1] : 0.0, hi = k < 3 ? p.split[(size_t) k] : 1e9; if (! quiet && outSlow.middle (lo, hi) + outSlow.side (lo, hi) > 0.0005 * tot && v > worst) { worst = v; worstBand = k; } }
    const auto where = juce::String (imgp::bandNames[worstBand]).toLowerCase();
    const bool safeOn = real ("safe") > 0.5f;
    const float allowed = safeOn ? std::max (3.2f, real ("limit") + 0.6f) : 3.2f;
    if (worst <= allowed) r[0] = { S::ok, "Holds up in mono", worst < 0.3 ? "Nothing to lose." : "The " + where + " lose the most: " + juce::String (worst, 1) + " dB." };
    else if (safeOn) r[0] = { S::warn, "Weak in mono", "The " + where + " lose " + juce::String (worst, 1) + " dB.", "Safer", [this] { change ("limit", std::max (1.0f, real ("limit") - 1.0f)); } };
    else r[0] = { worst > 6 ? S::bad : S::warn, worst > 6 ? "Parts vanish in mono" : "Weak in mono", "The " + where + " lose " + juce::String (worst, 1) + " dB on a phone or in a club.", "Mono-safe", [this] { change ("safe", 1); } };

    const double lm = outSlow.middle (0, 130), ls = outSlow.side (0, 130), low = imgp::lossDb (lm, ls);
    if (lm + ls < 0.001 * tot) r[1] = { S::ok, "Low end", "No bass in this sound." };
    else if (low > 0.35) r[1] = { low > 1.5 ? S::bad : S::warn, "The low end is wide", "The bass loses " + juce::String (low, 1) + " dB in mono.", "Tighten it", [this] { tighten(); } };
    else r[1] = { S::ok, "Tight low end", "The bass sits in the middle." };

    const double bal = outSlow.balanceDb(); const auto side = juce::String (bal > 0 ? "left" : "right");
    if (std::abs (bal) > 0.8) r[2] = { std::abs (bal) > 3 ? S::bad : S::warn, "Leans to the " + side, "The " + side + " is " + juce::String (std::abs (bal), 1) + " dB louder.", "Centre it", [this] { centreIt(); } };
    else r[2] = { S::ok, "Centred", "Both sides within " + juce::String (std::max (0.1, std::abs (bal)), 1) + " dB." };

    const double was = inSlow.side (150) / (inSlow.middle (150) + 1e-14), now = outSlow.side (150) / (outSlow.middle (150) + 1e-14), d = 10 * std::log10 ((now + 1e-12) / (was + 1e-12));
    if (real ("hearMono") > 0.5f) r[3] = { S::warn, "You hear it in mono", "For checking only.", "Back to stereo", [this] { change ("hearMono", 0); } };
    else if (real ("hearSide") > 0.5f) r[3] = { S::warn, "You hear only the sides", "For checking only.", "Back to stereo", [this] { change ("hearSide", 0); } };
    else if (was < 1e-6 && real ("make") < 1.0f) r[3] = { S::warn, "This sound is mono", "Width has nothing to work with.", "Make it stereo", [this] { makeItStereo(); } };
    else if (was < 1e-6) r[3] = { S::ok, "Made stereo", "The middle is exactly what it was." };
    else if (now < 1e-6) r[3] = { S::ok, "Mono", "Everything is in the middle." };
    else if (std::abs (d) < 0.3) r[3] = { S::ok, "As wide as before", "The sides are as loud as they were." };
    else r[3] = { S::ok, d > 0 ? "Wider" : "Narrower", "Above the bass, the sides are " + juce::String (std::abs (d), 1) + " dB " + (d > 0 ? "louder." : "quieter.") };
    check->refresh();
}

void ImagerEditor::tick (bool simple, int count)
{
    pro = ! simple;
    if (count % 2 == 0)
    {
        const double fs = proc.currentRate.load(); analyser.prepare (fs);
        const int lat = proc.latencyNow.load(), n = 1400, stepN = juce::jmax (1, juce::roundToInt (fs / 48000.0));
        const auto a = analyser.read (proc.ringInput, lat);
        auto take = [&] (std::vector<float>& L, std::vector<float>& R, int c) { L.clear(); R.clear(); for (int i = analyser.size() - c * 2 * stepN; i < analyser.size(); i += 2 * stepN) if (i >= 0) { L.push_back (analyser.L[(size_t) i]); R.push_back (analyser.R[(size_t) i]); } };
        take (dotsInL, dotsInR, n * 2 / 3);
        const auto o = analyser.read (proc.ringOutput);
        take (dotsL, dotsR, n);
        double lr = 0, ll = 0, rr = 0; float pk = 0.02f; for (size_t i = 0; i < dotsL.size(); ++i) { lr += dotsL[i] * dotsR[i]; ll += dotsL[i] * dotsL[i]; rr += dotsR[i] * dotsR[i]; pk = std::max ({ pk, std::abs (0.5f * (dotsL[i] + dotsR[i])), std::abs (0.5f * (dotsL[i] - dotsR[i])) }); }
        const float cNow = ll * rr > 1e-14 ? (float) (lr / std::sqrt (ll * rr)) : 1.0f; correlation += (cNow - correlation) * 0.15f;
        const float want = juce::jlimit (0.9f, 12.0f, 0.9f / pk); zoom += (want - zoom) * (want < zoom ? 0.5f : 0.03f);
        // plain averages first, then ones that forget: 0.25 s for the picture, 1.5 s for the words
        ++seen; const double quick = std::max (1.0 - std::exp (-1.0 / 30.0 / 0.25), 1.0 / seen), slow = std::max (1.0 - std::exp (-1.0 / 30.0 / 1.5), 1.0 / seen);
        in.mix (a, quick); out.mix (o, quick); inSlow.mix (a, slow); outSlow.mix (o, slow);
    }
    (simple ? displaySimple : displayPro)->repaint();
    if (count % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const auto dash = juce::String::charToString (0x2014);
    double hold = 0; for (auto& h : proc.holding) hold = std::max (hold, (double) h.load());
    panel->heading = "Lost in mono"; panel->note = proc.monoTrack() ? "mono track" : (real ("safe") > 0.5f ? (hold > 0.2 ? "holding" : "safe") : "");
    panel->bigName = "All together"; panel->bigValue = juce::String (imgp::lossDb (outSlow.middle(), outSlow.side()), 1); panel->unit = "dB";
    panel->rows.clear();
    const auto p = proc.readParams();
    for (int k = 0; k < img::bands; ++k)
    {
        bool quiet; const double v = bandLoss (k, quiet), lo = k > 0 ? p.split[(size_t) k - 1] : 0.0, hi = k < 3 ? p.split[(size_t) k] : 1e9, m = outSlow.middle (lo, hi), s = outSlow.side (lo, hi), c = m + s > 1e-14 ? (m - s) / (m + s) : 1.0;
        panel->rows.push_back ({ imgp::bandNames[k], quiet ? dash : lossText (v) + "  " + (c >= 0 ? "+" : "") + txt::num ((float) c, 2) });
    }
    panel->rows.push_back ({ "Mono sum", "unchanged", true });
    if (real ("match") > 0.5f) panel->rows.push_back ({ "Equal loud", txt::num (proc.match.nowDb.load(), 1) + " dB" });
    panel->repaint();
}
