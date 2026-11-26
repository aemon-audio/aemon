// Aemon Multiband plugin-level tests and screenshots. AGPL-3.0-or-later.
// Run: AemonMbTests [--screenshots <dir> [music.wav]]
#include "MbEditor.h"
#include "PerceptReport.h"
#include "StyleRows.h"
#include "EffectTestKit.h"
#include "VoiceKit.h"

using namespace kit;
using Buf = juce::AudioBuffer<float>;

namespace
{
const double fs = 48000;
/** Full-range music: kick and bass, chords, voice-like mids, hats; phrases that get louder and quieter. */
Buf song (double seconds, float gainDb, int seed = 5)
{
    auto b = music (fs, seconds, gainDb); juce::Random r (seed); const float g = juce::Decibels::decibelsToGain (gainDb); const double tau = juce::MathConstants<double>::twoPi; float z = 0, y = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double t = i / fs, h = std::fmod (t, 0.125), s = std::fmod (t + 0.25, 0.5), swell = 0.55 + 0.45 * std::sin (tau * 0.4 * t); const float nz = r.nextFloat() * 2 - 1; z += 0.35f * (nz - z); y += 0.06f * (nz - y);
        float v = g * (float) (0.22 * (nz - z) * std::exp (-h * 45) + 0.5 * y * std::exp (-s * 12) + swell * (0.10 * std::sin (tau * 196 * t) + 0.07 * std::sin (tau * 1568 * t) + 0.05 * std::sin (tau * 3136 * t) + 0.18 * std::sin (tau * 55 * t)));
        b.addSample (0, i, v); b.addSample (1, i, v);
    }
    return b;
}
Buf voice (float db) { const auto v = voicekit::takes (fs, {}, { 0 }); Buf b (2, (int) v.x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, v.x.data(), (int) v.x.size()); b.applyGain (juce::Decibels::decibelsToGain (db)); return b; }
double pct (std::vector<float> v, double p) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[(size_t) juce::jlimit (0, (int) v.size() - 1, (int) (p * (double) v.size()))]; }
/** Gain of every band in 16 ms frames while the sound runs (a whole pass after the first one). */
std::array<std::vector<float>, mbp::N> gains (MbProcessor& p, const Buf& src)
{
    std::array<std::vector<float>, mbp::N> out; aemon::mb::Engine::Frame f[64]; while (p.popBands (f, 64) > 0) {}
    auto b = src; run (p, b, 768, [&] (int) { const int n = p.popBands (f, 64); for (int i = 0; i < n; ++i) for (size_t k = 0; k < mbp::N; ++k) out[k].push_back (f[i].gain[k]); });
    return out;
}
}

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--styles") { style::run<MbProcessor> (rows::mb(), -18, [] (bool ok, const juce::String& n, const juce::String& d) { std::printf ("%s  %s (%s)\n", ok ? "PASS" : "FAIL", n.toRawUTF8(), d.toRawUTF8()); }, argc > 2, 0.88); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--percept") return preport::run<MbProcessor> ("Multiband", argv[2]);
    if (argc > 2 && juce::String (argv[1]) == "--screenshots")
    {
        const auto tmp = juce::File::createTempFile (".wav"); { auto b = song (12, -8); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 48000, 2, 24, {}, 0)); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
        std::string own = tmp.getFullPathName().toStdString(); char* args[] { argv[0], argv[1], argv[2], argc > 3 ? argv[3] : own.data() };
        const int rc = screenshots<MbProcessor, MbEditor> (4, args, "multiband", [] (MbProcessor& p, const juce::String& mode) { load (p, mode == "pro" ? "Master: loud and dense" : "Goal: even and dense"); }, [] (MbEditor& e) { e.fitToMusic(); e.select (2); });
        tmp.deleteFile(); return rc;
    }

    nullTest<MbProcessor> ("Multiband null test", 0.8f);
    nullTest<MbProcessor> ("Multiband null test, straight phase, lookahead", 0.8f, [] (MbProcessor& p) { set (p, "phase", 1); set (p, "look", 4); set (p, "bands", 3); });
    nullTest<MbProcessor> ("Multiband bypass", 0.8f, [] (MbProcessor& p) { load (p, "Creative"); load (p, "Over the top: everything"); set (p, "bypass", 1); });

    // Every preset, set to four different sounds: each band comes down and up as far as the recipe says
    {
        const Buf sounds[] { song (8, -10), song (8, -26, 9), voice (-4), music (fs, 8, -14) }; const char* names[] { "song", "quiet song", "voice", "beat" };
        int bandsTested = 0, off = 0, n = 0, lifted = 0, flat = 0; juce::String worst, bad, flatList, tightest; double worstD = 0, least = 99;
        for (auto& r : mbp::recipes())
        {
            ++n; juce::String line;
            for (int k = 0; k < 4; ++k)
            {
                MbProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<MbEditor*> (base.get());
                auto b = sounds[k]; run (p, b, 768); ed->tickForTest(); load (p, r.name); ed->tickForTest(); set (p, "extSc", 0);
                const auto g = gains (p, sounds[k]); std::vector<float> L, R; double rate; p.recent.copy (L, R, rate); const auto lv = mbp::measure (L, R, rate, p.readParams());
                for (int i = 0; i < r.bands; ++i)
                {
                    const auto& w = r.want[(size_t) i]; if (lv.loud[(size_t) i] < -58) continue;      // nothing in this band
                    std::vector<float> d, u; for (float v : g[(size_t) i]) { d.push_back (-v); u.push_back (v); }
                    const double down = pct (d, 0.9), up = pct (u, 0.9);
                    ++bandsTested; bool ok = true;
                    if (w.down > 0) { const double e = down - w.down; if (std::abs (e) > worstD) { worstD = std::abs (e); worst = juce::String (r.name) + " " + names[k] + " band " + juce::String (i + 1) + ": " + juce::String (down, 1) + " for " + juce::String (w.down, 1); } ok = std::abs (e) < std::max (0.5, 0.15 * w.down); line << juce::String (down, 1) << "/" << juce::String (w.down, 1) << " "; }
                    else if (w.up <= 0) ok = down < 0.05 && up < 0.05;
                    if (w.up > 0) { const bool atEdge = p.get (mbp::id ('u', i)) > (w.down > 0 ? p.get (mbp::id ('t', i)) - 1.6f : -10.6f); if (w.down <= 0 || ! atEdge) ok = ok && std::abs (up - w.up) < 0.8; ok = ok && up < w.up + 0.8 && pct (u, 1.0) <= 12.01; line << "+" << juce::String (up, 1) << "/" << juce::String (w.up, 1) << " "; }
                    if (w.down > 0 && w.up > 0) { ++lifted; const double closer = down + up; if (closer < least) { least = closer; tightest = juce::String (r.name) + " " + names[k] + " band " + juce::String (i + 1); } if (closer < 1.0) { ++flat; flatList << r.name << " (" << names[k] << ", band " << juce::String (i + 1) << "): " << juce::String (closer, 1) << "; "; } }
                    if (! ok) { ++off; bad << r.name << " (" << names[k] << ", band " << juce::String (i + 1) << "): " << juce::String (down, 1) << " for " << juce::String (w.down, 1) << "; "; }
                }
                line << "| ";
            }
            std::printf ("      %-42s %s\n", r.name, line.toRawUTF8());
        }
        check (flat == 0 && lifted > 40, "Quiet parts always end up closer to the loud ones", flat ? flatList : juce::String (lifted) + " bands, least " + juce::String (least, 1) + " dB closer (" + tightest + ")");
        check (off == 0 && n >= 30, "Every preset fits every sound", bad.isEmpty() ? juce::String (n) + " presets, " + juce::String (bandsTested) + " bands, furthest: " + worst : bad);
    }
    // ... and comes out as loud as it went in, without clipping the world
    {
        const auto src = song (8, -10); juce::String bad; for (auto& r : mbp::recipes()) { if (! r.autoLevel) continue; MbProcessor p; load (p, r.name); prepare (p, fs, 2); auto b = src; run (p, b); const float d = lufs (b, fs, nullptr, (int) fs * 5) - lufs (src, fs, nullptr, (int) fs * 5); if (std::abs (d) > 1.0f) bad << r.name << " " << juce::String (d, 2) << "; "; }
        check (bad.isEmpty(), "Presets come out level matched, on their own numbers", bad.isEmpty() ? "all within 1 LU" : bad);
    }
    // Goals: choosing one is recognised, fits, and is one undo step
    {
        MbProcessor p; prepare (p, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<MbEditor*> (base.get()); bool ok = p.matchingGoal() == -1; juce::String list;
        auto b = song (6, -12); run (p, b); for (int g = 0; g < mbp::numGoals; ++g) { ed->chooseGoal (g); ok = ok && p.matchingGoal() == g; list << juce::String (p.get (mbp::id ('t', 0)), 1) << " "; }
        p.pluginState.history.undo (p.apvts); ok = ok && p.matchingGoal() == 2; set (p, mbp::id ('r', 0), 1.2f); ok = ok && p.matchingGoal() == -1;
        check (ok, "Goals are recognised from the settings, one undo step each", "thresholds of band 1: " + list);
        MbProcessor q; prepare (q, fs, 2); std::unique_ptr<juce::AudioProcessorEditor> b2 (q.createEditor()); auto* e2 = dynamic_cast<MbEditor*> (b2.get()); const bool none = ! e2->fitToMusic(); auto c = song (6, -12); run (q, c); const bool did = e2->fitToMusic(); const auto g2 = gains (q, song (6, -12)); juce::String l2; bool about3 = true; for (int i = 0; i < 4; ++i) { std::vector<float> d; for (float v : g2[(size_t) i]) d.push_back (-v); const double x = pct (d, 0.9); about3 = about3 && std::abs (x - 3) < 0.6; l2 << juce::String (x, 1) << " "; }
        check (none && did && about3, "Fit to my music works from nothing: about 3 dB per band", l2 + "dB; without signal it says so");
        e2->less(); check (q.real ("amount") == 75.0f, "Less takes a quarter off", juce::String (q.real ("amount"), 0) + " %");
    }
    // Moving a split keeps a goal, changing the band count keeps the sound running
    {
        MbProcessor p; load (p, "Goal: glue"); prepare (p, fs, 2); auto b = song (6, -12); run (p, b, 512, [&] (int k) { if (k == 100) set (p, "bands", 3); if (k == 200) set (p, "bands", 0); if (k == 300) set (p, "bands", 2); if (k == 400) set (p, "slope", 1); }); bool fin = true; float pk = 0; for (int i = 0; i < b.getNumSamples(); ++i) { fin = fin && std::isfinite (b.getSample (0, i)); pk = std::max (pk, std::abs (b.getSample (0, i))); }
        check (fin && pk < 1.5f, "Changing the number of bands while playing", "peak " + juce::String (pk, 2));
        MbProcessor q; set (q, "r2", 4); set (q, "t2", -40); prepare (q, fs, 2); const int lat0 = q.getLatencySamples(); int lat1 = 0, lat2 = 0; auto t = tone (fs, 512 * 400, 300, 0.4f); run (q, t, 512, [&] (int k) { if (k == 80) set (q, "phase", 1); if (k == 160) { lat1 = q.getLatencySamples(); set (q, "look", 5); } if (k == 240) { lat2 = q.getLatencySamples(); set (q, "phase", 0); set (q, "look", 0); } if (k == 330) set (q, "bypass", 1); });
        const float step = largestStep (t, 4000), limit = 0.4f * (float) (juce::MathConstants<double>::twoPi * 300 / fs) * 2.2f; check (lat0 == 0 && lat1 == 2304 && lat2 == 2544 && q.getLatencySamples() == 0 && step < limit, "Latency follows phase and lookahead, every switch is silent", juce::String (lat1) + ", " + juce::String (lat2) + " samples, largest step " + juce::String (step, 4) + ", limit " + juce::String (limit, 4));
    }
    // Sidechain bus: connected, mono and stereo, and absent
    for (int mainCh : { 2, 1 })
        for (int scCh : { 0, 1, 2 })
        {
            MbProcessor p; load (p, "Bass ducks"); juce::AudioProcessor::BusesLayout l; l.inputBuses.add (mainCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo()); l.inputBuses.add (scCh == 0 ? juce::AudioChannelSet::disabled() : (scCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo())); l.outputBuses.add (mainCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
            const bool okLayout = p.setBusesLayout (l); p.setRateAndBufferSizeDetails (fs, 512); p.prepareToPlay (fs, 512); juce::MidiBuffer m; double before = 0, after = 0; bool scClean = true;
            for (int k = 0; k < 190; ++k) { Buf b (mainCh + scCh, 512); for (int i = 0; i < 512; ++i) { const double t = (k * 512 + i) / fs; for (int c = 0; c < mainCh; ++c) b.setSample (c, i, 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * 70 * t)); for (int c = mainCh; c < mainCh + scCh; ++c) b.setSample (c, i, k > 95 ? 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * 55 * t) : 0.0f); } p.processBlock (b, m); double e = 0; for (int i = 0; i < 512; ++i) e += std::pow (b.getSample (0, i), 2); if (k > 60 && k < 90) before += e; if (k > 150) after += e; if (scCh > 0 && k > 95) { float pk = 0; for (int i = 0; i < 512; ++i) pk = std::max (pk, std::abs (b.getSample (mainCh, i))); scClean = scClean && std::abs (pk - 0.6f) < 0.02f; } }
            const double d = 10 * std::log10 (after / 39 / (before / 29) + 1e-9);
            check (okLayout && scClean && p.sidechainConnected.load() == (scCh > 0) && (scCh > 0 ? d < -6 : std::abs (d) < 3), "Sidechain: main " + juce::String (mainCh) + ", side " + juce::String (scCh), juce::String (d, 1) + " dB, connected " + juce::String ((int) p.sidechainConnected.load()));
        }
    // Export and playback are the same, and a fresh instance repeats itself
    {
        const auto src = song (4, -12); auto go = [&] (bool nrt) { MbProcessor p; load (p, "Master: transparent"); p.setNonRealtime (nrt); prepare (p, fs, 2); auto b = src; run (p, b); return b; }; const auto a = go (false), c = go (true); float w = 0; for (int i = 0; i < a.getNumSamples(); ++i) w = std::max (w, std::abs (a.getSample (0, i) - c.getSample (0, i))); check (w == 0, "An export is what was heard", juce::String (w));
    }

    stateTest<MbProcessor> ((int) mbp::recipes().size() + 1, "Piano", [] (MbProcessor& p) { set (p, "x2", 777); set (p, "u3", -33.5f); set (p, "phase", 1); set (p, "s4", 1); set (p, "bands", 3); });
    blockTest<MbProcessor> ([] (MbProcessor& p) { load (p, "Over the top: everything"); });

    style::run<MbProcessor> (rows::mb(), -18, [] (bool ok, const juce::String& n, const juce::String& d) { check (ok, n, d); }, false, 0.88);
    std::printf ("\n%s\n", failures ? "SOME TESTS FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
