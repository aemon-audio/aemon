// Aemon Limiter. AGPL-3.0-or-later.
#include "LimEditor.h"

using namespace aemon;

namespace
{
    constexpr int baseW = 960, baseH = 600, historyLen = 400;
    const juce::String minusSign = juce::String::charToString (0x2212), dot = juce::String::charToString (0xb7), dash = juce::String::charToString (0x2014);
    juce::String mns (const juce::String& s) { return s.replace ("-", minusSign); }
    juce::String num (float v, int d = 1) { auto s = juce::String (v, d); if (s == "-0.0") s = "0.0"; return mns (s); }
    juce::String numOrDash (float v) { return std::isfinite (v) && v > -70.0f ? num (v) : dash; }
    juce::String fmtDb (float v) { return (v > 0.05f ? "+" : "") + num (v) + " dB"; }
    juce::String fmtCeil (float v) { return num (v) + " dBTP"; }
    juce::String fmtMs (float v) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }
    std::optional<float> parseNum (const juce::String& s)
    {
        auto t = s.replace (minusSign, "-").trim();
        if (! t.containsAnyOf ("0123456789")) return {};
        return t.retainCharacters ("-.0123456789").getFloatValue();
    }
}

namespace limui
{
//==============================================================================
/** History of level, reduction and loudness, with the ceiling and the target band. */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (LimEditor& e) : ed (e) {}
    juce::String getHelpText() const override
    {
        return "Grey: what comes in, after Push. Teal: what goes out. Lilac: loudness. Amber from the top: how much is turned down. Red: the ceiling. Band: your target.";
    }
    void paint (juce::Graphics& g) override
    {
        drawWell (g, getLocalBounds().toFloat(), 8);
        const juce::Rectangle<float> H (40, 26, (float) getWidth() - 80, (float) getHeight() - 44);
        auto hy = [&] (float db) { return H.getBottom() - (juce::jlimit (-48.0f, 3.0f, db) + 48.0f) / 51.0f * H.getHeight(); };
        auto gy = [&] (float gr) { return H.getY() + juce::jlimit (0.0f, 18.0f, -gr) / 18.0f * H.getHeight() * 0.45f; };
        g.setFont (Fonts::mono (9.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f })
        {
            g.setColour (col::bone1.withAlpha (0.055f)); g.fillRect (H.getX(), std::round (hy (db)) + 0.5f, H.getWidth(), 1.0f);
            g.setColour (col::bone3); g.drawText (db == 0 ? juce::String ("0") : mns (juce::String ((int) db)), juce::Rectangle<float> (H.getX() - 34, hy (db) - 6, 28, 12), juce::Justification::centredRight);
        }
        g.setColour (col::emberLo);
        for (float gr : { -3.0f, -6.0f, -12.0f }) g.drawText (mns (juce::String ((int) gr)), juce::Rectangle<float> (H.getRight() + 6, gy (gr) - 6, 28, 12), juce::Justification::centredLeft);

        const int target = ed.proc.target();
        if (target != limp::loudestClean)
        {
            const float t = limp::targets[target].lufs;
            g.setColour (col::phosphor.withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float> (H.getX(), hy (t + 1), H.getWidth(), hy (t - 1) - hy (t + 1)));
        }
        const auto& h = ed.history;
        const int n = (int) h.size();
        if (n > 1)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (H.expanded (2, 4).toNearestInt());
            const float dx = H.getWidth() / (float) (historyLen - 1);
            auto xAt = [&] (int i) { return H.getRight() - (float) (n - 1 - i) * dx; };
            juce::Path in, out, st, grFill, grLine;
            in.startNewSubPath (xAt (0), H.getBottom());
            grFill.startNewSubPath (xAt (0), H.getY());
            bool stOpen = false;
            for (int i = 0; i < n; ++i)
            {
                const auto& p = h[(size_t) i];
                const float x = xAt (i);
                in.lineTo (x, hy (p.inDb));
                if (i == 0) out.startNewSubPath (x, hy (p.outDb)); else out.lineTo (x, hy (p.outDb));
                if (std::isfinite (p.st) && p.st > -47.0f) { if (stOpen) st.lineTo (x, hy (p.st)); else { st.startNewSubPath (x, hy (p.st)); stOpen = true; } } else stOpen = false;
                grFill.lineTo (x, gy (p.grDb));
                if (i == 0) grLine.startNewSubPath (x, gy (p.grDb)); else grLine.lineTo (x, gy (p.grDb));
            }
            in.lineTo (H.getRight(), H.getBottom()); in.closeSubPath();
            grFill.lineTo (H.getRight(), H.getY()); grFill.closeSubPath();
            g.setColour (col::bone1.withAlpha (0.10f)); g.fillPath (in);
            g.setColour (col::phosphor); g.strokePath (out, juce::PathStrokeType (1.3f));
            g.setColour (col::lilac); g.strokePath (st, juce::PathStrokeType (2.0f));
            g.setColour (col::ember.withAlpha (0.16f)); g.fillPath (grFill);
            g.setColour (col::ember.withAlpha (0.3f)); g.strokePath (grLine, juce::PathStrokeType (5.0f));
            g.setColour (col::ember); g.strokePath (grLine, juce::PathStrokeType (2.0f));
        }
        {
            const float y = hy (ed.getReal ("ceil"));
            juce::Path t; t.startNewSubPath (H.getX(), y); t.lineTo (H.getRight(), y);
            juce::Path dashed; const float dl[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, t, dl, 2);
            g.setColour (col::sigClip.withAlpha (0.6f)); g.fillPath (dashed);
        }
        drawEngraved (g, "Level  " + dot + "  reduction  " + dot + "  loudness", { H.getX(), 6, 260, 14 }, 10.0f, col::bone3, juce::Justification::centredLeft);
        float lx = H.getRight();
        const std::pair<const char*, juce::Colour> legend[] = { { "ceiling", col::sigClip }, { "reduction", col::ember }, { "loudness", col::lilac }, { "out", col::phosphor } };
        for (auto& [t, c] : legend)
        {
            const auto f = Fonts::label (9.5f).withExtraKerningFactor (0.14f);
            const float w = juce::GlyphArrangement::getStringWidth (f, juce::String (t).toUpperCase()) + 4;
            drawEngraved (g, t, { lx - w, 6, w, 14 }, 9.5f, col::bone3, juce::Justification::centredRight);
            lx -= w + 12;
            g.setColour (c); g.fillRect (lx, 11.5f, 8.0f, 3.0f);
            lx -= 14;
        }
        const juce::String now = n > 0 ? numOrDash (h.back().st) : dash;
        g.setColour (col::basalt0.withAlpha (0.72f));
        g.fillRoundedRectangle (H.getX() + 6, H.getBottom() - 52, 112, 46, 5);
        g.setFont (Fonts::mono (22.0f)); g.setColour (col::bone1);
        g.drawText (now, juce::Rectangle<float> (H.getX() + 14, H.getBottom() - 50, 100, 26), juce::Justification::centredLeft);
        drawEngraved (g, "LUFS short-term", { H.getX() + 14, H.getBottom() - 23, 110, 12 }, 9.0f, col::bone3, juce::Justification::centredLeft);
        if (ed.getReal ("delta") > 0.5f) drawEngraved (g, "Hearing only what is removed", { H.getRight() - 300, H.getBottom() - 18, 296, 14 }, 9.5f, col::lilac, juce::Justification::centredRight);
        else if (ed.getReal ("match") > 0.5f) drawEngraved (g, "Equal loudness  " + num (ed.proc.matchNowDb.load()) + " dB", { H.getRight() - 300, H.getBottom() - 18, 296, 14 }, 9.5f, col::lilac, juce::Justification::centredRight);
    }
