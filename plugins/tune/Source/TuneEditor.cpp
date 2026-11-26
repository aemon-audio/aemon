// Aemon Tune. AGPL-3.0-or-later.
// Teal is the singer, amber is what comes out. Simple is a ring and a line, Pro is the take and one note under the glass. No knobs.
#include "TuneEditor.h"

using namespace aemon;
using Frame = aemon::tune::Engine::Frame;

namespace
{
    constexpr float pi = juce::MathConstants<float>::pi;
    juce::Point<float> onCircle (juce::Point<float> c, float r, float a) { return { c.x + r * std::sin (a), c.y - r * std::cos (a) }; }
    juce::String centsText (float v) { const int c = juce::roundToInt (std::abs (v)); return c == 0 ? juce::String ("on the note") : juce::String (c) + (v < 0 ? " under" : " over"); }
    void dotHandle (juce::Graphics& g, juce::Point<float> p, juce::Colour c, bool hot) { g.setColour (c.withAlpha (hot ? 0.34f : 0.22f)); g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre (p)); g.setColour (c); g.fillEllipse (juce::Rectangle<float> (13, 13).withCentre (p)); }
    void glow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float w) { g.setColour (c.withAlpha (0.10f)); g.strokePath (p, juce::PathStrokeType (w * 4.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); g.setColour (c.withAlpha (0.20f)); g.strokePath (p, juce::PathStrokeType (w * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); g.setColour (c); g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
}

namespace tuneui
{
/** The twelve notes as a ring, the voice from the middle (a while ago) to the rim (now). */
class Ring : public juce::Component, public HelpProvider
{
public:
    explicit Ring (TuneEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return hot >= 0 ? tunep::noteName (hot) + (ed.proc.notesOn()[(size_t) hot] ? ": on. Click to switch it off." : ": off. Click to let the voice land here.") : "The twelve notes. Teal: you. Amber: what comes out. Click a note to switch it on or off."; }
    void paint (juce::Graphics& g) override
    {
        const auto C = centre(); const float R = radius(); const auto on = ed.proc.notesOn(); const auto& h = ed.past; const int n = (int) h.size(); const int lag = juce::roundToInt (ed.lag());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1e25), C, juce::Colour (0xff08090b), C.translated (0, R + 6), true)); g.fillEllipse (juce::Rectangle<float> (2 * R + 12, 2 * R + 12).withCentre (C)); g.setColour (col::edge); g.drawEllipse (juce::Rectangle<float> (2 * R + 12, 2 * R + 12).withCentre (C), 1.5f);
        const Frame* now = nullptr; if (n > lag && h[(size_t) (n - 1 - lag)].voiced) now = &h[(size_t) (n - 1 - lag)];
        const int target = now != nullptr ? ((juce::roundToInt (now->target) % 12) + 12) % 12 : -1;
        for (int i = 0; i < 12; ++i)
        {
            const float a = (float) i * pi / 6, w = pi / 12 - 0.03f; const bool t = i == target && on[(size_t) i];
            if (on[(size_t) i]) { g.setColour (t ? col::ember.withAlpha (0.55f) : col::basalt4.withAlpha (0.9f)); g.drawLine ({ onCircle (C, R * 0.40f, a), onCircle (C, R - 14, a) }, t ? 1.6f : 1.0f); }
            juce::Path p; p.addCentredArc (C.x, C.y, R, R, 0, a - w, a + w, true); if (t) { g.setColour (col::ember.withAlpha (0.22f)); g.strokePath (p, juce::PathStrokeType (30)); }
            g.setColour (t ? col::ember : (on[(size_t) i] ? juce::Colour (i == hot ? 0xff454a58 : 0xff343844) : juce::Colour (i == hot ? 0xff262932 : 0xff17191f))); g.strokePath (p, juce::PathStrokeType (t ? 18.0f : 14.0f));
            g.setFont (Fonts::label (t ? 17.0f : 14.0f)); g.setColour (t ? col::emberHi : (on[(size_t) i] ? col::bone1 : juce::Colour (0xff565a66))); g.drawText (tunep::noteName (i), juce::Rectangle<float> (44, 22).withCentre (onCircle (C, R + 25, a)), juce::Justification::centred);
        }
        // the voice: the newest at the rim
        const int span = 720; const auto r = [&] (int back) { return R * 0.40f + (R - 14 - R * 0.40f) * std::pow (1.0f - (float) back / span, 0.8f); };
        for (int pass = 0; pass < 2; ++pass)
        {
            juce::Path p; bool pen = false; int from = 0; float lastV = 0;
            const auto flush = [&] (int back) { if (p.isEmpty()) return; const float fade = juce::jlimit (0.0f, 1.0f, 1.12f - (float) back / span); const auto c = pass == 0 ? col::phosphor : col::emberHi; if (pass == 0) { g.setColour (c.withAlpha (0.75f * fade)); g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved)); } else { g.setColour (col::ember.withAlpha (0.22f * fade)); g.strokePath (p, juce::PathStrokeType (9, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); g.setColour (c.withAlpha (fade)); g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); } p.clear(); pen = false; };
            for (int back = juce::jmin (span, n - 1 - lag); back >= 0; --back)
            {
                const auto& f = h[(size_t) (n - 1 - lag - back)]; if (! f.voiced) { flush (back); continue; }
                const float v = pass == 0 ? f.sung : f.out; const auto q = onCircle (C, r (back), v * pi / 6);
                if (pen && std::abs (v - lastV) > 0.5f) { const int steps = juce::jmin (24, 1 + (int) (std::abs (v - lastV) * 4)); for (int s = 1; s <= steps; ++s) p.lineTo (onCircle (C, r (back), (lastV + (v - lastV) * (float) s / (float) steps) * pi / 6)); }
                if (pen) p.lineTo (q); else { p.startNewSubPath (q); pen = true; from = back; }
                lastV = v; if (from - back > 40) { const auto keep = q; flush (back); p.startNewSubPath (keep); pen = true; from = back; }
            }
            flush (0);
        }
        if (now != nullptr) { const auto q = onCircle (C, R - 14, now->out * pi / 6); g.setColour (col::ember.withAlpha (0.35f)); g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre (q)); g.setColour (juce::Colour (0xfffff3e2)); g.fillEllipse (juce::Rectangle<float> (13, 13).withCentre (q)); g.setColour (col::phosphor); g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre (onCircle (C, R - 14, now->sung * pi / 6))); }
        g.setColour (juce::Colour (0xff16181d)); g.fillEllipse (juce::Rectangle<float> (2 * R * 0.34f, 2 * R * 0.34f).withCentre (C)); g.setColour (col::line); g.drawEllipse (juce::Rectangle<float> (2 * R * 0.34f, 2 * R * 0.34f).withCentre (C), 1);
        const auto hub = juce::Rectangle<float> (120, 100).withCentre (C);
        if (now != nullptr) { g.setFont (Fonts::display (44)); g.setColour (on[(size_t) target] ? col::emberHi : col::bone2); g.drawText (tunep::noteName (target), hub.withHeight (62), juce::Justification::centred); g.setFont (Fonts::mono (11.5f)); g.setColour (col::phosphor); g.drawText (centsText ((now->sung - now->target) * 100), hub.withTrimmedTop (64).withHeight (20), juce::Justification::centred); }
        else { g.setFont (Fonts::text (13)); g.setColour (col::bone3); g.drawFittedText (n > 0 && h.back().level > -60 && h.back().sharp ? "S or breath" : "Sing", hub.toNearestInt(), juce::Justification::centred, 2); }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int i = at (e.position); if (i != hot) { hot = i; setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hot = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { const int i = at (e.position); if (i >= 0) ed.toggleNote (i); }
    juce::Point<float> placeForTest (int pc) const { return onCircle (centre(), radius(), (float) pc * pi / 6); }
private:
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    float radius() const { return juce::jmin ((float) getWidth(), (float) getHeight()) * 0.5f - 40; }
    int at (juce::Point<float> p) const { const auto d = p - centre(); const float r = d.getDistanceFromOrigin(), R = radius(); if (r < R - 16 || r > R + 42) return -1; return ((int) std::lround (std::atan2 (d.x, -d.y) / (pi / 6)) % 12 + 12) % 12; }
    TuneEditor& ed; int hot = -1;
};

