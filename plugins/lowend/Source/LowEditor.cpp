// Aemon Low End. AGPL-3.0-or-later.
#include "LowEditor.h"

using namespace aemon;
using Say = aemon::low::Advice::Say;

namespace
{
    juce::String signedDb (double v) { return (v > 0.05 ? "+" : "") + txt::num ((float) v, 1) + " dB"; }
    juce::String msText (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String delayText (float v) { return std::abs (v) < 0.005f ? juce::String ("0 ms") : (v > 0 ? "+" : txt::minusSign()) + juce::String (std::abs (v), 2) + " ms"; }
    juce::String turnText (float v) { return juce::String (juce::roundToInt (v)) + juce::String::charToString (0xb0); }
    juce::String hzText (float v) { return juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String cutText (float v) { return juce::String (v, 1) + " dB"; }
}

namespace lowui
{
/** The last kick and the bass under it, and their sum. Drag the bass sideways to move it. */
class Waves : public juce::Component, public HelpProvider
{
public:
    explicit Waves (LowEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "The last kick (green), the bass under it (lilac) and what you hear of both (white), lows only. Drag sideways to move the bass, double-click to flip it."; }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const juce::Rectangle<float> W (14, 28, b.getWidth() - 28, b.getHeight() - 28 - 62), S (14, b.getBottom() - 48, b.getWidth() - 28, 38);
        drawEngraved (g, "Kick and bass, lows only", { W.getX(), 7, 220, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        float right = W.getRight(); auto legend = [&] (const char* t, juce::Colour c, float w) { drawEngraved (g, t, { right - w, 7, w, 14 }, 10.0f, c, juce::Justification::centredRight); right -= w + 12; };
        legend ("together", col::bone1, 58); legend ("bass", col::lilac, 34); legend ("kick", col::phosphor, 30);
        const float mid = W.getCentreY(); g.setColour (col::bone1.withAlpha (0.10f)); g.fillRect (W.getX(), mid, W.getWidth(), 1.0f);
        const auto& k = ed.kickWave; const auto& x = ed.bassWave; const int n = (int) std::min (k.size(), x.size());
        for (int ms : { 0, 50, 100 }) { const float px = W.getX() + ((float) ms + 15.0f) / 160.0f * W.getWidth(); g.setColour (col::bone1.withAlpha (0.05f)); g.fillRect (px, W.getY(), 1.0f, W.getHeight()); g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3); g.drawText (juce::String (ms) + " ms", juce::Rectangle<float> (40, 12).withCentre ({ px, W.getBottom() + 7 }), juce::Justification::centred); }
        if (n > 8)
        {
            float top = 1e-4f; for (int i = 0; i < n; ++i) top = std::max ({ top, std::abs (k[(size_t) i]), std::abs (x[(size_t) i]), std::abs (k[(size_t) i] + x[(size_t) i]) });
            auto path = [&] (auto f) { juce::Path p; for (int i = 0; i < n; ++i) { const juce::Point<float> q (W.getX() + (float) i / (float) (n - 1) * W.getWidth(), mid - f (i) / top * W.getHeight() * 0.48f); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); } return p; };
            const auto pk = path ([&] (int i) { return k[(size_t) i]; }), px = path ([&] (int i) { return x[(size_t) i]; }), ps = path ([&] (int i) { return k[(size_t) i] + x[(size_t) i]; });
            // where they pull against each other
            for (int i = 0; i < n; ++i) { const float a = k[(size_t) i], c = x[(size_t) i]; if (a * c < 0) { const float h = std::min (std::abs (a), std::abs (c)) / top; if (h > 0.04f) { g.setColour (col::sigClip.withAlpha (juce::jmin (0.22f, h * 0.5f))); g.fillRect (W.getX() + (float) i / (float) (n - 1) * W.getWidth(), W.getY(), W.getWidth() / (float) n + 0.5f, W.getHeight()); } } }
            g.setColour (col::phosphor.withAlpha (0.9f)); g.strokePath (pk, juce::PathStrokeType (1.6f));
            g.setColour (col::lilac.withAlpha (0.95f)); g.strokePath (px, juce::PathStrokeType (1.6f));
            g.setColour (col::bone1); g.strokePath (ps, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved));
        }
        else { g.setFont (Fonts::text (14.0f)); g.setColour (col::bone3); g.drawText (ed.proc.sidechainConnected ? "Play the song. The next kick shows up here." : "Send the kick to this plugin's sidechain input.", W, juce::Justification::centred); }
        // the duck over the last seconds
        g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (S);
        const int m = (int) ed.past.size(); const float dx = S.getWidth() / (float) (LowEditor::shown - 1), span = std::max (6.0f, ed.real ("duck"));
        for (int i = 0; i < m; ++i)
        {
            const auto& f = ed.past[(size_t) i]; const float xx = S.getRight() - (float) (m - 1 - i) * dx, h = juce::jlimit (0.0f, 1.0f, (f.kick + 48.0f) / 48.0f) * S.getHeight();
            g.setColour (col::phosphor.withAlpha (0.28f)); g.fillRect (xx, S.getBottom() - h, dx + 0.4f, h);
            const float c = std::max ({ f.cut[0], f.cut[1], f.cut[2] }) / span * S.getHeight();
            if (c > 0.3f) { g.setColour (col::ember.withAlpha (0.8f)); g.fillRect (xx, S.getY(), dx + 0.4f, std::min (c, S.getHeight())); }
        }
        g.setColour (col::basalt0.withAlpha (0.75f)); g.fillRoundedRectangle (S.getX() + 2, S.getY() + 2, 96, 12, 3);
        drawEngraved (g, ed.real ("duck") > 0.05f ? "duck, up to " + cutText (ed.real ("duck")) : juce::String ("no duck"), { S.getX() + 6, S.getY() + 2, 200, 12 }, 9.0f, ed.real ("duck") > 0.05f ? col::emberHi : col::bone3, juce::Justification::centredLeft);
    }
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); }
    void mouseDown (const juce::MouseEvent&) override { ed.begin(); from = ed.real ("delay"); }
    void mouseDrag (const juce::MouseEvent& e) override { ed.setReal ("delay", juce::jlimit (-10.0f, 10.0f, from + (float) e.getDistanceFromDragStartX() / ((float) getWidth() - 28) * 160.0f * (e.mods.isShiftDown() ? 0.1f : 0.5f))); ed.measureNow (false); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { ed.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { ed.change ("flip", ed.real ("flip") > 0.5f ? 0.0f : 1.0f); ed.measureNow (false); }
private:
    LowEditor& ed; float from = 0;
};

/** What the lows come to for every delay: over 0 they add up, under 0 they cancel. Drag the dot. */
class Curve : public juce::Component, public HelpProvider
{
public:
    explicit Curve (LowEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "What the two come to for every delay of the bass. Over the line they add up, under it they cancel. Drag the dot, double-click to put it back."; }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); drawWell (g, b, 8);
        const auto C = box();
        drawEngraved (g, "Together, by delay", { C.getX(), 7, 200, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        g.setFont (Fonts::mono (9.0f));
        for (float d : { 3.0f, 0.0f, -3.0f, -6.0f }) { g.setColour (col::bone1.withAlpha (d == 0.0f ? 0.25f : 0.06f)); g.fillRect (C.getX(), Y (d), C.getWidth(), 1.0f); g.setColour (col::bone3); g.drawText ((d > 0 ? "+" : "") + txt::num (d, 0), juce::Rectangle<float> (C.getX() - 24, Y (d) - 6, 20, 12), juce::Justification::centredRight); }
        for (float ms : { -10.0f, -5.0f, 0.0f, 5.0f, 10.0f }) { g.setColour (col::bone3); g.drawText (txt::num (ms, 0), juce::Rectangle<float> (30, 12).withCentre ({ X (ms), C.getBottom() + 9 }), juce::Justification::centred); }
        if (ed.hits > 0 && ed.curve.size() > 4)
        {
            juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (C.expanded (1).toNearestInt());
            juce::Path p; const int n = (int) ed.curve.size(); for (int i = 0; i < n; ++i) { const juce::Point<float> q (X (-10.0f + 20.0f * (float) i / (float) (n - 1)), Y (ed.curve[(size_t) i])); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
            auto area = p; area.lineTo (X (10), Y (0)); area.lineTo (X (-10), Y (0)); area.closeSubPath();
            { juce::Graphics::ScopedSaveState u (g); g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (C.getX(), C.getY(), C.getRight(), Y (0)).toNearestInt()); g.setColour (col::sigOk.withAlpha (0.16f)); g.fillPath (area); }
            { juce::Graphics::ScopedSaveState u (g); g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (C.getX(), Y (0), C.getRight(), C.getBottom()).toNearestInt()); g.setColour (col::sigClip.withAlpha (0.18f)); g.fillPath (area); }
            g.setColour (col::bone2); g.strokePath (p, juce::PathStrokeType (1.8f));
        }
        const juce::Point<float> dot (X (ed.real ("delay")), ed.hits > 0 ? Y ((float) ed.reading) : Y (0));
        g.setColour (col::ember.withAlpha (0.3f)); g.fillRect (dot.x - 0.5f, C.getY(), 1.0f, C.getHeight()); g.fillEllipse (juce::Rectangle<float> (22, 22).withCentre (dot));
        g.setColour (col::ember); g.fillEllipse (juce::Rectangle<float> (11, 11).withCentre (dot));
        if (ed.hits > 0) { g.setFont (Fonts::mono (12.0f)); g.setColour (col::emberHi); g.drawText (signedDb (ed.reading), juce::Rectangle<float> (70, 14).withCentre ({ juce::jlimit (C.getX() + 34, C.getRight() - 34, dot.x), juce::jmax (C.getY() + 8, dot.y - 18) }), juce::Justification::centred); }
    }
    void mouseDown (const juce::MouseEvent& e) override { ed.begin(); drag (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drag (e); }
    void mouseUp (const juce::MouseEvent&) override { ed.end(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { ed.change ("delay", 0); ed.measureNow (false); }
private:
    void drag (const juce::MouseEvent& e) { const auto C = box(); ed.setReal ("delay", juce::jlimit (-10.0f, 10.0f, -10.0f + 20.0f * (e.position.x - C.getX()) / C.getWidth())); ed.measureNow (false); repaint(); }
    juce::Rectangle<float> box() const { return { 38, 28, (float) getWidth() - 52, (float) getHeight() - 52 }; }
    float X (float ms) const { const auto C = box(); return C.getX() + (ms + 10.0f) / 20.0f * C.getWidth(); }
    float Y (float d) const { const auto C = box(); return C.getY() + juce::jlimit (0.0f, 1.0f, (3.6f - d) / 10.6f) * C.getHeight(); }
    LowEditor& ed;
};
} // namespace lowui

using namespace lowui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {}) { Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o; }
    Knob::Options duckO() { return opt ("Duck", "The deepest the lows of the bass step back while the kick sounds.", cutText, 9, "0", "24"); }
    Knob::Options belowO() { return opt ("Below", "Only what is under this line is measured and ducked. An octave over it nothing moves.", hzText, 9, "60", "200"); }
    const char* clashHelp = "Only what clashes: ducks only the part of the lows where kick and bass both are.";
}

