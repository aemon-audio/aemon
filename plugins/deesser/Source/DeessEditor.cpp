// Aemon De-esser. AGPL-3.0-or-later.
#include "DeessEditor.h"

using namespace aemon;
using Frame = aemon::deess::Engine::Frame;

namespace
{
    juce::String hzText (float v) { return (v >= 1000.0f ? juce::String (v / 1000.0f, v >= 9950.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"); }
    juce::String msText (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String cutText (float v) { return juce::String (v, 1) + " dB"; }
    std::optional<float> parseHz (const juce::String& s) { auto v = txt::parse (s); if (v && (s.containsIgnoreCase ("k") || *v < 30.0f)) *v *= 1000.0f; return v; }
    float percentile (std::vector<float> v, double p) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) (p * (double) v.size()))]; }
}

namespace deessui
{
/** The voice and its S sounds over time (left), the S next to the voice and the cut (right, drag to aim). */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (DeessEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "Left: the voice over time, S sounds in lilac, what is cut in amber. The dashed line follows the singer. Right: your S next to your voice. Drag to aim."; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const auto B = box();
        const juce::Rectangle<float> H (40, 28, B.getX() - 40 - 30, b.getHeight() - 52);
        auto hy = [&] (float d) { return H.getBottom() - (juce::jlimit (-60.0f, 0.0f, d) + 60.0f) / 60.0f * H.getHeight(); };
        drawDbAxis (g, H, { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f }, hy);
        drawEngraved (g, "Voice and S over time", { H.getX(), 7, 220, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "cut", { H.getRight() - 40, 7, 40, 14 }, 10.0f, col::ember, juce::Justification::centredRight);
        drawEngraved (g, "S", { H.getRight() - 60, 7, 24, 14 }, 10.0f, col::lilac, juce::Justification::centredRight);
        drawEngraved (g, "voice", { H.getRight() - 110, 7, 56, 14 }, 10.0f, col::phosphor, juce::Justification::centredRight);

        const auto& h = ed.past;
        const int n = (int) h.size();
        const float dx = H.getWidth() / (float) (DeessEditor::shown - 1);
        auto xa = [&] (int i) { return H.getRight() - (float) (n - 1 - i) * dx; };
        if (n > 1)
        {
            juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (H.expanded (0, 12).toNearestInt());
            juce::Path body, line;
            for (int i = 0; i < n; ++i) { const float y = hy (h[(size_t) i].body); if (i == 0) body.startNewSubPath (xa (0), y); else body.lineTo (xa (i), y); }
            auto fill = body; fill.lineTo (H.getRight(), H.getBottom()); fill.lineTo (xa (0), H.getBottom()); fill.closeSubPath();
            g.setColour (col::phosphor.withAlpha (0.14f)); g.fillPath (fill);
            g.setColour (col::phosphor.withAlpha (0.75f)); g.strokePath (body, juce::PathStrokeType (1.2f));
            for (int i = 0; i < n; ++i)
            {
                const auto& f = h[(size_t) i];
                if (f.sib < 0.3f) continue;
                const float t = hy (f.s), u = hy (f.s - f.cut);
                g.setColour (col::lilac.withAlpha (0.62f)); g.fillRect (xa (i), u, dx + 0.3f, H.getBottom() - u);
                if (f.cut > 0.1f) { g.setColour (col::ember.withAlpha (0.95f)); g.fillRect (xa (i), t, dx + 0.3f, u - t); }
            }
            const float lineDb = ed.real ("line");
            bool on = false;
            for (int i = 0; i < n; ++i) { if (h[(size_t) i].voice < -120.0f) { on = false; continue; } const juce::Point<float> q (xa (i), hy (h[(size_t) i].voice + lineDb)); if (on) line.lineTo (q); else line.startNewSubPath (q); on = true; }
            juce::Path dashed; const float d[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, line, d, 2);
            g.setColour (col::bone1.withAlpha (0.55f)); g.fillPath (dashed);
            g.setFont (Fonts::mono (10.0f)); g.setColour (col::emberHi);
            for (int i = 0, from = -1; i <= n; ++i)
            {
                const bool is = i < n && h[(size_t) i].sib > 0.5f;
                if (is && from < 0) from = i;
                if (is || from < 0) continue;
                float cut = 0, top = -120; for (int k = from; k < i; ++k) { cut = std::max (cut, h[(size_t) k].cut); top = std::max (top, h[(size_t) k].s); }
                if (cut > 0.3f && i - from > 2) g.drawText (txt::num (-cut, 1), juce::Rectangle<float> (40, 12).withCentre ({ juce::jlimit (H.getX() + 16, H.getRight() - 16, xa ((from + i) / 2)), std::max (H.getY() + 6, hy (top) - 8) }), juce::Justification::centred);
                from = -1;
            }
        }
        drawEngraved (g, ed.real ("follow") > 0.5f ? "fixed line" : "line follows the singer", { H.getX(), H.getBottom() + 6, 240, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "Your S next to your voice", { B.getX(), 7, 200, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        const float freq = ed.real ("freq"), fs = (float) ed.proc.currentRate.load();
        drawEngraved (g, hzText (freq), { B.getRight() - 90, 7, 90, 14 }, 10.0f, col::emberHi, juce::Justification::centredRight);
        g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3);
        for (auto [f, t] : { std::pair { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } }) g.drawText (t, juce::Rectangle<float> (30, 12).withCentre ({ X (f), B.getBottom() + 12 }), juce::Justification::centred);

        auto Y = [&] (float d) { return B.getY() + (juce::jlimit (-6.0f, 60.0f, -d) + 6.0f) / 66.0f * B.getHeight(); };
        g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (B);
        juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (B.toNearestInt());
        g.setColour (col::bone1.withAlpha (0.06f)); for (float f : { 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f }) g.fillRect (X (f), B.getY(), 1.0f, B.getHeight());
        const float b0 = X ((float) aemon::deess::bandLow (freq)), b1 = X ((float) aemon::deess::bandHigh (freq));
        g.setColour (col::lilac.withAlpha (0.08f)); g.fillRect (b0, B.getY(), b1 - b0, B.getHeight());
        auto spectrum = [&] (const std::vector<float>& p, juce::Colour c, float fillAlpha)
        {
            if (p.empty()) return;
            std::vector<float> v; float top = 1e-20f;
            for (float px = 0; px <= B.getWidth(); px += 3.0f)
            {
                const double f0 = 200.0 * std::pow (80.0, (px - 4) / B.getWidth()), f1 = 200.0 * std::pow (80.0, (px + 4) / B.getWidth());
                const int a = juce::jmax (1, (int) (f0 / ed.binHz)), e = juce::jmax (a + 1, (int) std::ceil (f1 / ed.binHz));
                double sum = 0; for (int k = a; k < e && k < (int) p.size(); ++k) sum += p[(size_t) k];
                v.push_back ((float) (sum / (e - a) * f0)); top = std::max (top, v.back());
            }
            juce::Path path; for (size_t i = 0; i < v.size(); ++i) { const juce::Point<float> q (B.getX() + 3.0f * (float) i, Y (10.0f * std::log10 (v[i] / top + 1e-9f) - 4.0f)); if (i == 0) path.startNewSubPath (q); else path.lineTo (q); }
            auto area = path; area.lineTo (B.getRight(), B.getBottom()); area.lineTo (B.getX(), B.getBottom()); area.closeSubPath();
            g.setColour (c.withAlpha (fillAlpha)); g.fillPath (area);
            g.setColour (c); g.strokePath (path, juce::PathStrokeType (1.5f));
        };
        spectrum (ed.voiceSpec, col::phosphor, 0.10f);
        spectrum (ed.sSpec, col::lilac, 0.14f);
        const auto shape = (aemon::deess::Shape) juce::roundToInt (ed.real ("shape"));
        auto curve = [&] (float gd) { juce::Path p; for (float px = 0; px <= B.getWidth(); px += 2.0f) { const double f = std::min (200.0 * std::pow (80.0, px / B.getWidth()), fs * 0.49); const juce::Point<float> q (B.getX() + px, Y ((float) aemon::deess::Cut::responseDb (f, gd, shape, freq, fs))); if (px == 0) p.startNewSubPath (q); else p.lineTo (q); } return p; };
        { juce::Path dashed; const float d[] = { 3.0f, 4.0f }; juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve (ed.real ("range")), d, 2); g.setColour (col::ember.withAlpha (0.45f)); g.fillPath (dashed); }
        const auto now = curve (ed.cutNow);
        g.setColour (col::ember.withAlpha (0.25f)); g.strokePath (now, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember); g.strokePath (now, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember.withAlpha (0.5f)); g.fillRect (X (freq) - 0.5f, B.getY(), 1.0f, B.getHeight());
        g.setColour (hot ? col::emberHi : col::ember); g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre ({ X (freq), B.getBottom() - 12 }));
    }

    void mouseMove (const juce::MouseEvent& e) override { const bool in = box().expanded (8, 0).contains (e.position); if (in != hot) { hot = in; setMouseCursor (in ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hot = false; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { dragging = box().expanded (8, 0).contains (e.position); if (dragging) { ed.begin(); aim (e.position.x); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) aim (e.position.x); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) ed.end(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (box().expanded (8, 0).contains (e.position)) ed.findMyS(); }

private:
    juce::Rectangle<float> box() const { const float w = std::round ((float) getWidth() * 0.36f); return { (float) getWidth() - w - 16, 28, w, (float) getHeight() - 52 }; }
    float X (float f) const { const auto B = box(); return B.getX() + std::log (f / 200.0f) / std::log (80.0f) * B.getWidth(); }
    void aim (float x) { const auto B = box(); ed.setReal ("freq", juce::jlimit (3000.0f, 12000.0f, 200.0f * std::pow (80.0f, (x - B.getX()) / B.getWidth()))); }
    DeessEditor& ed;
    bool hot = false, dragging = false;
};
} // namespace deessui

using namespace deessui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {})
    {
        Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o;
    }
    Knob::Options amountO() { return opt ("Amount", "How much of the excess is taken away. At 100 % every S is brought down to the line.", txt::percent, 11, "off", "all"); }
    Knob::Options freqO() { auto o = opt ("Where", "Where your S sits. Find my S sets it for you. Low voices are lower, high voices higher.", hzText, 9, "3k", "12k"); o.parse = parseHz; return o; }
    Knob::Options rangeO() { return opt ("At most", "The deepest cut allowed. Keeps an S from turning into a lisp.", cutText, 9, "0", "24"); }
    const char* soloHelp = "Hear only the S: plays what is recognised as an S, uncut. If you hear words, lower Strictness or move Where.";
}

DeessEditor::DeessEditor (DeessProcessor& p) : EffectEditor (p), proc (p)
{
    buildSimple();
    buildPro();
    start();
}

void DeessEditor::findMyS()
{
    const auto f = listen (false);
    if (! f.ok) { toast (f.freq > 0 ? "Not enough S sounds yet. Play a little more and try again." : "No clear S heard in the last seconds. Play some vocal and try again."); return; }
    begin();
    setReal ("freq", f.freq); setReal ("amount", 70); setReal ("range", juce::jlimit (8.0f, 12.0f, real ("range")));
    if (real ("follow") > 0.5f) setReal ("follow", 0);
    auto p = proc.readParams(); p.fixedLine = false; p.freq = f.freq;
    double fs; const auto x = proc.recent.copy (fs);
    const auto aimed = deessp::find (x, fs, p, true);
    setReal ("line", deessp::lineFor (aimed.ok ? aimed : f, 4.5f, 0.7f));
    end();
    toast ("Your S sits around " + hzText (f.freq) + ". The sharper ones now come down by about 4.5 dB.");
}

void DeessEditor::less()
{
    begin();
    setReal ("amount", std::round (real ("amount") * 0.8f)); setReal ("range", juce::jmax (2.0f, real ("range") - 1.0f));
    end();
    toast ("A fifth less, and never deeper than " + cutText (real ("range")) + ".");
}

void DeessEditor::presetChosen (const Preset& chosen)
{
    if (chosen.user) return;
    const auto in = deessp::intentOf (chosen.name);
    if (in.downDb <= 0) return;
    const auto f = listen (! in.aim);
    if (! f.ok) return;      // nothing heard yet: the preset's own numbers stay
    begin();
    if (in.aim) setReal ("freq", f.freq);
    setReal ("line", deessp::lineFor (f, in.downDb, real ("amount") / 100.0f));
    end();
    toast (chosen.name + ": " + (in.aim ? "aimed at your S, " + hzText (f.freq) + ". " : juce::String ("set to this sound. ")) + chosen.description);
}

void DeessEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "What is being cut, in numbers.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (200)); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& freq = knob (*deck, "freq", freqO());
    auto& strict = knob (*deck, "strict", opt ("Strictness", "What counts as an S. Left: only clear ones. Right: breaths, T and Z as well.", txt::percent, 11, "sure", "wide"));
    auto& amount = knob (*deck, "amount", amountO());
    auto& line = knob (*deck, "line", opt ("Line", "How loud an S may be, measured against the singer. The line moves with the voice.", txt::db, 11, txt::minusSign() + "24", "+6"));
    auto& range = knob (*deck, "range", rangeO());
    auto& rel = knob (*deck, "rel", opt ("Release", "How fast it lets go while the S fades. When the voice returns it lets go at once, whatever this says.", msText));
    auto& look = knob (*deck, "look", opt ("Lookahead", "How early it moves, so the first instant of an S is caught too. Adds this much delay.", msText));
    auto& shape = paramSeg (*deck, "shape", { deessp::shapeNames[0], deessp::shapeNames[1], deessp::shapeNames[2] }, { deessp::shapeHelps[0], deessp::shapeHelps[1], deessp::shapeHelps[2] });
    auto& on = paramSeg (*deck, "on", { "Stereo", "Mid", "Side" }, { "Both sides together.", "Only the middle. The sides are untouched.", "Only the sides. The middle is untouched." });
    auto& follow = paramSeg (*deck, "follow", { "Voice", "Fixed" }, { "Follows the singer: the same result whether the voice is loud or quiet.", "A fixed line, for hats, cymbals and noise. Measured from " + txt::minusSign() + "24 dB." });
    auto& solo = toggle (*deck, "solo", "Hear S", soloHelp, true);
    auto& removed = toggle (*deck, "removed", "Removed", "Hear removed: plays only what is taken away.", true);
    for (auto [b, id, other] : { std::tuple { &solo, "solo", "removed" }, { &removed, "removed", "solo" } })
    {
        auto f = b->onClick;
        const juce::String me (id), them (other);
        b->onClick = [this, f, me, them] { f(); if (base.apvts.getRawParameterValue (me)->load() > 0.5f) setReal (them, 0); };
    }
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kw = 72, kh = 64 + 25;
        auto hero = [&] (Knob& k, int x, const char* t) { k.setBounds (x, top + 4, kw, kh); pnt.under (k, t); };
        int x = 24;
        pnt.groups.push_back ({ { x, 14, 2 * kw, 20 }, "What is an S" }); hero (freq, x, "Where"); hero (strict, x + kw, "Strictness"); x += 2 * kw + 24;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 3 * kw, 20 }, "How much" }); hero (amount, x, "Amount"); hero (line, x + kw, "Line"); hero (range, x + 2 * kw, "At most"); x += 3 * kw + 24;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 2 * kw, 20 }, "Time" }); hero (rel, x, "Release"); hero (look, x + kw, "Lookahead"); x += 2 * kw + 24;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 168, 20 }, "Cut" }); shape.setBounds (x, top + 8, 168, 24); pnt.subs.push_back ({ { x, top + 42, 168, 11 }, "Works on" }); on.setBounds (x, top + 58, 168, 24); x += 168 + 24;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 120, 20 }, "Line" }); follow.setBounds (x, top + 8, 120, 24); solo.setBounds (x, top + 38, 120, 22); removed.setBounds (x, top + 65, 120, 22);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void DeessEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    check = &make<CheckCards> (*body, "S check", "S check: what the de-esser is doing, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "S sounds", "Lisp", "Vowels", "Aim" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& amount = knob (*deck, "amount", amountO()); auto& freq = knob (*deck, "freq", freqO()); auto& range = knob (*deck, "range", rangeO());
    auto& go = button (*deck, "Find my S", "listens, sets it", "Listens back over the last seconds, aims at your S and sets a safe amount.", [this] { findMyS(); });
    auto& solo = toggle (*deck, "solo", "Hear only the S", soloHelp); solo.hint = "check the aim";
    auto& lessB = button (*deck, "Less", "softer, safer", "A fifth less, and a shallower limit.", [this] { less(); });
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38; int x = 80;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &amount, "Amount" }, { &freq, "Where" }, { &range, "At most" } }) { pnt.groups.push_back ({ { x, 14, 112, 20 }, t }); k->setBounds (x, top - 4, 112, 98 + 25); x += 136; pnt.seps.push_back (x - 12); }
        pnt.groups.push_back ({ { x + 20, 14, 300, 20 }, "One-click helpers" });
        go.setBounds (x + 26, top + 2, 290, 30); solo.setBounds (x + 26, top + 38, 290, 30); lessB.setBounds (x + 26, top + 74, 290, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void DeessEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const auto cuts = proc.recentCuts();
    const float p = percentile (cuts, 0.8), st = proc.loudOut.getShortTerm();
    const auto goFind = [this] { findMyS(); };
    const auto back = [this] { less(); };
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (real ("solo") > 0.5f) r[0] = { S::warn, "You hear only the S", "Everything else is muted for listening.", "Back to normal", [this] { change ("solo", 0); } };
    else if (real ("removed") > 0.5f) r[0] = { S::warn, "You hear what is removed", "Everything else is muted for listening.", "Back to normal", [this] { change ("removed", 0); } };
    else if (proc.heard.load() == 0) r[0] = { S::ok, "No S heard yet", std::isfinite (st) && st > -60.0f ? "Waiting for the first one." : "Play some vocal." };
    else if (real ("amount") < 1.0f) r[0] = { S::warn, "Nothing is cut", "Amount is at zero. The sound is untouched.", "Find my S", goFind };
    else if (cuts.empty()) r[0] = { S::ok, "New setting", "Waiting for the next S." };
    else if (p < 1.0f) r[0] = { S::warn, "Hardly changed", "The sharper ones come down by " + juce::String (p, 1) + " dB.", "Find my S", goFind };
    else r[0] = { S::ok, p < 3.0f ? "Softened" : (p < 9.0f ? "Under control" : "Strongly cut"), "The sharper ones come down by " + juce::String (p, 1) + " dB." };

    if (cuts.empty() || real ("amount") < 1.0f) r[1] = { S::ok, "Lisp", "Nothing to worry about yet." };
    else if (p > 12.0f) r[1] = { S::bad, "Will sound like a lisp", "Cuts of " + juce::String (juce::roundToInt (p)) + " dB turn an S into an F. Fine on stacks and sends.", "Less", back };
    else if (p > 9.0f) r[1] = { S::warn, "Close to a lisp", "Deep cuts. Listen for soft, lazy words.", "Less", back };
    else r[1] = { S::ok, "No lisp", "The S is still an S." };

    const float vow = proc.vowelCut.load();
    if (! std::isfinite (st) || st < -60.0f) r[2] = { S::ok, "Vowels", "Waiting for signal." };
    else if (vow < 0.1f) r[2] = { S::ok, "Vowels untouched", "Everything that is not an S passes exactly as it is." };
    else r[2] = { S::warn, "Some vowels are cut", "Up to " + juce::String (vow, 1) + " dB.", "Stricter", [this] { change ("strict", juce::jmax (0.0f, real ("strict") - 20.0f)); } };

    const float centre = heardCentre();
    if (centre < 3000.0f) r[3] = { S::ok, "Aim", "Waiting for a clear S." };
    else if (std::abs (std::log2 (real ("freq") / centre)) > 0.33f) r[3] = { S::warn, "Aimed beside it", "Your S sits around " + hzText (centre) + ".", "Aim there", [this, centre] { change ("freq", std::round (centre / 50.0f) * 50.0f); } };
    else r[3] = { S::ok, "On target", "Your S sits around " + hzText (centre) + "." };
    check->refresh();
}

