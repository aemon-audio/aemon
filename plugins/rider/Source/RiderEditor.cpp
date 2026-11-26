// Aemon Rider. AGPL-3.0-or-later.
#include "RiderEditor.h"

using namespace aemon;
using Frame = aemon::rider::Engine::Frame;

namespace
{
    juce::String msText (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String wordsText (float v) { return v < 995.0f ? msText (v) : juce::String (v / 1000.0f, 1) + " s"; }
    std::optional<float> parseMs (const juce::String& s) { auto v = txt::parse (s); if (v && s.containsChar ('s') && ! s.contains ("ms")) *v *= 1000.0f; return v; }
    juce::String speedText (float v) { return juce::String (juce::roundToInt (v)) + " dB/s"; }
    juce::String mostText (float v) { return juce::String (v, 1) + " dB"; }
    juce::String levelText (float v) { return v < -120.0f ? juce::String::charToString (0x2014) : txt::num (v, 1) + " dB"; }
}

// The window has no knobs: a fader that moves by itself, the words it carries, and the settings as sentences to edit.
namespace riderui
{
/** The words as they come in: a hollow bar where a word was, a solid one where it ends up, and the pull between them. The line is the target. */
class Stage : public juce::Component, public HelpProvider
{
public:
    explicit Stage (RiderEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return hot ? "The line: every word is brought towards it. Drag it up or down. Double-click: set it for me." : "Every word as a bar. Hollow: where it was. Solid: where it ends up. Small dots: breaths, S sounds and bleed, left alone."; }

    void step()
    {
        const float t = ed.real ("target");
        if (! dragging) centre += (t - centre) * (std::abs (t - centre) > 30.0f ? 1.0f : 0.12f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 10);
        const auto S = box();
        const float t = ed.real ("target");
        const auto& h = ed.past; const int n = (int) h.size();
        const float dx = S.getWidth() / (float) (RiderEditor::shown - 1);
        auto xa = [&] (int i) { return S.getRight() - (float) (n - 1 - i) * dx; };
        const int late = juce::roundToInt ((double) ed.proc.latencyNow.load() / ed.proc.currentRate.load() / 0.016);      // the fader is for the sound that comes out: that much later
        auto ride = [&] (int i) { return h[(size_t) juce::jmin (n - 1, i + late)].gain; };

        // the stave: a faint rule every 6 dB from the line, the line itself glowing
        for (int k = -3; k <= 2; ++k) if (k != 0) { g.setColour (col::bone1.withAlpha (0.045f)); g.fillRect (S.getX(), Y (t + 6.0f * (float) k), S.getWidth(), 1.0f); g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3.withAlpha (0.8f)); g.drawText ((k > 0 ? "+" : "") + txt::num (6.0f * (float) k, 0), juce::Rectangle<float> (30, 12).withCentre ({ S.getRight() + 20, Y (t + 6.0f * (float) k) }), juce::Justification::centred); }
        juce::Graphics::ScopedSaveState clip (g); g.reduceClipRegion (getLocalBounds().reduced (2));

        // the beat, if one is followed or connected
        juce::Path beat; bool on = false;
        for (int i = 0; i < n; ++i) { if (h[(size_t) i].beat < -70.0f) { on = false; continue; } const juce::Point<float> q (xa (i), Y (t + h[(size_t) i].beat - ed.real ("beat"))); if (on) beat.lineTo (q); else beat.startNewSubPath (q); on = true; }
        g.setColour (col::lilac.withAlpha (ed.real ("follow") > 0.5f ? 0.75f : 0.25f)); g.strokePath (beat, juce::PathStrokeType (1.3f));

        const float y0 = Y (t);
        g.setColour (col::ember.withAlpha (0.07f)); g.fillRect (S.getX(), y0 - 9, S.getWidth() + 8, 18.0f);
        g.setColour (hot || dragging ? col::emberHi : col::ember); g.fillRect (S.getX(), y0 - 0.75f, S.getWidth() + 8, 1.5f);

