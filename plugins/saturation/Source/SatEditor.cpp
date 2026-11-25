// Aemon Saturation. AGPL-3.0-or-later.
#include "SatEditor.h"

using namespace aemon;

namespace satui
{
/** The harmonics it adds (left), the curve and the waveform going through it (right). */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (SatEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "Left: the harmonics added to a tone, 2 to 9. Lilac is even (fuller), amber is odd (grittier). Right: the curve, and your waveform going through it."; }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        const float side = std::min (b.getHeight() - 50, b.getWidth() * 0.36f);
        const juce::Rectangle<float> C (b.getRight() - side - 16, 28, side, side), H (44, 28, C.getX() - 44 - 26, b.getHeight() - 56);
        auto hy = [&] (float d) { return H.getY() + juce::jlimit (0.0f, 72.0f, -d) / 72.0f * H.getHeight(); };
        drawDbAxis (g, H, { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f }, hy);
        const float bw = H.getWidth() / 9.0f;
        for (size_t i = 0; i < 9; ++i)
        {
            const float v = ed.harm[i], x = H.getX() + (float) i * bw + bw * 0.22f, w = bw * 0.56f, y = hy (v);
            const auto c = i == 0 ? col::phosphor.withAlpha (0.5f) : (i % 2 == 1 ? col::lilac : col::ember);
            if (v > -72.0f)
            {
                if (i > 0) { g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (x - 4, y - 4, w + 8, H.getBottom() - y + 4, 5); }
                g.setColour (c); g.fillRoundedRectangle (x, y, w, H.getBottom() - y, 3);
            }
            g.setFont (Fonts::mono (10.0f)); g.setColour (col::bone3);
            g.drawText (i == 0 ? juce::String ("tone") : juce::String (i + 1), juce::Rectangle<float> (x - 14, H.getBottom() + 7, w + 28, 12), juce::Justification::centred);
            if (i > 0 && v > -60.0f) { g.setColour (col::bone2); g.drawText (txt::num (v, 0), juce::Rectangle<float> (x - 14, y - 16, w + 28, 12), juce::Justification::centred); }
        }
        const auto dot = "  " + juce::String::charToString (0xb7) + "  ";
        drawEngraved (g, "Harmonics added, dB", { H.getX(), 7, 220, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, "odd", { H.getRight() - 40, 7, 40, 14 }, 10.0f, col::ember, juce::Justification::centredRight);
        drawEngraved (g, "even", { H.getRight() - 100, 7, 54, 14 }, 10.0f, col::lilac, juce::Justification::centredRight);
        drawEngraved (g, "Curve" + dot + "waveform", { C.getRight() - 200, 7, 200, 14 }, 10.0f, col::bone3, juce::Justification::centredRight);

        g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (C);
        juce::Graphics::ScopedSaveState s (g); g.reduceClipRegion (C.toNearestInt());
        auto X = [&] (float v) { return C.getX() + (v + 1.0f) / 2.0f * C.getWidth(); };
        auto Y = [&] (float v) { return C.getBottom() - (v + 1.1f) / 2.2f * C.getHeight(); };
        g.setColour (col::bone1.withAlpha (0.06f)); g.fillRect (C.getX(), Y (0), C.getWidth(), 1.0f); g.fillRect (X (0), C.getY(), 1.0f, C.getHeight());
        drawDashed (g, { X (-1), Y (-1), X (1), Y (1) }, col::bone1.withAlpha (0.18f), 1.0f, 2.0f);
        const auto curve = ed.proc.curve();
        float top = 0; for (float v = -1; v <= 1; v += 0.02f) top = std::max (top, std::abs ((float) curve.f (v)));
        const float sc = 1.0f / std::max (0.02f, top);
        {
            const int N = juce::jlimit (256, 8192, juce::roundToInt (0.03 * ed.proc.currentRate.load())), hop = juce::jmax (1, N / 300);     // 30 ms at every rate
            std::vector<float> w ((size_t) N); ed.proc.ringIn.latest (w.data(), N);
            float pk = 0.05f; for (float v : w) pk = std::max (pk, std::abs (v));
            const float zoom = std::min (1.0f / pk, 4.0f) * 0.9f;
            double ein = 1e-9, eout = 1e-9; for (float v : w) { const double o = curve.f (v); ein += v * v; eout += o * o; }
            const float same = (float) std::sqrt (ein / eout);      // drawn as loud as the original: what changes is the shape
            juce::Path in, out;
            for (int i = 0; i < N; i += hop)
            {
                const float x = C.getX() + (float) i / (float) N * C.getWidth(), a = w[(size_t) i] * zoom, o = juce::jlimit (-1.1f, 1.1f, (float) curve.f (w[(size_t) i]) * zoom * same);
                if (i == 0) { in.startNewSubPath (x, Y (a)); out.startNewSubPath (x, Y (o)); } else { in.lineTo (x, Y (a)); out.lineTo (x, Y (o)); }
            }
            g.setColour (col::bone1.withAlpha (0.3f)); g.strokePath (in, juce::PathStrokeType (1.0f));
            g.setColour (col::phosphor.withAlpha (0.9f)); g.strokePath (out, juce::PathStrokeType (1.2f));
        }
        juce::Path p;
        for (int i = 0; i <= 200; ++i) { const float v = -1.0f + 0.01f * (float) i; const juce::Point<float> q (X (v), Y ((float) curve.f (v) * sc)); if (i == 0) p.startNewSubPath (q); else p.lineTo (q); }
        g.setColour (col::ember.withAlpha (0.25f)); g.strokePath (p, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col::ember); g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    SatEditor& ed;
};
} // namespace satui

