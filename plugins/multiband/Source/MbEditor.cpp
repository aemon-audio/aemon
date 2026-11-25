// Aemon Multiband. AGPL-3.0-or-later.
#include "MbEditor.h"

using namespace aemon;
using mbp::N;

namespace
{
    juce::String hzText (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, v >= 9950.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String msText (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String ratioText (float v) { return v >= 19.95f ? juce::String ("inf:1") : juce::String (v, v < 9.95f ? 1 : 0) + ":1"; }
    juce::String plain (float v) { return txt::num (v, 1); }
    const juce::Colour bandColours[] = { col::phosphor, col::lilac, col::phosphor, col::lilac, col::phosphor };
}

namespace mbui
{
/** Bands over frequency: how loud each is, its lines, what is taken off (amber) and brought up (lilac). */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (MbEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "Every band with its level. Amber is taken off, lilac is brought up. Drag a divider sideways, a line up or down. Click a band to select it."; }

    void paint (juce::Graphics& g) override
    {
        drawWell (g, getLocalBounds().toFloat(), 8);
        const auto A = area();
        drawDbAxis (g, A, { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f }, [this] (float d) { return Y (d); });
        drawEngraved (g, "Bands", { A.getX(), 7, 100, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "taken off", { A.getRight() - 90, 7, 90, 14 }, 10.0f, col::ember, juce::Justification::centredRight);
        drawEngraved (g, "brought up", { A.getRight() - 196, 7, 100, 14 }, 10.0f, col::lilac, juce::Justification::centredRight);
        g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3);
        for (auto [f, t] : { std::pair { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } }) g.drawText (t, juce::Rectangle<float> (32, 12).withCentre ({ X (f), A.getBottom() + 11 }), juce::Justification::centred);
        juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (A.toNearestInt());
        const int B = ed.proc.bandCount(); const bool anySolo = [&] { for (int b = 0; b < B; ++b) if (get ('s', b) > 0.5f) return true; return false; }();

        if (! ed.spectrum.empty())
        {
            juce::Path p; bool on = false;
            for (float px = 0; px <= A.getWidth(); px += 2.0f)
            {
                const double f0 = 20.0 * std::pow (1000.0, (px - 2) / A.getWidth()), f1 = 20.0 * std::pow (1000.0, (px + 2) / A.getWidth()); const int a = juce::jmax (1, (int) (f0 / ed.binHz)), e = juce::jmax (a + 1, (int) std::ceil (f1 / ed.binHz));
                double sum = 0; for (int k = a; k < e && k < (int) ed.spectrum.size(); ++k) sum = std::max (sum, (double) ed.spectrum[(size_t) k]);
                if (e - a == 1) { const double at = 0.5 * (f0 + f1) / ed.binHz; const int k = juce::jlimit (0, (int) ed.spectrum.size() - 2, (int) at); const double q = at - k; sum = (1 - q) * ed.spectrum[(size_t) k] + q * ed.spectrum[(size_t) k + 1]; }
                const juce::Point<float> q (A.getX() + px, Y (juce::jlimit (-70.0f, 0.0f, (float) (10 * std::log10 (sum + 1e-9)) + 3.0f + 3.0f * (float) std::log2 (std::max (1.0, f0 / 100.0))))); if (! on) p.startNewSubPath (q); else p.lineTo (q); on = true;
            }
            auto fill = p; fill.lineTo (A.getBottomRight()); fill.lineTo (A.getBottomLeft()); fill.closeSubPath();
            g.setColour (col::bone1.withAlpha (0.05f)); g.fillPath (fill); g.setColour (col::bone1.withAlpha (0.22f)); g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        for (int b = 0; b < B; ++b)
        {
            auto r = band (b); const bool off = get ('o', b) < 0.5f || (anySolo && get ('s', b) < 0.5f); const float dim = off ? 0.3f : 1.0f; const auto c = bandColours[b];
            if (ed.pro && b == ed.selected) { g.setColour (col::bone1.withAlpha (0.045f)); g.fillRect (r); g.setColour (col::ember.withAlpha (0.35f)); g.drawRect (r.reduced (1), 1.0f); }
            const float y = Y (ed.level[(size_t) b]);
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.30f * dim), 0, y, c.withAlpha (0.04f * dim), 0, A.getBottom(), false)); g.fillRect (r.reduced (2, 0).withTop (y));
            g.setColour (c.withAlpha (0.8f * dim)); g.fillRect (r.reduced (2, 0).withTop (y).withHeight (1.6f));
            const float gr = ed.gain[(size_t) b], px = A.getHeight() / 60.0f * 2.0f;      // 2 pixels-dB: easier to read
            if (gr < -0.05f) { g.setColour (col::ember.withAlpha (0.85f * dim)); g.fillRect (r.reduced (4, 0).withHeight (-gr * px)); g.setColour (col::ember.withAlpha (0.16f * dim)); g.fillRect (r.reduced (2, 0).withHeight (-gr * px + 5)); }
            if (gr > 0.05f) { g.setColour (col::lilac.withAlpha (0.85f * dim)); g.fillRect (r.reduced (4, 0).withHeight (gr * px)); }
            if (get ('r', b) > 1.005f) { const float ty = Y (get ('t', b)); const bool h = ed.hot == mbp::id ('t', b); g.setColour ((h ? col::emberHi : col::ember).withAlpha (dim)); g.fillRect (r.reduced (3, 0).withTop (ty - 1).withHeight (h ? 2.6f : 2.0f)); g.fillEllipse (juce::Rectangle<float> (8, 8).withCentre ({ r.getCentreX(), ty })); }
            if (get ('v', b) > 1.005f) { const float uy = Y (get ('u', b)); drawDashed (g, { r.getX() + 3, uy, r.getRight() - 3, uy }, col::lilac.withAlpha ((ed.hot == mbp::id ('u', b) ? 1.0f : 0.8f) * dim), 1.6f); }
            g.setFont (Fonts::mono (12.0f)); g.setColour (gr < -0.05f ? col::emberHi : (gr > 0.05f ? col::lilac : col::bone3));
            g.drawText (std::abs (gr) < 0.05f ? juce::String ("0.0") : (gr > 0 ? "+" : "") + txt::num (gr, 1), juce::Rectangle<float> (60, 14).withCentre ({ r.getCentreX(), A.getBottom() - 26 }), juce::Justification::centred);
            drawEngraved (g, ed.bandName (b), juce::Rectangle<float> (std::max (60.0f, r.getWidth()), 12).withCentre ({ r.getCentreX(), A.getBottom() - 11 }), 8.5f, col::bone2.withAlpha (dim), juce::Justification::centred);
        }
        for (int i = 0; i < B - 1; ++i) { const bool h = ed.hot == mbp::id ('x', i); const float x = X (ed.splits()[(size_t) i]); g.setColour (h ? col::emberHi.withAlpha (0.8f) : col::bone1.withAlpha (0.35f)); g.fillRect (x - 0.75f, A.getY(), 1.5f, A.getHeight()); g.setColour (h ? col::emberHi : col::ember); g.fillRoundedRectangle (juce::Rectangle<float> (7, 18).withCentre ({ x, A.getY() + A.getHeight() * 0.5f }), 3); if (h) { g.setFont (Fonts::mono (10.0f)); g.drawText (hzText (ed.splits()[(size_t) i]), juce::Rectangle<float> (64, 12).withCentre ({ x + 38, A.getY() + A.getHeight() * 0.5f }), juce::Justification::centredLeft); } }
    }