        // dots: what is heard and left alone
        for (int i = 0; i < n; i += 4)
        {
            const auto& f = h[(size_t) i];
            if (f.voiced || f.raw < -62.0f || xa (i) < 96.0f) continue;
            const float r = juce::jlimit (0.8f, 2.4f, 2.4f + (f.raw - t) * 0.06f);
            g.setColour (col::bone3.withAlpha (0.75f)); g.fillEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre ({ xa (i), getHeight() - 16.0f }));
        }
        // words
        for (int i = 0; i < n;)
        {
            if (! h[(size_t) i].voiced) { ++i; continue; }
            int e = i; double before = 0, after = 0; int c = 0;
            while (e < n && e - i < 60 && (h[(size_t) e].voiced || (e + 1 < n && h[(size_t) e + 1].voiced)))
            {
                const auto& f = h[(size_t) e];
                if (c > 8 && std::abs (f.raw - 10.0 * std::log10 (before / c)) > 5.0 && f.voiced && e + 1 < n && std::abs (h[(size_t) e + 1].raw - f.raw) < 2.5) break;      // another syllable
                if (f.voiced) { before += std::pow (10.0, f.raw / 10.0); after += std::pow (10.0, (f.raw + ride (e)) / 10.0); ++c; }
                ++e;
            }
            e = juce::jmax (e, i + 1);
            if (c > 1)
            {
                const float was = 10.0f * (float) std::log10 (before / c), is = 10.0f * (float) std::log10 (after / c), x0 = xa (i) + 1, w = juce::jmax (5.0f, (float) (e - i) * dx - 3.0f), cx = x0 + w / 2;
                const float yw = Y (was), yi = Y (is), move = is - was;
                const float fade = juce::jlimit (0.25f, 1.0f, (x0 - S.getX()) / 60.0f);
                if (std::abs (move) > 0.3f)
                {
                    juce::ColourGradient pull (col::ember.withAlpha (0.0f), cx, yw, col::ember.withAlpha (0.38f * fade), cx, yi, false);
                    g.setGradientFill (pull); g.fillRect (juce::Rectangle<float> (x0, std::min (yw, yi), w, std::abs (yi - yw)));
                }
                g.setColour (col::phosphor.withAlpha (0.65f * fade)); g.drawRoundedRectangle (x0, yw - 3.0f, w, 6.0f, 3.0f, 1.1f);
                g.setColour (col::bone1.withAlpha (fade)); g.fillRoundedRectangle (x0, yi - 3.5f, w, 7.0f, 3.5f);
                if (std::abs (move) > 0.7f && w > 24.0f)
                {
                    g.setFont (Fonts::mono (10.0f)); g.setColour (col::emberHi.withAlpha (fade));
                    g.drawText ((move > 0 ? "+" : "") + txt::num (move, 1), juce::Rectangle<float> (46, 12).withCentre ({ cx, yw + (move > 0 ? 12.0f : -12.0f) }), juce::Justification::centred);
                }
            }
            i = e;
        }
        // the handle of the line
        auto handle = juce::Rectangle<float> (58, 18).withCentre ({ S.getX() + 14, y0 });
        g.setColour (hot || dragging ? col::emberHi : col::ember); g.fillRoundedRectangle (handle, 9);
        g.setFont (Fonts::mono (11.0f)); g.setColour (col::basalt0); g.drawText (txt::num (t, 1), handle, juce::Justification::centred);
        drawEngraved (g, "left alone", { 14, (float) getHeight() - 22, 80, 12 }, 9.0f, col::bone3.withAlpha (0.8f), juce::Justification::centredLeft);
        if (n == 0) { g.setFont (Fonts::text (14.0f)); g.setColour (col::bone3); g.drawText ("Play a vocal. Every word shows up here as a bar.", b, juce::Justification::centred); }
    }