/** One line from a free voice to steps. Its shape under the thumb is what happens to the voice. */
class Line : public juce::Component, public HelpProvider
{
public:
    explicit Line (TuneEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return ed.hasOwn() ? "A shape of your own from Pro, or from a preset. Drag to go back to the line." : "Left leaves you as you are, right puts every note on its place at once. Double-click: all the way left."; }
    void paint (juce::Graphics& g) override
    {
        const float x0 = X (0), x1 = X (1), y = mid(), place = ed.linePlace(); const bool own = ed.hasOwn();
        juce::Path a; bool pen = false; for (float x = x0; x <= x1; x += 1.0f) { const float u = (x - x0) / (x1 - x0), ph = u * 8 * 2 * pi, k = 1 + 26 * u * u, sn = std::sin (ph) + 0.32f * std::sin (ph * 3.1f + 1) * (1 - u), sq = std::tanh (std::sin (ph) * k) / std::tanh (k), v = y - 10.5f * ((1 - u) * sn + u * sq); if (! pen) a.startNewSubPath (x, v); else a.lineTo (x, v); pen = true; }
        g.setColour (juce::Colour (0xff3a3e4a)); g.strokePath (a, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved));
        const juce::Point<float> t (X (place), y);
        if (! own) { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (0, 0, (int) t.x, getHeight()); g.setColour (col::ember); g.strokePath (a, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved)); }
        g.setColour (col::ember.withAlpha (hot ? 0.34f : 0.22f)); g.fillEllipse (juce::Rectangle<float> (44, 44).withCentre (t)); g.setColour (juce::Colour (0xff23262e)); g.fillEllipse (juce::Rectangle<float> (32, 32).withCentre (t)); g.setColour (own ? col::emberLo : col::ember); g.drawEllipse (juce::Rectangle<float> (32, 32).withCentre (t), 2.5f); if (! own) { g.setColour (col::emberHi); g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre (t)); }
        for (int i = 0; i < 5; ++i) { const bool near = ! own && place > 0.015f && std::abs (place - (float) i * 0.25f) < 0.126f; drawEngraved (g, tunep::stops[i], { X ((float) i * 0.25f) - 70, y + 28, 140, 14 }, 11.0f, near ? col::emberHi : col::bone3); }
        if (own) { g.setFont (Fonts::text (12)); g.setColour (col::emberHi); g.drawText ("a shape of your own", juce::Rectangle<float> (160, 16).withCentre ({ juce::jlimit (x0 + 80, x1 - 80, t.x), 8 }), juce::Justification::centred); }
        else if (place <= 0.015f) { g.setFont (Fonts::text (12)); g.setColour (col::bone2); g.drawText ("drag to tune", juce::Rectangle<float> (120, 16).withCentre ({ x0 + 70, 8 }), juce::Justification::centred); }
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { ed.begin(); ed.line (P (e.position.x)); }
    void mouseDrag (const juce::MouseEvent& e) override { ed.line (P (e.position.x)); }
    void mouseUp (const juce::MouseEvent&) override { ed.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { ed.begin(); ed.line (0); ed.end(); }
    juce::Point<float> placeForTest (float u) const { return { X (u), mid() }; }
private:
    float X (float u) const { return 40 + u * ((float) getWidth() - 80); }
    float P (float x) const { const float v = juce::jlimit (0.0f, 1.0f, (x - X (0)) / (X (1) - X (0))); return v < 0.012f ? 0.0f : (v > 0.988f ? 1.0f : v); }
    float mid() const { return (float) getHeight() * 0.5f - 4; }
    TuneEditor& ed; bool hot = false;
};