    void mouseMove (const juce::MouseEvent& e) override { const auto h = hit (e.position); if (h != ed.hot) { ed.hot = h; setMouseCursor (h.isEmpty() ? juce::MouseCursor::NormalCursor : (h[0] == 'x' ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor)); } }
    void mouseExit (const juce::MouseEvent&) override { ed.hot.clear(); }
    void mouseDown (const juce::MouseEvent& e) override { drag = hit (e.position); const int b = bandAt (e.position.x); if (drag.isEmpty() || drag[0] != 'x') ed.select (b); if (drag.isNotEmpty()) { ed.begin(); apply (e.position); } }
    void mouseDrag (const juce::MouseEvent& e) override { if (drag.isNotEmpty()) apply (e.position); }
    void mouseUp (const juce::MouseEvent&) override { if (drag.isNotEmpty()) ed.end(); drag.clear(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { const auto h = hit (e.position); if (h.isNotEmpty()) if (auto* p = ed.proc.apvts.getParameter (h)) ed.change (h, p->convertFrom0to1 (p->getDefaultValue())); }

private:
    float get (char w, int b) const { return ed.proc.get (mbp::id (w, b)); }
    juce::Rectangle<float> area() const { return { 40, 26, (float) getWidth() - 54, (float) getHeight() - 50 }; }
    float X (double f) const { const auto A = area(); return A.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * A.getWidth(); }
    float Y (float d) const { const auto A = area(); return A.getBottom() - (juce::jlimit (-60.0f, 0.0f, d) + 60.0f) / 60.0f * A.getHeight(); }
    juce::Rectangle<float> band (int b) const { const auto A = area(); const int B = ed.proc.bandCount(); const auto s = ed.splits(); const float l = b > 0 ? X (s[(size_t) b - 1]) : A.getX(), r = b < B - 1 ? X (s[(size_t) b]) : A.getRight(); return { l, A.getY(), r - l, A.getHeight() }; }
    int bandAt (float x) const { const int B = ed.proc.bandCount(); for (int b = 0; b < B - 1; ++b) if (x < band (b).getRight()) return b; return B - 1; }
    juce::String hit (juce::Point<float> p) const
    {
        if (! area().contains (p)) return {};
        for (int i = 0; i < ed.proc.bandCount() - 1; ++i) if (std::abs (p.x - X (ed.splits()[(size_t) i])) < 5) return mbp::id ('x', i);
        const int b = bandAt (p.x);
        if (get ('r', b) > 1.005f && std::abs (p.y - Y (get ('t', b))) < 6) return mbp::id ('t', b);
        if (get ('v', b) > 1.005f && std::abs (p.y - Y (get ('u', b))) < 6) return mbp::id ('u', b);
        return {};
    }
    void apply (juce::Point<float> p)
    {
        const auto A = area(); const auto range = ed.proc.apvts.getParameterRange (drag);
        if (drag[0] != 'x') { ed.setReal (drag, juce::jlimit (range.start, range.end, -60.0f + 60.0f * (A.getBottom() - p.y) / A.getHeight())); return; }
        const int i = drag.getTrailingIntValue() - 1; const auto s = ed.splits(); const double lo = i > 0 ? s[(size_t) i - 1] * 1.26 : 25.0, hi = i < ed.proc.bandCount() - 2 ? s[(size_t) i + 1] / 1.26 : 18000.0;
        ed.setReal (drag, (float) juce::jlimit (lo, hi, 20.0 * std::pow (1000.0, (p.x - A.getX()) / A.getWidth())));
    }
    MbEditor& ed;
    juce::String drag;
};
} // namespace mbui

using namespace mbui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {}, bool bipolar = false)
    {
        Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; o.bipolar = bipolar; o.detent = bipolar; return o;
    }
    Knob::Options amountO() { return opt ("Amount", "Scales everything the bands do. 0 is nothing, 100 is as set, 200 is twice as much.", txt::percent, 9, "0", "200"); }
    Knob::Options mixO() { return opt ("Mix", "Blend with the uncompressed sound, inside each band: nothing combs.", txt::percent, 11, "dry", "wet"); }
    Knob::Options outO() { return opt ("Output", "Output level.", txt::db, 13, txt::minusSign() + "12", "+12", true); }
    const char* autoHelp = "Auto level: comes out as loud as it went in, so you judge the sound and not the volume.";
}

