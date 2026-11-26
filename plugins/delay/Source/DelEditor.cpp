// Aemon Delay. AGPL-3.0-or-later.
#include "DelEditor.h"

using namespace aemon;
using Frame = aemon::del::Engine::Frame;

namespace
{
    juce::String msText (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }
    juce::String hzText (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    juce::String cutText (float v) { return juce::String (v, 1) + " dB"; }
    std::optional<float> parseHz (const juce::String& s) { auto v = txt::parse (s); if (v && s.containsIgnoreCase ("k")) *v *= 1000.0f; return v; }
}

namespace delui
{
/** The echoes on a ruler of beats, left up, right down. Drag the first echo sideways for the time, any echo up or down for the feedback. */
class Display : public juce::Component, public HelpProvider
{
public:
    explicit Display (DelEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return over == 1 ? "The first echo. Drag it sideways: it snaps to the beat, hold Alt for free time." : (over == 2 ? "How loud each repeat is against the one before: drag up or down." : "The echoes on a ruler of beats, left side up, right side down. Drag the first for the time, the others for the feedback."); }
    struct Echo { float ms, level; int side; int n; };       // side 0 left, 1 right, 2 both
    std::vector<Echo> echoes() const
    {
        std::vector<Echo> v; const auto p = ed.proc.readParams(); const double tr = p.style == aemon::del::Style::stereo ? p.ms * p.right : p.ms;
        for (int side = 0; side < (p.style == aemon::del::Style::stereo ? 2 : 1); ++side) for (int k = 1; k < 60; ++k)
        {
            const double t = k * (side ? tr : p.ms); if (t > span()) break;
            const int loops = p.style == aemon::del::Style::pingpong ? (k - 1) / 2 + 0 : k - 1; const double lv = std::pow (std::min (1.0, p.feedback), p.style == aemon::del::Style::pingpong ? loops : k - 1); if (lv < 0.012) break;
            v.push_back ({ (float) t, (float) lv, p.style == aemon::del::Style::mono ? 2 : (p.style == aemon::del::Style::pingpong ? (k - 1) % 2 : side), k });
        }
        return v;
    }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); drawWell (g, b, 8); const auto C = box(); const auto p = ed.proc.readParams(); const double beat = 60000.0 / juce::jlimit (30.0, 300.0, ed.proc.bpm.load());
        const int s = juce::roundToInt (ed.real ("sync")); drawEngraved (g, (s == 0 ? msText ((float) p.ms) : juce::String (delp::divisionNames[s]) + "  " + msText ((float) p.ms)) + (p.reverse ? ", backwards" : ""), { C.getX(), 7, 320, 14 }, 10.0f, col::emberHi, juce::Justification::centredLeft); drawEngraved (g, juce::String (juce::roundToInt (ed.proc.bpm.load())) + " bpm", { C.getRight() - 100, 7, 100, 14 }, 10.0f, col::bone3, juce::Justification::centredRight);
        for (int k = 0; k * beat / 4 <= span(); ++k) { const float x = X (k * beat / 4); const bool one = k % 4 == 0; g.setColour (col::bone1.withAlpha (one ? 0.13f : 0.05f)); g.fillRect (x, C.getY(), 1.0f, C.getHeight()); if (one) { g.setFont (Fonts::mono (9.0f)); g.setColour (col::bone3); g.drawText (juce::String (k / 4 + 1), juce::Rectangle<float> (24, 12).withCentre ({ x, C.getBottom() + 9 }), juce::Justification::centred); } }
        const float mid = C.getCentreY(), half = C.getHeight() * 0.5f - 8; g.setColour (col::bone1.withAlpha (0.2f)); g.fillRect (C.getX(), mid, C.getWidth(), 1.0f);
        drawEngraved (g, "L", { C.getX() - 20, mid - half, 14, 12 }, 9.0f, col::bone3, juce::Justification::centred); drawEngraved (g, "R", { C.getX() - 20, mid + half - 12, 14, 12 }, 9.0f, col::bone3, juce::Justification::centred);
        // the dry sound, then its echoes: each a little darker if the loop darkens it
        g.setColour (col::bone1.withAlpha (0.9f)); g.fillRoundedRectangle (X (0) - 2, mid - half, 4, 2 * half, 2);
        const float live = juce::jlimit (0.0f, 1.0f, (liveWet + 55.0f) / 50.0f); const double top = p.character == aemon::del::Character::digital ? p.highCut : (p.character == aemon::del::Character::tape ? std::min (p.highCut, 7500.0) : std::min (p.highCut, 4200.0));
        for (auto& e : echoes())
        {
            const float h = half * std::pow (e.level, 0.55f) * 0.94f, x = X (e.ms), w = juce::jlimit (3.0f, 9.0f, 11.0f - 0.5f * (float) e.n); const float dark = (float) juce::jlimit (0.0, 1.0, (1 - std::log2 (top / 1000.0) / 4.3) * (p.character == aemon::del::Character::tape ? 0.35 * e.n : 1.0));
            const auto c = col::phosphor.interpolatedWith (col::lilac, dark).withAlpha ((0.35f + 0.6f * e.level) * (0.55f + 0.45f * live));
            auto bar = [&] (bool up) { juce::Rectangle<float> r (x - w / 2, up ? mid - h : mid + 1, w, h); juce::ColourGradient gr (c, 0, up ? r.getY() : r.getBottom(), c.withMultipliedAlpha (p.reverse ? 0.15f : 0.5f), 0, mid, false); g.setGradientFill (gr); g.fillRoundedRectangle (r, 2.5f); };
            if (e.side != 1) bar (true);
            if (e.side != 0) bar (false);
            if (e.n == 1) { g.setColour (over == 1 || held == 1 ? col::emberHi : col::ember); g.fillEllipse (juce::Rectangle<float> (11, 11).withCentre ({ x, e.side == 1 ? mid + h : mid - h })); }
        }
        if (p.feedback >= 1.0) { g.setFont (Fonts::textMedium (12.0f)); g.setColour (col::sigHot); g.drawText ("It feeds itself: never louder than the safe level", C.withHeight (16), juce::Justification::centredRight); }
        const auto S = strip(); g.setColour (col::groove.withAlpha (0.5f)); g.fillRect (S); const auto& h = ed.past; const int n = (int) h.size(); const float dx = S.getWidth() / (float) (DelEditor::shown - 1); auto H = [&] (float d) { return juce::jlimit (0.0f, 1.0f, (d + 60.0f) / 60.0f) * S.getHeight(); };
        for (int i = 0; i < n; ++i) { const float x = S.getRight() - (float) (n - 1 - i) * dx; g.setColour (col::bone1.withAlpha (0.28f)); g.fillRect (x, S.getBottom() - H (h[(size_t) i].dry), dx + 0.3f, H (h[(size_t) i].dry)); g.setColour (col::phosphor.withAlpha (0.65f)); g.fillRect (x, S.getBottom() - H (h[(size_t) i].wet), dx + 0.3f, H (h[(size_t) i].wet)); const float d = h[(size_t) i].duck / 24.0f * S.getHeight(); if (d > 0.4f) { g.setColour (col::ember.withAlpha (0.8f)); g.fillRect (x, S.getY(), dx + 0.3f, d); } if (h[(size_t) i].thrown) { g.setColour (col::sigClip.withAlpha (0.8f)); g.fillRect (x, S.getBottom() - 3, dx + 0.3f, 3.0f); } }
        g.setColour (col::basalt0.withAlpha (0.78f)); g.fillRoundedRectangle (S.getX() + 2, S.getY() + 2, 168, 12, 3); float tx = S.getX() + 6; for (auto [t, c] : { std::pair { "dry", col::bone2 }, { "echo", col::phosphor }, { "ducked", col::ember }, { "thrown", col::sigClip } }) { drawEngraved (g, t, { tx, S.getY() + 2, 50, 12 }, 9.0f, c, juce::Justification::centredLeft); tx += 40; }
    }
    void mouseMove (const juce::MouseEvent& e) override { const int k = hit (e.position); if (k != over) { over = k; setMouseCursor (k == 1 ? juce::MouseCursor::LeftRightResizeCursor : (k == 2 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor)); repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { over = 0; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { held = hit (e.position); if (held) { ed.begin(); from = ed.real ("fb"); } }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (held == 1)
        {
            const double ms = juce::jlimit (5.0, 2500.0, (double) (e.position.x - box().getX()) / box().getWidth() * span());
            if (e.mods.isAltDown() || ed.real ("sync") < 0.5f) { ed.setReal ("sync", 0); ed.setReal ("ms", (float) ms); }
            else { const double beat = 60000.0 / juce::jlimit (30.0, 300.0, ed.proc.bpm.load()); int best = 1; for (int i = 1; i < delp::numDivisions; ++i) if (std::abs (std::log (delp::divisionBeats[i] * beat / ms)) < std::abs (std::log (delp::divisionBeats[best] * beat / ms))) best = i; ed.setReal ("sync", (float) best); }
        }
        else if (held == 2) ed.setReal ("fb", juce::jlimit (0.0f, 110.0f, std::round (from - (float) e.getDistanceFromDragStartY() * 0.4f)));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (held) ed.end(); held = 0; }
    float liveWet = -120;

private:
    juce::Rectangle<float> box() const { return { 34, 28, (float) getWidth() - 50, (float) getHeight() - 28 - 80 }; }
    juce::Rectangle<float> strip() const { return { 34, (float) getHeight() - 50, (float) getWidth() - 50, 40 }; }
    double span() const { const double beat = 60000.0 / juce::jlimit (30.0, 300.0, ed.proc.bpm.load()); return 4 * beat * (ed.proc.readParams().ms > 2.2 * beat ? 2.5 : 1.05); }
    float X (double ms) const { const auto C = box(); return C.getX() + (float) (ms / span()) * C.getWidth(); }
    int hit (juce::Point<float> q) const { const auto C = box(); if (! C.expanded (0, 8).contains (q)) return 0; const float first = X (ed.proc.readParams().ms); return std::abs (q.x - first) < 10 ? 1 : (q.x > first + 10 ? 2 : 0); }
    DelEditor& ed; int over = 0, held = 0; float from = 0;
};

/** The throw: a big pad. Down, and what is sung goes into the echo. A real parameter, so it can be written as automation. */
class Throw : public juce::Component, public HelpProvider
{
public:
    explicit Throw (DelEditor& e) : ed (e) {}
    juce::String getHelpText() const override { return "Throw: hold it down and what is sung goes into the echo, nothing else. Write it as automation on the last word. Click with Shift to latch."; }
    void paint (juce::Graphics& g) override
    {
        const bool on = ed.real ("throw") > 0.5f, armed = ed.real ("throwOnly") > 0.5f; auto r = getLocalBounds().toFloat().reduced (2);
        drawRaised (g, r, 8, on); if (on) { g.setColour (col::ember.withAlpha (0.35f)); g.fillRoundedRectangle (r, 8); } g.setColour (on ? col::emberHi : (armed ? col::ember : col::bone3.withAlpha (0.5f))); g.drawRoundedRectangle (r.reduced (3), 6, 1.4f);
        drawEngraved (g, "throw", r.withTrimmedBottom (r.getHeight() * 0.3f), 15.0f, on ? col::bone1 : (armed ? col::emberHi : col::bone2), juce::Justification::centred);
        g.setFont (Fonts::text (11.0f)); g.setColour (col::bone3); g.drawText (armed ? (on ? "going in" : "hold to send") : "everything echoes", r.withTrimmedTop (r.getHeight() * 0.52f), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (ed.real ("throwOnly") < 0.5f) ed.change ("throwOnly", 1); latch = e.mods.isShiftDown(); ed.setReal ("throw", latch && ed.real ("throw") > 0.5f ? 0.0f : 1.0f); repaint(); }
    void mouseUp (const juce::MouseEvent&) override { if (! latch) ed.setReal ("throw", 0); repaint(); }
private:
    DelEditor& ed; bool latch = false;
};
} // namespace delui

using namespace delui;

namespace
{
    Knob::Options opt (const char* name, const char* help, std::function<juce::String (float)> fmt, int ticks = 9, juce::String lo = {}, juce::String hi = {}) { Knob::Options o; o.name = name; o.help = help; o.format = std::move (fmt); o.parse = txt::parse; o.ticks = ticks; o.minLabel = lo; o.maxLabel = hi; return o; }
    Knob::Options fbO() { return opt ("Feedback", "How loud each repeat is against the one before. Over 100 % it feeds itself, never louder than the safe level.", txt::percent, 12, "one", "110"); }
    Knob::Options mixO() { return opt ("Mix", "How much of the echoes. Up to 50 % the dry sound is untouched.", txt::percent, 11, "dry", "wet"); }
    Knob::Options duckO() { return opt ("Duck", "How far the echoes step back while the sound itself plays. They come up in the gaps, so words stay clear.", cutText, 9, "off", "24"); }
    Knob::Options timeO() { auto o = opt ("Time", "On the beat, or free in milliseconds at the far left.", [] (float v) { return juce::String (delp::divisionNames[juce::jlimit (0, delp::numDivisions - 1, juce::roundToInt (v))]).replace (" triplet", "T").replace (" dotted", "D"); }, 14); return o; }
    const juce::StringArray styleNames { "Stereo", "Ping-pong", "Mono" }, styleHelps { "Each side repeats itself.", "Left, right, left, right.", "One echo in the middle." }, charNames { "Digital", "Tape", "Lo-fi" }, charHelps { "Digital: every repeat is the same sound again.", "Tape: every repeat loses some top and wanders a little.", "Lo-fi: a small band, worn." };
}

DelEditor::DelEditor (DelProcessor& p) : EffectEditor (p), proc (p)
{
    buildSimple();
    buildPro();
    start();
}

void DelEditor::clearWords() { begin(); setReal ("duck", juce::jmax (9.0f, real ("duck"))); setReal ("lowcut", juce::jmax (300.0f, real ("lowcut"))); setReal ("highcut", juce::jmin (6000.0f, real ("highcut"))); end(); toast ("The echoes keep to the middle and step back " + cutText (real ("duck")) + " while the words play."); }
void DelEditor::calm() { change ("fb", juce::jmin (60.0f, real ("fb"))); toast ("Feedback at 60 %: the echoes die away."); }

void DelEditor::buildPro()
{
    auto body = std::make_unique<LayoutBox>();
    displayPro = &make<Display> (*body, *this);
    panel = &make<ReadoutPanel> (*body);
    setHelp (*panel, "The echoes in numbers.");
    throwPro = &make<Throw> (*body, *this);
    body->layout = [this] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); auto s = r.removeFromRight (200); throwPro->setBounds (s.removeFromBottom (74)); s.removeFromBottom (8); panel->setBounds (s); r.removeFromRight (10); displayPro->setBounds (r); };
    proBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto hzO = [] (const char* n, const char* h, juce::String lo, juce::String hi) { auto o = opt (n, h, hzText, 9, lo, hi); o.parse = parseHz; return o; };
    std::vector<Knob*> list { &knob (*deck, "sync", timeO()), &knob (*deck, "ms", opt ("Free", "The time in milliseconds, when Time is at Free.", msText)), &knob (*deck, "right", opt ("Right", "The time of the right side against the left.", txt::percent)), &knob (*deck, "fb", fbO()), &knob (*deck, "lowcut", hzO ("Low cut", "Taken out of every repeat.", "20", "2k")), &knob (*deck, "highcut", hzO ("High cut", "Taken out of every repeat.", "1k", "20k")),
                              &knob (*deck, "drive", opt ("Grit", "Drives the echoes, more at every repeat.", txt::percent)), &knob (*deck, "wobble", opt ("Wobble", "How much tape and lo-fi wander in pitch.", txt::percent)), &knob (*deck, "duck", duckO()), &knob (*deck, "duckrel", opt ("Back", "How long the echoes take to come back after the words.", msText)), &knob (*deck, "width", opt ("Width", "How wide the echoes are. 0 is mono.", txt::percent)), &knob (*deck, "mix", mixO()) };
    std::vector<const char*> names { "Time", "Free", "Right", "Feedback", "Low cut", "High cut", "Grit", "Wobble", "Duck", "Back", "Width", "Mix" };
    auto* style = &paramSeg (*deck, "style", styleNames, styleHelps, 8.5f); auto* ch = &paramSeg (*deck, "char", charNames, charHelps, 8.5f); auto* change_ = &paramSeg (*deck, "change", { "Glide", "Fade" }, { "A change of time bends the pitch, like a tape.", "A change of time fades over, no bend." }, 8.5f);
    auto* rev = &toggle (*deck, "reverse", "Reverse", "Plays every piece backwards: what dies away swells.", true); auto* send = &toggle (*deck, "send", "Send", "On a send: only the echoes come out.", true); auto* only = &toggle (*deck, "throwOnly", "Throws only", "Nothing goes into the echo but what is thrown.", true);
    auto& pnt = painter (*deck);
    deck->layout = [&pnt, list, names, style, ch, change_, rev, send, only] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear(); const int top = 28, kw = 64, kh = 56 + 25; int x = 14;
        auto group = [&] (int a, int count, const char* t) { pnt.groups.push_back ({ { x, 10, count * kw, 18 }, t }); for (int i = a; i < a + count; ++i) { list[(size_t) i]->setBounds (x, top + 2, kw, kh); pnt.under (*list[(size_t) i], names[(size_t) i]); x += kw; } x += 7; pnt.seps.push_back (x); x += 7; };
        group (0, 4, "Echoes"); group (4, 4, "Sound of the repeats"); group (8, 2, "Words"); group (10, 2, "Level");
        const int y = 130; style->setBounds (14, y, 190, 20); ch->setBounds (214, y, 170, 20); change_->setBounds (394, y, 110, 20); rev->setBounds (516, y - 1, 90, 22); only->setBounds (614, y - 1, 116, 22); send->setBounds (738, y - 1, 80, 22);
        pnt.toFront (false);
    };
    proDeck = std::move (deck);
}

