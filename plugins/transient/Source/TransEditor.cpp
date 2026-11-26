// Aemon Transient. AGPL-3.0-or-later.
#include "TransEditor.h"

using namespace aemon;

namespace
{
    juce::String msText (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }
    float toDb (float v) { return v > 1e-5f ? 20.0f * std::log10 (v) : -100.0f; }
    juce::String signedDb (float v) { return (v > 0.05f ? "+" : "") + txt::num (v, 1) + " dB"; }
}

namespace transui
{
/** Your hit, the average of the last ones, before and after. Drag its top for the attack, its tail for the body, the two lines for how long they last. */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (TransEditor& e) : ed (e) {}
    juce::String getHelpText() const override
    {
        switch (over) { case 1: return "Attack: drag the top of the hit up or down."; case 2: return "Body: drag the tail up or down."; case 3: return "How long the attack lasts. Drag sideways."; case 4: return "How long the body is followed. Drag sideways."; default: return "Your hit, the average of the last ones: green before, white after, amber the change. Drag the top, the tail and the two lines."; }
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const auto H = box();
        drawDbAxis (g, H, { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f }, [&] (float d) { return Y (d); }, true);
        drawEngraved (g, ed.hits > 0 ? "Your hit (average of the last " + juce::String (juce::jmin (ed.hits, 8)) + ")" : juce::String ("Your hit"), { H.getX(), 7, 280, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "before", { H.getRight() - 100, 7, 50, 14 }, 10.0f, col::phosphor, juce::Justification::centredRight);
        drawEngraved (g, "after", { H.getRight() - 46, 7, 46, 14 }, 10.0f, col::bone1, juce::Justification::centredRight);
        g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3);
        for (int ms : { 0, 50, 100, 200, 300, 400 }) { const float x = X ((float) ms); g.setColour (col::bone1.withAlpha (0.05f)); g.fillRect (x, H.getY(), 1.0f, H.getHeight()); g.setColour (col::bone3); g.drawText (juce::String (ms), juce::Rectangle<float> (30, 12).withCentre ({ x, H.getBottom() + 9 }), juce::Justification::centred); }

        const float alen = ed.real ("alen"), attack = ed.real ("attack"), body = ed.real ("body");
        // what belongs to the attack, what to the body
        g.setColour (col::ember.withAlpha (0.05f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (X (0), H.getY(), X (alen), H.getBottom()));
        for (int k : { 3, 4 }) { const float x = handleX (k); drawDashed (g, { x, H.getY(), x, H.getBottom() }, (over == k || held == k ? col::emberHi : col::ember.withAlpha (0.55f)), 1.2f); drawEngraved (g, k == 3 ? "attack " + msText (alen) : "body " + msText (ed.real ("blen")), { x + 5, H.getY() + 2, 120, 12 }, 9.0f, over == k || held == k ? col::emberHi : col::bone3, juce::Justification::centredLeft); }

        if (ed.hits > 0)
        {
            juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (H.toNearestInt());
            auto path = [&] (const std::vector<float>& v) { juce::Path p; for (int i = 0; i < (int) v.size(); ++i) { const juce::Point<float> q (X ((float) (i - TransEditor::pre)), Y (v[(size_t) i])); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); } return p; };
            auto in = path (ed.avgIn), out = path (ed.avgOut);
            auto fill = in; fill.lineTo (H.getRight(), H.getBottom()); fill.lineTo (H.getX(), H.getBottom()); fill.closeSubPath();
            g.setColour (col::phosphor.withAlpha (0.16f)); g.fillPath (fill);
            auto change = in; for (int i = (int) ed.avgOut.size() - 1; i >= 0; --i) change.lineTo (X ((float) (i - TransEditor::pre)), Y (ed.avgOut[(size_t) i])); change.closeSubPath();
            g.setColour (col::ember.withAlpha (0.30f)); g.fillPath (change);
            g.setColour (col::phosphor.withAlpha (0.85f)); g.strokePath (in, juce::PathStrokeType (1.3f));
            g.setColour (col::bone1); g.strokePath (out, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        }
        else { g.setFont (Fonts::text (14.0f)); g.setColour (col::bone3); g.drawText ("Play drums. Your hit shows up here.", H, juce::Justification::centred); }
        for (int k : { 1, 2 })
        {
            const juce::Point<float> p (handleX (k), handleY (k)); const bool lit = over == k || held == k;
            g.setColour (col::ember.withAlpha (lit ? 0.35f : 0.2f)); g.fillEllipse (juce::Rectangle<float> (26, 26).withCentre (p));
            g.setColour (lit ? col::emberHi : col::ember); g.fillEllipse (juce::Rectangle<float> (13, 13).withCentre (p));
            g.setFont (Fonts::mono (11.0f)); g.drawText ((k == 1 ? "attack " : "body ") + signedDb (k == 1 ? attack : body), juce::Rectangle<float> (p.x + (k == 1 ? 16.0f : 4.0f), p.y - (k == 1 ? 16.0f : 34.0f), 130, 14), juce::Justification::centredLeft);
        }
        // the last seconds
        const auto S = strip(); g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (S);
        const int n = (int) ed.strip.size(); const float mid = S.getCentreY();
        for (float x = 0; x < S.getWidth(); x += 2.0f)
        {
            const int i1 = n - 1 - (int) ((S.getWidth() - x) / S.getWidth() * (float) TransEditor::stripLen), i0 = i1 - (int) (2.0f / S.getWidth() * (float) TransEditor::stripLen);
            if (i0 < 0) continue;
            float in = 0, out = 0; for (int i = i0; i <= i1; ++i) { in = std::max (in, ed.strip[(size_t) i].in); out = std::max (out, ed.strip[(size_t) i].out); }
            auto hh = [&] (float v) { return juce::jlimit (0.0f, 1.0f, (toDb (v) + 48.0f) / 54.0f) * S.getHeight() * 0.5f; };
            const float a = hh (in), o = hh (out);
            g.setColour (col::phosphor.withAlpha (0.45f)); g.fillRect (S.getX() + x, mid - a, 1.4f, 2 * a);
            if (o > a + 0.5f) { g.setColour (col::ember); g.fillRect (S.getX() + x, mid - o, 1.4f, o - a); g.fillRect (S.getX() + x, mid + a, 1.4f, o - a); }
            else if (o < a - 0.5f) { g.setColour (col::ember.withAlpha (0.45f)); g.fillRect (S.getX() + x, mid - a, 1.4f, a - o); g.fillRect (S.getX() + x, mid + o, 1.4f, a - o); }
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int k = hit (e.position); if (k != over) { over = k; setMouseCursor (k == 0 ? juce::MouseCursor::NormalCursor : (k < 3 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor)); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { over = 0; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { held = hit (e.position); if (held) { ed.begin(); from = ed.real (id (held)); } }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! held) return;
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        if (held < 3) ed.setReal (id (held), juce::jlimit (-12.0f, 12.0f, std::round ((from - (float) e.getDistanceFromDragStartY() / box().getHeight() * 54.0f * fine) * 10.0f) / 10.0f));
        else if (held == 3) ed.setReal ("alen", juce::jlimit (3.0f, 80.0f, std::round (ms (e.position.x))));
        else ed.setReal ("blen", juce::jlimit (50.0f, 800.0f, std::round (ms (e.position.x))));
    }
    void mouseUp (const juce::MouseEvent&) override { if (held) ed.end(); held = 0; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (const int k = hit (e.position)) ed.change (id (k), k < 3 ? 0.0f : (k == 3 ? 20.0f : 200.0f)); }

private:
    static const char* id (int k) { return k == 1 ? "attack" : (k == 2 ? "body" : (k == 3 ? "alen" : "blen")); }
    juce::Rectangle<float> box() const { return { 42, 28, (float) getWidth() - 58, (float) getHeight() - 28 - 80 }; }
    juce::Rectangle<float> strip() const { return { 42, (float) getHeight() - 50, (float) getWidth() - 58, 40 }; }
    float Y (float d) const { const auto H = box(); return H.getY() + juce::jlimit (0.0f, 54.0f, 14.0f - d) / 54.0f * H.getHeight(); }
    // time is stretched at the start, where a hit happens
    float X (float m) const { const auto H = box(); const float w = (std::pow ((m + TransEditor::pre) / (float) (TransEditor::hitLen), 0.6f)); return H.getX() + juce::jlimit (0.0f, 1.0f, w) * H.getWidth(); }
    float ms (float x) const { const auto H = box(); return std::pow (juce::jlimit (0.0f, 1.0f, (x - H.getX()) / H.getWidth()), 1.0f / 0.6f) * (float) TransEditor::hitLen - TransEditor::pre; }
    float at (const std::vector<float>& v, float m, float none) const { const int i = (int) m + TransEditor::pre; return ed.hits > 0 && i >= 0 && i < (int) v.size() ? v[(size_t) i] : none; }
    float handleX (int k) const { return k == 1 ? X (4) : (k == 2 ? X (ed.real ("alen") + 75.0f) : (k == 3 ? X (ed.real ("alen")) : X (juce::jmin (398.0f, ed.real ("blen"))))); }
    float handleY (int k) const { return k == 1 ? Y (ed.real ("attack")) : Y (at (ed.avgIn, ed.real ("alen") + 75.0f, -14.0f) + ed.real ("body")); }
    int hit (juce::Point<float> p) const { for (int k : { 1, 2 }) if (p.getDistanceFrom ({ handleX (k), handleY (k) }) < 15.0f) return k; if (box().contains (p)) for (int k : { 3, 4 }) if (std::abs (p.x - handleX (k)) < 6.0f) return k; return 0; }
    TransEditor& ed; int over = 0, held = 0; float from = 0;
};
} // namespace transui

using namespace transui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {}) { Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o; }
    Knob::Options two (Knob::Options o) { o.bipolar = true; o.detent = true; return o; }
    Knob::Options attackO() { return two (opt ("Attack", "The most the hit is lifted or held back. The same on a loud and on a quiet hit.", signedDb, 13, txt::minusSign() + "12", "+12")); }
    Knob::Options bodyO() { return two (opt ("Body", "The most what comes after the hit is lifted or pulled down: ring, room, tail.", signedDb, 13, txt::minusSign() + "12", "+12")); }
    Knob::Options outO() { return two (opt ("Output", "Level after everything.", signedDb, 13, txt::minusSign() + "12", "+12")); }
    const juce::StringArray focusNames { "All", "Lows", "Mids", "Highs" }, focusHelps { "The whole sound.", "Only below about 250 Hz: the boom and the thump.", "Only 250 Hz to 2.5 kHz.", "Only above about 3 kHz: click, sticks, sizzle." };
    const char* matchHelp = "Equal loudness: plays the result as loud as the original, so bypass compares sound, not volume. Only while listening, never in an export.";
    const char* deltaHelp = "Hear the change: plays only what is added or taken away.";
}