/** The key, how sure it is, and one sentence. */
class KeyPanel : public juce::Component
{
public:
    explicit KeyPanel (TuneEditor& e) : ed (e)
    {
        addAndMakeVisible (name); name.setInterceptsMouseClicks (true, false); name.onClick = [this] { ed.keyMenu (name); }; setHelp (name, "The notes that are on. Click to choose a key, or tap the notes on the ring.");
        addChildComponent (use); use.small = true; use.onClick = [this] { const auto f = ed.proc.found(); if (f.root >= 0) ed.useKey (f.root, f.minor); }; setHelp (use, "Switches on the notes of the key that was heard.");
        addAndMakeVisible (again); again.small = true; again.onClick = [this] { ed.proc.listenAgain(); ed.toast ("Listening again. Sing a few lines."); }; setHelp (again, "Forgets what was heard and listens again.");
    }
    void resized() override { name.setBounds (0, 18, getWidth(), 66); use.setBounds (0, 142, getWidth(), 26); again.setBounds (0, 174, getWidth(), 26); }
    void refresh()
    {
        const auto f = ed.proc.found(); juce::String h; const juce::String k = f.root >= 0 ? tunep::noteName (f.root) + (f.minor ? " minor" : " major") : juce::String();
        if (f.seconds < 1.5f || f.root < 0) h = "Sing, and the key is found from your voice."; else if (f.sure > 0.75f) h = "Heard: " + k + ", " + juce::String (juce::roundToInt (f.sure * 100)) + " % sure."; else if (f.sure > 0.4f) h = "Perhaps " + k + ". Sing a little more."; else h = "Not sure yet. Sing a little more.";
        const bool same = f.root >= 0 && aemon::tune::KeyFinder::notesOf (f.root, f.minor) == ed.proc.notesOn(); use.setVisible (f.root >= 0 && f.sure > 0.4f && ! same); use.setButtonText (k.isEmpty() ? juce::String() : "Use " + k);
        if (f.root >= 0 && f.sure > 0.75f && std::abs (f.cents) > 18) h << " You sing " << juce::String (juce::roundToInt (std::abs (f.cents))) << " cents " << (f.cents < 0 ? "low." : "high.");
        if (h != heard) { heard = h; repaint(); } const auto t = ed.keyText(); if (t != title) { title = t; repaint(); } const auto s = ed.sentence(); if (s != say) { say = s; repaint(); }
    }
    void paint (juce::Graphics& g) override
    {
        drawEngraved (g, "Key", { 0, 0, 100, 12 }, 10.0f, col::bone3, juce::Justification::centredLeft); g.setFont (Fonts::display (22)); g.setColour (name.isOver() ? col::emberHi : col::bone1); g.drawFittedText (title, name.getBounds(), juce::Justification::centredLeft, 2, 0.8f);
        g.setFont (Fonts::text (13)); g.setColour (col::bone2); g.drawFittedText (heard, 0, 88, getWidth(), 50, juce::Justification::topLeft, 3); g.setFont (Fonts::text (12.5f)); g.setColour (col::sigHot); g.drawFittedText (say, 0, getHeight() - 46, getWidth(), 46, juce::Justification::bottomLeft, 3);
    }
private:
    struct Plain : juce::Button { Plain() : juce::Button ("") {} void paintButton (juce::Graphics&, bool, bool) override {} };
    TuneEditor& ed; Plain name; Button use { "" }, again { "Listen again" }; juce::String heard, title, say;
};

/** The size of the throat, drawn as one. */
class Throat : public juce::Component, public HelpProvider
{
public:
    explicit Throat (TuneEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "The size of the throat. Changes the voice, not the notes. Double-click: as sung."; }
    void paint (juce::Graphics& g) override
    {
        const float v = ed.real ("throat"), cx = (float) getWidth() * 0.5f, top = 64, bot = (float) getHeight() - 64, y = Y (v), open = (v - 100) / 40.0f;
        drawEngraved (g, "Throat", { 0, 0, (float) getWidth(), 12 }, 10.0f, col::bone3); g.setFont (Fonts::mono (14)); g.setColour (col::lilac); g.drawText (v > 100.05f ? juce::String (v / 100, 2) + " deeper" : (v < 99.95f ? juce::String (100 / v, 2) + " smaller" : juce::String ("as sung")), 0, 18, getWidth(), 18, juce::Justification::centred);
        const float neck = 13 + 9 * open, end = 34 + 8 * open; for (float s : { -1.0f, 1.0f }) { juce::Path p; p.startNewSubPath (cx + s * end, bot); p.cubicTo (cx + s * end, bot - 60, cx + s * neck, bot - 70, cx + s * neck, (top + bot) * 0.5f); p.cubicTo (cx + s * neck, top + 60, cx + s * (neck + 11), top + 40, cx + s * (neck + 11), top); g.setColour (juce::Colour (0xff3a3e4a)); g.strokePath (p, juce::PathStrokeType (2)); }
        g.setColour (col::basalt3); g.fillRect (cx - 1, top, 2.0f, bot - top); g.setColour (col::bone3); g.fillRect (cx - 7, Y (100) - 0.75f, 14.0f, 1.5f); g.setColour (col::lilac); g.fillRect (cx - 1.5f, juce::jmin (y, Y (100)), 3.0f, std::abs (y - Y (100)));
        g.setColour (col::lilac.withAlpha (hot ? 0.32f : 0.16f)); g.fillEllipse (juce::Rectangle<float> (38, 38).withCentre ({ cx, y })); g.setColour (juce::Colour (0xff23262e)); g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre ({ cx, y })); g.setColour (col::lilac); g.drawEllipse (juce::Rectangle<float> (26, 26).withCentre ({ cx, y }), 2.4f); g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ cx, y }));
        drawEngraved (g, "Smaller", { 0, top - 18, (float) getWidth(), 12 }, 10.0f, col::bone3); drawEngraved (g, "Deeper", { 0, bot + 8, (float) getWidth(), 12 }, 10.0f, col::bone3); g.setFont (Fonts::text (12)); g.setColour (col::bone3); g.drawFittedText ("Changes the voice, not the notes.", 0, (int) bot + 26, getWidth(), 34, juce::Justification::centredTop, 2);
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; setMouseCursor (juce::MouseCursor::UpDownResizeCursor); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { ed.begin(); from = ed.real ("throat"); down = e.position.y; if (std::abs (e.position.y - Y (from)) > 22) { from = V (e.position.y); ed.setReal ("throat", from); } }
    void mouseDrag (const juce::MouseEvent& e) override { float v = from + (e.position.y - down) * (e.mods.isShiftDown() ? 0.1f : 1.0f) * 70.0f / ((float) getHeight() - 128); if (std::abs (v - 100) < 1.0f) v = 100; ed.setReal ("throat", juce::jlimit (70.0f, 140.0f, v)); }
    void mouseUp (const juce::MouseEvent&) override { ed.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { ed.change ("throat", 100); }
private:
    float Y (float v) const { return 64 + (v - 70) / 70 * ((float) getHeight() - 128); }
    float V (float y) const { return juce::jlimit (70.0f, 140.0f, 70 + (y - 64) / ((float) getHeight() - 128) * 70); }
    TuneEditor& ed; float from = 100, down = 0; bool hot = false;
};

