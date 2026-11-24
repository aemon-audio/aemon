// Aemon Utility. AGPL-3.0-or-later.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace aemon::utility
{

enum class Monitor { stereo = 0, mid, side };
enum class Matrix { off = 0, encode, decode };

struct Params
{
    float gainDb = 0.0f;
    float pan = 0.0f;          // -100..100
    int panLawIndex = 1;       // 0 dB, -3, -4.5, -6
    float width = 100.0f;      // percent
    bool mono = false, swap = false, polL = false, polR = false;
    Monitor monitor = Monitor::stereo;
    Matrix matrix = Matrix::off;
    bool bassMono = false;
    float bassFreq = 120.0f;
    bool bypass = false;
};

using M2 = std::array<float, 4>; // out L = m0*L + m1*R, out R = m2*L + m3*R
inline M2 mul (const M2& a, const M2& b) noexcept
{
    return { a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3] };
}

/** Balance gains, centre = unity. Law = how much louder a hard-panned side is than centre. */
inline std::array<float, 2> panGains (float p, int lawIndex) noexcept
{
    if (p == 0.0f) return { 1.0f, 1.0f };
    if (lawIndex == 0) return { p > 0 ? 1.0f - p : 1.0f, p < 0 ? 1.0f + p : 1.0f };
    const double th = (p + 1.0) * juce::MathConstants<double>::pi / 4.0;
    const double l = std::cos (th) / std::sqrt (0.5), r = std::sin (th) / std::sqrt (0.5);
    const double e = lawIndex == 1 ? 1.0 : (lawIndex == 2 ? 1.5 : 2.0);
    return { (float) std::pow (std::max (0.0, l), e), (float) std::pow (std::max (0.0, r), e) };
}

/** Before the M/S stage: polarity, swap, M/S encode/decode. */
inline M2 preMatrix (const Params& s) noexcept
{
    M2 m { s.polL ? -1.0f : 1.0f, 0.0f, 0.0f, s.polR ? -1.0f : 1.0f };
    if (s.swap) m = mul ({ 0, 1, 1, 0 }, m);
    if (s.matrix == Matrix::encode) m = mul ({ 0.5f, 0.5f, 0.5f, -0.5f }, m);
    else if (s.matrix == Matrix::decode) m = mul ({ 1, 1, 1, -1 }, m);
    return m;
}

/** After the M/S stage: balance, gain, mid/side listening. */
inline M2 postMatrix (const Params& s) noexcept
{
    const auto g = panGains (s.pan / 100.0f, s.panLawIndex);
    const float k = juce::Decibels::decibelsToGain (s.gainDb, -200.0f);
    M2 m { g[0] * k, 0.0f, 0.0f, g[1] * k };
    if (s.monitor == Monitor::mid) m = mul ({ 0.5f, 0.5f, 0.5f, 0.5f }, m);
    else if (s.monitor == Monitor::side) m = mul ({ 0.5f, -0.5f, 0.5f, -0.5f }, m);
    return m;
}

/** The whole Utility signal path. Every control is smoothed (20 ms), so nothing clicks.
    At neutral settings the audio passes through bit-for-bit untouched. Zero latency. */