TransEditor::TransEditor (TransProcessor& p) : EffectEditor (p), proc (p)
{
    avgIn.assign ((size_t) hitLen, -60.0f); avgOut = avgIn;
    buildSimple();
    buildPro();
    start();
}

void TransEditor::punch() { begin(); setReal ("attack", juce::jmin (12.0f, real ("attack") + 2.0f)); end(); toast ("Two dB more hit."); }
void TransEditor::tighter() { begin(); setReal ("body", juce::jmax (-12.0f, real ("body") - 2.0f)); end(); toast ("Two dB less tail."); }
void TransEditor::samePeaks()
{
    const float d = proc.peakOut.load() - proc.peakIn.load();
    if (proc.peakIn.load() < -60.0f) { toast ("Play something first."); return; }
    change ("out", juce::jlimit (-12.0f, 12.0f, std::round ((real ("out") - d) * 10.0f) / 10.0f));
    toast ("Output set so that the peaks are where they were.");
}

void TransEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "The average hit, in numbers.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (200)); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& attack = knob (*deck, "attack", attackO()); auto& alen = knob (*deck, "alen", opt ("Attack length", "How long the attack lasts.", msText, 9));
    auto& bodyK = knob (*deck, "body", bodyO()); auto& blen = knob (*deck, "blen", opt ("Body length", "How long the body is followed after the hit.", msText, 9));
    auto& out = knob (*deck, "out", outO());
    auto& focus = paramSeg (*deck, "focus", focusNames, focusHelps);
    auto& match = toggle (*deck, "match", "Equal loudness", matchHelp, true);
    auto& delta = toggle (*deck, "delta", "Hear change", deltaHelp, true);
    auto& peaks = button (*deck, "Same peaks", "", "Sets the output so that the peaks are where they were.", [this] { samePeaks(); }); peaks.small = true;
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kw = 80, kh = 64 + 25; int x = 30;
        auto k = [&] (Knob& kn, const char* t) { kn.setBounds (x, top + 4, kw, kh); pnt.under (kn, t); x += kw; };
        auto sep = [&] { x += 14; pnt.seps.push_back (x); x += 14; };
        pnt.groups.push_back ({ { x, 14, 2 * kw, 20 }, "The hit" }); k (attack, "Attack"); k (alen, "Length"); sep();
        pnt.groups.push_back ({ { x, 14, 2 * kw, 20 }, "What follows" }); k (bodyK, "Body"); k (blen, "Length"); sep();
        pnt.groups.push_back ({ { x, 14, 200, 20 }, "Focus" }); focus.setBounds (x, top + 18, 200, 26); pnt.subs.push_back ({ { x, top + 56, 200, 11 }, "works on this part only" }); x += 200; sep();
        pnt.groups.push_back ({ { x, 14, kw, 20 }, "Output" }); k (out, "Level"); sep();
        pnt.groups.push_back ({ { x, 14, 130, 20 }, "Monitor" }); match.setBounds (x, top + 8, 130, 24); delta.setBounds (x, top + 38, 130, 24); peaks.setBounds (x, top + 68, 130, 24);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void TransEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    check = &make<CheckCards> (*body, "Hit check", "Hit check: what happens to your hits, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "Hits", "Body", "Peaks", "Loudness" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& attack = knob (*deck, "attack", attackO()); auto& bodyK = knob (*deck, "body", bodyO());
    auto& focus = paramSeg (*deck, "focus", focusNames, focusHelps);
    auto& p1 = button (*deck, "More punch", "2 dB more hit", "Two dB more hit.", [this] { punch(); });
    auto& p2 = button (*deck, "Tighter", "2 dB less tail", "Two dB less of what follows the hit.", [this] { tighter(); });
    auto& match = toggle (*deck, "match", "Compare at equal loudness", matchHelp); match.hint = "honest A/B";
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38; int x = 70;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &attack, "Attack" }, { &bodyK, "Body" } }) { pnt.groups.push_back ({ { x, 14, 112, 20 }, t }); k->setBounds (x, top - 4, 112, 98 + 25); x += 140; pnt.seps.push_back (x - 14); }
        pnt.groups.push_back ({ { x, 14, 220, 20 }, "Focus" }); focus.setBounds (x + 4, top + 34, 212, 28); x += 240; pnt.seps.push_back (x - 10);
        pnt.groups.push_back ({ { x, 14, 300, 20 }, "One-click helpers" }); p1.setBounds (x + 6, top + 2, 290, 30); p2.setBounds (x + 6, top + 38, 290, 30); match.setBounds (x + 6, top + 74, 290, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

/** Finds the hits in the last points and adds them to the average. */
void TransEditor::take()
{
    for (auto& w : waiting)
    {
        if (w < hitLen - pre) continue;
        const int n = (int) strip.size(), start = n - w - pre;
        if (start >= 0)
        {
            float peak = 1e-4f; int at = start; for (int i = start; i < start + pre + 12; ++i) if (strip[(size_t) i].in > peak) { peak = strip[(size_t) i].in; at = i; }
            juce::ignoreUnused (at);
            const float share = hits < 8 ? 1.0f / (float) (hits + 1) : 0.125f, ref = toDb (peak);
            double a = 0, b = 0, c = 0, d = 0;
            for (int i = 0; i < hitLen; ++i)
            {
                const auto& q = strip[(size_t) (start + i)]; const float in = juce::jmax (-60.0f, toDb (q.in) - ref), out = juce::jmax (-60.0f, toDb (q.out) - ref);
                avgIn[(size_t) i] += (in - avgIn[(size_t) i]) * share; avgOut[(size_t) i] += (out - avgOut[(size_t) i]) * share;
                if (i >= pre && i < pre + 12) { a = std::max (a, (double) q.in); b = std::max (b, (double) q.out); }
                if (i >= pre + 100 && i < pre + 250) { c += q.in * q.in; d += q.out * q.out; }
            }
            hitChange += ((float) (20 * std::log10 ((b + 1e-9) / (a + 1e-9))) - hitChange) * share; bodyChange += ((float) (10 * std::log10 ((d + 1e-12) / (c + 1e-12))) - bodyChange) * share; lastPeak = ref;
            ++hits;
        }
        w = -1;
    }
    waiting.erase (std::remove (waiting.begin(), waiting.end(), -1), waiting.end());
}

void TransEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const float attack = real ("attack"), body = real ("body"), st = proc.loudOut.getShortTerm(), si = proc.loudIn.getShortTerm();
    const bool playing = std::isfinite (si) && si > -60.0f, off = std::abs (attack) < 0.05f && std::abs (body) < 0.05f;
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (real ("delta") > 0.5f) r[0] = { S::warn, "You hear only the change", "Everything else is muted for listening.", "Back to normal", [this] { change ("delta", 0); } };
    else if (off) r[0] = { S::ok, "Nothing is changed", "Both knobs at zero. The sound is untouched." };
    else if (hits == 0) r[0] = { S::ok, "No hits yet", playing ? "Nothing that starts suddenly. Steady sounds stay as they are." : "Play some drums." };
    else r[0] = { S::ok, std::abs (hitChange) < 0.5f ? "The hits stay" : (hitChange > 0 ? "Hits come forward" : "Hits step back"), "By " + signedDb (hitChange) + ". The same on loud and quiet ones." };

    if (off || hits == 0) r[1] = { S::ok, "Body", "Nothing changed yet." };
    else if (body > 8.0f) r[1] = { S::warn, "Room comes up too", "Everything between the hits is lifted, noise as well.", "Back to +6", [this] { change ("body", 6); } };
    else if (body < -10.0f) r[1] = { S::warn, "Close to a gate", "The tails almost disappear. Fine as an effect.", "Back to " + txt::minusSign() + "8", [this] { change ("body", -8); } };
    else r[1] = { S::ok, std::abs (bodyChange) < 0.5f ? "The tail stays" : (bodyChange > 0 ? "Longer" : "Tighter"), "What follows the hit moves by " + signedDb (bodyChange) + "." };

    const float po = proc.peakOut.load(), d = po - proc.peakIn.load();
    if (! playing) r[2] = { S::ok, "Peaks", "Waiting for signal." };
    else if (po > 0.0f) r[2] = { S::bad, "Peaks over full scale", signedDb (po) + " at the output. It will clip in an export.", "Same peaks", [this] { samePeaks(); } };
    else if (d > 2.0f) r[2] = { S::warn, "Higher peaks", "Up by " + signedDb (d) + ". There is room, up to " + txt::num (-po, 1) + " dB.", "Same peaks", [this] { samePeaks(); } };
    else r[2] = { S::ok, "Peaks", "Highest " + txt::num (po, 1) + " dB." };

    if (! playing || ! std::isfinite (st)) r[3] = { S::ok, "Loudness", "Waiting for signal." };
    else if (real ("match") > 0.5f) r[3] = { S::ok, "Equal loudness", "Played at " + signedDb (proc.match.nowDb.load()) + ", as loud as the original." };
    else if (std::abs (st - si) > 1.0f) r[3] = { S::warn, st > si ? "Louder than before" : "Quieter than before", "By " + txt::num (std::abs (st - si), 1) + " LU. Louder always sounds better: compare fairly.", "Equal loudness", [this] { change ("match", 1); } };
    else r[3] = { S::ok, "Same loudness", "Within 1 LU of the original." };
    check->refresh();
}

