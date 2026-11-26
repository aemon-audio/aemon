// Aemon Tune plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonTuneTests [--screenshots <dir> [music.wav]]
#include "TuneEditor.h"
#include "PerceptKit.h"
#include "EffectTestKit.h"
#include "TuneVoice.h"

using namespace kit;
using tunevoice::Note; using tunevoice::Settings;
using percept::Vec;

namespace
{
juce::AudioBuffer<float> stereo (const Vec& x, int extra = 0) { juce::AudioBuffer<float> b (2, (int) x.size() + extra); b.clear(); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); return b; }
Vec leftOf (const juce::AudioBuffer<float>& b, int from, int n) { Vec v; for (int i = from; i < from + n && i < b.getNumSamples(); ++i) v.push_back (b.getSample (0, i)); return v; }
std::vector<double> track (const Vec& y, double fs) { aemon::tune::Listener l; l.prepare (fs); aemon::tune::Heard h; std::vector<double> v (y.size() / 120 + 2, 0.0); for (size_t i = 0; i < y.size(); ++i) if (l.push (y[i], h) && h.voiced) { const int64_t at = (int64_t) i - h.age; if (at >= 0) v[(size_t) at / 120] = 69 + 12 * std::log2 (h.hz / 440); } return v; }
const std::vector<Note> line { { 57, 0.1, 0.6, -35 }, { 60, 0.6, 1.0, 30 }, { 64, 1.1, 2.6, -40 }, { 62, 2.6, 3.0, 35 }, { 57, 3.15, 4.0, 25 } };
tunevoice::Take take (double fs, double vibrato = 40) { Settings q; q.vibrato = vibrato; q.vowel = 1; return tunevoice::sing (fs, 4.2, tunevoice::melody (line, 0.09), q); }
void aMinor (TuneProcessor& p) { p.setNotes (aemon::tune::KeyFinder::notesOf (9, true)); }
struct Got { double off = 0, swing = 0, depth = 0, formant = 0; };      // cents from the notes, vibrato kept 0..1, what is left of it in cents
Got listen (TuneProcessor& p, double fs)
{
    prepare (p, fs, 2); const auto t = take (fs); const int lat = p.getLatencySamples(); auto b = stereo (t.x, lat + 1024); run (p, b); const auto y = leftOf (b, lat, (int) t.x.size()); const auto a = track (t.x, fs), o = track (y, fs); Got g; std::vector<double> e; const double tr = p.real ("trans");
    for (auto& n : line) for (double s = n.from + 0.25; s < n.to - 0.15; s += 0.0025) { double m = 0; int c = 0; for (double u = -0.0925; u < 0.0925; u += 0.0025) { const double v = o[(size_t) ((s + u) * 400)]; if (v > 0) { m += v; ++c; } } if (c > 60) e.push_back (std::abs (m / c - tr - n.midi) * 100); }      // the resting place over one swing of the vibrato
    std::sort (e.begin(), e.end()); g.off = e.empty() ? 999 : e[e.size() / 2]; const auto depth = [] (const std::vector<double>& q) { double lo = 1e9, hi = -1e9; for (double s = 1.7; s < 2.45; s += 0.0025) if (q[(size_t) (s * 400)] > 0) { lo = std::min (lo, q[(size_t) (s * 400)]); hi = std::max (hi, q[(size_t) (s * 400)]); } return (hi - lo) * 50; }; g.depth = depth (o); g.swing = g.depth / depth (a); return g;
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui; const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { std::vector<Note> n { { 68, 0.1, 0.6, -35 }, { 71, 0.6, 1.0, 25 }, { 70, 1.1, 1.7, 30 }, { 68, 1.85, 2.2, -25 }, { 66, 2.2, 2.6, 25 }, { 68, 2.6, 3.8, -38 }, { 71, 4.0, 4.4, 30 }, { 73, 4.4, 4.9, -30 }, { 71, 4.9, 5.3, 25 }, { 70, 5.3, 6.6, -28 } }; for (auto& q : n) q.midi -= 12; Settings q; q.vibrato = 34; q.vowel = 1; const auto b = stereo (tunevoice::sing (fs, 6.8, tunevoice::melody (n, 0.09), q).x); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        const auto path = tmp.getFullPathName(); std::string own = path.toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<TuneProcessor, TuneEditor> (4, args, "tune", [] (TuneProcessor& p, const juce::String&) { load (p, "Melodic rap lead"); p.setNotes (aemon::tune::KeyFinder::notesOf (8, true)); set (p, "throat", 106); }, [] (TuneEditor&) {});
        tmp.deleteFile(); return rc;
    }

    nullTest<TuneProcessor> ("Tune as it opens", 0.8f);
    nullTest<TuneProcessor> ("Tune bypassed", 0.8f, [] (TuneProcessor& p) { set (p, "hard", 100); set (p, "throat", 120); set (p, "bypass", 1); });
    { TuneProcessor p; prepare (p, fs, 2); const auto t = take (fs); auto b = stereo (t.x, 8192); run (p, b); const int lat = p.getLatencySamples(); float worst = 0; for (int i = 0; i < (int) t.x.size(); ++i) worst = std::max (worst, std::abs (b.getSample (0, i + lat) - t.x[(size_t) i])); check (worst == 0, "A singing voice with the line at its left end is bit-exact", "latency " + juce::String (lat)); }

    // every preset: what its words say
    {
        int tested = 0, vocals = 0; juce::String names; TuneProcessor probe;
        for (auto* pr : probe.pluginState.allPresets())
        {
            if (pr->user) continue;
            TuneProcessor p; p.pluginState.loadPreset (*pr); aMinor (p); if (p.real ("mix") < 100) set (p, "mix", 100);      // what the tuned voice does, without the dry one over it
            const auto g = listen (p, fs); const juce::String n = pr->name, d = pr->description; juce::StringArray miss; const bool style = pr->category.startsWith ("Style"); vocals += style; ++tested;
            const double ms = 1000.0 * p.getLatencySamples() / fs; const auto says = [&] (const char* w) { return (n + " " + d).containsWholeWordIgnoreCase (w); };
            if ((says ("hard") || says ("locked")) && ! says ("almost") && ! says ("natural")) { if (g.off > 5) miss.add ("not on the notes"); if (g.swing > 0.35) miss.add ("vibrato left " + juce::String (g.swing, 2)); }
            if (d.contains ("vibrato gone") || d.contains ("vibrato the")) if (g.swing > 0.15 && d.contains ("gone")) miss.add ("vibrato not gone");
            if (says ("vibrato") && (d.contains ("kept") || d.contains ("singer's")) && g.swing < 0.65) miss.add ("vibrato lost " + juce::String (g.swing, 2));
            if (says ("deeper") || says ("deep") || says ("bigger") || says ("big")) if (p.real ("throat") <= 103) miss.add ("throat not deeper");
            if (says ("smaller") || says ("small")) if (p.real ("throat") >= 97) miss.add ("throat not smaller");
            if (says ("least")) if (p.mixing()) miss.add ("not the mode with the least delay");
            if (says ("steadied") && g.swing > 0.45) miss.add ("still shaky " + juce::String (g.swing, 2));
            if (says ("leaves") && n.startsWith ("Clean")) if (g.off < 25) miss.add ("moved");
            if ((says ("pulled") || says ("polished") || says ("land")) && ! says ("slow") && g.off > 14) miss.add ("too far from the notes");
            if (n.startsWith ("Boom bap") && (g.off < 8 || g.swing < 0.8)) miss.add ("more than hardly there");
            if (d.contains ("notes untouched") && g.off < 25) miss.add ("notes were moved");
            check (miss.isEmpty() && d.isNotEmpty() && ! names.contains ("|" + n + "|"), "Preset: " + n, "median " + juce::String (g.off, 1) + " cents from the notes, vibrato kept " + juce::String (juce::roundToInt (g.swing * 100)) + " %, throat " + juce::String (p.real ("throat") / 100, 2) + ", delay " + juce::String (ms, 1) + " ms" + (miss.isEmpty() ? juce::String() : "  MISSES " + miss.joinIntoString ("; "))); names << "|" << n << "|";
        }
        check (tested >= 34 && vocals >= 20, "Presets tested", juce::String (tested) + ", styles for voices " + juce::String (vocals));
    }
    // a preset leaves the key alone, in the window
    {
        TuneProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<TuneEditor*> (base.get()); ed->tickForTest(); ed->useKey (9, true); set (p, "ref", 442); set (p, "voice", 1); ed->tickForTest(); ed->tickForTest(); const auto before = p.notesOn(); load (p, "Trap lead"); ed->tickForTest(); ed->tickForTest();
        check (p.notesOn() == before && std::abs (p.real ("ref") - 442) < 0.06f && p.real ("voice") == 1 && p.real ("hard") == 100, "A preset keeps the key, the tuning and the voice", ed->keyText().replace ("\n", " "));
        ed->toggleNote (0); ed->tickForTest(); const bool off = ! p.notesOn()[0]; p.pluginState.history.undo (p.apvts); check (off && p.notesOn() == before, "A note switched on the ring can be undone", "");
        ed->begin(); ed->line (0.8f); ed->end(); const auto s = ed->shape(); ed->begin(); ed->own (1, s[1]); ed->end(); const auto s2 = ed->shape(); float worst = 0; for (int i = 0; i < 6; ++i) worst = std::max (worst, std::abs (s[(size_t) i] - s2[(size_t) i])); check (ed->hasOwn() && worst < 0.11f && std::abs (ed->linePlace() - 0.8f) < 0.03f, "From the line to a shape of one's own: the same settings, the thumb stays", "largest difference " + juce::String (worst, 3)); ed->begin(); ed->line (0.3f); ed->end(); check (! ed->hasOwn(), "The line takes over again", "");
    }
    // both windows do the same
    { juce::AudioBuffer<float> r[2]; for (int k = 0; k < 2; ++k) { TuneProcessor p; p.pluginState.ui.setProperty ("mode", k ? "pro" : "simple", nullptr); load (p, "Pop lead"); aMinor (p); prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<TuneEditor*> (base.get()); r[k] = stereo (take (fs).x); run (p, r[k], 512, [&] (int i) { if (i % 3 == 0) ed->tickForTest(); }); } float w = 0; for (int i = 0; i < r[0].getNumSamples(); ++i) w = std::max (w, std::abs (r[0].getSample (0, i) - r[1].getSample (0, i))); check (w == 0, "Simple or Pro open: the same sound", ""); }
    // the key is found while it plays
    { TuneProcessor p; prepare (p, fs, 2); std::vector<Note> n; const int deg[] { 57, 60, 64, 62, 60, 57, 55, 57, 59, 60, 62, 64, 65, 64, 60, 57 }; double t = 0.1; for (int rep = 0; rep < 2; ++rep) for (int m : deg) { n.push_back ({ (double) m, t, t + 0.34, rep ? 15.0 : -12.0 }); t += 0.36; } auto b = stereo (tunevoice::sing (fs, t + 0.2, tunevoice::melody (n, 0.05), {}).x); run (p, b); const auto f = p.found(); check ((f.root == 9 && f.minor) || (f.root == 0 && ! f.minor), "The key of a sung line", tunep::noteName (f.root) + (f.minor ? " minor" : " major") + ", sure " + juce::String (f.sure, 2)); p.listenAgain(); juce::AudioBuffer<float> z (2, 30000); z.clear(); run (p, z); check (p.found().sure == 0, "Listen again forgets", ""); }
    // MIDI
    { TuneProcessor p; set (p, "hard", 100); set (p, "source", 1); prepare (p, fs, 2); const auto t = tunevoice::sing (fs, 1.6, [] (double s) { return s > 0.1 && s < 1.5 ? 225.0 : 0.0; }); auto b = stereo (t.x, 4096); juce::MidiBuffer m; for (int pos = 0; pos < b.getNumSamples(); pos += 512) { m.clear(); if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0); if (pos == 512 * 90) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0); juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos)); p.processBlock (v, m); } const auto y = track (leftOf (b, p.getLatencySamples(), (int) t.x.size()), fs); check (std::abs (y[200] - 60) < 0.06 && std::abs (y[500] - (69 + 12 * std::log2 (225.0 / 440))) < 0.06, "A held MIDI note is the target, none held moves nothing", juce::String (y[200], 2) + ", then " + juce::String (y[500], 2)); }
    // changes while it plays: no click, finite, the right delay
    {
        TuneProcessor p; aMinor (p); set (p, "hard", 70); prepare (p, fs, 2); auto b = stereo (take (fs, 20).x, 9000); juce::Random r (3); run (p, b, 256, [&] (int k) { if (k % 9 != 0) return; switch ((k / 9) % 7) { case 0: set (p, "mode", (float) (k & 1)); break; case 1: set (p, "voice", (float) r.nextInt (3)); break; case 2: set (p, "throat", 80 + 50 * r.nextFloat()); break; case 3: set (p, "hard", 100 * r.nextFloat()); break; case 4: set (p, "trans", (float) (r.nextInt (9) - 4)); break; case 5: set (p, "n" + juce::String (r.nextInt (12)), (float) r.nextInt (2)); break; default: set (p, "bypass", (float) r.nextInt (2)); } });
        bool fine = true; float top = 0; for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (0, i); fine &= std::isfinite (v); top = std::max (top, std::abs (v)); } check (fine && top < 2, "Everything changed while it plays: finite, bounded", "peak " + juce::String (top, 2) + ", click " + juce::String (percept::clickDb (leftOf (b, 4800, (int) (3.6 * fs)), fs), 1));
        for (int mode : { 0, 1 }) for (int v : { 0, 1, 2 }) { TuneProcessor q; set (q, "mode", (float) mode); set (q, "voice", (float) v); prepare (q, fs, 2); std::printf ("INFO  delay, %s, %s voice: %.1f ms\n", mode ? "mixing" : "recording", v == 0 ? "low" : (v == 1 ? "middle" : "high"), 1000.0 * q.getLatencySamples() / fs); }
        TuneProcessor q; set (q, "mode", 0); set (q, "voice", 2); prepare (q, fs, 2); check (1000.0 * q.getLatencySamples() / fs < 12, "Recording, high voice: under 12 ms", juce::String (1000.0 * q.getLatencySamples() / fs, 1) + " ms");
    }
    stateTest<TuneProcessor> (35, "Pitchy take", [] (TuneProcessor& p) { set (p, "n3", 0); set (p, "c5", 12.5f); set (p, "land", 33.3f); set (p, "ref", 441); set (p, "voice", 2); });
    blockTest<TuneProcessor> ([] (TuneProcessor& p) { set (p, "hard", 90); set (p, "throat", 110); });
    // "keep this one for mixing": once, remembered, never in the way
    {
        const auto tmp = juce::File::createTempFile (".txt"); aemon::Shell::delayCards = true; aemon::Shell::seenFile = tmp;
        { TuneProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); base->setSize (960, 600); auto* ed = dynamic_cast<TuneEditor*> (base.get()); auto& sh = ed->getShell(); sh.checkDelay(); auto* c = sh.getDelayCard(); check (c != nullptr && c->isVisible() && sh.delayText().startsWith ("mixing") && c->getY() >= 48, "Over 12 ms: the card, and 'mixing' in the bar", sh.delayText());
          if (const auto f = juce::SystemStats::getEnvironmentVariable ("AEMON_CARD", {}); f.isNotEmpty()) { juce::Image img (juce::Image::ARGB, 960, 600, true); { juce::Graphics g (img); base->paintEntireComponent (g, true); } juce::File out (f); out.deleteFile(); juce::FileOutputStream s (out); juce::PNGImageFormat().writeImageToStream (img, s); }
          set (p, "mode", 0); set (p, "voice", 2); juce::AudioBuffer<float> b (2, 4096); b.clear(); run (p, b); sh.checkDelay(); check (! c->isVisible() && sh.delayText().startsWith ("live"), "Under it: the card goes, 'live' in the bar", sh.delayText()); set (p, "mode", 1); b.clear(); run (p, b); sh.checkDelay(); check (c->isVisible() && ! aemon::Shell::delaySeen ("Tune"), "Back over it: it is there again, nothing remembered without a click", ""); if (c->onOther) c->onOther(); b.clear(); run (p, b); sh.checkDelay(); check (p.real ("mode") == 0 && sh.getDelayCard() == nullptr && aemon::Shell::delaySeen ("Tune"), "Its button switches to recording and closes it", sh.delayText()); }
        { TuneProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto& sh = dynamic_cast<TuneEditor*> (base.get())->getShell(); sh.checkDelay(); check (sh.getDelayCard() == nullptr && sh.delayText().startsWith ("mixing"), "The next time it is only in the bar", sh.delayText()); }
        aemon::Shell::seenFile = juce::File::getSpecialLocation (juce::File::windowsSystemDirectory).getChildFile ("no such place").getChildFile ("x.txt"); { TuneProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto& sh = dynamic_cast<TuneEditor*> (base.get())->getShell(); sh.checkDelay(); const bool shown = sh.getDelayCard() != nullptr; if (shown) sh.getDelayCard()->onGotIt(); sh.checkDelay(); check (shown && sh.getDelayCard() == nullptr, "A folder that cannot be written: it closes all the same", ""); }
        aemon::Shell::delayCards = false; aemon::Shell::seenFile = juce::File(); tmp.deleteFile();
    }
    // every control of both windows answers
    {
        for (auto mode : { "simple", "pro" }) { TuneProcessor p; p.pluginState.ui.setProperty ("mode", mode, nullptr); prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); base->setSize (960, 600); auto* ed = dynamic_cast<TuneEditor*> (base.get()); auto b = stereo (take (fs).x); run (p, b, 512, [&] (int k) { if (k % 3 == 0) ed->tickForTest(); }); juce::Image img (juce::Image::ARGB, 960, 600, true); { juce::Graphics g (img); base->paintEntireComponent (g, true); } check (ed->past.size() > 300 && (ed->lastNote.size() > 60), juce::String ("Window, ") + mode + ": draws a sung line and keeps the last note", juce::String ((int) ed->past.size()) + " looks, last note " + juce::String ((int) ed->lastNote.size())); }
    }
    { TuneProcessor p; set (p, "hard", 100); set (p, "throat", 110); prepare (p, fs, 2); auto b = stereo (take (fs).x); const auto a = juce::Time::getMillisecondCounterHiRes(); run (p, b); const double ms = juce::Time::getMillisecondCounterHiRes() - a, cpu = 100 * ms / (b.getNumSamples() / 48.0); check (cpu < 15, "CPU, stereo at 48 kHz (bar 10 on a quiet computer, 15 here)", juce::String (cpu, 1) + " % of one core"); }
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