private:
    LimEditor& ed;
};

//==============================================================================
/** Pro: loudness readings. */
class LoudPanel : public juce::Component
{
public:
    explicit LoudPanel (LimEditor& e) : ed (e)
    {
        addAndMakeVisible (reset);
        reset.small = true;
        reset.help = "Start the loudness measurement again.";
        reset.onClick = [this] { ed.proc.loudIn.requestReset(); ed.proc.loudOut.requestReset(); };
        setHelp (*this, "Loudness of the output (ITU-R BS.1770 / EBU R128). Integrated is the whole piece since Reset, short-term the last 3 seconds, momentary the last 0.4 s.");
    }
    Button reset { "Reset" };
    void resized() override { reset.setBounds (getWidth() - 12 - 62, getHeight() - 12 - 22, 62, 22); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawWell (g, b, 8);
        auto r = b.reduced (12);
        const int t = ed.proc.target();
        drawEngraved (g, "Loudness", r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredLeft);
        drawEngraved (g, t == limp::loudestClean ? juce::String ("loudest clean") : "target " + num (limp::targets[t].lufs, 0), r.withHeight (14), 10.0f, col::bone3, juce::Justification::centredRight);
        r.removeFromTop (24);
        auto& m = ed.proc.loudOut;
        const float I = m.getIntegrated(), tp = m.getTruePeakMaxDb();
        drawEngraved (g, "Integrated", r.removeFromTop (14), 10.0f, col::bone3, juce::Justification::centredLeft);
        auto big = r.removeFromTop (36);
        g.setFont (Fonts::mono (30.0f)); g.setColour (col::bone1);
        const auto txt = numOrDash (I);
        g.drawText (txt, big, juce::Justification::centredLeft);
        drawEngraved (g, "LUFS", big.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (Fonts::mono (30.0f), txt) + 8).withTrimmedTop (12), 10.5f, col::bone3, juce::Justification::centredLeft);
        r.removeFromTop (8);
        auto row = [&] (const char* name, const juce::String& v, juce::Colour c)
        {
            auto rr = r.removeFromTop (30);
            g.setColour (col::lineSoft); g.fillRect (rr.withHeight (1));
            drawEngraved (g, name, rr, 10.0f, col::bone3, juce::Justification::centredLeft);
            g.setFont (Fonts::mono (12.5f)); g.setColour (c);
            g.drawText (v, rr, juce::Justification::centredRight);
        };
        const bool tpOk = std::isfinite (tp) && tp > -100.0f;
        row ("Short-term", numOrDash (m.getShortTerm()), col::bone1);
        row ("Momentary", numOrDash (m.getMomentary()), col::bone1);
        row ("True peak max", tpOk ? num (tp) + " dBTP" : dash, tpOk && tp > ed.getReal ("ceil") + 0.05f && ed.getReal ("bypass") < 0.5f && ed.getReal ("tp") > 0.5f ? col::sigClip : col::bone1);
        row ("Peak to loudness", tpOk && std::isfinite (I) ? num (tp - I) + " dB" : dash, col::bone1);
        const float gr = ed.history.empty() ? 0.0f : ed.history.back().grDb;
        row ("Reduction now", (gr > -0.05f ? juce::String ("0.0") : num (gr)) + " dB", col::emberHi);
    }
