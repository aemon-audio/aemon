// Aemon Imager. AGPL-3.0-or-later.
#pragma once

#include <aemon_core/aemon_core.h>
#include "ImagerEngine.h"

namespace imgp
{
    constexpr int cells = 24;       // 30 Hz to 18 kHz, log
    inline double cellHz (double i) { return 30.0 * std::pow (600.0, i / cells); }
    constexpr const char* bandNames[] = { "Lows", "Low mids", "High mids", "Highs" };

    /** The last samples of left and right. Audio thread writes, window reads. */
    class Ring
    {
    public:
        static constexpr int size = 65536, mask = size - 1;
        void push (const float* L, const float* R, int n) noexcept { int w = pos.load (std::memory_order_relaxed); for (int i = 0; i < n; ++i) { l[(size_t) w] = L[i]; r[(size_t) w] = R[i]; w = (w + 1) & mask; } pos.store (w, std::memory_order_release); }
        void latest (float* L, float* R, int n, int ago = 0) const noexcept { const int w = pos.load (std::memory_order_acquire) - ago; for (int i = 0; i < n; ++i) { const auto q = (size_t) ((w - n + i) & mask); L[i] = l[q]; R[i] = r[q]; } }
    private:
        std::vector<float> l = std::vector<float> ((size_t) size, 0.0f), r = l;
        std::atomic<int> pos { 0 };
    };

    /** Power of middle, side, left and right per cell. */
    struct Cells
    {
        std::array<double, cells> m {}, s {}, l {}, r {};
        void mix (const Cells& o, double k) { for (size_t i = 0; i < cells; ++i) { m[i] += (o.m[i] - m[i]) * k; s[i] += (o.s[i] - s[i]) * k; l[i] += (o.l[i] - l[i]) * k; r[i] += (o.r[i] - r[i]) * k; } }
        double middle (double lo = 0, double hi = 1e9) const { return sum (m, lo, hi); }
        double side (double lo = 0, double hi = 1e9) const { return sum (s, lo, hi); }
        double total() const { return middle() + side(); }
        /** Left louder is positive, dB. */
        double balanceDb() const { return 10 * std::log10 ((sum (l, 0, 1e9) + 1e-14) / (sum (r, 0, 1e9) + 1e-14)); }
    private:
        static double sum (const std::array<double, cells>& a, double lo, double hi) { double t = 0; for (int i = 0; i < cells; ++i) if (cellHz (i + 0.5) >= lo && cellHz (i + 0.5) < hi) t += a[(size_t) i]; return t; }
    };
    /** What mono loses, dB, never negative. */
    inline double lossDb (double middle, double side) { const double t = middle + side; return t > 1e-13 ? 10 * std::log10 (t / std::max (middle, t * 1e-4)) : 0.0; }

    class Analyser
    {
    public:
        void prepare (double rate) { if (rate == fs && fft) return; fs = rate; const int order = 12 + juce::jmax (0, juce::roundToInt (std::log2 (fs / 48000.0))); N = 1 << order; fft = std::make_unique<juce::dsp::FFT> (order); a.assign ((size_t) N, {}); b = a; win.resize ((size_t) N); for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N); L.assign ((size_t) N, 0.0f); R = L; }
        int size() const { return N; }
        Cells read (const Ring& ring, int ago = 0) { ring.latest (L.data(), R.data(), N, ago); return run(); }
        std::vector<float> L, R;
        Cells run()
        {
            Cells o;
            for (int i = 0; i < N; ++i) a[(size_t) i] = { L[(size_t) i] * win[(size_t) i], R[(size_t) i] * win[(size_t) i] };
            fft->perform (a.data(), b.data(), false);
            const double scale = 1.0 / ((double) N * N);
            for (int k = 1; k < N / 2; ++k)
            {
                const int q = (int) std::floor (cells * std::log (k * fs / N / 30.0) / std::log (600.0));
                if (q < 0 || q >= cells) continue;
                const auto z = b[(size_t) k], c = std::conj (b[(size_t) (N - k)]);
                const auto x = 0.5f * (z + c), d = 0.5f * (z - c); const std::complex<float> y (d.imag(), -d.real());
                o.m[(size_t) q] += scale * std::norm (0.5f * (x + y)); o.s[(size_t) q] += scale * std::norm (0.5f * (x - y)); o.l[(size_t) q] += scale * std::norm (x); o.r[(size_t) q] += scale * std::norm (y);
            }
            return o;
        }
    private:
        double fs = 0; int N = 4096; std::unique_ptr<juce::dsp::FFT> fft; std::vector<std::complex<float>> a, b; std::vector<float> win;
    };
}

class ImagerProcessor : public aemon::EffectProcessor
{
public:
    ImagerProcessor();
    juce::AudioProcessorEditor* createEditor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    /** What the engine gets; a mono track is left alone (there is no second side to put anything in). */
    aemon::img::Params readParams() const;
    bool monoTrack() const { return getMainBusNumInputChannels() == 1; }
    /** Balance that would centre the input, percent; nothing when there is no signal. */
    std::optional<float> centring() const { const double v = var.load(); if (v < 1e-9) return {}; return juce::jlimit (-50.0f, 50.0f, (float) std::round (200.0 * cov.load() / v) / 2.0f); }

    imgp::Ring ringInput, ringOutput;
    aemon::LoudnessMatch match;
    std::array<std::atomic<float>, aemon::img::bands> holding {};

protected:
    void prepareEngine (double fs) override { engine.setParams (readParams()); engine.prepare (fs); match.prepare (fs); cov = 0; var = 0; statCoef = 1.0 / (3.0 * fs); }
    bool wouldChangeLatency() const override { return engine.changesLatency (readParams()); }
    void applyParameters (bool withLatency) override { engine.setParams (readParams()); if (withLatency) engine.applyLatency(); }
    int engineLatency() const override { return engine.latencySamples(); }
    void processEngine (float* L, float* R, int n) override;
    float frameAux() override { for (int b = 0; b < aemon::img::bands; ++b) holding[(size_t) b] = (float) engine.holdingDb (b); return 0.0f; }
    void afterEngine (float* L, float* R, int n) override { match.process (L, R, n, real ("match") > 0.5f && ! isNonRealtime() && real ("hearMono") < 0.5f && real ("hearSide") < 0.5f, real ("bypass") > 0.5f, loudIn, loudOut); }

private:
    static std::vector<aemon::Preset> factoryPresets();
    aemon::img::Engine engine;
    std::atomic<double> cov { 0 }, var { 0 };
    double statCoef = 0;
};
