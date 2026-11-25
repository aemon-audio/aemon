// Aemon Clipper. AGPL-3.0-or-later.
#include "ClipEditor.h"

using namespace aemon;

namespace clipui
{
/** Peaks over time with what is cut (left), the curve and the waveform going through it (right). */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (ClipEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "Left: your peaks over time. Amber is what is cut off above the ceiling. Right: the curve, and your waveform going through it."; }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const float side = std::min (b.getHeight() - 50, b.getWidth() * 0.36f);
        const juce::Rectangle<float> C (b.getRight() - side - 16, 28, side, side), H (40, 28, C.getX() - 40 - 24, b.getHeight() - 46);
        const float ceilDb = ed.real ("ceil");
        auto hy = [&] (float d) { return H.getBottom() - (juce::jlimit (-30.0f, 12.0f, d) + 30.0f) / 42.0f * H.getHeight(); };
        drawDbAxis (g, H, { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f }, hy, true);
        const auto& h = ed.history;
        const int n = (int) h.size();
        const float cy = hy (ceilDb);
        if (n > 1)
        {
            const float dx = H.getWidth() / (float) (EffectEditor::historyLen - 1);
            auto xa = [&] (int i) { return H.getRight() - (float) (n - 1 - i) * dx; };
            juce::Path in, out;
            in.startNewSubPath (xa (0), H.getBottom());
            for (int i = 0; i < n; ++i)
            {
                in.lineTo (xa (i), hy (h[(size_t) i].inDb));
                const float o = hy (std::min (h[(size_t) i].outDb, h[(size_t) i].inDb));
                if (i == 0) out.startNewSubPath (xa (i), o); else out.lineTo (xa (i), o);
            }
            in.lineTo (H.getRight(), H.getBottom()); in.closeSubPath();
            { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (juce::Rectangle<float> (H.getX(), H.getY(), H.getWidth(), cy - H.getY()).toNearestInt()); g.setColour (col::ember.withAlpha (0.55f)); g.fillPath (in); }
            { juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (juce::Rectangle<float> (H.getX(), cy, H.getWidth(), H.getBottom() - cy).toNearestInt()); g.setColour (col::phosphor.withAlpha (0.16f)); g.fillPath (in); }
            juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (H.expanded (1, 3).toNearestInt());
            g.setColour (col::phosphor); g.strokePath (out, juce::PathStrokeType (1.4f));
        }
        drawDashed (g, { H.getX(), cy, H.getRight(), cy }, col::sigClip.withAlpha (0.8f));
        const auto dot = "  " + juce::String::charToString (0xb7) + "  ";
        drawEngraved (g, "Peaks" + dot + "what is cut", { H.getX(), 7, 220, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "ceiling " + txt::num (ceilDb), { H.getRight() - 160, 7, 160, 14 }, 10.0f, col::sigClip, juce::Justification::centredRight);
        drawEngraved (g, "Curve" + dot + "waveform", { C.getRight() - 200, 7, 200, 14 }, 10.0f, col::bone3, juce::Justification::centredRight);

        // curve and waveform
        g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (C);
        juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (C.toNearestInt());
        auto X = [&] (float v) { return C.getX() + (v + 2.0f) / 4.0f * C.getWidth(); };
        auto Y = [&] (float v) { return C.getBottom() - (v + 1.3f) / 2.6f * C.getHeight(); };
        g.setColour (col::bone1.withAlpha (0.06f)); g.fillRect (C.getX(), Y (0), C.getWidth(), 1.0f); g.fillRect (X (0), C.getY(), 1.0f, C.getHeight());
        const float c = juce::Decibels::decibelsToGain (ceilDb), push = juce::Decibels::decibelsToGain (ed.real ("push"));
        aemon::dist::ClipCurve curve; curve.knee = ed.real ("soft") / 100.0;
        auto f = [&] (float v) { return c * (float) curve.f (v / c); };
        drawDashed (g, { X (-2), Y (-2), X (2), Y (2) }, col::bone1.withAlpha (0.18f), 1.0f, 2.0f);
        for (float v : { c, -c }) drawDashed (g, { C.getX(), Y (v), C.getRight(), Y (v) }, col::sigClip.withAlpha (0.5f));
        {
            const int N = juce::jlimit (256, 8192, juce::roundToInt (0.03 * ed.proc.currentRate.load())), hop = juce::jmax (1, N / 300);     // 30 ms at every rate
            std::vector<float> w ((size_t) N); ed.proc.ringIn.latest (w.data(), N);
            juce::Path in, out;
            for (int i = 0; i < N; i += hop)
            {
                const float x = C.getX() + (float) i / (float) N * C.getWidth(), v = w[(size_t) i] * push;
                if (i == 0) { in.startNewSubPath (x, Y (v)); out.startNewSubPath (x, Y (f (v))); } else { in.lineTo (x, Y (v)); out.lineTo (x, Y (f (v))); }
            }
            g.setColour (col::ember.withAlpha (0.55f)); g.strokePath (in, juce::PathStrokeType (1.0f));
            g.setColour (col::phosphor.withAlpha (0.9f)); g.strokePath (out, juce::PathStrokeType (1.2f));
        }
        juce::Path p;
        for (float v = -2.0f; v <= 2.0f; v += 0.02f) if (v <= -2.0f) p.startNewSubPath (X (v), Y (f (v))); else p.lineTo (X (v), Y (f (v)));
        g.setColour (col::ember.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember); g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    ClipEditor& ed;
};
} // namespace clipui