private:
    LimEditor& ed;
};

//==============================================================================
/** Simple: the master check, in plain words, with fixes. */
class Check : public CheckCards
{
public:
    explicit Check (LimEditor& e)
        : CheckCards ("Master check", "Master check: how loud and how safe the result is, in plain words, with a fix when something is off.", { 24, 58, 82, 6, 7, 28, true }), ed (e)
    {
        const char* names[] = { "Loudness", "Peaks", "How hard it works", "On streaming" };
        for (size_t i = 0; i < 4; ++i) rows[i] = { S::ok, names[i], "Waiting for signal." };
    }
    void update()
    {
        auto& m = ed.proc.loudOut;
        const float st = m.getShortTerm(), tp = m.getTruePeakMaxDb(), ceil = ed.getReal ("ceil");
        const bool byp = ed.getReal ("bypass") > 0.5f;
        if (! std::isfinite (st) || st < -60.0f)
        {
            const char* names[] = { "Loudness", "Peaks", "How hard it works", "On streaming" };
            for (size_t i = 0; i < 4; ++i) rows[i] = { S::ok, names[i], "Waiting for signal." };
        }
        else
        {
            const int t = ed.proc.target();
            if (ed.getReal ("delta") > 0.5f)
                rows[0] = { S::warn, "Hearing what is removed", "This is only the part the limiter takes away.", "Turn it off", [this] { ed.begin(); ed.setReal ("delta", 0); ed.end(); } };
            else if (byp) rows[0] = { S::ok, "Bypassed", num (st) + " LUFS. You are hearing the input untouched." };
            else if (t == limp::loudestClean) rows[0] = { S::ok, "Loudness", num (st) + " LUFS short-term." };
            else
            {
                const float goal = limp::targets[t].lufs, d = st - goal;
                if (std::abs (d) <= 1.0f) rows[0] = { S::ok, "On target", num (st) + " LUFS, right on " + num (goal, 0) + "." };
                else rows[0] = { S::warn, d < 0 ? "Quieter than target" : "Louder than target", num (st) + " LUFS, " + juce::String (std::abs (d), 1) + " dB " + (d < 0 ? "under." : "over."),
                                 "Make it loud safely", [this] { ed.makeItLoud(); } };
            }
            if (byp) rows[1] = { S::ok, "Peaks", "Highest peak " + num (tp) + " dBTP." };
            else if (ed.getReal ("tp") < 0.5f) rows[1] = { S::warn, "Only sample peaks are caught", "Peaks between samples can pass the ceiling and clip after MP3 or AAC.", "Turn on true peak", [this] { ed.begin(); ed.setReal ("tp", 1); ed.end(); } };
            else if (tp <= ceil + 0.05f) rows[1] = { S::ok, "Peaks safe", "Never above " + num (ceil) + " dBTP. No clipping after MP3 or AAC." };
            else rows[1] = { S::warn, "Peaks were over the ceiling", "Highest since reset: " + num (tp) + " dBTP. Measure again after a change.", "Measure again", [this] { ed.proc.loudIn.requestReset(); ed.proc.loudOut.requestReset(); } };
            const float avg = ed.avgGr;
            const auto soft = [this] { ed.lessSquashed(); };
            if (byp) rows[2] = { S::ok, "How hard it works", "Nothing while bypassed." };
            else if (avg < 0.5f) rows[2] = { S::ok, "Barely touching", "Under 0.5 dB of limiting. Your dynamics are untouched." };
            else if (avg < 3.0f) rows[2] = { S::ok, "Clean", "About " + juce::String (avg, 1) + " dB of limiting on average. Inaudible on most material." };
            else if (avg < 6.0f) rows[2] = { S::warn, "Working hard", "About " + juce::String (avg, 1) + " dB on average. Punch starts to soften.", "Less squashed", soft };
            else rows[2] = { S::bad, "Squashing", "About " + juce::String (avg, 1) + " dB on average. Distortion and lost punch are likely.", "Less squashed", soft };
            const float over = st + 14.0f;
            if (over > 0.5f) rows[3] = { S::warn, "Turned down on streaming", "Played about " + juce::String (over, 1) + " dB quieter on Spotify and YouTube." };
            else rows[3] = { S::ok, "Full level on streaming", "Spotify and YouTube will not turn this down." };
        }
        refresh();
    }
private:
    LimEditor& ed;
};
} // namespace limui

