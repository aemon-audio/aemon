// Aemon Reverb. AGPL-3.0-or-later.
#include "RevEditor.h"

using namespace aemon;
using Frame = aemon::rev::Engine::Frame;

namespace
{
    juce::String secText (float v) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; }
    juce::String msText (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String cutText (float v) { return juce::String (v, 1) + " dB"; }
    juce::String gateText (float v) { return v < 1.0f ? juce::String ("off") : msText (v); }
    std::optional<float> parseHz (const juce::String& s) { auto v = txt::parse (s); if (v && s.containsIgnoreCase ("k")) *v *= 1000.0f; return v; }
    std::optional<float> parseSec (const juce::String& s) { auto v = txt::parse (s); if (v && s.containsIgnoreCase ("ms")) *v /= 1000.0f; return v; }
}

namespace revui
{
/** The space as a cloud: time to the right, low to high upwards. The gap in front is the pre-delay, the edge is where 60 dB are gone. Drag the edge. */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (RevEditor& e) : ed (e) {}
    juce::String getHelpText() const override
    {
        switch (over) { case 1: return "Pre-delay: the gap before the space answers. Drag sideways."; case 2: return "Decay: how long the middle lasts. Drag sideways."; case 3: return "High decay: how long the top lasts. Drag sideways."; case 4: return "Low decay: how long the bottom lasts. Drag sideways."; default: return "The space: time to the right, low to high upwards. Drag the dots: the gap, the decay, the top and the bottom."; }
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); drawWell (g, b, 8); const auto C = box(); const auto p = ed.proc.readParams();
        drawEngraved (g, juce::String (revp::spaceNames[(int) p.space]) + (p.freeze ? ", frozen" : ""), { C.getX(), 7, 260, 14 }, 10.0f, p.freeze ? col::lilac : col::bone3, juce::Justification::centredLeft);
        g.setFont (Fonts::mono (9.0f));
        for (double s : { 0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }) { if (s > span() * 1.02) continue; const float x = X (s); g.setColour (col::bone1.withAlpha (0.05f)); g.fillRect (x, C.getY(), 1.0f, C.getHeight()); g.setColour (col::bone3); g.drawText (s < 1 ? juce::String (juce::roundToInt (s * 1000)) + " ms" : juce::String (juce::roundToInt (s)) + " s", juce::Rectangle<float> (44, 12).withCentre ({ x, C.getBottom() + 9 }), juce::Justification::centred); }
        for (auto [f, t] : { std::pair { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } }) { g.setColour (col::bone3); g.drawText (t, juce::Rectangle<float> (C.getX() - 34, Y (f) - 6, 28, 12), juce::Justification::centredRight); g.setColour (col::bone1.withAlpha (0.05f)); g.fillRect (C.getX(), Y (f), C.getWidth(), 1.0f); }

