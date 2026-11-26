// Aemon Rider plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonRiderTests [--screenshots <dir> [music.wav]]
#include "RiderEditor.h"
#include "PerceptReport.h"
#include "EffectTestKit.h"
#include "VoiceKit.h"

using namespace kit;

namespace
{
juce::AudioBuffer<float> stereo (const std::vector<float>& x) { juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); return b; }
struct Named { const char* name; voicekit::Kind kind; };
const Named voices[] { { "middle", {} }, { "low", { 0.5, 0.75, 1.0, 0.92, 0.5, 7 } }, { "high", { 1.8, 1.2, 1.2, 1.15, 0.3, 11 } }, { "soft S", { 0.9, 0.95, 0.55, 1.0, 0.6, 3 } }, { "very sharp S", { 1.2, 1.1, 2.2, 1.0, 0.4, 21 } } };
/** An uneven take: words and phrases up to 12 dB apart, with breaths between the takes. */
std::vector<float> uneven (double fs, voicekit::Kind kind, float db = -8.0f)
{
    const auto v = voicekit::sing (fs, kind).x; std::vector<float> x; juce::Random r (5); voicekit::Bp f;
    for (double d : { 0.0, -7.0, 4.0, -3.0 }) { const float g = (float) std::pow (10.0, (d + db) / 20.0); for (float s : v) x.push_back (s * g); const int n = (int) (0.45 * fs); for (int i = 0; i < n; ++i) x.push_back ((float) (f.run (r.nextDouble() * 2 - 1, 2500, 0.6, fs) * 0.02 * std::sin (3.14159 * i / n))); }
    return x;
}
void prepareSide (RiderProcessor& p, double fs, int block = 512)
{
    juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo());
    p.setBusesLayout (l); p.setRateAndBufferSizeDetails (fs, block); p.prepareToPlay (fs, block);
}
juce::AudioBuffer<float> withBeat (const juce::AudioBuffer<float>& v, const std::vector<float>& beat) { juce::AudioBuffer<float> b (4, v.getNumSamples()); for (int c = 0; c < 2; ++c) { b.copyFrom (c, 0, v, c, 0, v.getNumSamples()); b.copyFrom (c + 2, 0, beat.data(), v.getNumSamples()); } return b; }
std::vector<float> beatLike (double fs, int n, float db, bool lift) { auto m = music (fs, (double) n / fs + 0.1, db); std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = m.getSample (0, i) * (lift && i > n / 2 ? 1.6f : 1.0f); return v; }
/** Words of a sound: their spread in dB, as the rider hears them. */
double spreadOf (const juce::AudioBuffer<float>& b, double fs, int from, aemon::rider::Hears h, double words) { std::vector<float> l (b.getReadPointer (0) + from, b.getReadPointer (0) + b.getNumSamples()), r (b.getReadPointer (1) + from, b.getReadPointer (1) + b.getNumSamples()); return aemon::rider::hear (l, r, fs, h, words).spread; }
struct Playhead : juce::AudioPlayHead { bool playing = true; juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying (playing); return p; } };
struct Gestures : juce::AudioProcessorListener
{
    int index = -1, begun = 0, ended = 0; std::vector<std::pair<int, float>> sent; int block = 0;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int i, float v) override { if (i == index) sent.push_back ({ block, v }); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int i) override { begun += i == index; }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int i) override { ended += i == index; }
};
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<RiderProcessor> ("Rider", argv[2]);
    const double fs = 48000;
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { const auto b = stereo (uneven (fs, {})); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 16, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        const auto path = tmp.getFullPathName(); std::string own = path.toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<RiderProcessor, RiderEditor> (4, args, "rider", [] (RiderProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Rap: every bar" : "Vocal: every word"); }, [] (RiderEditor& e) { e.setForMe(); });
        tmp.deleteFile(); return rc;
    }

    nullTest<RiderProcessor> ("Rider null test", 0.8f);
    nullTest<RiderProcessor> ("Rider bypass", 0.8f, [] (RiderProcessor& p) { set (p, "even", 100); set (p, "hears", 1); set (p, "bypass", 1); });

    // Set it for me
    for (auto& v : voices)
    {
        const auto x = uneven (fs, v.kind); RiderProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get());
        auto b = stereo (x); run (p, b, 512, [&] (int k) { if (k % 2 == 0) ed->tickForTest(); }); ed->setForMe();
        const auto in = stereo (x); auto c = in; run (p, c); const double before = spreadOf (in, fs, 24000, aemon::rider::Hears::voice, 300), after = spreadOf (c, fs, 24000 + p.getLatencySamples(), aemon::rider::Hears::voice, 300);
        check (after < 0.75 * before && p.real ("even") >= 80, juce::String ("Set it for me, ") + v.name + " voice: words a quarter closer or better", "target " + juce::String (p.real ("target"), 1) + ", at most " + juce::String (p.real ("up"), 0) + ", spread " + juce::String (before, 1) + " to " + juce::String (after, 1) + " dB");
    }
    {
        RiderProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get());
        ed->setForMe(); juce::AudioBuffer<float> n (2, (int) (5 * fs)); juce::Random r (1); for (int c = 0; c < 2; ++c) for (int i = 0; i < n.getNumSamples(); ++i) n.setSample (c, i, 0.1f * (r.nextFloat() * 2 - 1)); run (p, n); ed->setForMe();
        check (p.real ("even") == 0.0f && p.real ("target") == -20.0f, "Set it for me changes nothing without a voice", "silence and noise");
        auto b = stereo (uneven (fs, {})); run (p, b); ed->setForMe(); const float e = p.real ("even"), t = p.real ("target"); p.pluginState.history.undo (p.apvts);
        check (e == 80.0f && t != -20.0f && p.real ("even") == 0.0f && p.real ("target") == -20.0f, "Set it for me is one undo step", juce::String (t, 1) + " dB and back");
        auto c = stereo (uneven (fs, {})); run (p, c); const auto t0 = juce::Time::getMillisecondCounterHiRes(); ed->setForMe(); const double ms = juce::Time::getMillisecondCounterHiRes() - t0; check (ms < 800, "Set it for me takes a moment", juce::String (ms, 0) + " ms");
    }

    // Presets: every one, on every voice, keeps inside what its text says, and brings the words closer
    {
        RiderProcessor probe; int tested = 0;
        for (auto* preset : probe.pluginState.allPresets())
        {
            if (preset->user || ! riderp::aims (preset->name)) continue;
            ++tested; bool ok = true; juce::String list; float far = 0;
            for (auto& v : voices)
            {
                const auto x = uneven (fs, v.kind); const auto beat = beatLike (fs, (int) x.size(), -14.0f, false), louder = beatLike (fs, (int) x.size(), -14.0f, true);      // heard before the chorus, then with it
                RiderProcessor p; prepareSide (p, fs); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get());
                const auto in = withBeat (stereo (x), beat); auto b = in; run (p, b); ed->tickForTest(); load (p, preset->name.toRawUTF8()); ed->tickForTest();
                auto c = withBeat (stereo (x), louder); aemon::rider::Engine::Frame fr[256]; p.popRide (fr, 256); while (p.popRide (fr, 256) > 0) {} float lo = 0, hi = 0; run (p, c, 512, [&] (int) { const int n = p.popRide (fr, 256); for (int i = 0; i < n; ++i) { lo = std::min (lo, fr[i].gain); hi = std::max (hi, fr[i].gain); } });
                const float up = p.real ("up"), down = p.real ("down"); ok &= hi <= up + 1e-4f && lo >= -down - 1e-4f; far = std::max (far, std::max (hi, -lo));
                const auto d = preset->description; for (int n : { 3, 4, 5, 6, 8, 9, 10, 12 }) if (d.contains (juce::String (n) + " dB")) ok &= std::max (up, down) == (float) n;      // the number in the text is the limit
                if (d.contains ("Only lifts")) ok &= lo == 0.0f; if (d.contains ("Only turns down")) ok &= hi == 0.0f;
                const double words = p.real ("words"); const auto hears = (aemon::rider::Hears) (int) p.real ("hears"); juce::AudioBuffer<float> out (c.getArrayOfWritePointers(), 2, c.getNumSamples());
                const juce::AudioBuffer<float> src (const_cast<float* const*> (in.getArrayOfReadPointers()), 2, in.getNumSamples());
                const double before = spreadOf (src, fs, 24000, hears, words), after = spreadOf (out, fs, 24000 + p.getLatencySamples(), hears, words);
                if (p.real ("even") > 0) ok &= after < 0.9 * before && hi - lo > 1.0f; else ok &= hi - lo > 1.0f;      // follows the louder half of the beat
                list << juce::String (before, 1) << ">" << juce::String (after, 1) << " ";
            }
            check (ok, "Preset on five voices: " + preset->name, "words' spread " + list.trim() + " dB, furthest " + juce::String (far, 1));
        }
        check (tested == 32, "Presets tested", juce::String (tested));
        RiderProcessor a; load (a, "Clean start"); prepare (a, fs, 2); auto b = stereo (uneven (fs, {})); const auto in = b; run (a, b); bool exact = true; for (int i = a.getLatencySamples(); i < b.getNumSamples(); ++i) exact &= b.getSample (0, i) == in.getSample (0, i - a.getLatencySamples()); check (exact, "Preset: Clean start is bit-exact on a voice", "");
    }
    // Preset promises in words
    {
        auto gainOn = [&] (const char* preset, const std::vector<float>& x, double fromSec) { RiderProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get()); auto b = stereo (x); run (p, b); ed->tickForTest(); load (p, preset); ed->tickForTest(); auto c = stereo (x); aemon::rider::Engine::Frame fr[256]; while (p.popRide (fr, 256) > 0) {} std::vector<float> g; run (p, c, 512, [&] (int) { const int n = p.popRide (fr, 256); for (int i = 0; i < n; ++i) g.push_back (fr[i].gain); }); const size_t at = std::min (g.size() - 1, (size_t) (fromSec / 0.016)); return g[at]; };
        auto x = uneven (fs, {}); const double end = (double) x.size() / fs; x.resize (x.size() + (size_t) (3 * fs), 0.0f);
        check (gainOn ("Intimate vocal", x, end + 2.5) == 0.0f && gainOn ("Voice-over", x, end + 2.5) == 0.0f, "Presets that return in gaps are back at zero", "");
        RiderProcessor p; load (p, "Recording"); prepare (p, fs, 2); check (p.getLatencySamples() == 0, "Preset: Recording has no delay", juce::String (p.getLatencySamples()) + " samples");
    }

    // Follow the beat through the window's own button
    {
        RiderProcessor p; prepareSide (p, fs); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get());
        std::vector<float> v; const auto one = voicekit::sing (fs).x; for (int k = 0; k < 2; ++k) for (float s : one) v.push_back (s * 0.4f);
        const auto beat = beatLike (fs, (int) v.size(), -14, false); auto b = withBeat (stereo (v), beat); run (p, b); ed->followBeat();
        const float lv = p.real ("beat"); set (p, "up", 8); set (p, "down", 8); set (p, "follow", 100); set (p, "target", -24); set (p, "speed", 30);
        auto loud = beat; for (auto& s : loud) s *= 1.5849f; auto c = withBeat (stereo (v), loud); aemon::rider::Engine::Frame fr[256]; while (p.popRide (fr, 256) > 0) {} std::vector<float> g; run (p, c, 512, [&] (int) { const int n = p.popRide (fr, 256); for (int i = 0; i < n; ++i) g.push_back (fr[i].gain); });
        std::vector<float> late (g.begin() + (long) g.size() / 3, g.end()); std::sort (late.begin(), late.end()); const float mid = late[late.size() / 2];
        check (std::abs (mid - 4.0f) < 1.0f, "Follow the beat: 4 dB louder beat, the voice goes up 4 dB", "beat level " + juce::String (lv, 1) + " dB, ride " + juce::String (mid, 2) + " dB");
        RiderProcessor q; prepare (q, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> b2 (q.createEditor()); dynamic_cast<RiderEditor*> (b2.get())->followBeat(); check (q.real ("follow") == 0.0f, "Follow the beat changes nothing without a beat", "");
    }

    // Write and play, through the real parameter: what a DAW records, played back by a fresh plugin, at two block sizes and uneven timing
    for (int block : { 256, 1024 })
        for (float speed : { 12.0f, 25.0f })
        {
            const auto x = uneven (fs, {}); auto settings = [&] (RiderProcessor& p) { set (p, "even", 90); set (p, "target", -22); set (p, "up", 10); set (p, "down", 10); set (p, "speed", speed); };
            RiderProcessor w; settings (w); set (w, "rideMode", 1); prepare (w, fs, 2, block); Playhead ph; w.setPlayHead (&ph); Gestures ges; ges.index = w.apvts.getParameter ("ride")->getParameterIndex(); w.addListener (&ges);
            auto a = stereo (x); juce::Random r (9); double next = 0;
            run (w, a, block, [&] (int k) { ges.block = k; const double t = k * block / fs; if (t >= next) { w.writeStep(); next = t + 0.016 + 0.010 * r.nextDouble(); } });
            ph.playing = false; { juce::AudioBuffer<float> t (2, block); t.clear(); juce::MidiBuffer m; w.processBlock (t, m); } w.writeStep(); w.removeListener (&ges);
            RiderProcessor q; settings (q); set (q, "rideMode", 2); prepare (q, fs, 2, block); auto b = stereo (x); size_t at = 0; auto* ride = q.apvts.getParameter ("ride");
            run (q, b, block, [&] (int k) { while (at < ges.sent.size() && ges.sent[at].first <= k) ride->setValueNotifyingHost (ges.sent[at++].second); });
            // the two sounds, 20 ms at a time
            double worst = 0; const int N = 960; for (int i = N; i + N < a.getNumSamples(); i += N) { double ea = 0, eb = 0; for (int j = i; j < i + N; ++j) { ea += std::pow (a.getSample (0, j), 2); eb += std::pow (b.getSample (0, j), 2); } if (ea / N > 1e-7) worst = std::max (worst, std::abs (10 * std::log10 (eb / ea))); }
            const double bar = speed < 13 ? 0.3 : 0.6;
            check (worst < bar && ges.begun == 1 && ges.ended == 1 && ges.sent.size() > 100, "Written automation played back gives the same sound, " + juce::String (speed, 0) + " dB/s, blocks of " + juce::String (block), "worst " + juce::String (worst, 3) + " dB over " + juce::String ((int) ges.sent.size()) + " values, one gesture");
        }
    {
        RiderProcessor p; set (p, "even", 90); set (p, "rideMode", 1); prepare (p, fs, 2); Playhead ph; ph.playing = false; p.setPlayHead (&ph); Gestures ges; ges.index = p.apvts.getParameter ("ride")->getParameterIndex(); p.addListener (&ges);
        auto b = stereo (uneven (fs, {})); run (p, b, 512, [&] (int) { p.writeStep(); }); const auto stopped = ges.sent.size(); ph.playing = true; set (p, "rideMode", 0); auto c = stereo (uneven (fs, {})); run (p, c, 512, [&] (int) { p.writeStep(); });
        check (stopped == 0 && ges.sent.empty() && ges.begun == 0, "Writes nothing while the DAW is stopped, or in Ride", ""); p.removeListener (&ges);
    }
    {
        RiderProcessor a; prepare (a, fs, 2); set (a, "rideMode", 1); juce::MemoryBlock mb; a.getStateInformation (mb); RiderProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize()); const float fresh = b.real ("rideMode");
        Playhead ph; a.setPlayHead (&ph); auto x = stereo (uneven (fs, {})); run (a, x, 512, [&] (int) { a.writeStep(); }); juce::MemoryBlock m2; a.getStateInformation (m2); RiderProcessor c; c.setStateInformation (m2.getData(), (int) m2.getSize());
        check (fresh == 0.0f && c.real ("rideMode") == 2.0f, "A project never opens in Write", "nothing written: Ride; written: Play");
        set (a, "rideMode", 2); set (a, "ride", 3); load (a, "Podcast"); check (a.real ("rideMode") == 2.0f && std::abs (a.real ("ride") - 3.0f) < 0.02f, "Presets leave the written ride alone", "");
    }

    // Latency and clicks
    {
        RiderProcessor p; set (p, "even", 100); set (p, "hears", 1); prepare (p, fs, 2); const int lat0 = p.getLatencySamples(); int lat1 = 0;
        auto b = tone (fs, 512 * 400, 200, 0.3f);
        run (p, b, 512, [&] (int k) { if (k == 100) set (p, "look", 0); if (k == 160) { lat1 = p.getLatencySamples(); set (p, "look", 150); } if (k == 220) set (p, "bypass", 1); if (k == 250) set (p, "bypass", 0); if (k == 280) set (p, "target", -30); if (k == 320) set (p, "rideMode", 2); if (k == 340) set (p, "ride", -6); if (k == 370) set (p, "rideMode", 0); });
        const float step = largestStep (b, 6000), limit = 0.6f * (float) (juce::MathConstants<double>::twoPi * 200 / fs) * 1.2f;
        check (lat0 == 7200 && lat1 == 0 && p.getLatencySamples() == 7200 && step < limit, "Latency follows the lookahead, every switch is silent", juce::String (lat0) + ", " + juce::String (lat1) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }
    {
        RiderProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get()); load (p, "Very uneven"); ed->gentler(); const float e = p.real ("even"), u = p.real ("up"); for (int i = 0; i < 20; ++i) ed->gentler();
        RiderProcessor q; prepare (q, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> b2 (q.createEditor()); load (q, "Vocal: hold"); dynamic_cast<RiderEditor*> (b2.get())->gentler();
        check (e == 71.0f && u == 11.0f && p.real ("up") >= 1.0f && p.real ("speed") >= 2.0f && q.real ("up") == 0.0f, "Gentler backs off, and keeps a one-way ride one-way", juce::String (e, 0) + " %, " + juce::String (u, 0) + " dB");
    }
    // the window: no knobs, everything in sentences; dragging a value moves the parameter, one undo step, a one-way preset stays one-way
    {
        RiderProcessor p; prepare (p, fs, 2); p.pluginState.ui.setProperty ("mode", "simple", nullptr); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<RiderEditor*> (base.get()); ed->setSize (960, 600); load (p, "Vocal: every word"); ed->tickForTest();
        const auto said = ed->sentenceText (true); const bool words = said.contains ("6.0 dB either way") && said.contains ("80 % of the way") && said.contains ("24 dB a second") && said.contains ("not at all");
        const bool moved = ed->dragForTest (true, "up", 40); const float up = p.real ("up"), down = p.real ("down"); p.pluginState.history.undo (p.apvts); const float back = p.real ("up"), backDown = p.real ("down");
        load (p, "Vocal: hold the loud"); ed->tickForTest(); const bool oneWay = ed->sentenceText (true).contains ("0.0 dB up and 8.0 dB down"); ed->dragForTest (true, "up", 40); const float d2 = p.real ("down");
        check (words && moved && up > 6.5f && std::abs (up - down) < 0.05f && back == 6.0f && backDown == 6.0f && oneWay && d2 == 8.0f, "The sentence says the settings and edits them", juce::String (up, 1) + " and " + juce::String (down, 1) + " dB, undone to " + juce::String (back, 1) + "; one-way keeps " + juce::String (d2, 1));
        bool all = true; for (auto id : { "target", "even", "words", "up", "down", "speed", "look", "gaps", "hears", "below", "follow", "beat" }) all &= ed->dragForTest (false, id, 0); check (all, "Pro says every setting", "12 values");
    }
    stateTest<RiderProcessor> (33, "Podcast", [] (RiderProcessor& p) { set (p, "look", 35); set (p, "follow", 40); set (p, "rideMode", 2); set (p, "ride", -2.5f); set (p, "target", -31); });
    blockTest<RiderProcessor> ([] (RiderProcessor& p) { set (p, "even", 100); set (p, "hears", 1); set (p, "speed", 60); });

    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