using namespace limui;

//==============================================================================
LimEditor::LimEditor (LimProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    shell = std::make_unique<Shell> (*this, proc.pluginState, "Limiter", baseW, baseH);
    addAndMakeVisible (*shell);
    buildSimple();
    buildPro();
    shell->setModeComponents (simpleBody.get(), simpleDeck.get(), proBody.get(), proDeck.get());

    setResizable (true, true);
    setResizeLimits (baseW * 6 / 10, baseH * 6 / 10, baseW * 2, baseH * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    const double s = (double) proc.pluginState.ui.getProperty ("scale", 1.0);
    setSize (juce::roundToInt (baseW * juce::jlimit (0.6, 2.0, s)), juce::roundToInt (baseH * juce::jlimit (0.6, 2.0, s)));
    setWantsKeyboardFocus (true);
    lastPreset = proc.pluginState.current.name;
    seenLoads = proc.stateLoads.load();
    syncSegments();
    for (size_t i = 0; i < knobs.size(); ++i) knobs[i]->sweepIn (120 + (int) (i % 6) * 60);
    startTimerHz (60);
}

LimEditor::~LimEditor()
{
    stopTimer();
    atts.clear();
    shell.reset();
    setLookAndFeel (nullptr);
}

void LimEditor::resized() { shell->setBounds (getLocalBounds()); }

void LimEditor::setReal (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
}

aemon::Knob& LimEditor::knob (juce::Component& parent, const juce::String& id, Knob::Options o)
{
    auto k = std::make_unique<Knob> (*proc.apvts.getParameter (id), std::move (o));
    k->onGestureStart = [this] { begin(); };
    k->onGestureEnd = [this] { end(); };
    auto& ref = *k;
    parent.addAndMakeVisible (ref);
    knobs.push_back (&ref);
    owned.push_back (std::move (k));
    return ref;
}

aemon::Button& LimEditor::toggle (juce::Component& parent, const juce::String& id, const juce::String& text, const juce::String& help)
{
    auto b = std::make_unique<Button> (text, true);
    b->help = help;
    b->setClickingTogglesState (false);
    auto& ref = *b;
    parent.addAndMakeVisible (ref);
    auto att = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (id), [&ref] (float v) { ref.setToggleState (v > 0.5f, juce::dontSendNotification); });
    att->sendInitialUpdate();
    auto* a = att.get();
    ref.onClick = [this, &ref, a] { begin(); a->setValueAsCompleteGesture (ref.getToggleState() ? 0.0f : 1.0f); end(); };
    atts.push_back (std::move (att));
    owned.push_back (std::move (b));
    return ref;
}