void DeessEditor::updateSpectra()
{
    const double fs = proc.currentRate.load();
    const int order = 10 + juce::jmax (0, juce::roundToInt (std::log2 (fs / 48000.0))), N = 1 << order;
    if (order != fftOrder) { fft = std::make_unique<juce::dsp::FFT> (order); fftOrder = order; fftBuf.assign ((size_t) (2 * N), 0.0f); voiceSpec.clear(); sSpec.clear(); sSeen = voiceSeen = 0; binHz = fs / N; }
    binHz = fs / N;
    const auto& f = past.back();
    const bool isS = f.sib > 0.95f && past.size() > 1 && past[past.size() - 2].sib > 0.95f && f.s > f.body, isVoice = f.sib <= 0.0f && f.body > -70.0f && f.body - f.s > 8.0f;
    if (! isS && ! isVoice) return;
    proc.ringIn.latest (fftBuf.data(), N);
    for (int i = 0; i < N; ++i) fftBuf[(size_t) i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    std::fill (fftBuf.begin() + N, fftBuf.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
    float e = 1e-20f; for (int k = isS ? (int) (3000.0 / binHz) : 1; k < N / 2; ++k) e += fftBuf[(size_t) k] * fftBuf[(size_t) k];
    if (e < 1e-10f) return;
    auto& to = isS ? sSpec : voiceSpec;
    if (to.empty()) to.assign ((size_t) (N / 2), 0.0f);
    const float mixIn = std::max (0.015f, 1.0f / (float) ++(isS ? sSeen : voiceSeen));     // a plain average first, then one that forgets slowly
    for (int k = 0; k < N / 2; ++k) to[(size_t) k] += (fftBuf[(size_t) k] * fftBuf[(size_t) k] / e - to[(size_t) k]) * mixIn;
}

void DeessEditor::tick (bool simple, int count)
{
    Frame fr[128];
    const int n = proc.popS (fr, 128);
    past.insert (past.end(), fr, fr + n);
    if ((int) past.size() > shown) past.erase (past.begin(), past.begin() + ((long) past.size() - shown));
    float top = 0; for (int i = 0; i < n; ++i) top = std::max (top, fr[i].cut);
    cutNow = top > cutNow ? top : cutNow + (top - cutNow) * (n > 0 ? 0.25f : 0.08f);
    if (cutNow < 0.02f) cutNow = 0;
    if (n > 0) updateSpectra();
    (simple ? displaySimple : displayPro)->repaint();
    if (count % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const auto cuts = proc.recentCuts();
    const auto dash = juce::String::charToString (0x2014);
    const float vow = proc.vowelCut.load(), v = past.empty() ? -200.0f : past.back().voice;
    panel->heading = "Cut now"; panel->note = real ("bypass") > 0.5f ? "bypassed" : (real ("amount") < 1.0f ? "bit-exact" : (cutNow > 0.1f ? "cutting" : "waiting"));
    panel->bigName = "S turned down by"; panel->bigValue = juce::String (cutNow, 1); panel->unit = "dB";
    panel->rows = { { "Sharper ones", cuts.empty() ? dash : cutText (percentile (cuts, 0.8)) }, { "Deepest of late", cuts.empty() ? dash : cutText (*std::max_element (cuts.begin(), cuts.end())) },
                    { "S heard", juce::String (proc.heard.load()) }, { real ("follow") > 0.5f ? "Fixed at" : "Singer level", v < -120.0f ? dash : txt::num (v, 1) + " dB" }, { "On vowels", vow < 0.1f ? juce::String ("untouched") : txt::num (-vow, 1) + " dB", true } };
    panel->repaint();
}