std::array<double, 4> MbEditor::splits() const
{
    std::array<double, 4> s {}; double prev = 0;
    for (int i = 0; i < 4; ++i) { s[(size_t) i] = juce::jlimit (20.0, 20000.0, std::max ((double) proc.get (mbp::id ('x', i)), prev * 1.26)); prev = s[(size_t) i]; }
    return s;
}

juce::String MbEditor::bandName (int b) const
{
    static const char* names[4][5] = { { "Low", "High", "", "", "" }, { "Low", "Mid", "High", "", "" }, { "Low", "Low mid", "High mid", "High", "" }, { "Sub", "Low", "Mid", "High mid", "High" } };
    return names[proc.bandCount() - 2][b];
}

MbEditor::MbEditor (MbProcessor& p) : EffectEditor (p), proc (p)
{
    level.fill (-60);
    buildSimple();
    buildPro();
    select (1);
    start();
}

void MbEditor::select (int b)
{
    selected = juce::jlimit (0, proc.bandCount() - 1, b);
    for (int i = 0; i < N; ++i) for (auto* c : perBand[(size_t) i]) c->setVisible (i == selected);
    if (bandSeg != nullptr) bandSeg->setIndex (selected, false);
}

const mbp::Recipe* MbEditor::current() const
{
    if (const int g = proc.matchingGoal(); g >= 0) return mbp::find (mbp::goals[g].recipe);
    const auto* r = proc.pluginState.current.user ? nullptr : mbp::find (proc.pluginState.current.name);
    return r != nullptr && r->bands == proc.bandCount() ? r : nullptr;
}