void TransEditor::tick (bool simple, int count)
{
    // other settings: the picture starts over
    juce::String now; for (auto id : { "attack", "body", "alen", "blen", "focus", "out", "bypass", "delta" }) now << real (id) << " ";
    if (now != settings) { settings = now; hits = juce::jmin (hits, 2); }
    Point pts[512]; const int n = proc.popPoints (pts, 512);
    for (int i = 0; i < n; ++i)
    {
        strip.push_back (pts[i]); for (auto& w : waiting) ++w;
        if (pts[i].hit > 0.75f && cooled > 120 && pts[i].in > 0.004f) { waiting.push_back (0); cooled = 0; }
        cooled = pts[i].hit < 0.3f ? cooled + 1 : (cooled > 120 ? cooled : 0);
    }
    const int keep = stripLen + hitLen + 50;
    if ((int) strip.size() > keep) strip.erase (strip.begin(), strip.begin() + ((long) strip.size() - keep));
    take();
    (simple ? displaySimple : displayPro)->repaint();
    if (count % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const auto dash = juce::String::charToString (0x2014);
    panel->heading = "Average hit"; panel->note = real ("bypass") > 0.5f ? "bypassed" : (hits > 0 ? "live" : "waiting");
    panel->bigName = "The hit moves by"; panel->bigValue = hits > 0 ? (hitChange > 0.05f ? "+" : "") + txt::num (hitChange, 1) : dash; panel->unit = "dB";
    panel->rows = { { "What follows", hits > 0 ? signedDb (bodyChange) : dash }, { "Peaks in", proc.peakIn.load() < -60 ? dash : txt::num (proc.peakIn.load(), 1) + " dB" }, { "Peaks out", proc.peakOut.load() < -60 ? dash : txt::num (proc.peakOut.load(), 1) + " dB", proc.peakOut.load() > 0 },
                    { "Hits seen", juce::String (hits) }, { "Equal loudness", real ("match") > 0.5f ? signedDb (proc.match.nowDb.load()) : juce::String ("off"), true } };
    panel->repaint();
}