LowEditor::LowEditor (LowProcessor& p) : EffectEditor (p), proc (p)
{
    buildSimple();
    buildPro();
    kept = alignNow();
    start();
}

void LowEditor::measureNow (bool withAdvice)
{
    const auto a = alignNow();
    measure.prepare (proc.tracks(), real ("below"));
    hits = (int) measure.hits.size();
    if (hits == 0) { reading = 0; kickWave.clear(); bassWave.clear(); if (withAdvice) advice = {}; return; }
    const auto b = measure.moved (a); reading = measure.total (b).db();
    curve = measure.curve (a, 81);
    const auto& h = measure.hits.back(); const int len = (int) (0.16 * measure.fs);
    kickWave.clear(); bassWave.clear(); for (int i = h.from; i < h.from + len && i < (int) b.size(); ++i) { kickWave.push_back (measure.kick[(size_t) i]); bassWave.push_back (b[(size_t) i]); }
    if (withAdvice) advice = low::advise (measure, a);
}

void LowEditor::setAlign (const low::Align& a) { begin(); setReal ("delay", (float) a.delayMs); setReal ("turn", (float) a.turn); setReal ("flip", a.flip ? 1.0f : 0.0f); end(); measureNow (false); }

void LowEditor::lineUp()
{
    if (! proc.sidechainConnected) { toast ("No kick connected. Send the kick to this plugin's sidechain input in your DAW."); return; }
    measureNow (true);
    switch (advice.say)
    {
        case Say::wait: toast ("Not enough kicks heard yet. Play a few bars and try again."); return;
        case Say::noOverlap: toast (advice.overlap < 0 ? "The bass is hardly there while the kick sounds. Nothing to line up." : "The kick is hardly there in the lows. Nothing to line up."); return;
        case Say::varies: toast ("It is different on every kick, so no single move helps. Let the 808 start its phase on every note, or duck."); return;
        case Say::fine: toast ("Already good: the best move would add only " + cutText ((float) juce::jmax (0.0, advice.then - advice.now)) + "."); return;
        case Say::helps: break;
    }
    const auto a = advice.align; const double before = advice.now;
    setAlign (a);
    toast ("Moved: " + delayText ((float) a.delayMs) + (a.flip ? ", flipped" : "") + (a.turn > 0 ? ", turned " + turnText ((float) a.turn) : juce::String()) + ". Together " + signedDb (before) + " before, " + signedDb (reading) + " now.");
    measureNow (true);
}