using namespace clipui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {})
    {
        Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o;
    }
    Knob::Options pushO() { return opt ("Push", "How far the peaks are driven over the ceiling. What goes over is cut off.", txt::db, 10, "0", "+18"); }
    Knob::Options ceilO() { return opt ("Ceiling", "Nothing gets louder than this, between the samples too.", txt::db, 7, txt::minusSign() + "12", "0"); }
    Knob::Options softO() { return opt ("Softness", "How gently the top bends before it is cut. Hard keeps more of the sound untouched, soft is warmer and starts earlier.", txt::percent, 11, "hard", "soft"); }
    const char* matchHelp = "Equal loudness: plays the clipped sound as loud as the original, so bypass compares sound, not volume. Only while listening, never in an export.";
}

ClipEditor::ClipEditor (ClipProcessor& p) : EffectEditor (p), proc (p)
{
    buildSimple();
    buildPro();
    start();
}

float ClipEditor::offPeaks() const
{
    float m = 0; for (size_t i = history.size() > 260 ? history.size() - 260 : 0; i < history.size(); ++i) m = std::max (m, history[i].aux);
    return m;
}

bool ClipEditor::shavePeaks (bool quiet)
{
    const auto s = clipp::suggest (proc.levels.copy(), real ("ceil"), real ("soft"));
    if (! s.ok) { if (! quiet) toast ("No signal yet. Play the loudest part for a few seconds, then press again."); return false; }
    change ("push", s.push);
    toast ("Push " + txt::db (s.push) + ": about " + juce::String (s.off, 1) + " dB off the tallest peaks.");
    return true;
}

void ClipEditor::presetChosen (const Preset& p) { if (p.name.startsWith ("Master: 2 to 3 dB")) shavePeaks (true); }

void ClipEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "What is cut off, in numbers.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (210)); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& push = knob (*deck, "push", pushO()); auto& ceil = knob (*deck, "ceil", ceilO()); auto& soft = knob (*deck, "soft", softO());
    juce::StringArray names, helps; for (auto& s : clipp::shapes) { names.add (s.name); helps.add (s.help); }
    shape = &seg (*deck, names, helps);
    shape->onChange = [this] (int i) { change ("soft", clipp::shapes[i].soft); };
    auto lo = opt ("Stereo link", "Stereo link: at 100 % both sides are cut together, so the image holds still.", txt::percent); lo.style = Knob::Style::compact;
    auto& link = knob (*deck, "link", lo);
    auto& mix = knob (*deck, "mix", opt ("Mix", "Blend with the untouched sound. Below 100 % peaks can pass the ceiling.", txt::percent, 11, "dry", "wet"));
    auto& os = paramSeg (*deck, "os", { "Off", "2x", "4x", "8x" }, { "Off: lowest CPU, most false tones.", "2x: light.", "4x: good.", "8x: the default. False tones are 90 dB down." });
    auto& match = toggle (*deck, "match", "Equal loudness", matchHelp, true);
    auto& delta = toggle (*deck, "delta", "Hear removed", "Hear removed: plays only what is cut off. Short clicks are fine. If you hear the music, it is too much.", true);
    auto& pnt = painter (*deck);
    deck->layout = [&, this] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kh = 70 + 25;
        auto hero = [&] (Knob& k, int x, const char* t) { k.setBounds (x, top, 78, kh); pnt.under (k, t); };
        int x = 60;
        pnt.groups.push_back ({ { x, 14, 164, 20 }, "Level" }); hero (push, x, "Push"); hero (ceil, x + 86, "Ceiling"); x += 188;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 262, 20 }, "Shape" }); hero (soft, x, "Softness");
        shape->setBounds (x + 92, top + 2, 170, 24); link.setBounds (x + 145, top + 28, 64, 67); pnt.under (link, "Stereo link"); x += 286;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 78, 20 }, "Blend" }); hero (mix, x, "Mix"); x += 102;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 132, 20 }, "Quality" }); pnt.subs.push_back ({ { x, top + 22, 132, 11 }, "Oversampling" }); os.setBounds (x, top + 38, 132, 24); x += 156;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 132, 20 }, "Monitor" }); match.setBounds (x, top + 18, 132, 24); delta.setBounds (x, top + 52, 132, 24);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void ClipEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    check = &make<CheckCards> (*body, "Clip check", "Clip check: what the clipper is doing, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "How much", "Will you hear it", "Level", "Room gained" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& push = knob (*deck, "push", pushO()); auto& soft = knob (*deck, "soft", softO()); auto& ceil = knob (*deck, "ceil", ceilO());
    auto& go = button (*deck, "Shave the peaks for me", "unheard", "Finds the most you can take off the peaks before you would hear it.", [this] { shavePeaks(); });
    auto& match = toggle (*deck, "match", "Compare at equal loudness", matchHelp); match.hint = "honest A/B";
    auto& lessB = button (*deck, "Less", "back off 2 dB", "Backs Push off by 2 dB.", [this] { less(); });
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38; int x = 80;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &push, "Push" }, { &soft, "Softness" }, { &ceil, "Ceiling" } }) { pnt.groups.push_back ({ { x, 14, 112, 20 }, t }); k->setBounds (x, top - 4, 112, 98 + 25); x += 136; pnt.seps.push_back (x - 12); }
        pnt.groups.push_back ({ { x + 20, 14, 300, 20 }, "One-click helpers" });
        go.setBounds (x + 26, top + 2, 290, 30); match.setBounds (x + 26, top + 38, 290, 30); lessB.setBounds (x + 26, top + 74, 290, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void ClipEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const float off = offPeaks(), t = proc.clippedPercent.load(), st = proc.loudOut.getShortTerm();
    if (! std::isfinite (st) || st < -60.0f) { const char* n[] = { "How much", "Will you hear it", "Level", "Room gained" }; for (size_t i = 0; i < 4; ++i) r[i] = { S::ok, n[i], "Waiting for signal." }; }
    else if (real ("bypass") > 0.5f) { r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." }; for (size_t i = 1; i < 4; ++i) r[i] = { S::ok, i == 1 ? "Will you hear it" : (i == 2 ? "Level" : "Room gained"), "Nothing while bypassed." }; }
    else
    {
        const auto back = [this] { less(); };
        const auto amount = "About " + juce::String (off, 1) + " dB off the peaks, " + juce::String (t, 1) + " % of the time.";
        if (off < 0.15f) r[0] = { S::ok, "Nothing clipped", "Every peak is under the ceiling. The sound is untouched." };
        else if (off < 4.0f) r[0] = { S::ok, "A little off the top", amount };
        else if (off < 8.0f) r[0] = { S::warn, "A lot", amount, "Less", back };
        else r[0] = { S::bad, "Extreme", amount, "Less", back };
        if (t < 1.5f) r[1] = { S::ok, "Unlikely to be heard", "Only the very tips are cut, too short to notice." };
        else if (t < 6.0f) r[1] = { S::warn, "Adds some edge", "Sustained parts are cut too. Compare with bypass.", "Less", back };
        else r[1] = { S::bad, "Clearly distorted", "Long parts of the wave are cut flat.", "Less", back };
        if (real ("match") > 0.5f) r[2] = { S::ok, "Level matched", "Played " + txt::num (std::abs (proc.match.nowDb.load())) + " dB lower, as loud as the original." };
        else { const float d = st - proc.loudIn.getShortTerm(); r[2] = std::abs (d) < 1.0f ? CheckCards::Row { S::ok, "Same level", "As loud as the input." } : CheckCards::Row { S::warn, d > 0 ? "Louder than the input" : "Quieter than the input", juce::String (std::abs (d), 1) + " dB. Louder often sounds better without being better.", "Match it", [this] { change ("match", 1); } }; }
        if (off > 0.3f) r[3] = { S::ok, juce::String (off, 1) + " dB of room gained", "A limiter after this can go that much louder with the same effort." };
        else r[3] = { S::ok, "No room gained yet", "Push until the tallest peaks are trimmed." };
    }
    check->refresh();
}

void ClipEditor::tick (bool simple, int frame)
{
    shape->setIndex (clipp::matchingShape (real ("soft")), false);
    (simple ? displaySimple : displayPro)->repaint();
    if (frame % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const float off = offPeaks();
    float in = -120, out = -120; for (size_t i = history.size() > 120 ? history.size() - 120 : 0; i < history.size(); ++i) { in = std::max (in, history[i].inDb); out = std::max (out, history[i].outDb); }
    auto lvl = [] (float v) { return v > -70 ? txt::num (v) + " dB" : juce::String::charToString (0x2014); };
    panel->heading = "What is cut"; panel->note = off < 0.15f ? "bit-exact" : "clipping";
    panel->bigName = "Off the peaks"; panel->bigValue = txt::num (off); panel->unit = "dB";
    panel->rows = { { "Time clipped", juce::String (proc.clippedPercent.load(), 1) + " %" }, { "Peak in", lvl (in) }, { "Peak out", lvl (out) },
                    { "Loudness", std::isfinite (proc.loudOut.getShortTerm()) && proc.loudOut.getShortTerm() > -70 ? txt::num (proc.loudOut.getShortTerm()) + " LUFS" : juce::String::charToString (0x2014) },
                    { "Equal loudness", real ("match") > 0.5f ? txt::num (proc.match.nowDb.load()) + " dB" : juce::String ("off"), true } };
    panel->repaint();
}