bool MbEditor::fit (const mbp::Recipe& r)
{
    std::vector<float> L, R; double fs; proc.recent.copy (L, R, fs);
    auto p = proc.readParams(); p.bands = r.bands; for (size_t i = 0; i < 4; ++i) p.split[i] = r.split[i]; p.slope = r.slope ? 48 : 24;
    const auto l = mbp::measure (L, R, fs, p);
    if (! l.ok) return false;
    for (auto& [id, v] : mbp::presetOf (r, l).values) if (id[0] == 't' || id[0] == 'u') setReal (id, v);
    return true;
}

void MbEditor::chooseGoal (int g)
{
    const auto* r = mbp::find (mbp::goals[g].recipe);
    begin();
    for (auto& [id, v] : mbp::presetOf (*r, mbp::assumed()).values) if (id != "extSc") setReal (id, v);
    const bool heard = fit (*r);
    end();
    toast (juce::String (mbp::goals[g].name) + (heard ? ": set to this music. " : ": play some music, then press Fit to my music. ") + r->desc);
}

bool MbEditor::fitToMusic (bool quiet)
{
    const auto* r = current();
    if (r != nullptr)
    {
        // the recipe's wishes, with the bands as they are now
        mbp::Recipe now = *r; const auto s = splits(); for (size_t i = 0; i < 4; ++i) now.split[i] = (float) s[i]; now.slope = real ("slope") > 0.5f; now.knee = real ("knee");
        begin(); const bool ok = fit (now); end();
        if (! ok) { if (! quiet) toast ("No signal yet. Play the loudest part for a few seconds, then press again."); return false; }
        toast ("Every line now fits this music, the way " + juce::String (r->name) + " means it.");
        return true;
    }
    std::vector<float> L, R; double fs; proc.recent.copy (L, R, fs);
    const auto l = mbp::measure (L, R, fs, proc.readParams());
    if (! l.ok) { if (! quiet) toast ("No signal yet. Play the loudest part for a few seconds, then press again."); return false; }
    begin();
    for (int b = 0; b < proc.bandCount(); ++b)
    {
        const auto id = [b] (char w) { return mbp::id (w, b); };
        if (proc.get (id ('r')) < 1.005f && proc.get (id ('v')) < 1.005f) setReal (id ('r'), 2.5f);
        mbp::Want w { 0, proc.get (id ('r')), proc.get (id ('a')), proc.get (id ('e')), 0, proc.get (id ('v')) };
        if (w.ratio > 1.005f) w.down = 3;
        if (w.upRatio > 1.005f) w.up = 3;
        if (l.series[(size_t) b].empty()) continue;
        float t = proc.get (id ('t')), u = proc.get (id ('u')); mbp::aim (w, real ("knee"), l.series[(size_t) b], l.step, t, u);
        if (w.down > 0) setReal (id ('t'), t);
        if (w.up > 0) setReal (id ('u'), u);
    }
    end();
    toast ("About 3 dB off the loud parts of every band.");
    return true;
}