using namespace satui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 11, juce::String lo = {}, juce::String hi = {}, bool bipolar = false)
    {
        Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; o.bipolar = bipolar; o.detent = bipolar; return o;
    }
    const char* matchHelp = "Level matched: the result is as loud as the original, so you judge the colour and not the volume.";
    juce::StringArray charNames() { juce::StringArray a; for (auto& c : satp::chars) a.add (c.name); return a; }
    juce::StringArray charHelps() { juce::StringArray a; for (auto& c : satp::chars) a.add (c.help); return a; }
    const juce::StringArray focusNames { "All", "Lows", "Mids", "Highs" }, focusHelps { "All: the whole sound.", "Lows: weight and body.", "Mids: voices and guitars.", "Highs: presence and air." };
}

SatEditor::SatEditor (SatProcessor& p) : EffectEditor (p), proc (p)
{
    harm = satp::harmonics (proc.readParams());
    buildSimple();
    buildPro();
    start();
}

void SatEditor::setAmountForMe()
{
    change ("drive", satp::driveFor (proc.readParams(), 4.0f));
    toast ("Amount set to about 4 % of added harmonics.");
}

void SatEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "What is added to a tone at " + txt::minusSign() + "12 dB, in numbers.");
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); panel->setBounds (r.removeFromRight (210)); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& drive = knob (*deck, "drive", opt ("Drive", "How hard the sound is driven into the curve. More drive, more harmonics.", txt::db, 11, "0", "+30"));
    auto& bias = knob (*deck, "bias", opt ("Bias", "Bias: pushes the curve off centre. More bias, more even harmonics, rounder and fuller.", txt::percent, 9, "odd", "even"));
    auto& ch = paramSeg (*deck, "char", charNames(), charHelps(), 9.5f);
    auto& fo = paramSeg (*deck, "focus", focusNames, focusHelps);
    auto& tone = knob (*deck, "tone", opt ("Tone", "Tone of the result, darker or brighter.", txt::db, 13, "dark", "bright", true));
    auto& mix = knob (*deck, "mix", opt ("Mix", "Blend with the untouched sound. The two are lined up to the sample, so nothing combs.", txt::percent, 11, "dry", "wet"));
    auto& out = knob (*deck, "out", opt ("Output", "Output level, on top of level matching.", txt::db, 13, txt::minusSign() + "12", "+12", true));
    auto& match = toggle (*deck, "match", "Level matched", matchHelp, true);
    auto& os = paramSeg (*deck, "os", { "Off", "2x", "4x", "8x" }, { "Off: lowest CPU, most false tones.", "2x: light.", "4x: good.", "8x: the default, the cleanest." });
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 32, kh = 70 + 25;
        auto hero = [&] (Knob& k, int x, const char* t) { k.setBounds (x, top, 78, kh); pnt.under (k, t); };
        int x = 48;
        pnt.groups.push_back ({ { x, 14, 164, 20 }, "Drive" }); hero (drive, x, "Drive"); hero (bias, x + 86, "Bias"); x += 188;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 220, 20 }, "Character" }); ch.setBounds (x, top + 8, 220, 24); pnt.subs.push_back ({ { x, top + 42, 220, 11 }, "Focus" }); fo.setBounds (x + 20, top + 58, 180, 24); x += 244;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 250, 20 }, "Output" }); hero (tone, x, "Tone"); hero (mix, x + 86, "Mix"); hero (out, x + 172, "Output"); x += 274;
        pnt.seps.push_back (x - 12);
        pnt.groups.push_back ({ { x, 14, 140, 20 }, "Quality" }); match.setBounds (x, top + 8, 140, 24); pnt.subs.push_back ({ { x, top + 42, 140, 11 }, "Oversampling" }); os.setBounds (x, top + 58, 140, 24);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void SatEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    check = &make<CheckCards> (*body, "Colour check", "Colour check: what is added, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "How much", "What it adds", "Level", "Harshness" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& ch = paramSeg (*deck, "char", charNames(), charHelps(), 9.5f);
    auto& fo = paramSeg (*deck, "focus", focusNames, focusHelps);
    auto& drive = knob (*deck, "drive", opt ("Amount", "How much colour. More amount, more harmonics.", txt::db, 11, "0", "+30"));
    auto& mix = knob (*deck, "mix", opt ("Mix", "Blend with the untouched sound.", txt::percent, 11, "dry", "wet"));
    auto& go = button (*deck, "Set amount for me", "noticeable, not harsh", "Finds an amount you can notice without it getting harsh.", [this] { setAmountForMe(); });
    auto& match = toggle (*deck, "match", "Level matched", matchHelp); match.hint = "same loudness";
    auto& lessB = button (*deck, "Cleaner", "back off 3 dB", "Backs the amount off by 3 dB.", [this] { cleaner(); });
    auto& pnt = painter (*deck);
    deck->layout = [&] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear();
        const int top = 38;
        pnt.groups.push_back ({ { 40, 14, 270, 20 }, "Character" }); ch.setBounds (40, top + 6, 270, 30); pnt.subs.push_back ({ { 40, top + 48, 270, 12 }, "Focus" }); fo.setBounds (70, top + 66, 210, 26);
        pnt.groups.push_back ({ { 340, 14, 112, 20 }, "Amount" }); drive.setBounds (340, top - 4, 112, 98 + 25);
        pnt.groups.push_back ({ { 476, 14, 112, 20 }, "Mix" }); mix.setBounds (476, top - 4, 112, 98 + 25);
        pnt.groups.push_back ({ { 620, 14, 300, 20 }, "One-click helpers" }); go.setBounds (626, top + 2, 290, 30); match.setBounds (626, top + 38, 290, 30); lessB.setBounds (626, top + 74, 290, 30);
        pnt.seps = { 326, 464, 606 };
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void SatEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    const float t = thdNow;
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (t < 0.2f) r[0] = { S::ok, "Clean", "Nothing is added. The sound is untouched.", "Set it for me", [this] { setAmountForMe(); } };
    else if (t < 3.0f) r[0] = { S::ok, "A hint", juce::String (t, 1) + " % of harmonics. Felt more than heard." };
    else if (t < 12.0f) r[0] = { S::ok, "Clearly coloured", juce::String (t, 1) + " % of harmonics." };
    else r[0] = { S::warn, "Distorted", juce::String (juce::roundToInt (t)) + " % of harmonics. Great as an effect.", "Cleaner", [this] { cleaner(); } };
    double ev = 0, od = 0; for (size_t i = 1; i < 9; ++i) (i % 2 == 1 ? ev : od) += std::pow (10.0, harm[i] / 10.0);
    if (t < 0.2f) r[1] = { S::ok, "What it adds", "Turn Amount up to hear the character." };
    else if (ev > od) r[1] = { S::ok, "Mostly even: fuller", "Octaves above your notes. Sounds warm and rounded." };
    else r[1] = { S::ok, "Mostly odd: grittier", "Adds edge and density." };
    const float li = proc.loudIn.getShortTerm(), lo = proc.loudOut.getShortTerm(), d = lo - li;
    if (! std::isfinite (li) || li < -60.0f || ! std::isfinite (lo)) r[2] = { S::ok, "Level", "Waiting for signal." };
    else if (real ("match") > 0.5f) r[2] = { S::ok, "Level matched", "As loud as the original (" + txt::db (proc.matchDb.load()) + " applied)." };
    else if (std::abs (d) < 1.0f) r[2] = { S::ok, "Same level", "As loud as the input." };
    else r[2] = { S::warn, d > 0 ? "Louder than the input" : "Quieter than the input", juce::String (std::abs (d), 1) + " dB. Louder often sounds better without being better.", "Match it", [this] { change ("match", 1); } };
    float high = -99; for (size_t i = 4; i < 9; ++i) high = std::max (high, harm[i]);
    if (t > 0.2f && high > -30.0f) r[3] = { S::warn, "Getting harsh", "High harmonics are strong. A darker tone or less amount helps.", "Darker", [this] { change ("tone", std::max (-6.0f, real ("tone") - 2.0f)); } };
    else r[3] = { S::ok, "Smooth", "High harmonics stay low." };
    check->refresh();
}