void DelEditor::buildSimple()
{
    auto body = std::make_unique<LayoutBox>();
    displaySimple = &make<Display> (*body, *this);
    auto* style = &paramSeg (*body, "style", styleNames, styleHelps); auto* ch = &paramSeg (*body, "char", charNames, charHelps);
    check = &make<CheckCards> (*body, "Echo check", "Echo check: what the echoes do to your sound, in plain words, with a fix when something is off.", CheckCards::Metrics { 24, 58, 82, 6, 7, 28, true });
    const char* n[] = { "Words", "Feedback", "Lows", "Throw" };
    for (size_t i = 0; i < 4; ++i) check->rows[i] = { CheckCards::S::ok, n[i], "Waiting for signal." };
    body->layout = [this, style, ch] (juce::Rectangle<int> r) { r = r.reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (10); check->setBounds (r.removeFromRight (240)); r.removeFromRight (10); auto t = r.removeFromTop (26); style->setBounds (t.removeFromLeft (t.getWidth() / 2 - 5)); t.removeFromLeft (10); ch->setBounds (t); r.removeFromTop (8); displaySimple->setBounds (r); };
    simpleBody = std::move (body);

    auto deck = std::make_unique<LayoutBox>();
    auto& time = knob (*deck, "sync", timeO()); auto& fb = knob (*deck, "fb", fbO()); auto& duck = knob (*deck, "duck", duckO()); auto& mix = knob (*deck, "mix", mixO());
    throwSimple = &make<Throw> (*deck, *this);
    auto& clear = button (*deck, "Keep the words clear", "middle only, duck", "The echoes keep to the middle and step back while the words play.", [this] { clearWords(); });
    auto& send = toggle (*deck, "send", "On a send", "On a send: only the echoes come out. Use it on a send or aux track."); send.hint = "only the echoes";
    auto& pnt = painter (*deck); auto* thr = throwSimple;
    deck->layout = [&pnt, &time, &fb, &duck, &mix, &clear, &send, thr] (juce::Rectangle<int> r)
    {
        pnt.setBounds (r); pnt.clear(); const int top = 38; int x = 26;
        for (auto [k, t] : { std::pair<Knob*, const char*> { &time, "Time" }, { &fb, "Feedback" }, { &duck, "Duck" }, { &mix, "Mix" } }) { pnt.groups.push_back ({ { x, 14, 108, 20 }, t }); k->setBounds (x, top - 4, 108, 98 + 25); x += 120; pnt.seps.push_back (x - 6); }
        pnt.groups.push_back ({ { x + 4, 14, 160, 20 }, "Last word" }); thr->setBounds (x + 8, top, 152, 100); x += 172; pnt.seps.push_back (x);
        pnt.groups.push_back ({ { x + 8, 14, 250, 20 }, "Helpers" }); clear.setBounds (x + 14, top + 14, 236, 30); send.setBounds (x + 14, top + 56, 236, 30);
        pnt.toFront (false);
    };
    simpleDeck = std::move (deck);
}