void LowEditor::putBack() { setAlign ({}); toast ("The bass is where it was."); measureNow (true); }
void LowEditor::duckClash() { begin(); setReal ("duck", juce::jmax (6.0f, real ("duck"))); setReal ("clash", 1); end(); toast ("The lows that clash step back by up to " + cutText (real ("duck")) + " while the kick sounds."); }

void LowEditor::presetChosen (const Preset& chosen)
{
    // a preset is about the duck: the line-up of these two tracks stays
    if (chosen.user || chosen.name.startsWith ("Clean start") || kept == low::Align {}) return;
    setAlign (kept);
    toast (chosen.name + ". " + chosen.description + " Your line-up is kept.");
}

void LowEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    waves[1] = &make<Waves> (*body, *this); curves[1] = &make<Curve> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "Kick and bass in numbers.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (200)); r.removeFromRight (10); curves[1]->setBounds (r.removeFromRight (260)); r.removeFromRight (10); waves[1]->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto dO = opt ("Delay", "Moves the bass later (+) or earlier.", delayText, 11, txt::minusSign() + "10", "+10"); dO.bipolar = true; dO.detent = true;
    auto& delay = knob (*deck, "delay", std::move (dO));
    auto& turn = knob (*deck, "turn", opt ("Turn", "Turns the phase of the lows, in degrees at 60 Hz. The last resort, when delay and flip are not enough.", turnText, 11, "0", "150"));
    auto& flip = toggle (*deck, "flip", "Flip", "Turns the bass upside down.", true);
    auto& duck = knob (*deck, "duck", duckO()); auto& below = knob (*deck, "below", belowO());
    auto& rel = knob (*deck, "rel", opt ("Release", "How long the lows take to come back after the kick.", msText, 9));
    auto& clash = toggle (*deck, "clash", "Only clash", clashHelp, true);
    auto& line = button (*deck, "Line them up", "", "Measures the last seconds and moves the bass if that really helps.", [this] { lineUp(); }); line.small = true;
    auto& back = button (*deck, "Put it back", "", "No delay, no flip, no turn.", [this] { putBack(); }); back.small = true;
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kw = 84, kh = 64 + 25; int x = 36;
        auto k = [&] (Knob& kn, const char* t) { kn.setBounds (x, top + 4, kw, kh); pnt.under (kn, t); x += kw; };
        auto sep = [&] { x += 14; pnt.seps.push_back (x); x += 14; };
        pnt.groups.push_back ({ { x, 14, 2 * kw + 120, 20 }, "Line up" }); k (delay, "Delay"); k (turn, "Turn"); x += 8; flip.setBounds (x, top + 8, 112, 24); line.setBounds (x, top + 38, 112, 24); back.setBounds (x, top + 68, 112, 24); x += 112; sep();
        pnt.groups.push_back ({ { x, 14, 3 * kw + 130, 20 }, "Duck" }); k (duck, "Deepest"); k (below, "Below"); k (rel, "Release"); x += 8; clash.setBounds (x, top + 38, 112, 24);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void LowEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    waves[0] = &make<Waves> (*body, *this); curves[0] = &make<Curve> (*body, *this);
    check = &make<CheckCards> (*body, "Low check", "Low check: what kick and bass do to each other, in plain words, with a fix when there is one.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "Kick", "Together", "Line-up", "Duck" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); curves[0]->setBounds (r.removeFromRight (230)); r.removeFromRight (10); waves[0]->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& duck = knob (*deck, "duck", duckO()); auto& below = knob (*deck, "below", belowO());
    auto& clash = toggle (*deck, "clash", "Only what clashes", clashHelp);
    auto& flip = toggle (*deck, "flip", "Flip the bass", "Turns the bass upside down.");
    auto& line = button (*deck, "Line them up", "measures first", "Measures the last seconds and moves the bass if that really helps.", [this] { lineUp(); });
    auto& duckB = button (*deck, "Duck what clashes", "up to 6 dB", "The lows that clash step back while the kick sounds.", [this] { duckClash(); });
    auto& back = button (*deck, "Put it back", "undo the move", "No delay, no flip, no turn.", [this] { putBack(); });
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38; int x = 70;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &duck, "Duck" }, { &below, "Below" } }) { pnt.groups.push_back ({ { x, 14, 112, 20 }, t }); k->setBounds (x, top - 4, 112, 98 + 25); x += 140; pnt.seps.push_back (x - 14); }
        pnt.groups.push_back ({ { x, 14, 200, 20 }, "How" }); clash.setBounds (x + 4, top + 16, 200, 30); flip.setBounds (x + 4, top + 56, 200, 30); x += 228; pnt.seps.push_back (x - 10);
        pnt.groups.push_back ({ { x, 14, 300, 20 }, "One-click helpers" }); line.setBounds (x + 6, top + 2, 290, 30); duckB.setBounds (x + 6, top + 38, 290, 30); back.setBounds (x + 6, top + 74, 290, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void LowEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const bool side = proc.sidechainConnected.load(); float kick = -120; for (auto& f : past) kick = std::max (kick, f.kick);
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (! side) r[0] = { S::warn, "No kick connected", "Send the kick to this plugin's sidechain input in your DAW." };
    else if (kick < -60) r[0] = { S::ok, "Waiting for the kick", "Connected, but silent." };
    else r[0] = { S::ok, "Kick connected", hits > 0 ? juce::String (hits) + " kicks in the last seconds." : juce::String ("Heard. Play a few bars.") };

    const bool known = side && hits >= 4 && advice.say != Say::wait;
    if (! known) { r[1] = { S::ok, "Together", "Needs at least four kicks." }; r[2] = { S::ok, "Line-up", "Nothing measured yet." }; }
    else
    {
        if (advice.say == Say::noOverlap) r[1] = { S::ok, "They hardly meet", advice.overlap < 0 ? "The bass is far quieter while the kick sounds." : "The kick is far quieter in the lows." };
        else if (reading < -0.5) r[1] = { reading < -2.0 ? S::bad : S::warn, "They cancel", "Together " + signedDb (reading) + " against side by side. Lows are lost." };
        else if (reading < 0.7) r[1] = { S::ok, "They ignore each other", "Together " + signedDb (reading) + ". Nothing lost, nothing won." };
        else r[1] = { S::ok, "They add up", "Together " + signedDb (reading) + ". The most possible is +3." };
        switch (advice.say)
        {
            case Say::helps: r[2] = { S::warn, "Can be lined up", "Moving the bass would give about " + signedDb (advice.gain) + " more.", "Line them up", [this] { lineUp(); } }; break;
            case Say::varies: r[2] = { S::ok, "No single move helps", "It is different on every kick. Let the 808 start its phase on every note, or duck." }; break;
            case Say::noOverlap: r[2] = { S::ok, "Nothing to line up", "They hardly meet." }; break;
            default: r[2] = { S::ok, "Lined up", "The best move would add less than 1 dB." }; break;
        }
    }
    const float duck = real ("duck"); float deep = 0; for (auto& f : past) deep = std::max ({ deep, f.cut[0], f.cut[1], f.cut[2] });
    if (duck < 0.05f) r[3] = known && reading < 0.7 && advice.say != Say::helps && advice.say != Say::noOverlap ? CheckCards::Row { S::ok, "No duck", "They share the lows.", "Duck what clashes", [this] { duckClash(); } } : CheckCards::Row { S::ok, "No duck", "The lows of the bass stay as they are." };
    else if (! side) r[3] = { S::warn, "Nothing to duck to", "The duck needs the kick on the sidechain." };
    else if (duck > 14.0f) r[3] = { S::warn, "Deep duck", "Down by " + cutText (deep) + " of late. You will hear it pump.", "Back to 8 dB", [this] { change ("duck", 8); } };
    else r[3] = { S::ok, deep > 0.5f ? "Ducking" : "Ready", deep > 0.5f ? "Down by up to " + cutText (deep) + " of late, only under " + hzText (real ("below")) + "." : juce::String ("Waiting for kick and bass together.") };
    check->refresh();
}