    void mouseMove (const juce::MouseEvent& e) override { const bool in = near (e.position); if (in != hot) { hot = in; setMouseCursor (in ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); } }
    void mouseExit (const juce::MouseEvent&) override { hot = false; }
    void mouseDown (const juce::MouseEvent& e) override { dragging = near (e.position); if (dragging) { ed.begin(); from = ed.real ("target"); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) ed.setReal ("target", juce::jlimit (-48.0f, 0.0f, from - (float) e.getDistanceFromDragStartY() / perDb() * (e.mods.isShiftDown() ? 0.2f : 1.0f))); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) ed.end(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override { ed.setForMe(); }

private:
    juce::Rectangle<float> box() const { return { 16, 14, (float) getWidth() - 56, (float) getHeight() - 46 }; }
    float perDb() const { return box().getHeight() / 36.0f; }
    float Y (float d) const { return juce::jlimit (6.0f, (float) getHeight() - 30.0f, box().getY() + (centre + 15.0f - d) * perDb()); }
    bool near (juce::Point<float> p) const { return std::abs (p.y - Y (ed.real ("target"))) < 10.0f; }
    RiderEditor& ed;
    float centre = -20, from = 0; bool hot = false, dragging = false;
};

/** The hand on the fader: it moves by itself, between two stops you set. */
class Fader : public juce::Component, public HelpProvider
{
public:
    explicit Fader (RiderEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return over == 1 ? "The upper stop: the furthest the voice is lifted. Drag it." : (over == 2 ? "The lower stop: the furthest the voice is turned down. Drag it." : "The fader. It moves by itself, never past its stops and never faster than you allow."); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // the strip: brushed, like the decks of the other plugins
        g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff282b34), b.getY(), juce::Colour (0xff1f2128), b.getBottom())); g.fillRoundedRectangle (b, 10);
        g.setColour (juce::Colours::white.withAlpha (0.013f)); for (float x = 4; x < b.getRight() - 4; x += 3) g.fillRect (x, 1.0f, 1.0f, b.getHeight() - 2);
        g.setColour (juce::Colour (0xff0b0c0f)); g.drawRoundedRectangle (b.reduced (0.5f), 10, 1.0f);
        const auto T = track(); const float cx = T.getCentreX(), up = ed.real ("up"), down = ed.real ("down"), now = ed.past.empty() ? 0.0f : ed.past.back().gain;
        const bool play = ed.real ("rideMode") > 1.5f, write = ed.proc.isWriting();
        drawEngraved (g, play ? "played by the DAW" : (write ? "writing" : "riding"), { 0, 14, b.getWidth(), 14 }, 10.0f, write ? col::sigClip : (play ? col::lilac : col::bone2), juce::Justification::centred);
        g.setFont (Fonts::mono (26.0f)); g.setColour (col::bone1); g.drawText ((now > 0.05f ? "+" : "") + txt::num (now, 1), juce::Rectangle<float> (0, 30, b.getWidth(), 32), juce::Justification::centred);

        for (int d = -12; d <= 12; d += 1)
        {
            const float y = Y ((float) d); const bool big = d % 6 == 0, mid = d % 3 == 0;
            g.setColour (col::bone1.withAlpha (big ? 0.35f : (mid ? 0.18f : 0.09f))); const float w = big ? 16.0f : (mid ? 11.0f : 7.0f);
            g.fillRect (cx - 22 - w, y, w, 1.0f); g.fillRect (cx + 22, y, w, 1.0f);
            if (big) { g.setFont (Fonts::mono (10.0f)); g.setColour (col::bone2); g.drawText ((d > 0 ? "+" : "") + txt::num ((float) d, 0), juce::Rectangle<float> (cx - 80, y - 6, 34, 12), juce::Justification::centredRight); }
        }
        // the slot, lit where the fader may go
        g.setColour (col::groove); g.fillRoundedRectangle (cx - 4, T.getY() - 8, 8, T.getHeight() + 16, 4);
        g.setColour (col::ember.withAlpha (0.16f)); g.fillRoundedRectangle (cx - 3, Y (up), 6, Y (-down) - Y (up), 3);
        g.setColour (col::ember.withAlpha (0.85f)); { const float a = Y (0), y = Y (now); g.fillRect (cx - 2, std::min (a, y), 4.0f, std::abs (y - a)); }
        // where it has just been
        const auto& h = ed.past; float lo = 0, hi = 0; for (size_t i = h.size() > 120 ? h.size() - 120 : 0; i < h.size(); ++i) { lo = std::min (lo, h[i].gain); hi = std::max (hi, h[i].gain); }
        g.setColour (col::ember.withAlpha (0.35f)); g.fillRect (cx + 8, Y (hi), 2.0f, Y (lo) - Y (hi));
        // stops
        for (int k = 1; k <= 2; ++k)
        {
            const float y = Y (k == 1 ? up : -down), dir = k == 1 ? -1.0f : 1.0f; const bool lit = over == k || held == k;
            juce::Path p; p.startNewSubPath (cx + 14, y); p.lineTo (cx + 30, y); p.lineTo (cx + 30, y + 13 * dir); p.lineTo (cx + 22, y + 13 * dir); p.closeSubPath();
            g.setColour (lit ? col::emberHi : col::ember); g.fillPath (p); g.fillRect (cx - 14, y - 0.75f, 44.0f, 1.5f);
            g.setFont (Fonts::mono (11.0f)); g.drawText ((k == 1 ? "+" : txt::minusSign()) + juce::String (k == 1 ? up : down, 1), juce::Rectangle<float> (cx + 35, y + 6 * dir - 7, 44, 14), juce::Justification::centredLeft);
        }
        // the cap
        auto cap = juce::Rectangle<float> (58, 34).withCentre ({ cx, Y (now) });
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (cap.translated (0, 5).expanded (2), 6);
        juce::ColourGradient body (juce::Colour (0xff4a4f5c), cap.getX(), cap.getY(), juce::Colour (0xff191b21), cap.getX(), cap.getBottom(), false);
        body.addColour (0.46, juce::Colour (0xff2c2f38)); body.addColour (0.5, juce::Colour (0xff15171c)); body.addColour (0.56, juce::Colour (0xff353944));
        g.setGradientFill (body); g.fillRoundedRectangle (cap, 5);
        g.setColour (juce::Colours::white.withAlpha (0.10f)); g.drawRoundedRectangle (cap.reduced (0.5f), 5, 1.0f);
        for (float k : { -11.0f, -7.0f, 7.0f, 11.0f }) { g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (cap.getX() + 6, cap.getCentreY() + k, cap.getWidth() - 12, 1.0f); }
        const bool voice = ! h.empty() && h.back().voiced;
        g.setColour (voice ? col::emberHi : col::ember.withAlpha (0.7f)); g.fillRect (cap.getX() + 4, cap.getCentreY() - 1.0f, cap.getWidth() - 8, 2.0f);
        if (voice) { g.setColour (col::ember.withAlpha (0.22f)); g.fillRect (cap.getX() + 2, cap.getCentreY() - 4.0f, cap.getWidth() - 4, 8.0f); }
        // is it hearing a voice?
        const auto lamp = juce::Rectangle<float> (7, 7).withCentre ({ cx - 34, b.getBottom() - bottomRoom + 12 });
        g.setColour (voice ? col::phosphor : col::bone3.withAlpha (0.4f)); g.fillEllipse (lamp); if (voice) { g.setColour (col::phosphor.withAlpha (0.2f)); g.fillEllipse (lamp.expanded (4)); }
        drawEngraved (g, voice ? "voice" : "holding", { cx - 24, lamp.getCentreY() - 7, 80, 14 }, 9.5f, voice ? col::phosphor : col::bone3, juce::Justification::centredLeft);
    }
    void mouseMove (const juce::MouseEvent& e) override { const int o = hit (e.position); if (o != over) { over = o; setMouseCursor (o ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); } }
    void mouseExit (const juce::MouseEvent&) override { over = 0; }
    void mouseDown (const juce::MouseEvent& e) override { held = hit (e.position); if (held) { ed.begin(); from = ed.real (held == 1 ? "up" : "down"); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (held) { const float d = (float) e.getDistanceFromDragStartY() / track().getHeight() * 24.0f; ed.setReal (held == 1 ? "up" : "down", juce::jlimit (0.0f, 12.0f, std::round ((from + (held == 1 ? -d : d)) * 10.0f) / 10.0f)); } }
    void mouseUp (const juce::MouseEvent&) override { if (held) ed.end(); held = 0; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (const int k = hit (e.position)) ed.change (k == 1 ? "up" : "down", 6.0f); }
    float bottomRoom = 40;

private:
    juce::Rectangle<float> track() const { return { 0, 92, (float) getWidth(), (float) getHeight() - 92 - bottomRoom - 30 }; }
    float Y (float d) const { const auto T = track(); return T.getCentreY() - juce::jlimit (-12.0f, 12.0f, d) / 24.0f * T.getHeight(); }
    int hit (juce::Point<float> p) const { if (p.x < getWidth() * 0.5f + 8) return 0; const float a = std::abs (p.y - (Y (ed.real ("up")) - 6)), c = std::abs (p.y - (Y (-ed.real ("down")) + 6)); return a < 13 && a <= c ? 1 : (c < 13 ? 2 : 0); }
    RiderEditor& ed; int over = 0, held = 0; float from = 0;
};