void DelEditor::updateCheck()
{
    using S = CheckCards::S;
    auto& r = check->rows;
    double wd = 0, dd = 0; int n = 0; float wetNow = -120; for (auto& f : past) { if (f.dry > -45) { dd += std::pow (10.0, f.dry / 10); wd += std::pow (10.0, f.wet / 10); ++n; } } if (! past.empty()) wetNow = past.back().wet; juce::ignoreUnused (wetNow);
    const bool playing = n > 20, send = real ("send") > 0.5f, off = ! send && real ("mix") < 0.5f, only = real ("throwOnly") > 0.5f; const float under = playing ? (float) (10 * std::log10 ((wd + 1e-12) / (dd + 1e-12))) : -99;
    if (real ("bypass") > 0.5f) r[0] = { S::ok, "Bypassed", "You are hearing the input untouched." };
    else if (off) r[0] = { S::ok, "No echo yet", "Mix is at zero. The sound is untouched." };
    else if (send) r[0] = { S::ok, "On a send", "Only the echoes come out. Set how much with the send." };
    else if (! playing) r[0] = { S::ok, "Words", "Play something." };
    else if (under > -4.0f) r[0] = { S::bad, "The echoes cover the sound", "They are only " + juce::String (juce::roundToInt (juce::jmax (0.0f, -under))) + " dB under it.", "Keep the words clear", [this] { clearWords(); } };
    else if (under > -10.0f && real ("duck") < 3.0f) r[0] = { S::warn, "Close to covering words", "The echoes are " + juce::String (juce::roundToInt (-under)) + " dB under the sound while it plays.", "Keep the words clear", [this] { clearWords(); } };
    else r[0] = { S::ok, "Words are clear", under < -60 ? juce::String ("No echo under the sound right now.") : "The echoes are " + juce::String (juce::roundToInt (-under)) + " dB under the sound while it plays." };

    const float fb = real ("fb");
    if (fb >= 100.0f) r[1] = { S::warn, "Feeds itself", "It does not stop by itself. Never louder than the safe level.", "Calm it", [this] { calm(); } };
    else if (fb > 85.0f) r[1] = { S::ok, "Very long", "Many echoes before they are gone." };
    else r[1] = { S::ok, fb < 1.0f ? "One echo" : "Dies away", fb < 1.0f ? juce::String ("Nothing repeats after it.") : "About " + juce::String (juce::jmax (1, juce::roundToInt (-30 / (20 * std::log10 (juce::jmax (0.01f, fb / 100.0f)))))) + " echoes until it is 30 dB down." };

    if (off) r[2] = { S::ok, "Lows", "Nothing echoes yet." };
    else if (real ("lowcut") < 100.0f && real ("fb") > 5.0f) r[2] = { S::warn, "Lows in the echoes", "Kick and 808 will pile up.", "Cut below 300 Hz", [this] { change ("lowcut", 300); } };
    else r[2] = { S::ok, "Lows kept out", "Nothing under " + hzText (real ("lowcut")) + " echoes." };

    if (only && real ("throw") > 0.5f) r[3] = { S::ok, "Thrown", "What plays now goes into the echo." };
    else if (only) r[3] = { S::ok, "Waiting for a throw", "Hold Throw, or write it as automation on the last word.", "Echo everything", [this] { change ("throwOnly", 0); } };
    else r[3] = { S::ok, "Everything echoes", "For single words: press Throw." };
    check->refresh();
}