        const double pre = p.preMs * 0.001; const float live = juce::jlimit (0.0f, 1.0f, (liveWet + 60.0f) / 55.0f);
        // the cloud, row by row
        juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (C.toNearestInt());
        const float row = 3.0f; juce::Path edge; bool first = true;
        for (float y = C.getY(); y < C.getBottom(); y += row)
        {
            const double f = F (y + row / 2); const double T = p.freeze ? 1000 : revp::decayAt (p, f);
            const double in = (f < p.lowCut ? std::pow (f / p.lowCut, 2) : 1.0) * (f > p.highCut ? std::pow (p.highCut / f, 2) : 1.0); const float a = (float) juce::jlimit (0.0, 1.0, in) * (0.28f + 0.5f * live);
            const float x0 = X (pre + 0.002), x1 = X (pre + T), xm = X (pre + T * 0.33);
            const auto colour = col::phosphor.interpolatedWith (col::lilac, (float) juce::jlimit (0.0, 1.0, std::log2 (f / 60.0) / 8.0));
            juce::ColourGradient grad (colour.withAlpha (a), x0, 0, colour.withAlpha (0.0f), juce::jmax (x0 + 2, x1), 0, false); grad.addColour (juce::jlimit (0.01, 0.99, (double) (xm - x0) / juce::jmax (2.0f, x1 - x0)), colour.withAlpha (a * 0.45f));
            g.setGradientFill (grad); g.fillRect (x0, y, juce::jmax (1.0f, x1 - x0), row + 0.3f);
            const juce::Point<float> q (x1, y + row / 2); if (first) edge.startNewSubPath (q); else edge.lineTo (q); first = false;
        }
        g.setColour (col::bone1.withAlpha (0.55f)); if (! p.freeze) g.strokePath (edge, juce::PathStrokeType (1.4f));
        // the dry sound at the left, early reflections after the gap
        g.setColour (col::bone1.withAlpha (0.9f)); g.fillRect (X (0), C.getY() + 10, 2.0f, C.getHeight() - 20);
        const double spread = aemon::rev::makeOf (p.space).early * aemon::rev::scaleOf (p.size) * 0.001; for (int k = 0; k < 9; ++k) { const double u = std::pow ((k + 0.5) / 9, 0.8), t = pre + 0.002 + u * spread; const float h = C.getHeight() * 0.5f * (float) (1 - 0.6 * u); g.setColour (col::ember.withAlpha ((float) (0.15 + 0.65 * p.early))); g.fillRect (X (t), C.getCentreY() - h / 2, 1.2f, h); }
        for (int k = 1; k <= 4; ++k) { const auto q = dot (k); const bool lit = over == k || held == k; g.setColour (col::ember.withAlpha (lit ? 0.4f : 0.2f)); g.fillEllipse (juce::Rectangle<float> (24, 24).withCentre (q)); g.setColour (lit ? col::emberHi : col::ember); g.fillEllipse (juce::Rectangle<float> (11, 11).withCentre (q)); const char* t[] { "", "gap", "decay", "top", "bottom" }; drawEngraved (g, t[k], juce::Rectangle<float> (60, 12).withCentre ({ juce::jmin (C.getRight() - 24.0f, q.x), q.y - 15 }), 9.0f, col::emberHi); }
        // what plays: dry, wet, how far it is ducked
        const auto S = strip(); g.reduceClipRegion (getLocalBounds());
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        const auto S = strip(); g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (S); const auto& h = ed.past; const int n = (int) h.size(); const float dx = S.getWidth() / (float) (RevEditor::shown - 1); auto H = [&] (float d) { return juce::jlimit (0.0f, 1.0f, (d + 60.0f) / 60.0f) * S.getHeight(); };
        for (int i = 0; i < n; ++i) { const float x = S.getRight() - (float) (n - 1 - i) * dx; g.setColour (col::bone1.withAlpha (0.28f)); g.fillRect (x, S.getBottom() - H (h[(size_t) i].dry), dx + 0.3f, H (h[(size_t) i].dry)); g.setColour (col::phosphor.withAlpha (0.65f)); g.fillRect (x, S.getBottom() - H (h[(size_t) i].wet), dx + 0.3f, H (h[(size_t) i].wet)); const float d = h[(size_t) i].duck / 24.0f * S.getHeight(); if (d > 0.4f) { g.setColour (col::ember.withAlpha (0.85f)); g.fillRect (x, S.getY(), dx + 0.3f, d); } }
        g.setColour (col::basalt0.withAlpha (0.78f)); g.fillRoundedRectangle (S.getX() + 2, S.getY() + 2, 118, 12, 3);
        drawEngraved (g, "dry", { S.getX() + 6, S.getY() + 2, 30, 12 }, 9.0f, col::bone2, juce::Justification::centredLeft); drawEngraved (g, "wet", { S.getX() + 38, S.getY() + 2, 30, 12 }, 9.0f, col::phosphor, juce::Justification::centredLeft); drawEngraved (g, "ducked", { S.getX() + 70, S.getY() + 2, 50, 12 }, 9.0f, col::ember, juce::Justification::centredLeft);
    }
    void mouseMove (const juce::MouseEvent& e) override { const int k = hit (e.position); if (k != over) { over = k; setMouseCursor (k ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { over = 0; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { held = hit (e.position); if (held) ed.begin(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! held) return;
        const auto p = ed.proc.readParams(); const double t = T (e.position.x), after = juce::jmax (0.05, t - p.preMs * 0.001);
        if (held == 1) { if (ed.real ("sync") > 0.5f) ed.setReal ("sync", 0); ed.setReal ("pre", (float) juce::jlimit (0.0, 250.0, t * 1000)); }
        else if (held == 2) ed.setReal ("decay", (float) juce::jlimit (0.1, 30.0, after));
        else ed.setReal (held == 3 ? "highd" : "lowd", (float) juce::jlimit (held == 3 ? 10.0 : 30.0, held == 3 ? 120.0 : 200.0, 100 * after / p.decay));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (held) ed.end(); held = 0; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { const int k = hit (e.position); const char* id[] { "", "pre", "decay", "highd", "lowd" }; if (k) { const auto* pr = ed.base.apvts.getParameter (id[k]); ed.change (id[k], pr->convertFrom0to1 (pr->getDefaultValue())); } }
    float liveWet = -120;

private:
    juce::Rectangle<float> box() const { return { 44, 28, (float) getWidth() - 60, (float) getHeight() - 28 - 80 }; }
    juce::Rectangle<float> strip() const { return { 44, (float) getHeight() - 50, (float) getWidth() - 60, 40 }; }
    double span() const { const auto p = ed.proc.readParams(); return juce::jlimit (0.8, 40.0, (p.preMs * 0.001 + p.decay * juce::jmax (1.0, p.lowDecay, p.highDecay)) * 1.25); }
    float X (double t) const { const auto C = box(); return C.getX() + (float) std::pow (juce::jlimit (0.0, 1.0, t / span()), 0.5) * C.getWidth(); }
    double T (float x) const { const auto C = box(); return std::pow (juce::jlimit (0.0f, 1.0f, (x - C.getX()) / C.getWidth()), 2.0f) * span(); }
    float Y (double f) const { const auto C = box(); return C.getBottom() - (float) (std::log (f / 40.0) / std::log (18000.0 / 40.0)) * C.getHeight(); }
    double F (float y) const { const auto C = box(); return 40.0 * std::pow (18000.0 / 40.0, (double) (C.getBottom() - y) / C.getHeight()); }
    juce::Point<float> dot (int k) const { const auto p = ed.proc.readParams(); const double pre = p.preMs * 0.001; if (k == 1) return { X (pre), Y (2600) }; const double f = k == 2 ? 900 : (k == 3 ? 9500 : 75); return { X (pre + juce::jmin (30.0, revp::decayAt (p, f))), Y (f) }; }
    int hit (juce::Point<float> q) const { for (int k = 4; k >= 1; --k) if (q.getDistanceFrom (dot (k)) < 14) return k; return 0; }
    RevEditor& ed; int over = 0, held = 0;
};
} // namespace revui

using namespace revui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 9, juce::String lo = {}, juce::String hi = {}) { Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o; }
    Knob::Options decayO() { auto o = opt ("Decay", "How long the space rings, for the middle of the sound.", secText, 9, "0.1", "30"); o.parse = parseSec; return o; }
    Knob::Options mixO() { return opt ("Mix", "How much of the space. Up to 50 % the dry sound is untouched.", txt::percent, 11, "dry", "wet"); }
    Knob::Options duckO() { return opt ("Duck", "How far the space steps back while the sound itself plays. It comes up in the gaps, so words stay clear.", cutText, 9, "off", "24"); }
    const char* sendHelp = "On a send: only the space comes out. Use it on a send or aux track.";
    const char* freezeHelp = "Freeze: holds what is in the space for as long as it is on. Nothing new goes in.";
}

RevEditor::RevEditor (RevProcessor& p) : EffectEditor (p), proc (p)
{
    buildSimple();
    buildPro();
    start();
}

void RevEditor::clearWords() { begin(); setReal ("duck", juce::jmax (8.0f, real ("duck"))); setReal ("lowcut", juce::jmax (180.0f, real ("lowcut"))); if (real ("sync") < 0.5f) setReal ("pre", juce::jmax (30.0f, real ("pre"))); end(); toast ("The space waits 30 ms, leaves the lows out and steps back " + cutText (real ("duck")) + " while the words play."); }
void RevEditor::cleanLows() { begin(); setReal ("lowcut", juce::jmax (250.0f, real ("lowcut"))); setReal ("lowd", juce::jmin (80.0f, real ("lowd"))); end(); toast ("Nothing under 250 Hz goes into the space. 808s stay clean."); }
void RevEditor::shorter() { change ("decay", juce::jmax (0.1f, real ("decay") * 0.75f)); toast ("A quarter shorter: " + secText (real ("decay")) + "."); }

void RevEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "The space in numbers.");
    auto* space = &paramSeg (*body, "space", { "Room", "Plate", "Hall", "Chamber", "Lo-fi", "Endless" }, { revp::spaceHelps[0], revp::spaceHelps[1], revp::spaceHelps[2], revp::spaceHelps[3], revp::spaceHelps[4], revp::spaceHelps[5] });
    body->layout = [this, space] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (200)); r.removeFromRight (10); space->setBounds (r.removeFromTop (26)); r.removeFromTop (8); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto hzO = [] (const char* n, const char* h, juce::String lo, juce::String hi) { auto o = opt (n, h, hzText, 9, lo, hi); o.parse = parseHz; return o; };
    auto decK = decayO();
    Knob* ks[] { &knob (*deck, "decay", std::move (decK)), &knob (*deck, "size", opt ("Size", "How big the space is.", txt::percent)), &knob (*deck, "pre", opt ("Pre-delay", "The gap before the space answers.", msText)), &knob (*deck, "lowcut", hzO ("Low cut", "Nothing under this goes into the space.", "20", "1k")), &knob (*deck, "highcut", hzO ("High cut", "Nothing over this goes into the space.", "1k", "20k")),
                 &knob (*deck, "early", opt ("Early", "Left: only the tail. Right: only the first reflections.", txt::percent)), &knob (*deck, "mod", opt ("Move", "How much the space moves inside. Keeps long tails from ringing.", txt::percent)), &knob (*deck, "width", opt ("Width", "How wide the space is. 0 is mono.", txt::percent)), &knob (*deck, "duck", duckO()), &knob (*deck, "duckrel", opt ("Back", "How long the space takes to come back after the words.", msText)), &knob (*deck, "gate", opt ("Gate", "Cuts the space this long after the sound stops. The 80s drum.", gateText)), &knob (*deck, "drive", opt ("Grit", "Drives the tail.", txt::percent)), &knob (*deck, "mix", mixO()) };
    const char* names[] { "Decay", "Size", "Gap", "Low cut", "High cut", "Early", "Move", "Width", "Duck", "Back", "Gate", "Grit", "Mix" };
    auto* sync = &paramSeg (*deck, "sync", { "ms", "/64", "/32", "/16", ".16", "/8" }, { "The gap in milliseconds.", "A 64th note.", "A 32nd note.", "A 16th note.", "A dotted 16th.", "An 8th note." }, 8.5f);
    auto* freeze = &toggle (*deck, "freeze", "Freeze", freezeHelp, true); auto* send = &toggle (*deck, "send", "Send", sendHelp, true);
    auto& pnt = painter (*deck);
    std::vector<Knob*> list (std::begin (ks), std::end (ks)); std::vector<const char*> nl (std::begin (names), std::end (names));
    deck->layout = [&pnt, list, nl, sync, freeze, send] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear(); const int top = 30, kw = 61, kh = 58 + 25; int x = 14;
        auto group = [&] (int from, int count, const char* t) { pnt.groups.push_back ({ { x, 12, count * kw, 18 }, t }); for (int i = from; i < from + count; ++i) { list[(size_t) i]->setBounds (x, top + 2, kw, kh); pnt.under (*list[(size_t) i], nl[(size_t) i]); x += kw; } x += 7; pnt.seps.push_back (x); x += 7; };
        group (0, 3, "Space"); group (3, 2, "What goes in"); group (5, 3, "Shape"); group (8, 2, "Words"); group (10, 2, "Effect"); group (12, 1, "Level");
        sync->setBounds (14, 130, 190, 18); const int w = 960 - 14 - 220; juce::ignoreUnused (w); freeze->setBounds (884, 44, 70, 24); send->setBounds (884, 76, 70, 24);
        pnt.subs.push_back ({ { 208, 133, 90, 11 }, "gap on the beat" }); pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void RevEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    auto* space = &paramSeg (*body, "space", { "Room", "Plate", "Hall", "Chamber", "Lo-fi", "Endless" }, { revp::spaceHelps[0], revp::spaceHelps[1], revp::spaceHelps[2], revp::spaceHelps[3], revp::spaceHelps[4], revp::spaceHelps[5] });
    check = &make<CheckCards> (*body, "Space check", "Space check: what the reverb does to your sound, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "Words", "Lows", "Mono", "Level" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this, space] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); space->setBounds (r.removeFromTop (26)); r.removeFromTop (8); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& decay = knob (*deck, "decay", decayO()); auto& duck = knob (*deck, "duck", duckO()); auto& mix = knob (*deck, "mix", mixO());
    auto& clear = button (*deck, "Keep the words clear", "gap, lows, duck", "Adds a gap, leaves the lows out and lets the space step back while the words play.", [this] { clearWords(); });
    auto& send = toggle (*deck, "send", "On a send", sendHelp); send.hint = "only the space";
    auto& freeze = toggle (*deck, "freeze", "Freeze", freezeHelp); freeze.hint = "hold it";
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear(); const int top = 38; int x = 80;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &decay, "Decay" }, { &duck, "Duck" }, { &mix, "Mix" } }) { pnt.groups.push_back ({ { x, 14, 112, 20 }, t }); k->setBounds (x, top - 4, 112, 98 + 25); x += 136; pnt.seps.push_back (x - 12); }
        pnt.groups.push_back ({ { x + 20, 14, 300, 20 }, "One-click helpers" }); clear.setBounds (x + 26, top + 2, 290, 30); send.setBounds (x + 26, top + 38, 290, 30); freeze.setBounds (x + 26, top + 74, 290, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void RevEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    double wd = 0, dd = 0; int n = 0; for (auto& f : past) if (f.dry > -45) { dd += std::pow (10.0, f.dry / 10); wd += std::pow (10.0, f.wet / 10); ++n; }
    const bool playing = n > 20, send = real ("send") > 0.5f, off = ! send && real ("mix") < 0.5f; const float under = playing ? (float) (10 * std::log10 ((wd + 1e-12) / (dd + 1e-12))) : -99;
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (real ("freeze") > 0.5f) r[0] = { S::ok, "Frozen", "The space holds what it had. Nothing new goes in.", "Let go", [this] { change ("freeze", 0); } };
    else if (off) r[0] = { S::ok, "No reverb yet", "Mix is at zero. The sound is untouched." };
    else if (send) r[0] = { S::ok, "On a send", "Only the space comes out. Set how much with the send." };
    else if (! playing) r[0] = { S::ok, "Words", "Play something." };
    else if (under > -4.0f) r[0] = { S::bad, "The space covers the sound", "It is only " + juce::String (juce::jmax (0.0f, -under), 0) + " dB under it.", "Keep the words clear", [this] { clearWords(); } };
    else if (under > -9.0f && real ("duck") < 3.0f) r[0] = { S::warn, "Close to covering words", "The space is " + juce::String (juce::roundToInt (-under)) + " dB under the sound while it plays.", "Keep the words clear", [this] { clearWords(); } };
    else r[0] = { S::ok, "Words are clear", "The space is " + juce::String (juce::roundToInt (-under)) + " dB under the sound while it plays." };

    if (off) r[1] = { S::ok, "Lows", "Nothing goes in yet." };
    else if (real ("lowcut") < 90.0f) r[1] = { S::warn, "Lows in the space", "Kick and 808 will smear.", "Cut below 250 Hz", [this] { cleanLows(); } };
    else r[1] = { S::ok, real ("lowcut") >= 240.0f ? "808-safe" : "Lows kept out", "Nothing under " + hzText (real ("lowcut")) + " goes in." };

    const float w = real ("width");
    if (w > 130.0f) r[2] = { S::ok, "Very wide", "Some of the space is lost on a phone. The sound itself is not." };
    else if (w < 10.0f) r[2] = { S::ok, "Mono space", "The space sits in the middle." };
    else r[2] = { S::ok, "Safe in mono", "The two sides are unrelated: nothing cancels on a phone." };

    const float decay = real ("decay");
    if (off || real ("freeze") > 0.5f) r[3] = { S::ok, "Length", secText (decay) + "." };
    else if (decay > 4.0f && real ("duck") < 3.0f && ! send) r[3] = { S::warn, "Long", secText (decay) + ". It runs into the next line.", "A quarter shorter", [this] { shorter(); } };
    else r[3] = { S::ok, decay < 0.8f ? "Short" : (decay < 2.5f ? "Medium" : "Long"), secText (decay) + (real ("duck") > 0.5f ? ", steps back " + cutText (real ("duck")) + " while the sound plays." : juce::String (".")) };
    check->refresh();
}