/** The settings, as sentences. Lit values are dragged up and down, turned with the wheel, typed after a click, reset with a double-click. */
class Sentence : public juce::Component, public HelpProvider
{
public:
    struct Token { juce::String text, id, help; std::function<juce::String (float)> fmt; std::function<std::optional<float> (const juce::String&)> parse; int choices = 0; };
    struct Line { juce::String heading; std::vector<Token> tokens; };
    Sentence (RiderEditor& e, float textHeight, float lineHeight, float headWidth) : ed (e), th (textHeight), lh (lineHeight), hw (headWidth) {}
    std::vector<Line> lines;
    float gap = 6;
    /** Called when a value is changed by hand. */
    std::function<void (const juce::String&, float before, float now)> changed;
    juce::String getHelpText() const override { return over >= 0 ? places[(size_t) over].t->help : juce::String ("The settings, in words. Drag a lit value up or down, click it to type, double-click to reset."); }

    void refresh() { juce::String now; for (auto& l : lines) for (auto& t : l.tokens) if (t.id.isNotEmpty()) now << textOf (t) << "|"; if (now != last) { last = now; layoutText(); repaint(); } }
    void resized() override { layoutText(); }
    void paint (juce::Graphics& g) override
    {
        for (auto& h : heads) drawEngraved (g, h.second, h.first, 10.0f, col::bone3, juce::Justification::centredLeft);
        for (size_t i = 0; i < places.size(); ++i)
        {
            const auto& p = places[i]; const bool v = p.t->id.isNotEmpty(), hotNow = v && (over >= 0 && places[(size_t) over].t == p.t);
            if (v && hotNow) { g.setColour (col::ember.withAlpha (0.14f)); g.fillRoundedRectangle (p.r.expanded (4, -1), 5); }
            g.setFont (v ? Fonts::textMedium (th) : Fonts::text (th)); g.setColour (v ? (hotNow ? col::emberHi : col::ember) : col::bone2);
            g.drawText (p.s, p.r, juce::Justification::centredLeft, false);
            if (v) { g.setColour (col::ember.withAlpha (0.4f)); for (float x = p.r.getX(); x < p.r.getRight() - 2; x += 4) g.fillRect (x, p.r.getBottom() - lh * 0.16f, 2.0f, 1.0f); }
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int o = hit (e.position); if (o != over) { over = o; setMouseCursor (o < 0 ? juce::MouseCursor::NormalCursor : (places[(size_t) o].t->choices > 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor)); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { if (held == nullptr) { over = -1; repaint(); } }
    void mouseDown (const juce::MouseEvent& e) override { closeTyping (false); const int o = hit (e.position); held = o >= 0 ? places[(size_t) o].t : nullptr; heldBox = o >= 0 ? places[(size_t) o].r : juce::Rectangle<float>(); moved = false; if (held != nullptr) { auto* p = ed.base.apvts.getParameter (held->id); norm = p->getValue(); lastY = e.position.y; } }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (held == nullptr || held->choices > 0) return;
        if (! moved) { if (std::abs (e.getDistanceFromDragStartY()) < 3) return; moved = true; ed.begin(); }
        norm = juce::jlimit (0.0f, 1.0f, norm + (lastY - e.position.y) / 180.0f * (e.mods.isShiftDown() ? 0.2f : 1.0f)); lastY = e.position.y; put (norm);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (held == nullptr) return;
        if (moved) ed.end();
        else if (held->choices > 0) { ed.begin(); ed.setReal (held->id, (float) ((juce::roundToInt (ed.real (held->id.toRawUTF8())) + 1) % held->choices)); ed.end(); }
        else if (e.getNumberOfClicks() == 1) { pending = held; pendingBox = heldBox; juce::Component::SafePointer<Sentence> me (this); juce::Timer::callAfterDelay (260, [me] { if (me != nullptr && me->pending != nullptr) me->type(); }); }
        held = nullptr;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { pending = nullptr; const int o = hit (e.position); if (o < 0 || places[(size_t) o].t->choices > 0) return; held = places[(size_t) o].t; ed.begin(); put (ed.base.apvts.getParameter (held->id)->getDefaultValue()); ed.end(); held = nullptr; }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { const int o = hit (e.position); if (o < 0 || places[(size_t) o].t->choices > 0) return; held = places[(size_t) o].t; ed.begin(); put (juce::jlimit (0.0f, 1.0f, ed.base.apvts.getParameter (held->id)->getValue() + w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.1f))); ed.end(); held = nullptr; }
    bool drag (const juce::String& id, int px)
    {
        for (auto& p : places) if (p.t->id == id) { if (px == 0) return true; held = p.t; norm = ed.base.apvts.getParameter (id)->getValue(); ed.begin(); put (juce::jlimit (0.0f, 1.0f, norm + (float) px / 180.0f)); ed.end(); held = nullptr; return true; }
        return false;
    }
    juce::String plainText() const { juce::String s; for (auto& p : places) s << p.s; return s; }

private:
    struct Place { juce::Rectangle<float> r; juce::String s; const Token* t; };
    juce::String textOf (const Token& t) const { return t.id.isEmpty() ? t.text : t.fmt (ed.real (t.id.toRawUTF8())); }
    void put (float n) { auto* p = ed.base.apvts.getParameter (held->id); const float before = ed.real (held->id.toRawUTF8()); ed.setReal (held->id, p->convertFrom0to1 (n)); if (changed) changed (held->id, before, ed.real (held->id.toRawUTF8())); }
    void layoutText()
    {
        places.clear(); heads.clear(); float y = 0;
        for (auto& l : lines)
        {
            float x = hw; if (l.heading.isNotEmpty()) heads.push_back ({ { 0, y, hw - 10, lh }, l.heading });
            for (auto& t : l.tokens)
            {
                const auto font = t.id.isNotEmpty() ? Fonts::textMedium (th) : Fonts::text (th);
                // plain text breaks between its words, a value never breaks
                juce::StringArray parts; if (t.id.isNotEmpty()) parts.add (textOf (t)); else { juce::String word; for (auto c : t.text) { word << juce::String::charToString (c); if (c == ' ') { parts.add (word); word.clear(); } } if (word.isNotEmpty()) parts.add (word); }
                for (auto& s : parts)
                {
                    const float w = juce::GlyphArrangement::getStringWidth (font, s.endsWithChar (' ') ? s.trimEnd() : s), full = juce::GlyphArrangement::getStringWidth (font, s + ".") - juce::GlyphArrangement::getStringWidth (font, ".");
                    if (x + w > (float) getWidth() && x > hw + 1) { x = hw; y += lh; }
                    places.push_back ({ { x, y, full + 2, lh }, s, &t }); x += full;
                }
            }
            y += lh + gap;
        }
        if (over >= (int) places.size()) over = -1;
    }
    int hit (juce::Point<float> p) const { for (size_t i = 0; i < places.size(); ++i) if (places[i].t->id.isNotEmpty() && places[i].r.expanded (3, 0).contains (p)) return (int) i; return -1; }
    void type()
    {
        typing = std::exchange (pending, nullptr);
        editor = std::make_unique<juce::TextEditor>(); editor->setFont (Fonts::mono (th * 0.85f)); editor->setJustification (juce::Justification::centred);
        editor->setBounds (pendingBox.toNearestInt().withSizeKeepingCentre (juce::jmax (90, (int) pendingBox.getWidth() + 10), juce::roundToInt (lh) - 2).constrainedWithin (getLocalBounds()));
        editor->setText (textOf (*typing).upToFirstOccurrenceOf ("dB", false, true).trim(), false); editor->selectAll();
        editor->onReturnKey = [this] { closeTyping (true); }; editor->onEscapeKey = [this] { closeTyping (false); }; editor->onFocusLost = [this] { closeTyping (true); };
        addAndMakeVisible (*editor); editor->grabKeyboardFocus();
    }
    void closeTyping (bool take)
    {
        if (editor == nullptr) return;
        auto gone = std::move (editor); const auto* t = std::exchange (typing, nullptr);
        if (take && t != nullptr) if (auto v = t->parse ? t->parse (gone->getText()) : txt::parse (gone->getText())) { auto* p = ed.base.apvts.getParameter (t->id); const auto range = p->getNormalisableRange(); held = t; ed.begin(); put (p->convertTo0to1 (range.snapToLegalValue (juce::jlimit (range.start, range.end, *v)))); ed.end(); held = nullptr; }
    }
    RiderEditor& ed; const float th, lh, hw;
    std::vector<Place> places; std::vector<std::pair<juce::Rectangle<float>, juce::String>> heads;
    juce::String last; int over = -1; const Token *held = nullptr, *pending = nullptr, *typing = nullptr; juce::Rectangle<float> heldBox, pendingBox; bool moved = false; float norm = 0, lastY = 0;
    std::unique_ptr<juce::TextEditor> editor;
};

/** Says one thing: what is off and how to fix it, or else what the fader is doing. */
class Speaker : public juce::Component
{
public:
    Speaker() { addChildComponent (fix); fix.small = true; fix.onClick = [this] { if (row.fix) { auto f = row.fix; f(); } }; setHelp (*this, "What the rider is doing, in plain words, with a fix when something is off."); }
    void say (const CheckCards::Row& r) { row = r; fix.setVisible ((bool) r.fix); if (fix.getButtonText() != r.fixLabel) fix.setButtonText (r.fixLabel); repaint(); }
    void resized() override { fix.setBounds (getLocalBounds().removeFromRight (150).withSizeKeepingCentre (150, 24)); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const auto c = row.s == CheckCards::S::ok ? col::sigOk : (row.s == CheckCards::S::warn ? col::sigHot : col::sigClip);
        const auto lamp = juce::Rectangle<float> (9, 9).withCentre ({ 9.0f, b.getCentreY() });
        g.setColour (c.withAlpha (0.22f)); g.fillEllipse (lamp.expanded (4)); g.setColour (c); g.fillEllipse (lamp);
        auto r = b.withTrimmedLeft (24).withTrimmedRight (row.fix ? 160.0f : 0.0f);
        g.setFont (Fonts::textMedium (14.0f)); g.setColour (col::bone1);
        const float w = juce::GlyphArrangement::getStringWidth (Fonts::textMedium (14.0f), row.title) + 10;
        g.drawText (row.title, r.removeFromLeft (w), juce::Justification::centredLeft);
        g.setFont (Fonts::text (13.0f)); g.setColour (col::bone2); g.drawFittedText (row.text, r.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
    }
private:
    CheckCards::Row row; Button fix { "" };
};
} // namespace riderui

using namespace riderui;

namespace
{
    juce::String wayText (float v) { return v < 0.5f ? juce::String ("not at all") : (v > 99.5f ? juce::String ("all the way") : txt::percent (v) + " of the way"); }
    Sentence::Token say (const char* t) { Sentence::Token k; k.text = t; return k; }
    Sentence::Token val (const char* id, std::function<juce::String (float)> f, const char* help, std::function<std::optional<float> (const juce::String&)> parse = {}) { Sentence::Token k; k.id = id; k.fmt = std::move (f); k.help = help; k.parse = std::move (parse); return k; }
    Sentence::Token pick (const char* id, juce::StringArray names, const char* help) { Sentence::Token k; k.id = id; k.help = help; k.choices = names.size(); k.fmt = [names] (float v) { return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (v))]; }; return k; }
    const char* evenHelp = "How much of the difference between a word and the line is taken away, as far as the stops and the speed allow.";
    const char* followHelp = "When the beat gets louder, the voice goes up with it, and down again. Needs the beat on the sidechain.";
}