class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto* v : allSmoothers()) v->reset (fs, 0.02);
        freq.reset (fs / 64.0, 0.05);
        snapToTargets();
        updateFilter (true);
    }

    void setParams (const Params& s) noexcept
    {
        const auto a = preMatrix (s), b = postMatrix (s);
        for (int i = 0; i < 4; ++i) { pre[(size_t) i].setTargetValue (a[(size_t) i]); post[(size_t) i].setTargetValue (b[(size_t) i]); }
        width.setTargetValue (s.mono ? 0.0f : s.width / 100.0f);
        bass.setTargetValue (s.bassMono ? 1.0f : 0.0f);
        freq.setTargetValue (s.bassFreq);
        dry.setTargetValue (s.bypass ? 1.0f : 0.0f);
    }

    void snapToTargets() noexcept
    {
        for (auto* v : allSmoothers()) v->setCurrentAndTargetValue (v->getTargetValue());
        freq.setCurrentAndTargetValue (freq.getTargetValue());
        for (auto& f : hp) f.clear();
    }

    void process (float* L, float* R, int n) noexcept
    {
        const bool settling = isSettling();
        if (! settling)
        {
            if (dry.getTargetValue() >= 1.0f) return;              // bypassed: untouched
            if (isNeutral()) return;                                // neutral: untouched, bit exact
        }

        // bass mono crossover follows its frequency smoothly, once per 64 samples
        const bool msStage = settling || width.getTargetValue() != 1.0f || bass.getTargetValue() > 0.0f;
        const bool filterActive = bass.getTargetValue() > 0.0f || bass.getCurrentValue() > 0.0f;
        if (! filterActive) for (auto& f : hp) f.clear();

        for (int i = 0; i < n; ++i)
        {
            if (filterActive && (i & 63) == 0 && freq.isSmoothing()) { freq.skip (1); updateFilter (false); }
            const float l0 = L[i], r0 = R[i];
            const float a0 = pre[0].getNextValue(), a1 = pre[1].getNextValue(), a2 = pre[2].getNextValue(), a3 = pre[3].getNextValue();
            float l = a0 * l0 + a1 * r0, r = a2 * l0 + a3 * r0;
            const float w = width.getNextValue(), b = bass.getNextValue();
            if (msStage)
            {
                const float m = (l + r) * 0.5f;
                float s = (l - r) * 0.5f * w;
                if (filterActive)
                {
                    const float h = (float) hp[1].process (hp[0].process (s));
                    s += b * (h - s);
                }
                l = m + s; r = m - s;
            }
            const float p0 = post[0].getNextValue(), p1 = post[1].getNextValue(), p2 = post[2].getNextValue(), p3 = post[3].getNextValue();
            float ol = p0 * l + p1 * r, orr = p2 * l + p3 * r;
            const float d = dry.getNextValue();
            if (d > 0.0f) { ol += d * (l0 - ol); orr += d * (r0 - orr); }
            L[i] = ol; R[i] = orr;
        }
        if (filterActive && freq.isSmoothing()) updateFilter (false);
    }

    bool isSettling() const noexcept
    {
        for (auto* v : const_cast<Engine*> (this)->allSmoothers()) if (v->isSmoothing()) return true;
        return false;
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double process (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void clear() noexcept { z1 = z2 = 0; }
    };

    bool isNeutral() const noexcept
    {
        static constexpr M2 id { 1, 0, 0, 1 };
        for (int i = 0; i < 4; ++i)
            if (pre[(size_t) i].getCurrentValue() != id[(size_t) i] || post[(size_t) i].getCurrentValue() != id[(size_t) i]) return false;
        return width.getCurrentValue() == 1.0f && bass.getCurrentValue() == 0.0f && dry.getCurrentValue() == 0.0f;
    }

    /** 4th-order Linkwitz-Riley high-pass (two Butterworth stages) on the side signal. */
    void updateFilter (bool clearState)
    {
        const double f = juce::jlimit (10.0, fs * 0.45, (double) freq.getCurrentValue());
        const double K = std::tan (juce::MathConstants<double>::pi * f / fs), Q = std::sqrt (0.5);
        const double norm = 1.0 / (1.0 + K / Q + K * K);
        for (auto& h : hp)
        {
            h.b0 = norm; h.b1 = -2.0 * norm; h.b2 = norm;
            h.a1 = 2.0 * (K * K - 1.0) * norm; h.a2 = (1.0 - K / Q + K * K) * norm;
            if (clearState) h.clear();
        }
    }

    std::array<juce::SmoothedValue<float>*, 12> allSmoothers() noexcept
    {
        return { &pre[0], &pre[1], &pre[2], &pre[3], &post[0], &post[1], &post[2], &post[3], &width, &bass, &dry, &dummy };
    }

    double fs = 48000.0;
    std::array<juce::SmoothedValue<float>, 4> pre, post;
    juce::SmoothedValue<float> width { 1.0f }, bass { 0.0f }, dry { 0.0f }, dummy { 0.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freq { 120.0f };
    std::array<Biquad, 2> hp;
};

} // namespace aemon::utility