//==============================================================================
juce::String LimEditor::targetText() const
{
    const int t = proc.target();
    const auto peaks = "peaks below " + num (getReal ("ceil")) + " dBTP";
    return (t == limp::loudestClean ? juce::String ("as loud as it stays clean") : num (limp::targets[t].lufs, 0) + " LUFS") + "  " + dot + "  " + peaks;
}

void LimEditor::setTarget (int t)
{
    begin(); setReal ("target", (float) t); end();
    syncSegments();
}

bool LimEditor::makeItLoud (bool silentWithoutSignal)
{
    const auto plan = limp::planLoudness (proc.recentInput(), proc.currentRate.load(), proc.readParams(), proc.target());
    if (! plan.measured) { if (! silentWithoutSignal) shell->toast ("No signal yet. Play the loudest part for a few seconds, then press again."); return false; }
    begin(); setReal ("push", plan.push); end();
    proc.loudIn.requestReset(); proc.loudOut.requestReset();
    const int t = proc.target();
    const auto ceil = num (getReal ("ceil"));
    if (t == limp::loudestClean) shell->toast ("Push " + fmtDb (plan.push) + ": as loud as it stays clean, about " + juce::String (plan.avgGr, 1) + " dB of limiting on average.");
    else if (plan.overTarget) shell->toast ("This is already louder than " + num (limp::targets[t].lufs, 0) + " LUFS. Push set to 0: turn the mix down to reach the target.");
    else if (plan.shortBy > 0.3f) shell->toast ("Stopped " + juce::String (plan.shortBy, 1) + " dB short of the target: going further would squash it. Push harder only if you like the sound.");
    else shell->toast ("Push " + fmtDb (plan.push) + " reaches " + num (limp::targets[t].lufs, 0) + " LUFS with peaks below " + ceil + " dBTP.");
    return true;
}

void LimEditor::lessSquashed()
{
    begin(); setReal ("push", std::max (0.0f, getReal ("push") - 2.0f)); end();
    shell->toast ("Backed off 2 dB: more punch, a little quieter.");
}

void LimEditor::syncSegments()
{
    styleSeg->setIndex (limp::matchingStyle (getReal ("rel")), false);
    const int t = proc.target();
    targetSeg->setIndex (t < 4 ? t : -1, false);
    const auto txt = targetText();
    if (txt != shownTargetText) { shownTargetText = txt; if (simpleDeck) { simpleDeck->resized(); simpleDeck->repaint(); } }
}

