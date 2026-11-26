// The style presets, measured: what each one says is a claim with numbers, checked on voices or on a beat. AGPL-3.0-or-later.
#pragma once
#include "PerceptReport.h"
#include <functional>
#include <map>
#include <random>

namespace style { struct Row; }
namespace rules { inline const std::vector<style::Row>& of (const juce::String& plugin); }
namespace style
{
using percept::Vec;
/** what: "curve lo-hi" (dB against 700 to 1500 Hz, of quiet noise: the tone curve with nothing set off), "tone lo-hi" (the same on the sound itself), "spread" (dB the loud and quiet moments come closer), "crest" (dB), "harm"
    (percent of a 220 Hz tone at -12 dB), "side" (dB of sides against the middle, out minus in; "side high": over 6 kHz, "side low": under 250 Hz), "peak", "loud",
    "ess" (dB of 5 to 12 kHz against 300 to 3000 Hz: the S), "spread low" (under 220 Hz) and "spread high" (over 6 kHz). On sounds of their own:
    "808 side" (its sides under 90 Hz when it comes with some), "harm high" (percent, of a 3 kHz tone), "onset"
    (the first 15 ms of words that start out of a pause against the next 105, out minus in), "vowels": below.
    "808 sub" (change of a 46 Hz 808 under 70 Hz), "808 phone" (its 250 to 2500 Hz against that sub: what a phone plays), "snap" (the first 10 ms of every
    drum hit against the next 140, out minus in), "pump" (how far a 1 kHz tone under a kick every half second is pulled down, at most) and "back" (how far
    it is still down when the next kick comes), "pump low", "back low" (the same with 100 Hz), "hold sub", "hold hats" (loud and soft notes, 8 dB apart:
    dB closer). */
struct Claim { const char* what; double lo, hi; };
struct Row { const char* name; std::vector<Claim> claims; const char* allow = ""; };      // allow: d may bury 2-5 kHz, b may sharpen S, x an effect
struct M { std::array<double, percept::thirds> a {}, b {}; double loud = 0, spread = 0, crest = 0, side = 0, peak = 0, words = 1, pres = 0, ess = 0, harm = 0, sideHi = 0, sideLo = 0; };

inline double spreadOf (const Vec& x, double fs)
{
    const int w = (int) (0.05 * fs); std::vector<double> v, k; double top = -200; for (size_t i = 0; i + (size_t) w <= x.size(); i += (size_t) w) { const double d = percept::rmsDb (x, i, i + (size_t) w); v.push_back (d); top = std::max (top, d); }
    for (double d : v) if (d > top - 34) k.push_back (d);
    std::sort (k.begin(), k.end()); return k.size() < 4 ? 0 : k[(size_t) (0.9 * (double) k.size())] - k[(size_t) (0.15 * (double) k.size())];
}
inline double crestOf (const Vec& x) { float p = 0; for (float v : x) p = std::max (p, std::abs (v)); return 20 * std::log10 (p + 1e-9) - percept::rmsDb (x); }
struct Stereo { Vec l, r; };
/** The sound through a preset, lined up. Plugins that listen first hear it once before. */
template <typename P> Stereo through (const juce::String& name, const Stereo& in, double fs)
{
    P p; juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::stereo()); if (p.getBusCount (true) > 1) l.inputBuses.add (juce::AudioChannelSet::disabled()); l.outputBuses.add (juce::AudioChannelSet::stereo());
    p.setBusesLayout (l); p.setRateAndBufferSizeDetails (fs, 512); p.prepareToPlay (fs, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor()); auto* ed = dynamic_cast<aemon::EffectEditor*> (base.get());
    auto play = [&] (juce::AudioBuffer<float>& b) { juce::MidiBuffer m; for (int pos = 0; pos < b.getNumSamples(); pos += 512) { juce::AudioBuffer<float> v (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos)); p.processBlock (v, m); } };
    const int n = (int) in.l.size(); juce::AudioBuffer<float> b (2, n + 20000); auto fill = [&] { b.clear(); b.copyFrom (0, 0, in.l.data(), n); b.copyFrom (1, 0, in.r.data(), n); };
    if (ed != nullptr) { fill(); play (b); ed->tickForTest(); }
    for (auto* pr : p.pluginState.allPresets()) if (! pr->user && pr->name == name) { p.pluginState.loadPreset (*pr); break; }
    if (ed != nullptr) ed->tickForTest();
    fill(); play (b); const int lat = p.getLatencySamples(); Stereo o; for (int i = 0; i < n; ++i) { o.l.push_back (b.getSample (0, i + lat)); o.r.push_back (b.getSample (1, i + lat)); } return o;
}
inline Stereo sound (const std::string& kind, double fs, double db, bool wide = false)
{
    static std::map<std::string, Vec> cache; if (cache.empty()) for (auto& s : preport::sounds (fs)) cache[s.name] = s.x;
    Stereo o; o.l = cache.at (kind); const double g = std::pow (10.0, (db - percept::rmsDb (o.l)) / 20); for (auto& v : o.l) v = (float) (v * g);
    o.r = o.l;
    if (kind == "mix" || kind == "keys" || wide) { Vec d (o.l.size(), 0.0f); for (size_t i = 900; i < d.size(); ++i) d[i] = o.l[i - 900]; const auto h = percept::filtered (d, true, 400, fs); for (size_t i = 0; i < d.size(); ++i) { o.l[i] += 0.3f * h[i]; o.r[i] -= 0.3f * h[i]; } }
    return o;
}
inline M measure (const Stereo& in, const Stereo& out, double fs, bool voice)
{
    auto cut = [&] (const Vec& v) { return Vec (v.begin() + 12000, v.end()); }; const auto x = cut (in.l), y = cut (out.l), yr = cut (out.r), xr = cut (in.r); M m; m.a = percept::tone (x, fs); m.b = percept::tone (y, fs);
    m.loud = percept::rmsDb (y) - percept::rmsDb (x); m.spread = spreadOf (x, fs) - spreadOf (y, fs); m.crest = crestOf (y) - crestOf (x); float pk = 0; for (float v : y) pk = std::max (pk, std::abs (v)); m.peak = 20 * std::log10 (pk + 1e-9);
    auto side = [] (const Vec& l, const Vec& r) { double s = 1e-12, c = 1e-12; for (size_t i = 0; i < l.size(); ++i) { s += std::pow ((double) l[i] - r[i], 2); c += std::pow ((double) l[i] + r[i], 2); } return percept::db10 (s / c); };
    const double s0 = side (x, xr), s1 = side (y, yr); m.side = s0 < -80 ? s1 : s1 - s0;
    for (bool hi : { false, true }) { const auto a = percept::filtered (x, hi, hi ? 6000 : 250, fs), b = percept::filtered (xr, hi, hi ? 6000 : 250, fs), c = percept::filtered (y, hi, hi ? 6000 : 250, fs), e = percept::filtered (yr, hi, hi ? 6000 : 250, fs); const double q0 = side (a, b), q1 = side (c, e); (hi ? m.sideHi : m.sideLo) = juce::jlimit (-30.0, 30.0, q0 < -80 ? q1 : q1 - q0); }
    m.words = voice ? percept::words (x, y, fs) : 1; m.pres = percept::rangeChange (m.a, m.b, 2000, 5000) - m.loud; m.ess = percept::rangeChange (m.a, m.b, 5000, 12000) - percept::rangeChange (m.a, m.b, 300, 3000); return m;
}
template <typename P> double harmonics (const juce::String& name, double fs)
{
    Stereo t; for (int i = 0; i < 96000; ++i) t.l.push_back ((float) (0.25 * std::sin (2 * percept::pi * 220 * i / fs)));
    t.r = t.l; const auto o = through<P> (name, t, fs); const int n = 48000 / 20 * 12;
    auto line = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < n; ++i) { const double w = 2 * percept::pi * f * i / fs; re += o.l[(size_t) (40000 + i)] * std::cos (w); im += o.l[(size_t) (40000 + i)] * std::sin (w); } return re * re + im * im; };
    const double one = line (220); double h = 0; for (int k = 2; k < 20; ++k) h += line (220 * k);
    return 100 * std::sqrt (h / (one + 1e-30));
}
inline double valueOf (const M& m, const juce::String& what)
{
    if (what.startsWith ("tone") || what.startsWith ("curve")) { const auto r = what.fromFirstOccurrenceOf (" ", false, false); return percept::rangeChange (m.a, m.b, r.upToFirstOccurrenceOf ("-", false, false).getDoubleValue(), r.fromFirstOccurrenceOf ("-", false, false).getDoubleValue()) - percept::rangeChange (m.a, m.b, 700, 1500); }
    return what == "spread" ? m.spread : what == "crest" ? m.crest : what == "side" ? m.side : what == "peak" ? m.peak : what == "harm" ? m.harm : what == "ess" ? m.ess : what == "side high" ? m.sideHi : what == "side low" ? m.sideLo : m.loud;
}
template <typename P> M curveOf (const juce::String& name, double fs) { std::mt19937 g (5); std::normal_distribution<double> d (0, 0.0016); Stereo in; for (int i = 0; i < 240000; ++i) in.l.push_back ((float) d (g)); in.r = in.l; return measure (in, through<P> (name, in, fs), fs, false); }
/** What becomes of 3 to 10 kHz while a vowel sounds, dB, against the whole vowel. */
inline double vowelsOf (const Vec& x, const Vec& y, double fs)
{
    const auto hx = percept::filtered (percept::filtered (x, true, 3000, fs), false, 10000, fs), hy = percept::filtered (percept::filtered (y, true, 3000, fs), false, 10000, fs), sx = percept::filtered (x, true, 5000, fs); const int w = (int) (0.03 * fs); double a = 1e-20, b = 1e-20, c = 1e-20, d = 1e-20, top = -200; for (size_t i = 0; i + (size_t) w < x.size(); i += (size_t) w) top = std::max (top, percept::rmsDb (x, i, i + (size_t) w));
    for (size_t i = 0; i + (size_t) w < x.size(); i += (size_t) w) if (percept::rmsDb (x, i, i + (size_t) w) > top - 20 && percept::rmsDb (sx, i, i + (size_t) w) < percept::rmsDb (x, i, i + (size_t) w) - 14) for (size_t j = i; j < i + (size_t) w; ++j) { a += hx[j] * hx[j]; b += hy[j] * hy[j]; c += x[j] * x[j]; d += y[j] * y[j]; }
    return percept::db10 (b / a) - percept::db10 (d / c);
}
/** The measures that need a sound of their own. */
template <typename P> double special (const juce::String& name, const juce::String& what, double level, double fs, std::map<juce::String, double>& known)
{
    if (auto it = known.find (what); it != known.end()) return it->second;
    auto give = [&] (const Vec& x) { Stereo s; s.l = x; const double g = std::pow (10.0, (level - percept::rmsDb (x)) / 20); for (auto& v : s.l) v = (float) (v * g); s.r = s.l; return s; };
    if (what == "808 side")
    {
        auto in = sound ("808", fs, level); in.r.assign (in.l.size(), 0.0f); for (size_t i = 240; i < in.l.size(); ++i) in.r[i] = in.l[i - 240]; const auto out = through<P> (name, in, fs); auto side = [&] (const Stereo& s) { const auto l = percept::filtered (s.l, false, 90, fs), r = percept::filtered (s.r, false, 90, fs); double d = 1e-12, m = 1e-12; for (size_t i = 24000; i < l.size(); ++i) { d += std::pow ((double) l[i] - r[i], 2); m += std::pow ((double) l[i] + r[i], 2); } return percept::db10 (d / m); }; known[what] = side (out) - side (in);
    }
    else if (what.startsWith ("808"))
    {
        const auto in = sound ("808", fs, level), out = through<P> (name, in, fs); auto band = [&] (const Stereo& s, bool sub) { auto v = sub ? percept::filtered (s.l, false, 70, fs) : percept::filtered (percept::filtered (s.l, true, 250, fs), false, 2500, fs); return percept::rmsDb (v, 24000, v.size() - 4800); };
        const double sub = band (out, true) - band (in, true), ph = band (out, false) - band (in, false); known["808 sub"] = sub; known["808 phone"] = ph - sub;
    }
    else if (what == "onset")
    {
        // words with pauses: 0.2 s on, 0.15 s off
        const auto v = sound ("voice", fs, level); Vec x = v.l; const int on = 9600, all = 16800; for (size_t i = 0; i < x.size(); ++i) { const int k = (int) (i % (size_t) all); x[i] *= k < on ? (float) (std::min (1.0, k / 96.0) * std::min (1.0, (on - k) / 480.0)) : 0.0f; }
        const auto in = give (x), out = through<P> (name, in, fs); double s = 0; int n = 0; for (size_t i = (size_t) all * 4; i + (size_t) all < x.size(); i += (size_t) all) { if (percept::rmsDb (in.l, i, i + 5760) < level - 10) continue; auto hit = [&] (const Vec& q) { return percept::rmsDb (q, i, i + 720) - percept::rmsDb (q, i + 720, i + 5760); }; s += hit (out.l) - hit (in.l); ++n; } known[what] = n > 0 ? s / n : -999;
    }
    else if (what.startsWith ("harm "))
    {
        auto percent = [&] (double f) { Stereo in; const double a = std::pow (10.0, level / 20) * 1.41; for (int i = 0; i < 96000; ++i) in.l.push_back ((float) (a * std::sin (2 * percept::pi * f * i / fs))); in.r = in.l; const auto out = through<P> (name, in, fs); auto line = [&] (double g) { double re = 0, im = 0; for (int i = 0; i < 24000; ++i) { const double ph = 2 * percept::pi * g * i / fs; re += out.l[(size_t) (48000 + i)] * std::cos (ph); im += out.l[(size_t) (48000 + i)] * std::sin (ph); } return re * re + im * im; }; double h = 0; for (int k = 2; k < 7; ++k) h += line (f * k); return 100 * std::sqrt (h / (line (f) + 1e-30)); };
        known[what] = percent (3000);
    }
    else if (what == "snap")
    {
        const auto in = sound ("drums", fs, level), out = through<P> (name, in, fs); auto snap = [&] (const Vec& v) { double s = 0; int n = 0; for (size_t i = 48000; i + 12000 < v.size(); i += 12000, ++n) s += percept::rmsDb (v, i, i + 480) - percept::rmsDb (v, i + 480, i + 7200); return s / n; }; known["snap"] = snap (out.l) - snap (in.l);
    }
    else if (what.startsWith ("pump") || what.startsWith ("back"))
    {
        const bool low = what.endsWith ("low"); const double f = low ? 100 : 1000; const int win = low ? 2880 : 1440; auto k = drumkit::loop ("kick", fs, 12, 0.5); const auto g = std::pow (10.0, (level - percept::rmsDb (k)) / 20), a = std::pow (10.0, (level - (low ? 10 : 24)) / 20) * 1.41; Stereo in; for (size_t i = 0; i < k.size(); ++i) in.l.push_back ((float) (g * k[i] + a * std::sin (2 * percept::pi * f * (double) i / fs))); in.r = in.l; const auto out = through<P> (name, in, fs);
        // the tone's gain through the beat, with a window that keeps the kick out, over six beats
        std::vector<double> gain; for (int at = win / 2 + (low ? 1500 : 700); at + win / 2 < 24000; at += 480) { double s = 0; for (int beat = 4; beat < 10; ++beat) { double re = 0, im = 0, w = 0; for (int i = -win / 2; i < win / 2; ++i) { const size_t j = (size_t) (beat * 24000 + at + i); const double h = 0.5 + 0.5 * std::cos (2 * percept::pi * i / win), ph = 2 * percept::pi * f * (double) j / fs; re += h * out.l[j] * std::cos (ph); im += h * out.l[j] * std::sin (ph); w += h; } s += 20 * std::log10 (2 * std::sqrt (re * re + im * im) / (w * a) + 1e-9); } gain.push_back (s / 6); }
        double top = -99, least = 99; for (double v : gain) { top = std::max (top, v); least = std::min (least, v); } const juce::String tail (low ? " low" : ""); known["pump" + tail] = least - top; known["back" + tail] = gain.back() - top;
    }
    else if (what.startsWith ("hold"))
    {
        const bool hats = what.endsWith ("hats"); const double f = what.endsWith ("sub") ? 50 : what.getTrailingIntValue(); Vec x; for (int k = 0; k < 24; ++k) { const double g = k % 2 ? 0.4 : 1.0; if (hats) { for (float v : drumkit::hit ("hat", fs, 0.25, k + 1)) x.push_back ((float) (g * v)); } else for (int i = 0; i < 12000; ++i) { const double t = i / fs; x.push_back ((float) (g * std::sin (2 * percept::pi * f * t) * std::min (1.0, t / 0.01) * std::min (1.0, (0.25 - t) / 0.02))); } }
        const auto in = give (x), out = through<P> (name, in, fs); auto apart = [&] (const Vec& v) { double s = 0; for (int k = 8; k < 24; ++k) s += (k % 2 ? -1 : 1) * percept::rmsDb (v, (size_t) k * 12000 + 1200, (size_t) k * 12000 + 9600); return s / 8; }; known[what] = apart (in.l) - apart (out.l);
    }
    return known[what];
}
inline bool isSpecial (const juce::String& w) { return w.startsWith ("808") || w == "snap" || w == "onset" || w.startsWith ("harm ") || w.startsWith ("pump") || w.startsWith ("back") || w.startsWith ("hold"); }
/** Checks every row; with print: shows what is measured as well. wordsFloor: the words measure was made for noise and drowning; it also marks a voice
    down for being compressed or driven, so plugins that do that on purpose get a lower floor. */