void SatEditor::tick (bool simple, int frame)
{
    if (frame % 3 == 0) { harm = satp::harmonics (proc.readParams()); thdNow = satp::thd (harm) * real ("mix") / 100.0f; }
    (simple ? displaySimple : displayPro)->repaint();
    if (frame % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    double ev = 0, od = 0; size_t top = 1; for (size_t i = 1; i < 9; ++i) { (i % 2 == 1 ? ev : od) += std::pow (10.0, harm[i] / 10.0); if (harm[i] > harm[top]) top = i; }
    const char* suffix = top == 1 ? "nd" : (top == 2 ? "rd" : "th");
    panel->heading = "Harmonics added"; panel->note = satp::chars[juce::jlimit (0, 3, juce::roundToInt (real ("char")))].name;
    panel->bigName = "In total"; panel->bigValue = juce::String (thdNow, 1); panel->unit = "%";
    panel->rows = { { "Even (fuller)", juce::String (100.0 * std::sqrt (ev), 1) + " %" }, { "Odd (grittier)", juce::String (100.0 * std::sqrt (od), 1) + " %" },
                    { "Strongest", thdNow < 0.05f ? juce::String::charToString (0x2014) : juce::String (top + 1) + suffix + ", " + txt::num (harm[top], 0) + " dB" },
                    { "Level match", real ("match") > 0.5f ? txt::db (proc.matchDb.load()) : juce::String ("off"), true } };
    panel->repaint();
}