RiderEditor::RiderEditor (RiderProcessor& p) : EffectEditor (p), proc (p)
{
    getShell().deckless = true;
    build (true, simpleParts, simpleBody);
    build (false, proParts, proBody);
    start();
}

void RiderEditor::setForMe()
{
    const auto h = listen();
    if (! h.voice.ok) { toast ("Not enough voice heard in the last seconds. Play some vocal and try again."); return; }
    const float most = juce::jlimit (3.0f, 8.0f, std::round ((float) h.voice.spread * 1.5f));
    begin();
    setReal ("target", (float) h.voice.target); setReal ("even", juce::jmax (80.0f, real ("even"))); setReal ("speed", juce::jmax (24.0f, real ("speed"))); setReal ("up", most); setReal ("down", most);
    if (h.beatOk) setReal ("beat", (float) h.beat);
    end();
    toast ("Set to this voice: target " + txt::num ((float) h.voice.target, 1) + " dB, at most " + mostText (most) + " either way.");
}

void RiderEditor::gentler()
{
    begin();
    setReal ("even", std::round (real ("even") * 0.75f)); for (auto id : { "up", "down" }) if (real (id) > 1.0f) setReal (id, juce::jmax (1.0f, real (id) - 1.0f)); setReal ("speed", juce::jmax (2.0f, std::round (real ("speed") * 0.75f)));
    end();
    toast ("A quarter less, a little slower, 1 dB less far.");
}