void LowEditor::tick (bool simple, int count)
{
    low::Engine::Frame fr[128];
    const int n = proc.popLow (fr, 128);
    past.insert (past.end(), fr, fr + n);
    if ((int) past.size() > shown) past.erase (past.begin(), past.begin() + ((long) past.size() - shown));
    const auto now = alignNow();
    if (base.pluginState.current.name == lastName || lastName.isEmpty()) kept = now;      // a preset that was just chosen has not been seen by presetChosen yet
    lastName = base.pluginState.current.name;
    waves[simple ? 0 : 1]->repaint(); curves[simple ? 0 : 1]->repaint();
    if (count % 15 == 0) measureNow (false);
    if (count % 100 == 0 && hits >= 4 && ! thinking.exchange (true))
    {
        // the search takes a moment: not on this thread
        juce::Component::SafePointer<LowEditor> me (this);
        worker.give ([me, m = measure, a = now, w = &worker] { const auto out = low::advise (m, a, [w] { return w->threadShouldExit(); }); if (w->threadShouldExit()) return; juce::MessageManager::callAsync ([me, out] { if (me != nullptr) { me->advice = out; me->thinking = false; } }); });
    }
    if (count % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const auto dash = juce::String::charToString (0x2014); const bool known = hits > 0;
    float deep = 0; for (auto& f : past) deep = std::max ({ deep, f.cut[0], f.cut[1], f.cut[2] });
    panel->heading = "Kick and bass"; panel->note = real ("bypass") > 0.5f ? "bypassed" : (proc.sidechainConnected ? (known ? "measured" : "listening") : "no kick");
    panel->bigName = reading < -0.5 ? "They cancel" : (reading < 0.7 ? "They ignore each other" : "They add up"); panel->bigValue = known ? (reading > 0.05 ? "+" : "") + txt::num ((float) reading, 1) : dash; panel->unit = "dB";
    panel->rows = { { "Kicks heard", juce::String (hits) }, { "Best possible", known && advice.say != Say::wait ? signedDb (advice.then) : dash }, { "Proven gain", known && advice.say != Say::wait ? signedDb (advice.gain) : dash, advice.say == Say::helps }, { "Bass to kick", known && advice.say != Say::wait ? signedDb (advice.overlap) : dash }, { "Ducked", cutText (deep), true } };
    panel->repaint();
}