//==============================================================================
namespace
{
    Knob::Options pushOptions()
    {
        Knob::Options o; o.name = "Push"; o.ticks = 13; o.minLabel = "0"; o.maxLabel = "+24"; o.format = fmtDb; o.parse = parseNum;
        o.help = "How hard you drive into the limiter. More push is louder, until it starts to hurt the sound.";
        return o;
    }
    Knob::Options ceilOptions()
    {
        Knob::Options o; o.name = "Ceiling"; o.ticks = 7; o.minLabel = minusSign + "6"; o.maxLabel = "0"; o.format = fmtCeil; o.parse = parseNum;
        o.help = "The highest the output can ever go. " + minusSign + "1 dBTP is the safe choice for streaming.";
        return o;
    }
    const char* matchHelp = "Equal loudness: plays the limited sound as loud as the original, so bypass compares sound, not volume. Only while listening, never in an export.";
}

void LimEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    auto disp = std::make_unique<Display> (*this);
    auto lp = std::make_unique<LoudPanel> (*this);
    displayPro = disp.get(); loud = lp.get();
    body->addAndMakeVisible (*disp); body->addAndMakeVisible (*lp);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10);
        loud->setBounds (r.removeFromRight (214));
        r.removeFromRight (10);
        displayPro->setBounds (r);
    };
    owned.push_back (std::move (disp)); owned.push_back (std::move (lp));
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto painter = std::make_unique<DeckPainter>();
    auto* pnt = painter.get();
    auto& push = knob (*deck, "push", pushOptions());
    auto& ceil = knob (*deck, "ceil", ceilOptions());
    Knob::Options ro; ro.name = "Release"; ro.format = fmtMs; ro.parse = parseNum;
    ro.help = "How fast it recovers after a peak. Short is louder but can distort, long is smoother.";
    auto& rel = knob (*deck, "rel", ro);
    auto& autoRel = toggle (*deck, "autoRel", "Auto rel", "Auto release: recovers faster after short peaks and slower in dense passages. Louder and cleaner at the same time.");
    autoRel.small = true;
    Knob::Options lo; lo.name = "Lookahead"; lo.style = Knob::Style::compact; lo.format = fmtMs; lo.parse = parseNum;
    lo.help = "Sees peaks coming so it can turn them down cleanly. Adds this much latency.";
    auto& look = knob (*deck, "look", lo);

    auto addSeg = [&] (std::unique_ptr<SegControl> s) { auto* raw = s.get(); deck->addAndMakeVisible (*raw); owned.push_back (std::move (s)); return raw; };
    juce::StringArray names, helps;
    for (auto& s : limp::styles) { names.add (s.name); helps.add (s.help); }
    styleSeg = addSeg (std::make_unique<SegControl> (names, helps));
    styleSeg->textHeight = 9.5f;
    styleSeg->onChange = [this] (int i) { begin(); setReal ("rel", limp::styles[i].release); end(); };
    Knob::Options ko; ko.name = "Stereo link"; ko.style = Knob::Style::compact; ko.parse = parseNum; ko.format = [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
    ko.help = "Stereo link: 100 % keeps the stereo image steady, lower lets each side limit on its own.";
    auto& link = knob (*deck, "link", ko);
    auto& tp = toggle (*deck, "tp", "True peak", "True peak: also catches peaks that happen between samples, which can clip after MP3 or AAC conversion or on cheap converters.");
    tp.small = true;
    auto* os = addSeg (std::make_unique<SegControl> (juce::StringArray { "Off", "2x", "4x", "8x" },
        juce::StringArray { "No oversampling: only sample peaks are caught. Lowest CPU.", "2x: light detection of peaks between samples.", "4x: recommended. Catches peaks between samples.", "8x: highest precision for final masters." }));
    os->textHeight = 9.0f; os->attach (*proc.apvts.getParameter ("os"), [this] { begin(); }, [this] { end(); });
    auto* di = addSeg (std::make_unique<SegControl> (juce::StringArray { "Off", "16-bit", "24-bit" },
        juce::StringArray { "No dither: for 24 or 32-bit files, or further processing.", "16-bit: for CD-quality WAV files. Use only on the final export.", "24-bit dither for 24-bit masters." }));
    di->textHeight = 9.0f; di->attach (*proc.apvts.getParameter ("dither"), [this] { begin(); }, [this] { end(); });
    auto& match = toggle (*deck, "match", "Equal loudness", matchHelp); match.small = true;
    auto& delta = toggle (*deck, "delta", "Hear removed", "Hear removed: plays only what the limiter takes away. It should sound like quiet clicks of the loudest hits, not like your music."); delta.small = true;
    deck->addAndMakeVisible (*painter);
    owned.push_back (std::move (painter));

    deck->layout = [this, pnt, &push, &ceil, &rel, &autoRel, &look, &link, &tp, os, di, &match, &delta] (juce::Rectangle<int> r)
    {
        pnt->setBounds (r);
        pnt->clear();
        const int top = 32, kh = 70 + 25;
        auto heroAt = [&] (Knob& k, int x) { k.setBounds (x, top, 78, kh); };
        auto compactAt = [&] (Knob& k, int x, int y) { k.setBounds (x, y, 64, 42 + 25); };
        auto under = [&] (juce::Component& c, const char* t) { pnt->under (c, t); };
        int x = 30;
        pnt->groups.push_back ({ { x, 14, 164, 20 }, "Level" });
        heroAt (push, x); heroAt (ceil, x + 86); under (push, "Push"); under (ceil, "Ceiling");
        x += 184;
        pnt->groups.push_back ({ { x, 14, 168, 20 }, "Timing" });
        heroAt (rel, x); under (rel, "Release");
        autoRel.setBounds (x + 86, top + 2, 82, 22); compactAt (look, x + 95, top + 28); under (look, "Lookahead");
        x += 188;
        pnt->groups.push_back ({ { x, 14, 208, 20 }, "Style" });
        styleSeg->setBounds (x, top + 2, 208, 24);
        compactAt (link, x + 72, top + 28); under (link, "Stereo link");
        x += 228;
        pnt->groups.push_back ({ { x, 14, 150, 20 }, "Quality" });
        tp.setBounds (x + 20, top + 2, 110, 22);
        pnt->subs.push_back ({ { x, top + 29, 150, 11 }, "Oversampling" });
        os->setBounds (x, top + 42, 150, 22);
        pnt->subs.push_back ({ { x, top + 69, 150, 11 }, "Dither" });
        di->setBounds (x, top + 82, 150, 22);
        x += 170;
        pnt->groups.push_back ({ { x, 14, 132, 20 }, "Monitor" });
        match.setBounds (x, top + 22, 132, 24); delta.setBounds (x, top + 56, 132, 24);
        pnt->seps = { 30 + 184 - 11, 30 + 372 - 11, 30 + 600 - 11, 30 + 770 - 11 };
        pnt->toFront (false);
    };
    proDeck = std::move (deck);
}

void LimEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    auto disp = std::make_unique<Display> (*this);
    auto ck = std::make_unique<Check> (*this);
    displaySimple = disp.get(); check = ck.get();
    body->addAndMakeVisible (*disp); body->addAndMakeVisible (*ck);
    body->layout = [this] (juce::Rectangle<int> r)
    {
        r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10);
        check->setBounds (r.removeFromRight (240));
        r.removeFromRight (10);
        displaySimple->setBounds (r);
    };
    owned.push_back (std::move (disp)); owned.push_back (std::move (ck));
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto painter = std::make_unique<DeckPainter>();
    auto* pnt = painter.get();
    auto seg = std::make_unique<SegControl> (juce::StringArray { "Streaming", "Club", "Broadcast", "Loudest clean" },
        juce::StringArray { "Streaming: about " + minusSign + "14 LUFS. Spotify, YouTube and Tidal turn louder songs down to this.",
                            "Club: about " + minusSign + "9 LUFS. Loud and punchy for DJs and club systems.",
                            "Broadcast: " + minusSign + "23 LUFS, the EBU R128 standard for TV and radio.",
                            "Loudest clean: as loud as it gets before the limiter starts to audibly hurt the sound." });
    targetSeg = seg.get();
    targetSeg->textHeight = 9.5f;
    targetSeg->onChange = [this] (int i) { setTarget (i); };
    deck->addAndMakeVisible (*seg); owned.push_back (std::move (seg));
    auto& push = knob (*deck, "push", pushOptions());
    auto& ceil = knob (*deck, "ceil", ceilOptions());
    auto mkBtn = [&] (const juce::String& t, const juce::String& hint, const juce::String& help, std::function<void()> fn) -> Button&
    {
        auto b = std::make_unique<Button> (t); b->hint = hint; b->help = help; b->onClick = std::move (fn);
        auto& ref = *b; deck->addAndMakeVisible (ref); owned.push_back (std::move (b)); return ref;
    };
    auto& go = mkBtn ("Make it loud safely", "hits the target", "Measures your music and sets Push to reach the target, but stops before the limiter would squash it.", [this] { makeItLoud(); });
    auto& match = toggle (*deck, "match", "Compare at equal loudness", matchHelp); match.hint = "honest A/B";
    auto& soft = mkBtn ("Less squashed", "back off 2 dB", "Backs Push off by 2 dB for more punch and less distortion.", [this] { lessSquashed(); });
    deck->addAndMakeVisible (*painter);
    owned.push_back (std::move (painter));
    deck->layout = [this, pnt, &push, &ceil, &go, &match, &soft] (juce::Rectangle<int> r)
    {
        pnt->setBounds (r);
        pnt->clear();
        const int top = 38;
        pnt->groups.push_back ({ { 30, 14, 340, 20 }, "Target" });
        targetSeg->setBounds (30, top + 22, 340, 30);
        pnt->subs.push_back ({ { 30, top + 62, 340, 12 }, targetText() });
        pnt->groups.push_back ({ { 388, 14, 112, 20 }, "Push" });
        push.setBounds (388, top - 4, 112, 98 + 25);
        pnt->groups.push_back ({ { 514, 14, 112, 20 }, "Ceiling" });
        ceil.setBounds (514, top - 4, 112, 98 + 25);
        pnt->groups.push_back ({ { 644, 14, 290, 20 }, "One-click helpers" });
        go.setBounds (650, top + 2, 282, 30); match.setBounds (650, top + 38, 282, 30); soft.setBounds (650, top + 74, 282, 30);
        pnt->seps = { 380, 507, 636 };
        pnt->toFront (false);
    };
    simpleDeck = std::move (deck);
}