void RiderEditor::followBeat()
{
    if (! proc.sidechainConnected) { toast ("No beat connected. Send the beat to this plugin's sidechain input in your DAW."); return; }
    const auto h = listen();
    if (! h.beatOk) { toast ("The sidechain is connected but silent. Play the song and try again."); return; }
    begin(); setReal ("beat", (float) h.beat); setReal ("follow", juce::jmax (50.0f, real ("follow"))); end();
    toast ("Follows the beat. Its level over the last seconds, " + txt::num ((float) h.beat, 1) + " dB, counts as normal.");
}

void RiderEditor::presetChosen (const Preset& chosen)
{
    if (chosen.user || ! riderp::aims (chosen.name)) return;
    const auto h = listen();
    if (! h.voice.ok && ! h.beatOk) return;      // nothing heard yet: the preset's own numbers stay
    begin();
    if (h.voice.ok) setReal ("target", (float) h.voice.target);
    if (h.beatOk) setReal ("beat", (float) h.beat);
    end();
    if (h.voice.ok) toast (chosen.name + ": set to this voice. " + chosen.description);
}

void RiderEditor::build (bool simple, Parts& parts, std::unique_ptr<juce::Component>& target)
{
    auto body = std::make_unique<LayoutBox>();
    parts.stage = &make<Stage> (*body, *this);
    parts.fader = &make<Fader> (*body, *this);
    parts.speaker = &make<Speaker> (*body);
    parts.sentence = &make<Sentence> (*body, *this, simple ? 20.0f : 14.5f, simple ? 31.0f : 23.0f, simple ? 0.0f : 92.0f);
    auto& s = *parts.sentence;
    auto parseMsFn = [] (const juce::String& t) { return parseMs (t); };
    const auto level = [] (float v) { return txt::num (v, 1) + " dB"; };
    const auto speedWords = [] (float v) { return juce::String (juce::roundToInt (v)) + " dB a second"; };
    if (simple)
    {
        s.gap = 4;
        s.lines = { { {}, { say ("Bring every word to "), val ("target", level, "The line: the level every word is brought to."), say (", "), val ("even", wayText, evenHelp),
                          say (". Move the fader at most "), val ("up", [this] (float v) { const float d = real ("down"); return std::abs (v - d) < 0.05f ? mostText (v) + " either way" : mostText (v) + " up and " + mostText (d) + " down"; }, "The furthest the voice is moved. The stops on the fader set up and down apart."),
                          say (", never faster than "), val ("speed", speedWords, "The fastest the fader moves. It never goes faster, whatever comes in."), say (". Let the voice follow the beat "), val ("follow", wayText, followHelp), say (".") } } };
        s.changed = [this] (const juce::String& id, float before, float v) { if (id == "up" && std::abs (before - real ("down")) < 0.05f) setReal ("down", v); };      // a one-way preset stays one-way
    }
    else
    {
        s.gap = 3;
        s.lines = { { "Level", { say ("Bring the words to "), val ("target", level, "The line: the level every word is brought to."), say (", "), val ("even", wayText, evenHelp), say (". A word is about "), val ("words", wordsText, "How much voice counts as one level. Small: syllables. Large: whole phrases.", parseMsFn), say (" of voice.") } },
                    { "Fader", { say ("Lift at most "), val ("up", mostText, "The furthest the voice is lifted."), say (", lower at most "), val ("down", mostText, "The furthest the voice is turned down."), say (", never faster than "), val ("speed", speedWords, "The fastest the fader moves. It never goes faster, whatever comes in."), say (". Look "), val ("look", msText, "How early it moves, so the first word of a phrase is ready. Adds this much delay. 0 for recording."), say (" ahead, and in gaps "), pick ("gaps", { "stay where it is", "go back to zero" }, "What the fader does when nobody sings. Click to change."), say (".") } },
                    { "Listening", { say ("Listen to "), pick ("hears", { "clear voice only", "everything" }, "Clear voice: breaths, S sounds and whispers are left alone. Everything: instruments, stacks, whispers too. Click to change."), say (", and leave alone what is more than "), val ("below", [] (float v) { return juce::String (juce::roundToInt (v)) + " dB"; }, "Anything this far under the line is ignored: bleed, room, quiet breaths."), say (" under the line.") } },
                    { "Beat", { say ("Follow the beat "), val ("follow", wayText, followHelp), say (". It counts as normal at "), val ("beat", level, "The beat level that counts as normal. Louder than this and the voice goes up."), say (".") } } };
    }
    juce::Component *a = &button (*body, "Set it for me", "listens", "Listens back over the last seconds and sets the line and the stops to this voice.", [this] { setForMe(); }),
                    *b = &button (*body, "Follow the beat", "sidechain", "Measures the beat on the sidechain and lets the voice move with it.", [this] { followBeat(); }),
                    *c = &button (*body, "Gentler", "less, slower", "A quarter less, a little slower, 1 dB less far.", [this] { gentler(); });
    SegControl* mode = simple ? nullptr : &paramSeg (*body, "rideMode", { "Ride", "Write", "Play" }, { "Rides the voice as it comes.", "Rides, and writes its moves to the parameter called Ride while the DAW plays. Arm that parameter for writing in your DAW.", "The written parameter is the fader. Edit the curve in your DAW." });
    if (! simple) parts.fader->bottomRoom = 76;
    body->layout = [=] (juce::Rectangle<int> r)
    {
        r = r.reduced (12);
        auto f = r.removeFromRight (196); r.removeFromRight (12);
        parts.fader->setBounds (f);
        if (mode != nullptr) mode->setBounds (f.removeFromBottom (44).reduced (12, 9));
        auto buttons = r.removeFromBottom (30); r.removeFromBottom (10);
        const int w = (buttons.getWidth() - 20) / 3; a->setBounds (buttons.removeFromLeft (w)); buttons.removeFromLeft (10); b->setBounds (buttons.removeFromLeft (w)); buttons.removeFromLeft (10); c->setBounds (buttons);
        parts.speaker->setBounds (r.removeFromBottom (34)); r.removeFromBottom (8);
        parts.sentence->setBounds (r.removeFromBottom (simple ? 98 : 122).reduced (6, 0)); r.removeFromBottom (12);
        parts.stage->setBounds (r);
    };
    target = std::move (body);
}