/** The last seconds on note lanes. It shows; the past cannot be edited here. */
class Take : public juce::Component, public HelpProvider
{
public:
    explicit Take (TuneEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return hot >= 0 ? tunep::noteName (hot) + ": click to switch it " + (ed.proc.notesOn()[(size_t) hot] ? "off" : "on") + ". Drag sideways: meant a few cents low or high. Double-click: straight." : "The last seconds. Teal: as sung. Amber: what comes out. Lilac: S and breath, left alone."; }
    void step() { float want = centreNote; int n = 0; double s = 0; for (int i = (int) ed.past.size() - 1; i >= 0 && n < 200; --i) if (ed.past[(size_t) i].voiced) { s += ed.past[(size_t) i].sung; ++n; } if (n > 20) want = (float) (s / n); if (std::abs (want - centreNote) > 12) centreNote = want; else centreNote += (want - centreNote) * 0.06f; repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto B = box(); const auto on = ed.proc.notesOn(); const auto& h = ed.past; const int n = (int) h.size(), lag = juce::roundToInt (ed.lag());
        g.setColour (juce::Colour (0xff0b0c0f)); g.fillRoundedRectangle (B, 8); juce::Graphics::ScopedSaveState s (g);
        const int lo = (int) std::floor (centreNote - 6), hi = (int) std::ceil (centreNote + 6); const float lh = B.getHeight() / 11.0f;
        { juce::Graphics::ScopedSaveState c (g); g.reduceClipRegion (B.toNearestInt()); for (int m = lo; m <= hi; ++m) { const int pc = ((m % 12) + 12) % 12; g.setColour (on[(size_t) pc] ? juce::Colour (0xff14161b) : juce::Colour (0xff0b0c0f)); g.fillRect (B.getX(), Y ((float) m + 0.5f) + 0.5f, B.getWidth(), lh - 1); }
          for (int k = 0; k <= 12; ++k) { g.setColour (juce::Colours::white.withAlpha (k % 2 ? 0.02f : 0.05f)); g.fillRect (B.getRight() - B.getWidth() * (float) k / 13.0f, B.getY(), 1.0f, B.getHeight()); }
          const float dx = B.getWidth() / (float) (TuneEditor::shown - 1); const auto xa = [&] (int i) { return B.getRight() - (float) (n - 1 - lag - i) * dx; };
          int sh = -1; for (int i = juce::jmax (0, n - TuneEditor::shown); i < n - lag; ++i) { const bool q = ! h[(size_t) i].voiced && h[(size_t) i].level > -52; if (q && sh < 0) sh = i; if ((! q || i == n - lag - 1) && sh >= 0) { if (i - sh > 6) { g.setColour (col::lilac.withAlpha (0.10f)); g.fillRect (xa (sh), B.getY(), xa (i) - xa (sh), B.getHeight()); } sh = -1; } }
          // the notes that were meant
          int from = -1; for (int i = juce::jmax (0, n - TuneEditor::shown); i <= n - lag; ++i) { const bool in = i < n - lag && h[(size_t) i].voiced; const bool go = in && from >= 0 && std::abs (h[(size_t) i].target - h[(size_t) from].target) < 0.6f; if (from >= 0 && ! go) { const auto& f = h[(size_t) from]; if (i - from > 8 && on[(size_t) (((juce::roundToInt (f.target) % 12) + 12) % 12)]) { const juce::Rectangle<float> r (xa (from), Y (f.target) - lh * 0.4f, xa (i) - xa (from), lh * 0.8f); g.setColour (col::ember.withAlpha (0.10f)); g.fillRoundedRectangle (r, 4); g.setColour (col::ember.withAlpha (0.35f)); g.drawRoundedRectangle (r, 4, 1); } from = -1; } if (in && from < 0) from = i; }
          for (int pass = 0; pass < 2; ++pass) { juce::Path p; bool pen = false; for (int i = juce::jmax (0, n - TuneEditor::shown); i < n - lag; ++i) { const auto& f = h[(size_t) i]; if (! f.voiced) { pen = false; continue; } const juce::Point<float> q (xa (i), Y (pass == 0 ? f.sung : f.out)); if (pen) p.lineTo (q); else p.startNewSubPath (q); pen = true; } if (pass == 0) { g.setColour (col::phosphor.withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (1.7f)); } else glow (g, p, col::emberHi, 2.2f); } }
        for (int m = lo; m <= hi; ++m)
        {
            const int pc = ((m % 12) + 12) % 12; const float y = Y ((float) m); if (y < B.getY() + lh * 0.3f || y > B.getBottom() - lh * 0.3f) continue; const juce::Rectangle<float> r (0, y - lh * 0.5f + 1.5f, 58, lh - 3); const float c = ed.real (("c" + juce::String (pc)).toRawUTF8());
            g.setColour (on[(size_t) pc] ? juce::Colour (m == hotLane ? 0xff343844 : 0xff2a2d36) : juce::Colour (m == hotLane ? 0xff1a1c22 : 0xff101116)); g.fillRoundedRectangle (r, 4); const bool lit = n > lag && h[(size_t) (n - 1 - lag)].voiced && juce::roundToInt (h[(size_t) (n - 1 - lag)].target) == m; g.setColour (lit ? col::ember : col::edge); g.drawRoundedRectangle (r, 4, 1);
            g.setFont (Fonts::label (11.5f)); g.setColour (on[(size_t) pc] ? col::bone1 : juce::Colour (0xff565a66)); g.drawText (tunep::noteName (pc), r.withTrimmedLeft (9), juce::Justification::centredLeft); if (std::abs (c) > 0.05f) { g.setFont (Fonts::mono (9.0f)); g.setColour (col::ember); g.drawText ((c > 0 ? "+" : "") + txt::num (c, 0), r.withTrimmedRight (6), juce::Justification::centredRight); } else { g.setColour (on[(size_t) pc] ? (lit ? col::ember : col::emberLo) : col::basalt3); g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre ({ r.getRight() - 10, y })); }
        }
        if (n == 0) { g.setFont (Fonts::text (14)); g.setColour (col::bone3); g.drawText ("Sing, or play the track.", B, juce::Justification::centred); }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int m = lane (e.position); if (m != hotLane) { hotLane = m; hot = m < -900 ? -1 : ((m % 12) + 12) % 12; setMouseCursor (hot >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hot = -1; hotLane = -999; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { const int m = lane (e.position); down = m < -900 ? -1 : ((m % 12) + 12) % 12; dragged = false; if (down >= 0) from = ed.real (("c" + juce::String (down)).toRawUTF8()); }
    void mouseDrag (const juce::MouseEvent& e) override { if (down < 0) return; if (! dragged && std::abs (e.getDistanceFromDragStartX()) > 4) { dragged = true; ed.begin(); } if (dragged) ed.setReal ("c" + juce::String (down), juce::jlimit (-50.0f, 50.0f, std::round (from + (float) e.getDistanceFromDragStartX() * 0.5f))); }
    void mouseUp (const juce::MouseEvent&) override { if (dragged) ed.end(); else if (down >= 0) ed.toggleNote (down); down = -1; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { const int m = lane (e.position); if (m > -900) { ed.change ("c" + juce::String (((m % 12) + 12) % 12), 0); } }
private:
    juce::Rectangle<float> box() const { return getLocalBounds().toFloat().withTrimmedLeft (64); }
    float Y (float m) const { const auto B = box(); return B.getCentreY() - (m - centreNote) * B.getHeight() / 11.0f; }
    int lane (juce::Point<float> p) const { if (p.x > 62) return -999; const auto B = box(); return (int) std::lround (centreNote - (p.y - B.getCentreY()) / (B.getHeight() / 11.0f)); }
    TuneEditor& ed; float centreNote = 60, from = 0; int hot = -1, hotLane = -999, down = -1; bool dragged = false;
};

/** One note, large. The points on its curve are the settings; the curve shows what they do to the last note that was sung. */
class Glass : public juce::Component, public HelpProvider
{
public:
    explicit Glass (TuneEditor& e) : ed (e) {}
    juce::String getHelpText() const override
    {
        switch (hot) { case 0: return "Slide in: how much of a slide into the note is left alone. Drag up or down."; case 1: return "Land: how long the way to the note takes. Drag left or right."; case 2: return "Let go: after how long a held note gets its freedom back (sideways), and how much (up)."; case 3: return "Vibrato: how much of it is kept. Drag up or down."; case 4: return "Pull: how far to the note in the end. Drag up or down."; default: return "Your last held note. Teal: as sung. Amber: with these settings. Drag the points."; }
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); drawWell (g, b, 10); drawEngraved (g, ed.lastNote.empty() ? "One note under the glass (an example)" : "Your last held note under the glass", { 16, 12, 320, 12 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        const auto B = box(); const auto in = sungCurve(); const auto out = tuned (in); const int n = (int) in.size(); const auto X = [&] (int i) { return B.getX() + B.getWidth() * (float) i / 180.0f; };
        g.setColour (juce::Colours::white.withAlpha (0.05f)); for (float c : { -50.0f, 50.0f }) g.fillRect (B.getX(), Y (c), B.getWidth(), 1.0f); drawDashed (g, { { B.getX(), Y (0) }, { B.getRight(), Y (0) } }, col::ember.withAlpha (0.4f));
        { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (B.expanded (4).toNearestInt()); juce::Path p, q; for (int i = 0; i < n; ++i) { if (i == 0) { p.startNewSubPath (X (i), Y (in[0])); q.startNewSubPath (X (i), Y (out[0])); } else { p.lineTo (X (i), Y (in[(size_t) i])); q.lineTo (X (i), Y (out[(size_t) i])); } } g.setColour (col::phosphor.withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (1.8f)); glow (g, q, col::emberHi, 2.4f); }
        if (n > 40) { float s = 0, o = 0; int c = 0; for (int i = n / 2; i < n; ++i) { s += in[(size_t) i]; o += out[(size_t) i]; ++c; } g.setFont (Fonts::label (11)); g.setColour (col::emberHi); const juce::String t = (ed.lastNote.empty() ? juce::String() : tunep::noteName (ed.lastTarget) + "  ") + centsText (s / (float) c) + ", comes out " + centsText (o / (float) c); g.drawText (t, (int) b.getRight() - 300, 8, 284, 20, juce::Justification::centredRight); }
        const auto v = ed.shape(); const juce::String vals[5] { juce::String (juce::roundToInt (v[2])) + " % kept", juce::String (juce::roundToInt (v[1])) + " ms", v[3] < 0.5f ? juce::String ("never") : juce::String (juce::roundToInt (v[3])) + " % after " + juce::String (juce::roundToInt (v[4])) + " ms", juce::String (juce::roundToInt (v[5])) + " % kept", juce::String (juce::roundToInt (v[0])) + " %" }; const char* names[] { "Slide in", "Land", "Let go", "Vibrato", "Pull" };
        for (int i = 0; i < 5; ++i) { const auto p = handle (i); dotHandle (g, p, col::ember, hot == i); const float tx = labelX (i); drawEngraved (g, names[i], { tx - 60, B.getBottom() + 12, 120, 12 }, 10.0f, hot == i ? col::emberHi : col::bone3); g.setFont (Fonts::mono (11)); g.setColour (col::bone1); g.drawText (vals[i], juce::Rectangle<float> (130, 16).withCentre ({ tx, B.getBottom() + 34 }), juce::Justification::centred); }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int i = at (e.position); if (i != hot) { hot = i; setMouseCursor (i < 0 ? juce::MouseCursor::NormalCursor : (i == 1 ? juce::MouseCursor::LeftRightResizeCursor : (i == 2 ? juce::MouseCursor::UpDownLeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor))); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { if (grab < 0) { hot = -1; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override { grab = at (e.position); if (grab >= 0) { ed.begin(); start = ed.shape(); } }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (grab < 0) return; const float k = e.mods.isShiftDown() ? 0.15f : 1.0f, dx = (float) e.getDistanceFromDragStartX() * k, dy = -(float) e.getDistanceFromDragStartY() * k; const float w = box().getWidth();
        switch (grab) { case 0: ed.own (2, juce::jlimit (0.0f, 100.0f, start[2] + dy)); break; case 1: ed.own (1, juce::jlimit (0.0f, 300.0f, start[1] + dx / w * 900)); break; case 2: ed.own (4, juce::jlimit (60.0f, 1000.0f, start[4] + dx / w * 900)); ed.own (3, juce::jlimit (0.0f, 100.0f, start[3] + dy)); break; case 3: ed.own (5, juce::jlimit (0.0f, 100.0f, start[5] + dy)); break; default: ed.own (0, juce::jlimit (0.0f, 100.0f, start[0] + dy)); }
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (grab >= 0) ed.end(); grab = -1; }
    juce::Point<float> handleForTest (int i) const { return handle (i); }
private:
    juce::Rectangle<float> box() const { return getLocalBounds().toFloat().reduced (26, 0).withTrimmedTop (40).withTrimmedBottom (58); }
    float Y (float c) const { const auto B = box(); return B.getCentreY() - juce::jlimit (-75.0f, 75.0f, c) / 75.0f * B.getHeight() * 0.5f; }
    std::vector<float> sungCurve() const
    {
        if (ed.lastNote.size() > 50) { auto v = ed.lastNote; if (v.size() > 180) v.resize (180); return v; }
        std::vector<float> v; for (int i = 0; i < 180; ++i) { const float t = (float) i * 0.005f, swing = juce::jlimit (0.0f, 1.0f, (t - 0.35f) / 0.15f) * 26 * std::sin (t * 34.5f); v.push_back (-34 * std::exp (-juce::jmax (0.0f, t - 0.1f) * 1.2f) - 4 - 40 * juce::jmax (0.0f, 1 - t / 0.1f) + swing); } return v;
    }
    std::vector<float> tuned (const std::vector<float>& in) const
    {
        aemon::tune::Brain brain; brain.prepare (0.005); aemon::tune::Params p; const auto v = ed.shape(); p.manner = { v[0] / 100.0, v[1], v[2] / 100.0, v[3] / 100.0, v[4], v[5] / 100.0 }; p.notes.fill (false); p.notes[0] = true; std::vector<float> o;
        for (float c : in) { aemon::tune::Heard h; h.voiced = true; h.sure = 1; h.hz = 440 * std::pow (2.0, (60 + c / 100.0 - 69) / 12); const auto b = brain.step (h, p); o.push_back (c + (float) b.cents); } return o;
    }
    juce::Point<float> handle (int i) const
    {
        const auto B = box(); const auto v = ed.shape(); const auto X = [&] (float ms) { return B.getX() + B.getWidth() * ms / 900.0f; };
        switch (i) { case 0: return { X (45), Y (-55 * v[2] / 100) }; case 1: return { juce::jmax (X (45) + 30, X (60 + v[1])), Y (0) }; case 2: return { juce::jlimit (X (150) + 30, X (640), X (v[4])), Y (45 * v[3] / 100) }; case 3: return { X (730), Y (45 * v[5] / 100) }; default: return { X (870), Y (-60 * (1 - v[0] / 100)) }; }
    }
    float labelX (int i) const { const auto B = box(); static const float at[] { 0.05f, 0.23f, 0.50f, 0.78f, 0.96f }; return B.getX() + at[i] * B.getWidth(); }
    int at (juce::Point<float> p) const { int best = -1; float d = 18; for (int i = 4; i >= 0; --i) if (handle (i).getDistanceFrom (p) < d) { d = handle (i).getDistanceFrom (p); best = i; } if (best < 0 && p.y > box().getBottom() + 6) for (int i = 0; i < 5; ++i) if (std::abs (p.x - labelX (i)) < 46) best = i; return best; }
    TuneEditor& ed; int hot = -1, grab = -1; std::array<float, 6> start {};
};

/** The throat as the curve of a voice: as sung, and moved. */
class Mouth : public juce::Component, public HelpProvider
{
public:
    explicit Mouth (TuneEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "The throat: drag the curve sideways. Left is deeper, right is smaller. Double-click: as sung."; }
    void paint (juce::Graphics& g) override
    {
        drawWell (g, getLocalBounds().toFloat(), 10); drawEngraved (g, "Throat", { 14, 12, 120, 12 }, 10.0f, col::bone3, juce::Justification::centredLeft); const float v = ed.real ("throat"), base = (float) getHeight() - 46, w = (float) getWidth() - 28;
        for (float size : { 1.0f, v / 100.0f }) { juce::Path p; for (int i = 0; i <= 120; ++i) { const float u = (float) i / 120, x = u * size; const auto f = [&] (float m, float s, float a) { return a * std::exp (-juce::square ((x - m) / s)); }; const float y = base - 1.25f * (f (0.18f, 0.08f, 58) + f (0.42f, 0.07f, 42) + f (0.66f, 0.06f, 30) + f (0.85f, 0.06f, 16)); if (i == 0) p.startNewSubPath (14 + u * w, y); else p.lineTo (14 + u * w, y); } const bool moved = size != 1.0f || std::abs (v - 100) < 0.05f; if (&size != nullptr && ! moved) { g.setColour (juce::Colour (0xff5a5e6a)); g.strokePath (p, juce::PathStrokeType (1.4f)); } else { g.setColour (hot ? col::lilac.brighter (0.2f) : col::lilac); g.strokePath (p, juce::PathStrokeType (2.3f)); } }
        g.setFont (Fonts::mono (12)); g.setColour (col::lilac); g.drawText (v > 100.05f ? juce::String (v / 100, 2) + " deeper" : (v < 99.95f ? juce::String (100 / v, 2) + " smaller" : juce::String ("as sung")), 14, getHeight() - 32, getWidth() - 28, 18, juce::Justification::centredLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { hot = true; setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override { ed.begin(); from = ed.real ("throat"); }
    void mouseDrag (const juce::MouseEvent& e) override { float v = from - (float) e.getDistanceFromDragStartX() * (e.mods.isShiftDown() ? 0.04f : 0.4f); if (std::abs (v - 100) < 1) v = 100; ed.setReal ("throat", juce::jlimit (70.0f, 140.0f, v)); }
    void mouseUp (const juce::MouseEvent&) override { ed.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { ed.change ("throat", 100); }
private:
    TuneEditor& ed; float from = 100; bool hot = false;
};

/** What is heard, in words. */
class Source : public juce::Component
{
public:
    explicit Source (TuneEditor& e) : ed (e) { setInterceptsMouseClicks (false, true); }
    void paint (juce::Graphics& g) override
    {
        drawWell (g, getLocalBounds().toFloat(), 10); for (auto& l : labels) drawEngraved (g, l.first, l.second.toFloat(), 10.0f, col::bone3, juce::Justification::centredLeft);
        for (auto& l : under) drawEngraved (g, l.first, l.second.toFloat(), 8.0f, col::bone3, juce::Justification::centred);
        const bool any = ! ed.past.empty(); const auto f = any ? ed.past.back() : Frame {}; g.setFont (Fonts::mono (11.5f)); const juce::String s = ! any || f.level < -60 ? juce::String ("nothing") : (f.voiced ? "a voice, " + juce::String (juce::roundToInt (f.sure * 100)) + " % sure" : (f.sharp ? juce::String ("S or breath, left alone") : juce::String ("unsure, left alone"))); g.setColour (f.voiced ? col::phosphor : col::bone2); g.drawText (s, hears, juce::Justification::centredLeft);
        g.setColour (col::bone3); g.drawText (ed.real ("source") > 0.5f ? (ed.proc.heldNote.load() >= 0 ? "MIDI: " + tunep::noteName (ed.proc.heldNote.load()) : juce::String ("MIDI: no key held")) : ed.keyText().replace ("\n", " "), hears.translated (0, 16), juce::Justification::centredLeft);
    }
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels, under; juce::Rectangle<int> hears;
private:
    TuneEditor& ed;
};
} // namespace tuneui

using namespace tuneui;

TuneEditor::TuneEditor (TuneProcessor& p) : EffectEditor (p), proc (p)
{
    getShell().deckless = true; kept = songNow();
    {
        auto body = std::make_unique<LayoutBox>(); auto* b = body.get();
        key = &make<KeyPanel> (*b, *this); ring = &make<Ring> (*b, *this); throat = &make<Throat> (*b, *this); lineC = &make<Line> (*b, *this);
        auto* mode = &paramSeg (*b, "mode", { "Recording", "Mixing" }, { "Recording: the least delay, to hear yourself tuned while you sing.", "Mixing: the surest ear and the best sound. Delays the track, your DAW lines it up." });
        auto* voice = &paramSeg (*b, "voice", { "Low", "Middle", "High" }, { "A voice down to 65 Hz. The longest delay.", "A voice down to 100 Hz.", "A voice down to 130 Hz. The least delay." }); auto* lab = &painter (*b);
        body->layout = [this, mode, voice, lab] (juce::Rectangle<int> r)
        {
            lab->setBounds (r); lab->clear(); key->setBounds (r.getX() + 26, r.getY() + 22, 166, 300); lab->groups.push_back ({ { 26, 334, 166, 14 }, "I am" }); mode->setBounds (r.getX() + 26, r.getY() + 352, 166, 28); lab->groups.push_back ({ { 26, 390, 166, 14 }, "My voice is" }); voice->setBounds (r.getX() + 26, r.getY() + 408, 166, 28);
            ring->setBounds (r.getX() + 240, r.getY() + 4, 480, 432); throat->setBounds (r.getRight() - 170, r.getY() + 22, 140, 396); lineC->setBounds (r.getX() + 156, r.getBottom() - 88, r.getWidth() - 312, 84);
            lab->toBack();
        };
        simpleBody = std::move (body);
    }
    {
        auto body = std::make_unique<LayoutBox>(); auto* b = body.get();
        take = &make<Take> (*b, *this); glass = &make<Glass> (*b, *this); mouth = &make<Mouth> (*b, *this); source = &make<Source> (*b, *this);
        auto* from = &paramSeg (*b, "source", { "Notes", "Midi" }, { "The notes that are on.", "A held MIDI note is the target, in the octave that is sung. No key held: nothing is moved." }); auto* mode = &paramSeg (*b, "mode", { "Recording", "Mixing" }, { "Recording: the least delay.", "Mixing: the surest ear and the best sound." }); auto* voice = &paramSeg (*b, "voice", { "Low", "Middle", "High" }, { "Down to 65 Hz.", "Down to 100 Hz.", "Down to 130 Hz." });
        auto num = [&] (const char* id, const char* name, const char* help, std::function<juce::String (float)> f, bool bi) { Knob::Options o; o.name = name; o.help = help; o.format = std::move (f); o.parse = txt::parse; o.ticks = 5; o.bipolar = bi; o.detent = bi; return &knob (*b, id, std::move (o)); };
        auto* trans = num ("trans", "Transpose", "Moves the whole voice by half tones.", [] (float v) { return juce::String (juce::roundToInt (v)) + " st"; }, true); auto* mix = num ("mix", "Mix", "How much of the tuned voice, against the voice as sung.", txt::percent, false); auto* ref = num ("ref", "A4", "The tuning of the song.", [] (float v) { return juce::String (v, 1); }, false);
        body->layout = [this, from, mode, voice, trans, mix, ref] (juce::Rectangle<int> r)
        {
            r = r.reduced (12, 10); take->setBounds (r.removeFromTop (232)); r.removeFromTop (10); auto right = r.removeFromRight (206); r.removeFromRight (10); mouth->setBounds (r.removeFromRight (178)); r.removeFromRight (10); glass->setBounds (r); source->setBounds (right);
            source->labels = { { "Notes come from", { 12, 10, 180, 12 } }, { "Hears", { 12, 60, 180, 12 } } }; source->under = { { "Transpose", { 8, right.getHeight() - 16, 64, 12 } }, { "Mix", { 72, right.getHeight() - 16, 62, 12 } }, { "A4", { 134, right.getHeight() - 16, 64, 12 } } }; source->hears = { 12, 76, 184, 16 }; from->setBounds (right.getX() + 10, right.getY() + 26, 186, 26);
            mode->setBounds (right.getX() + 10, right.getY() + 114, 186, 24); voice->setBounds (right.getX() + 10, right.getY() + 142, 186, 24); int x = right.getX() + 8; for (auto* k : { trans, mix, ref }) { k->setBounds (x, right.getY() + 168, 63, right.getBottom() - right.getY() - 186); x += 63; } source->toBack();
        };
        proBody = std::move (body);
    }
    start(); getShell().setDefaultHint ("Teal is you, amber is what comes out.");
}

double TuneEditor::lag() const { return (double) proc.latencyNow.load() / juce::jmax (1.0, proc.currentRate.load()) / 0.005; }

std::array<float, 6> TuneEditor::shape() const { if (! hasOwn()) return tunep::along (real ("hard") / 100.0); std::array<float, 6> v {}; for (int i = 0; i < 6; ++i) v[(size_t) i] = real (tunep::ownIds[i]); return v; }

void TuneEditor::own (int which, float v)
{
    if (! hasOwn()) { const auto s = tunep::along (real ("hard") / 100.0); for (int i = 0; i < 6; ++i) setReal (tunep::ownIds[i], s[(size_t) i]); setReal ("own", 1); }
    setReal (tunep::ownIds[which], v);
}

void TuneEditor::line (float x) { if (hasOwn()) setReal ("own", 0); setReal ("hard", x * 100); }

float TuneEditor::linePlace() const
{
    if (! hasOwn()) return real ("hard") / 100.0f;
    const auto v = shape(); float best = 0, d = 1e9f; for (int i = 0; i <= 100; ++i) { const auto s = tunep::along (i / 100.0); const float e = std::abs (s[0] - v[0]) + 0.5f * std::abs (s[1] - v[1]) + 0.3f * std::abs (s[2] - v[2]) + 0.6f * std::abs (s[5] - v[5]); if (e < d) { d = e; best = (float) i / 100; } } return best;
}

void TuneEditor::toggleNote (int pc) { begin(); auto on = proc.notesOn(); on[(size_t) pc] = ! on[(size_t) pc]; proc.setNotes (on); end(); kept = songNow(); }
void TuneEditor::useKey (int root, bool minor) { proc.pluginState.ui.setProperty ("keyRoot", root, nullptr); proc.pluginState.ui.setProperty ("keyMinor", minor, nullptr); begin(); proc.setNotes (aemon::tune::KeyFinder::notesOf (root, minor)); end(); kept = songNow(); toast (tunep::noteName (root) + (minor ? " minor" : " major") + ". The same notes as " + tunep::noteName (minor ? root + 3 : root + 9) + (minor ? " major." : " minor.")); }
void TuneEditor::allNotes() { begin(); std::array<bool, 12> a; a.fill (true); proc.setNotes (a); end(); kept = songNow(); }

juce::String TuneEditor::keyText() const
{
    const auto on = proc.notesOn(); int n = 0; for (bool b : on) n += b; if (n == 12) return "All notes"; if (n == 0) return "No notes";
    const auto f = proc.found(); int r = -1; bool m = true; const auto& ui = proc.pluginState.ui; const bool told = ui.hasProperty ("keyRoot"); if (tunep::keyOf (on, told ? (int) ui["keyRoot"] : f.root, told ? (bool) ui["keyMinor"] : (f.root < 0 || f.sure < 0.4f || f.minor), r, m)) return tunep::noteName (r) + (m ? "\nminor" : "\nmajor"); return n == 1 ? juce::String ("One note") : "My " + juce::String (n) + " notes";
}

void TuneEditor::keyMenu (juce::Component& at)
{
    juce::PopupMenu m; juce::Component::SafePointer<TuneEditor> me (this); const auto f = proc.found(); if (f.root >= 0 && f.sure > 0.4f) { m.addItem (500, "What was heard: " + tunep::noteName (f.root) + (f.minor ? " minor" : " major")); m.addSeparator(); }
    for (int mi = 1; mi >= 0; --mi) { juce::PopupMenu s; for (int r = 0; r < 12; ++r) s.addItem (1 + r + mi * 20, tunep::noteName (r) + (mi ? " minor" : " major")); m.addSubMenu (mi ? "Minor" : "Major", s); }
    m.addSeparator(); m.addItem (100, "All notes");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (at), [me, f] (int r) { if (me == nullptr || r == 0) return; if (r == 500) me->useKey (f.root, f.minor); else if (r == 100) me->allNotes(); else me->useKey ((r - 1) % 20, r > 20); });
}

juce::String TuneEditor::sentence() const
{
    if (past.size() < 400) return {};
    int voiced = 0, sounds = 0, far = 0, unsure = 0; for (size_t i = past.size() - 400; i < past.size(); ++i) { const auto& f = past[i]; sounds += f.level > -50; voiced += f.voiced; far += f.voiced && std::abs (f.sung - f.target) > 0.62f; unsure += f.level > -50 && ! f.voiced && ! f.sharp; }
    if (real ("bypass") > 0.5f) return "Bypassed."; if (sounds < 40) return {};
    if (voiced > 80 && past.back().others > 0.3f) return "Two voices at once? Tune follows one. Give it one voice at a time.";
    if (voiced > 80 && far > voiced / 3) return "Many notes are far from the ones that are on. Is it another key?"; if (unsure > sounds * 0.6f) return "Hard to hear one voice: two at once, a room, or a beat in it?";
    return {};
}

std::vector<std::pair<juce::String, float>> TuneEditor::songNow() const { std::vector<std::pair<juce::String, float>> v; for (auto* p : proc.getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) if (tunep::belongsToSong (r->getParameterID())) v.emplace_back (r->getParameterID(), r->getValue()); return v; }

void TuneEditor::presetChosen (const Preset& chosen)
{
    // a preset is about the sound; the key, the voice and the tuning belong to the song
    if (chosen.user) { kept = songNow(); return; }
    bool put = false; for (auto& [id, v] : kept) if (auto* p = proc.apvts.getParameter (id)) if (p->getValue() != v) { p->setValueNotifyingHost (v); put = true; }
    if (put) toast (chosen.name + ". " + chosen.description + " Your key stays.");
}

void TuneEditor::tick (bool simple, int count)
{
    Frame f[256]; const int n = proc.popTune (f, 256); past.insert (past.end(), f, f + n); if ((int) past.size() > shown + 64) past.erase (past.begin(), past.begin() + ((long) past.size() - shown - 64));
    for (int i = 0; i < n; ++i)
    {
        const auto& q = f[i]; const int t = q.voiced ? juce::roundToInt (q.target) : -1;
        if (t >= 0 && t == buildingTarget) building.push_back ((q.sung - q.target) * 100); else { if (building.size() > 60) { lastNote = building; lastTarget = ((buildingTarget % 12) + 12) % 12; } building.clear(); buildingTarget = t; if (t >= 0) building.push_back ((q.sung - q.target) * 100); }
        if (building.size() > 400) { lastNote.assign (building.begin(), building.begin() + 180); lastTarget = ((buildingTarget % 12) + 12) % 12; }
    }
    if (base.pluginState.current.name == lastName || lastName.isEmpty()) kept = songNow();
    lastName = base.pluginState.current.name;
    if (simple) { ring->repaint(); lineC->repaint(); throat->repaint(); if (count % 6 == 0) key->refresh(); }
    else { take->step(); glass->repaint(); mouth->repaint(); if (count % 6 == 0) source->repaint(); }
}