//==============================================================================
void LimEditor::timerCallback()
{
    ++frame;
    limp::Frame fr[128];
    const int n = proc.popFrames (fr, 128);
    const float st = proc.loudOut.getShortTerm();
    for (int i = 0; i < n; ++i) history.push_back ({ fr[i].inDb, fr[i].outDb, fr[i].grDb, st });
    if ((int) history.size() > historyLen) history.erase (history.begin(), history.begin() + ((long) history.size() - historyLen));

    syncSegments();
    const bool simple = shell->isSimple();
    (simple ? displaySimple : displayPro)->repaint();
    if (! simple && frame % 6 == 0) loud->repaint();
    if (frame % 15 == 0)
    {
        const size_t from = history.size() > 250 ? history.size() - 250 : 0;      // about 4 s
        double sum = 0; int cnt = 0;
        for (size_t i = from; i < history.size(); ++i) if (history[i].inDb > -60.0f) { sum -= history[i].grDb; ++cnt; }
        avgGr = cnt > 0 ? (float) (sum / cnt) : 0.0f;
        if (simple) check->update();
        const int lat = proc.latencyNow.load();
        if (lat != shownLatency)
        {
            shownLatency = lat;
            const double rate = proc.currentRate.load();
            shell->setMeta (juce::String (rate / 1000.0, 1) + " kHz  " + dot + "  v" AEMON_VERSION);
        }
    }

    // A loudness preset chosen while music plays is set to this music, so it does what its name says.
    const auto name = proc.pluginState.current.name;
    if (seenLoads != proc.stateLoads.load()) { seenLoads = proc.stateLoads.load(); lastPreset = name; }
    if (name != lastPreset)
    {
        lastPreset = name;
        if (proc.pluginState.current.category == "Loudness targets") makeItLoud (true);
    }
}

bool LimEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (k.getModifiers().isShiftDown()) proc.pluginState.history.redo (proc.apvts);
        else proc.pluginState.history.undo (proc.apvts);
        shell->refreshHeader();
        return true;
    }
    return false;
}