void RiderEditor::updateCheck (bool simple)
{
    using S = CheckCards::S;
    std::array<CheckCards::Row, 4> r;
    int voiced = 0, capped = 0, pitchless = 0, breaths = 0; float lift = 0, lo = 99, hi = -99; std::vector<float> in;
    for (auto& f : past)
    {
        if (f.voiced) { ++voiced; capped += f.capped; in.push_back (f.level); lo = std::min (lo, f.gain); hi = std::max (hi, f.gain); }
        else if (f.raw > -60.0f) { ++pitchless; if (f.raw > real ("target") - real ("below") - 15.0f) { ++breaths; lift += f.gain; } }
    }
    std::sort (in.begin(), in.end());
    const float mid = in.empty() ? -200.0f : in[in.size() / 2], target = real ("target");
    const auto fix = [this] { setForMe(); };
    const bool doing = real ("even") > 0.5f || real ("follow") > 0.5f, playMode = real ("rideMode") > 1.5f;
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (playMode) r[0] = { S::ok, "The DAW holds the fader", "The written ride is played. Nothing is measured.", "Ride again", [this] { setReal ("rideMode", 0); } };
    else if (! doing) r[0] = { S::warn, "Nothing is ridden", "How even is at zero. The sound is untouched.", "Set it for me", fix };
    else if (voiced < 30) r[0] = pitchless > 120 ? CheckCards::Row { S::warn, "Hears no voice", "What plays is too quiet for the target, or is not a clear voice.", "Set it for me", fix } : CheckCards::Row { S::ok, "Waiting for a voice", "Play some vocal." };
    else if (real ("even") > 0.5f && std::abs (mid - target) > 8.0f) r[0] = { S::warn, mid < target ? "Voice under the target" : "Voice over the target", "It sits " + juce::String (juce::roundToInt (std::abs (mid - target))) + " dB away, so the fader leans one way.", "Set it for me", fix };
    else r[0] = { S::ok, "Riding", "Between " + txt::db (lo) + " and " + txt::db (hi) + " over the last seconds." };

    const float share = voiced > 0 ? (float) capped / (float) voiced : 0.0f;
    if (! doing || playMode || voiced < 30) r[1] = { S::ok, "Limits", "Never faster than " + speedText (real ("speed")) + ", never further than you allow." };
    else if (share > 0.35f && juce::jmin (real ("up"), real ("down")) < 9.0f) r[1] = { S::warn, "Often at its limit", juce::String (juce::roundToInt (share * 100)) + " % of the time it wants to go further.", "Allow 2 dB more", [this] { begin(); for (auto id : { "up", "down" }) if (real (id) > 0.0f) setReal (id, juce::jmin (12.0f, real (id) + 2.0f)); end(); } };
    else r[1] = { S::ok, "Within its limits", "At the limit " + juce::String (juce::roundToInt (share * 100)) + " % of the time. Never faster than " + speedText (real ("speed")) + "." };

    const float avg = breaths > 0 ? lift / (float) breaths : 0.0f;
    if (real ("hears") > 0.5f) r[2] = { S::ok, "Rides everything", "Breaths and S sounds count too.", "Voice only", [this] { change ("hears", 0); } };
    else if (breaths > 20 && avg > 4.0f && real ("gaps") < 0.5f && doing && ! playMode) r[2] = { S::warn, "Breaths come up with it", "The fader holds between words, about " + txt::db (avg) + ".", "Return in gaps", [this] { change ("gaps", 1); } };
    else r[2] = { S::ok, "Breaths and gaps", pitchless > 20 ? "Breaths, S sounds and gaps do not move the fader." : "Nothing but voice so far." };

    // peaks: the DAW lets them pass full scale, an export does not
    float peak = -120; for (auto& f : history) peak = std::max (peak, f.outDb);
    if (peak > 0.0f && r[1].s == S::ok) r[1] = { S::warn, "Peaks over full scale", "Up to " + txt::db (peak) + " after the ride. Lift less, or lower the track.", "Lift less", [this] { change ("up", juce::jmax (0.0f, real ("up") - 2.0f)); } };
    const bool side = proc.sidechainConnected.load();
    if (proc.isWriting()) r[3] = { S::ok, "Writing the ride", "The moves go to the parameter called Ride. Arm it for writing in your DAW.", "Stop, play it back", [this] { setReal ("rideMode", 2); } };
    else if (real ("rideMode") > 0.5f && ! playMode) r[3] = { S::warn, "Ready to write", "Press play in your DAW.", "Back to Ride", [this] { setReal ("rideMode", 0); } };
    else if (real ("follow") > 0.5f && ! side) r[3] = { S::warn, "No beat connected", "Follow the beat needs the beat on the sidechain input.", "Follow off", [this] { change ("follow", 0); } };
    else if (real ("follow") > 0.5f)
    {
        float bt = -200; for (auto& f : past) bt = std::max (bt, f.beat);
        if (bt > -100 && std::abs (bt - real ("beat")) > 12.0f) r[3] = { S::warn, "Beat level is off", "The beat is far from the level that counts as normal.", "Measure it", [this] { followBeat(); } };
        else r[3] = { S::ok, "Follows the beat", juce::String (juce::roundToInt (real ("follow"))) + " % of its moves. Breaks are not followed." };
    }
    else r[3] = side ? CheckCards::Row { S::ok, "Beat connected", "Not followed yet.", "Follow it", [this] { followBeat(); } } : CheckCards::Row { S::ok, "No beat", "Connect one to the sidechain to let the voice follow it." };
    // one thing is said at a time: what is wrong, or else what it is doing
    size_t say = 0; for (size_t i = 0; i < 4; ++i) if ((int) r[i].s > (int) r[say].s) say = i;
    if (r[say].s == S::ok && proc.isWriting()) say = 3;
    (simple ? simpleParts : proParts).speaker->say (r[say]);
}

void RiderEditor::tick (bool simple, int count)
{
    Frame fr[128];
    const int n = proc.popRide (fr, 128);
    past.insert (past.end(), fr, fr + n);
    if ((int) past.size() > shown) past.erase (past.begin(), past.begin() + ((long) past.size() - shown));
    auto& parts = simple ? simpleParts : proParts;
    parts.stage->step(); parts.fader->repaint();
    parts.sentence->refresh();
    if (count % 12 == 0) updateCheck (simple);
}

juce::String RiderEditor::sentenceText (bool simple) const { return (simple ? simpleParts : proParts).sentence->plainText(); }
bool RiderEditor::dragForTest (bool simple, const juce::String& id, int pixels) { auto& s = *(simple ? simpleParts : proParts).sentence; s.refresh(); return s.drag (id, pixels); }