void DelEditor::tick (bool simple, int count)
{
    Frame fr[128];
    const int n = proc.popDel (fr, 128);
    past.insert (past.end(), fr, fr + n);
    if ((int) past.size() > shown) past.erase (past.begin(), past.begin() + ((long) past.size() - shown));
    auto* d = simple ? displaySimple : displayPro; for (int i = 0; i < n; ++i) d->liveWet = fr[i].wet > d->liveWet ? fr[i].wet : d->liveWet - 1.5f;
    if (count % 2 == 0) { d->repaint(); (simple ? throwSimple : throwPro)->repaint(); }
    if (count % 12 != 0) return;
    if (simple) { updateCheck(); return; }
    const auto p = proc.readParams(); const auto dash = juce::String::charToString (0x2014);
    panel->heading = "The echoes"; panel->note = real ("bypass") > 0.5f ? "bypassed" : (p.mix <= 0 ? "off" : (p.throwOnly ? (p.throwNow ? "thrown" : "waiting") : "live"));
    panel->bigName = "Time"; panel->bigValue = juce::String (juce::roundToInt (p.ms)); panel->unit = "ms";
    panel->rows = { { "Tempo", juce::String (proc.bpm.load(), 1) + " bpm" }, { "Right side", p.style == aemon::del::Style::stereo ? msText ((float) (p.ms * p.right)) : dash }, { "Echo now", past.empty() || past.back().wet < -90 ? dash : txt::num (past.back().wet, 1) + " dB" }, { "Ducked", cutText (past.empty() ? 0.0f : past.back().duck), true } };
    panel->repaint();
}