void RevEditor::tick (bool simple, int count)
{
    Frame fr[128];
    const int n = proc.popRev (fr, 128);
    past.insert (past.end(), fr, fr + n);
    if ((int) past.size() > shown) past.erase (past.begin(), past.begin() + ((long) past.size() - shown));
    auto* d = simple ? displaySimple : displayPro; for (int i = 0; i < n; ++i) d->liveWet = fr[i].wet > d->liveWet ? fr[i].wet : d->liveWet - 1.0f;
    if (count % 2 == 0) d->repaint();
    if (count % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const auto p = proc.readParams(); const auto dash = juce::String::charToString (0x2014);
    panel->heading = "The space"; panel->note = real ("bypass") > 0.5f ? "bypassed" : (p.freeze ? "frozen" : (p.mix <= 0 ? "off" : "live"));
    panel->bigName = "Decay"; panel->bigValue = juce::String (p.decay, p.decay < 10 ? 2 : 1); panel->unit = "s";
    panel->rows = { { "At 100 Hz", secText ((float) revp::decayAt (p, 100)) }, { "At 8 kHz", secText ((float) revp::decayAt (p, 8000)) }, { "Gap", msText ((float) p.preMs) }, { "Wet now", past.empty() || past.back().wet < -90 ? dash : txt::num (past.back().wet, 1) + " dB" }, { "Ducked", cutText (past.empty() ? 0.0f : past.back().duck), true } };
    panel->repaint();
}