template <typename P> void run (const std::vector<Row>& rows, double level, const std::function<void (bool, const juce::String&, const juce::String&)>& check, bool print = false, double wordsFloor = 0.85, bool wide = false)
{
    const double fs = 48000; P probe; int styled = 0; juce::StringArray seen; for (auto* pr : probe.pluginState.allPresets()) if (! pr->user && pr->category.startsWith ("Style")) ++styled;
    struct F { juce::String name; std::vector<double> v; bool vocal; }; std::vector<F> features;
    for (auto& row : rows)
    {
        const aemon::Preset* pr = nullptr; for (auto* q : probe.pluginState.allPresets()) if (! q->user && q->category.startsWith ("Style") && q->name.startsWith (row.name) && ! seen.contains (q->name)) { pr = q; break; }
        if (pr == nullptr) { check (false, juce::String ("Style preset missing: ") + row.name, ""); continue; }
        seen.add (pr->name);
        const bool vocal = pr->category.endsWith ("vocals"); const juce::String allow (row.allow); bool ok = pr->description.isNotEmpty(); std::vector<double> feat; double worstWords = 1, worstPres = 99, worstEss = -99; const double harm = harmonics<P> (pr->name, fs); const M curve = curveOf<P> (pr->name, fs);
        const std::vector<std::string> on = vocal ? std::vector<std::string> { "voice", "low voice", "high voice" } : std::vector<std::string> { pr->name.contains ("808:") ? "808" : (pr->name.containsIgnoreCase ("drums") ? "drums" : "mix") };
        std::map<std::string, std::pair<double, double>> range;
        const Row* rule = nullptr; for (auto& r : rules::of (probe.pluginState.name)) if (pr->name.startsWith (r.name)) rule = &r;
        std::map<juce::String, double> known; std::map<juce::String, std::pair<double, double>> heard; juce::String broken;
        auto promise = [&] (const juce::String& w, double v, const Claim& c) { auto& r = heard.try_emplace (w, 1e9, -1e9).first->second; r.first = std::min (r.first, v); r.second = std::max (r.second, v); if (v < c.lo || v > c.hi) broken << w << " is " << juce::String (v, 1) << " and not " << (c.lo > -90 ? "over " + juce::String (c.lo, 1) : juce::String()) << (c.hi < 90 ? " under " + juce::String (c.hi, 1) : juce::String()) << "; "; };
        if (rule != nullptr) for (auto& c : rule->claims) if (isSpecial (c.what)) promise (c.what, special<P> (pr->name, c.what, level, fs, known), c);
        for (auto& kind : on) for (double lv : vocal ? std::vector<double> { level, level - 4 } : std::vector<double> { level })
        {
            const auto in = sound (kind, fs, lv, wide); const auto out = through<P> (pr->name, in, fs); auto m = measure (in, out, fs, vocal); m.harm = harm;
            for (auto& c : row.claims) { const double v = valueOf (juce::String (c.what).startsWith ("curve") ? curve : m, c.what); auto& r = range.try_emplace (c.what, 1e9, -1e9).first->second; r.first = std::min (r.first, v); r.second = std::max (r.second, v); ok &= v >= c.lo && v <= c.hi; }
            // what its words promise: a direction, not read off the result
            if (rule != nullptr) for (auto& c : rule->claims)
            {
                const juce::String w (c.what); if (isSpecial (w)) continue;
                const double v = w == "words" ? m.words : w == "vowels" ? vowelsOf (in.l, out.l, fs) : w == "spread low" ? spreadOf (percept::filtered (in.l, false, 220, fs), fs) - spreadOf (percept::filtered (out.l, false, 220, fs), fs) : w == "spread high" ? spreadOf (percept::filtered (in.l, true, 6000, fs), fs) - spreadOf (percept::filtered (out.l, true, 6000, fs), fs) : valueOf (w.startsWith ("curve") ? curve : m, w);
                promise (w, v, c);
            }
            if (print) { juce::String t; for (auto w : { "tone 40-110", "tone 150-350", "tone 2000-5000", "tone 8000-14000", "spread", "crest", "side", "peak", "loud", "ess", "side high", "side low" }) t << " " << w << " " << juce::String (valueOf (m, w), 1); for (auto w : { "curve 40-110", "curve 150-350", "curve 2000-5000", "curve 10000-14000" }) t << " " << w << " " << juce::String (valueOf (curve, w), 1); std::printf ("  %s | %s %.0f |%s harm %.1f words %.3f pres %.1f ess %.1f\n", pr->name.toRawUTF8(), kind.c_str(), lv, t.toRawUTF8(), harm, m.words, m.pres, m.ess); }
            if (vocal) { worstWords = std::min (worstWords, m.words); worstPres = std::min (worstPres, m.pres); worstEss = std::max (worstEss, m.ess); }
            if (lv == level) { for (int k = 2; k < percept::thirds; k += 2) feat.push_back (juce::jlimit (-25.0, 25.0, m.b[(size_t) k] - m.a[(size_t) k] - m.loud)); for (double f : { m.spread * 2, m.crest, juce::jlimit (-30.0, 10.0, m.side), m.sideHi, m.sideLo, juce::jmin (20.0, harm), m.peak, m.loud }) feat.push_back (f); }
        }
        if (vocal && ! allow.contains ("x")) ok &= worstWords > wordsFloor;
        if (vocal && ! allow.containsAnyOf ("dx")) ok &= worstPres > -3.5;
        if (vocal && ! allow.containsAnyOf ("bx")) ok &= worstEss < 3.5;
        juce::String said; for (auto& [w, r] : range) said << w << " " << juce::String (r.first, 1) << (r.second - r.first > 0.05 ? ".." + juce::String (r.second, 1) : juce::String()) << ", ";
        if (vocal) said << "words " << juce::String (worstWords, 2) << ", 2-5 kHz " << juce::String (worstPres, 1) << ", S " << juce::String (worstEss, 1);
        check (ok && ! row.claims.empty(), "Style: " + pr->name, said.trimCharactersAtEnd (", ")); features.push_back ({ pr->name, feat, vocal });
        juce::String kept; for (auto& [w, r] : heard) kept << w << " " << juce::String (r.first, 1) << (r.second - r.first > 0.05 ? ".." + juce::String (r.second, 1) : juce::String()) << ", ";
        check (rule != nullptr && broken.isEmpty(), "Words: " + pr->name, rule == nullptr ? juce::String ("NO RULE") : broken.isNotEmpty() ? broken : kept.trimCharactersAtEnd (", "));
    }
    juce::String missing; for (auto* pr : probe.pluginState.allPresets()) if (! pr->user && pr->category.startsWith ("Style") && ! seen.contains (pr->name)) missing << pr->name << "; ";
    juce::String same; double least = 1e9;
    for (size_t i = 0; i < features.size(); ++i) for (size_t j = i + 1; j < features.size(); ++j) if (features[i].vocal == features[j].vocal && features[i].v.size() == features[j].v.size()) { double d = 0; for (size_t k = 0; k < features[i].v.size(); ++k) d = std::max (d, std::abs (features[i].v[k] - features[j].v[k])); least = std::min (least, d); if (d < 0.8) same << features[i].name << " = " << features[j].name << "; "; }
    check (missing.isEmpty() && same.isEmpty(), "Every style preset is checked, and no two are the same", missing + same + juce::String (styled) + " presets, closest two " + juce::String (least, 1) + " dB apart");
}
} // namespace style