void MbEditor::less() { change ("amount", std::round (real ("amount") * 0.75f)); toast ("A quarter less in every band."); }

void MbEditor::presetChosen (const Preset& chosen)
{
    select (selected);
    if (chosen.user) return;
    const auto* r = mbp::find (chosen.name);
    if (r == nullptr) return;
    begin(); const bool ok = fit (*r); end();
    if (ok) toast (chosen.name + ": set to this music. " + chosen.description);
}

void MbEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "What each band is doing, over the last seconds.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (190)); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    bandSeg = &seg (*deck, { "1", "2", "3", "4", "5" }, { "Select band 1.", "Select band 2.", "Select band 3.", "Select band 4.", "Select band 5." });
    bandSeg->onChange = [this] (int i) { select (i); if (i >= proc.bandCount()) toast ("This setting has " + juce::String (proc.bandCount()) + " bands."); };
    struct PerBand { Knob *t, *r, *a, *e, *u, *v, *g; Button *o, *s; };
    std::array<PerBand, N> pb {};
    for (int b = 0; b < N; ++b)
    {
        auto& q = pb[(size_t) b]; auto I = [b] (char w) { return mbp::id (w, b); };
        q.t = &knob (*deck, I ('t'), opt ("Threshold", "Above this level the band comes down.", plain)); q.r = &knob (*deck, I ('r'), opt ("Ratio", "How much of what is over the threshold is taken off. 1:1 is off.", ratioText));
        q.a = &knob (*deck, I ('a'), opt ("Attack", "How fast it comes down. Slow lets hits through.", msText)); q.e = &knob (*deck, I ('e'), opt ("Release", "How fast it lets go.", msText));
        q.u = &knob (*deck, I ('u'), opt ("Up below", "Below this level the band comes up. A band that is compressed most of the time has little quiet left to raise.", plain)); q.v = &knob (*deck, I ('v'), opt ("Up ratio", "How much quiet parts are brought up. 1:1 is off. Near silence is never raised.", ratioText));
        q.g = &knob (*deck, I ('g'), opt ("Gain", "Level of the band.", plain, 13, "", "", true));
        q.o = &toggle (*deck, I ('o'), "On", "Band on. Off leaves the band as it is.", true); q.s = &toggle (*deck, I ('s'), "Solo", "Solo: plays this band only.", true);
        perBand[(size_t) b] = { q.t, q.r, q.a, q.e, q.u, q.v, q.g, q.o, q.s };
    }
    auto& bands = paramSeg (*deck, "bands", { "2", "3", "4", "5" }, { "Two bands.", "Three bands.", "Four bands.", "Five bands." });
    auto& slope = paramSeg (*deck, "slope", { "24", "48" }, { "24 dB per octave: the smoothest.", "48 dB per octave: bands further apart." });
    auto& phase = paramSeg (*deck, "phase", { "Natural", "Straight" }, { "Natural phase: no delay.", "Straight phase: nothing shifts in time between bands. 48 ms of delay, the DAW lines it up." });
    auto& amount = knob (*deck, "amount", amountO()); auto& mix = knob (*deck, "mix", mixO()); auto& out = knob (*deck, "out", opt ("Output", "Output level.", plain, 13, "", "", true));
    auto ko = opt ("Knee", "How gently compression begins, for every band.", plain); ko.style = Knob::Style::compact; auto& knee = knob (*deck, "knee", ko);
    auto lo = opt ("Lookahead", "How early the bands react. Adds this much delay.", msText); lo.style = Knob::Style::compact; auto& look = knob (*deck, "look", lo);
    auto& autoB = toggle (*deck, "auto", "Auto level", autoHelp, true); auto& sc = toggle (*deck, "extSc", "Sidechain", "Another track drives the bands: send it to this plugin's sidechain input.", true);
    auto& pnt = painter (*deck);
    deck->layout = [&, pb, this] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kw = 56, kh = 50 + 25;
        auto hero = [&] (Knob& k, int x, const char* t, bool label) { k.setBounds (x + 1, top + 8, kw - 3, kh); if (label) pnt.under (k, t); };
        int x = 12;
        pnt.groups.push_back ({ { x, 14, 96, 20 }, [this] { return "Band " + juce::String (selected + 1) + ": " + bandName (selected); } });
        bandSeg->setBounds (x, top + 8, 96, 24); for (auto& q : pb) { q.o->setBounds (x, top + 40, 96, 22); q.s->setBounds (x, top + 68, 96, 22); } x += 96 + 14;
        pnt.seps.push_back (x - 7);
        pnt.groups.push_back ({ { x, 14, 4 * kw, 20 }, "Down" }); bool lab = true; for (auto& q : pb) { hero (*q.t, x, "Threshold", lab); hero (*q.r, x + kw, "Ratio", lab); hero (*q.a, x + 2 * kw, "Attack", lab); hero (*q.e, x + 3 * kw, "Release", lab); lab = false; } x += 4 * kw + 14;
        pnt.seps.push_back (x - 7);
        pnt.groups.push_back ({ { x, 14, 2 * kw, 20 }, "Up" }); lab = true; for (auto& q : pb) { hero (*q.u, x, "Below", lab); hero (*q.v, x + kw, "Ratio", lab); lab = false; } x += 2 * kw + 14;
        pnt.seps.push_back (x - 7);
        pnt.groups.push_back ({ { x, 14, kw, 20 }, "Level" }); lab = true; for (auto& q : pb) { hero (*q.g, x, "Gain", lab); lab = false; } x += kw + 14;
        pnt.seps.push_back (x - 7);
        pnt.groups.push_back ({ { x, 14, 112, 20 }, "Bands" }); bands.setBounds (x, top + 6, 112, 24); slope.setBounds (x, top + 36, 112, 24); phase.setBounds (x, top + 66, 112, 24); x += 112 + 14;
        pnt.seps.push_back (x - 7);
        pnt.groups.push_back ({ { x, 14, 3 * kw, 20 }, "Everything" }); hero (amount, x, "Amount", true); hero (mix, x + kw, "Mix", true); hero (out, x + 2 * kw, "Output", true); x += 3 * kw + 12;
        knee.setBounds (x, top - 10, 45, 58); pnt.under (knee, "Knee"); look.setBounds (x + 48, top - 10, 45, 58); pnt.under (look, "Look");
        autoB.setBounds (x, top + 62, 94, 20); sc.setBounds (x, top + 85, 94, 20);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void MbEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    check = &make<CheckCards> (*body, "Band check", "Band check: what the bands are doing, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "How much", "Balance", "Level", "Low end" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    juce::StringArray names, helps; for (auto& g : mbp::goals) { names.add (g.name); helps.add (g.help); }
    goalSeg = &seg (*deck, names, helps, 9.0f);
    goalSeg->onChange = [this] (int i) { chooseGoal (i); };
    auto& amount = knob (*deck, "amount", amountO()); auto& mix = knob (*deck, "mix", mixO());
    auto& go = button (*deck, "Fit to my music", "sets every line", "Listens back over the last seconds and sets every threshold, so the goal does what it says on this music.", [this] { fitToMusic(); });
    auto& match = toggle (*deck, "auto", "Auto level", autoHelp); match.hint = "same loudness";
    auto& lessB = button (*deck, "Less", "a quarter less", "A quarter less in every band.", [this] { less(); });
    auto& pnt = painter (*deck);
    deck->layout = [&, this] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38;
        pnt.groups.push_back ({ { 24, 14, 330, 20 }, "Goal" }); goalSeg->setBounds (24, top + 22, 330, 34);
        pnt.subs.push_back ({ { 24, top + 68, 330, 12 }, "sets bands, ratios and times" });
        pnt.seps = { 368, 498, 630 };
        pnt.groups.push_back ({ { 376, 14, 112, 20 }, "Amount" }); amount.setBounds (376, top - 4, 112, 98 + 25);
        pnt.groups.push_back ({ { 508, 14, 112, 20 }, "Mix" }); mix.setBounds (508, top - 4, 112, 98 + 25);
        pnt.groups.push_back ({ { 640, 14, 290, 20 }, "One-click helpers" }); go.setBounds (644, top + 2, 282, 30); match.setBounds (644, top + 38, 282, 30); lessB.setBounds (644, top + 74, 282, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void MbEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const int B = proc.bandCount(); const float st = proc.loudOut.getShortTerm();
    const char* n[] = { "How much", "Balance", "Level", "Low end" };
    if (real ("bypass") > 0.5f) { r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." }; for (size_t i = 1; i < 4; ++i) r[i] = { S::ok, n[i], "Nothing while bypassed." }; check->refresh(); return; }
    if (! std::isfinite (st) || st < -60.0f) { for (size_t i = 0; i < 4; ++i) r[i] = { S::ok, n[i], "Waiting for signal." }; check->refresh(); return; }
    for (int b = 0; b < B; ++b) if (proc.get (mbp::id ('s', b)) > 0.5f) { r[0] = { S::warn, "You hear one band", "Solo is on.", "Solo off", [this] { begin(); for (int i = 0; i < N; ++i) setReal (mbp::id ('s', i), 0); end(); } }; for (size_t i = 1; i < 4; ++i) r[i] = { S::ok, n[i], "Waiting for the whole sound." }; check->refresh(); return; }
    float deepest = 0, up = 0, sum = 0; int db = 0; for (int b = 0; b < B; ++b) { if (avgDown[(size_t) b] > deepest) { deepest = avgDown[(size_t) b]; db = b; } up = std::max (up, avgUp[(size_t) b]); sum += avgDown[(size_t) b]; }
    const auto fitIt = [this] { fitToMusic(); }; const auto back = [this] { less(); };
    const auto name = [this] (int b) { return bandName (b).toLowerCase() + " band"; };
    if (deepest < 0.3f && up < 0.3f) r[0] = { S::warn, "Nothing happens yet", "No band reaches its line.", "Fit to my music", fitIt };
    else if (deepest < 4) r[0] = { S::ok, "Gentle", "At most " + juce::String (std::max (deepest, up), 1) + " dB, " + (up > deepest ? "bringing quiet parts up." : "in the " + name (db) + ".") };
    else if (deepest < 9) r[0] = { S::ok, "Firm", juce::String (deepest, 1) + " dB off the " + name (db) + "." };
    else r[0] = { S::warn, "Heavy", juce::String (deepest, 1) + " dB off the " + name (db) + ". Great as an effect.", "Less", back };
    if (deepest > 3 && B > 1 && deepest > 3.0f * (sum - deepest) / (float) (B - 1) + 2.0f) r[1] = { S::ok, "One band does the work", "Mostly the " + name (db) + ". The rest is left alone." };
    else r[1] = { S::ok, "Shared between the bands", "No band is held much more than the others." };
    const float li = proc.loudIn.getShortTerm(), d = st - li;
    if (real ("auto") > 0.5f) r[2] = { S::ok, "Level matched", "As loud as the original (" + txt::db (proc.matchDb.load()) + " applied)." };
    else if (std::abs (d) < 1.0f) r[2] = { S::ok, "Same level", "As loud as the input." };
    else r[2] = { S::warn, d > 0 ? "Louder than the input" : "Quieter than the input", juce::String (std::abs (d), 1) + " dB. Louder often sounds better without being better.", "Match it", [this] { change ("auto", 1); } };
    const bool fast = proc.get (mbp::id ('e', 0)) < 70.0f && proc.get (mbp::id ('a', 0)) < 5.0f && avgDown[0] > 3;
    if (fast) r[3] = { S::warn, "The lows may growl", "Very fast times on deep notes bend the wave.", "Smoother", [this] { begin(); setReal (mbp::id ('a', 0), 15); setReal (mbp::id ('e', 0), 120); end(); } };
    else if (avgDown[0] > 1) r[3] = { S::ok, "Low end held", "About " + juce::String (avgDown[0], 1) + " dB off its loud parts." };
    else r[3] = { S::ok, "Low end free", "The low end moves as it did." };
    check->refresh();
}

void MbEditor::tick (bool simple, int count)
{
    pro = ! simple;
    mb::Engine::Frame fr[128];
    const int n = proc.popBands (fr, 128), B = proc.bandCount();
    if (selected >= B) select (B - 1);
    for (int b = 0; b < N; ++b)
    {
        float lv = -120, g = 0; for (int i = 0; i < n; ++i) { lv = std::max (lv, fr[i].level[(size_t) b]); if (std::abs (fr[i].gain[(size_t) b]) > std::abs (g)) g = fr[i].gain[(size_t) b]; }
        if (n == 0) continue;
        auto& l = level[(size_t) b]; l = lv > l ? lv : l + (lv - l) * 0.12f;
        auto& k = gain[(size_t) b]; k += (g - k) * (std::abs (g) > std::abs (k) ? 0.7f : 0.2f); if (std::abs (k) < 0.02f) k = 0;
        // loud moments count: a peak that falls slowly, about four seconds
        auto& d = avgDown[(size_t) b]; d = std::max (-g, d * std::pow (0.996f, (float) n)); if (d < 0.02f) d = 0;
        auto& u = avgUp[(size_t) b]; u = std::max (g, u * std::pow (0.996f, (float) n)); if (u < 0.02f) u = 0;
    }
    if (count % 2 == 0)
    {
        const double fs = proc.currentRate.load(); const int order = 12 + juce::jmax (0, juce::roundToInt (std::log2 (fs / 48000.0))), M = 1 << order;
        if (order != fftOrder) { fft = std::make_unique<juce::dsp::FFT> (order); fftOrder = order; fftBuf.assign ((size_t) (2 * M), 0.0f); spectrum.assign ((size_t) (M / 2), 0.0f); }
        binHz = fs / M; proc.ringIn.latest (fftBuf.data(), M);
        for (int i = 0; i < M; ++i) fftBuf[(size_t) i] *= 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) M);
        std::fill (fftBuf.begin() + M, fftBuf.end(), 0.0f); fft->performFrequencyOnlyForwardTransform (fftBuf.data(), true);
        for (int k = 0; k < M / 2; ++k) { const float a = fftBuf[(size_t) k] * 4.0f / (float) M, p = a * a; auto& s = spectrum[(size_t) k]; s += (p - s) * (p > s ? 0.5f : 0.12f); }
    }
    (simple ? displaySimple : displayPro)->repaint();
    if (count % 12 != 0) return;
    if (simple) { goalSeg->setIndex (proc.matchingGoal(), false); updateCheck(); return; }
    panel->heading = "Off the loud parts"; panel->note = real ("bypass") > 0.5f ? "bypassed" : (proc.sidechainConnected.load() && real ("extSc") > 0.5f ? "sidechain" : "");
    float deepest = 0; for (int b = 0; b < B; ++b) deepest = std::max (deepest, avgDown[(size_t) b]);
    panel->bigName = "Most in one band"; panel->bigValue = juce::String (deepest, 1); panel->unit = "dB"; panel->rows.clear();
    for (int b = 0; b < B; ++b) panel->rows.push_back ({ bandName (b), (avgDown[(size_t) b] < 0.05f ? juce::String ("0.0") : txt::num (-avgDown[(size_t) b], 1)) + (avgUp[(size_t) b] > 0.05f ? "  +" + juce::String (avgUp[(size_t) b], 1) : juce::String()), b == selected });
    panel->rows.push_back ({ "Auto level", real ("auto") > 0.5f ? txt::db (proc.matchDb.load()) : juce::String ("off") });
    if (real ("extSc") > 0.5f && ! proc.sidechainConnected.load()) panel->rows.push_back ({ "Sidechain", "not connected", true });
    panel->repaint();
}
