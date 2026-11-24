// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Loudness per ITU-R BS.1770-4 / EBU R128: K-weighting, 400 ms momentary, 3 s short-term,
    gated integrated loudness, and 4x oversampled true-peak. Runs on the audio thread;
    results are published through atomics for the UI. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();                      // audio thread
    void requestReset() noexcept { resetRequested.store (true); } // any thread

    /** Feed one block. Channels beyond 2 are ignored (BS.1770 weights L/R at 1.0). */
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    float getMomentary() const noexcept  { return momentary.load(); }
    float getShortTerm() const noexcept  { return shortTerm.load(); }
    float getIntegrated() const noexcept { return integrated.load(); }
    float getTruePeakMaxDb() const noexcept { return truePeakMax.load(); }

    /** Pops short-term values written every 100 ms (for history graphs). Message thread. */
    int popShortTermHistory (float* dest, int maxItems) noexcept;

    static float powerToLufs (double p) noexcept { return p > 1.0e-15 ? (float) (-0.691 + 10.0 * std::log10 (p)) : -INFINITY; }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1 = z2 = 0; }
    };

    struct TruePeak
    {
        void prepare (double sampleRate);
        void clear() noexcept { history.fill (0.0f); pos = 0; }
        inline float process (float x) noexcept;
        int phases = 4;
        std::array<float, 12> history {};
        int pos = 0;
    };

    void finishBlock() noexcept;
    void computeIntegrated() noexcept;

    double fs = 48000.0;
    int blockLen = 4800;
    std::array<std::array<Biquad, 2>, 2> kFilter;
    std::array<TruePeak, 2> tp;
    double blockSum = 0.0;
    int blockCount = 0;
    std::array<double, 30> blocks {};
    int blocksWritten = 0;

    static constexpr int numBins = 800;     // 0.1 LU bins from -70 to +10 LUFS
    std::array<uint32_t, numBins> binCount {};
    std::array<double, numBins> binPower {};
    int blocksSinceIntegrated = 0;
    float peakSinceBlock = 0.0f;

    std::atomic<float> momentary { -INFINITY }, shortTerm { -INFINITY }, integrated { -INFINITY }, truePeakMax { -INFINITY };
    std::atomic<bool> resetRequested { false };

    juce::AbstractFifo historyFifo { 512 };
    std::array<float, 512> historyBuf {};
};

} // namespace aemon
