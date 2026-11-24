// Aemon core. AGPL-3.0-or-later.

namespace aemon
{

namespace
{
    // ITU-R BS.1770-4 Annex 2: 48-tap, 4-phase interpolation filter for true-peak.
    constexpr float tpCoeffs[4][12] = {
        { 0.0017089843750f, 0.0109863281250f, -0.0196533203125f, 0.0332031250000f, -0.0594482421875f, 0.1373291015625f,
          0.9721679687500f, -0.1022949218750f, 0.0476074218750f, -0.0266113281250f, 0.0148925781250f, -0.0083007812500f },
        { -0.0291748046875f, 0.0292968750000f, -0.0517578125000f, 0.0891113281250f, -0.1665039062500f, 0.4650878906250f,
          0.7797851562500f, -0.2003173828125f, 0.1015625000000f, -0.0582275390625f, 0.0330810546875f, -0.0189208984375f },
        { -0.0189208984375f, 0.0330810546875f, -0.0582275390625f, 0.1015625000000f, -0.2003173828125f, 0.7797851562500f,
          0.4650878906250f, -0.1665039062500f, 0.0891113281250f, -0.0517578125000f, 0.0292968750000f, -0.0291748046875f },
        { -0.0083007812500f, 0.0148925781250f, -0.0266113281250f, 0.0476074218750f, -0.1022949218750f, 0.9721679687500f,
          0.1373291015625f, -0.0594482421875f, 0.0332031250000f, -0.0196533203125f, 0.0109863281250f, 0.0017089843750f }
    };
}

void LoudnessMeter::TruePeak::prepare (double sampleRate)
{
    phases = sampleRate < 96000.0 ? 4 : (sampleRate < 176400.0 ? 2 : 1);
    clear();
}

inline float LoudnessMeter::TruePeak::process (float x) noexcept
{
    history[(size_t) pos] = x;
    float peak = std::abs (x);
    if (phases > 1)
    {
        const int step = 4 / phases;
        for (int ph = 0; ph < 4; ph += step)
        {
            float acc = 0.0f;
            int idx = pos;
            for (int k = 0; k < 12; ++k)
            {
                acc += tpCoeffs[ph][k] * history[(size_t) idx];
                idx = idx == 0 ? 11 : idx - 1;
            }
            peak = std::max (peak, std::abs (acc));
        }
    }
    pos = (pos + 1) % 12;
    return peak;
}

void LoudnessMeter::prepare (double sampleRate, int)
{
    fs = sampleRate;
    blockLen = juce::jmax (1, (int) std::round (fs * 0.1));

    // K-weighting, coefficients for any sample rate (BS.1770 stage 1 shelf + stage 2 high-pass).
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad s;
        s.b0 = (Vh + Vb * K / Q + K * K) / a0;
        s.b1 = 2.0 * (K * K - Vh) / a0;
        s.b2 = (Vh - Vb * K / Q + K * K) / a0;
        s.a1 = 2.0 * (K * K - 1.0) / a0;
        s.a2 = (1.0 - K / Q + K * K) / a0;
        for (auto& ch : kFilter) ch[0] = s;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad s;
        s.b0 = 1.0; s.b1 = -2.0; s.b2 = 1.0;
        s.a1 = 2.0 * (K * K - 1.0) / a0;
        s.a2 = (1.0 - K / Q + K * K) / a0;
        for (auto& ch : kFilter) ch[1] = s;
    }
    for (auto& t : tp) t.prepare (fs);
    reset();
}

void LoudnessMeter::reset()
{
    for (auto& ch : kFilter) for (auto& b : ch) b.clear();
    for (auto& t : tp) t.clear();
    blockSum = 0.0; blockCount = 0; blocks.fill (0.0); blocksWritten = 0;
    binCount.fill (0); binPower.fill (0.0); blocksSinceIntegrated = 0; peakSinceBlock = 0.0f;
    momentary = -INFINITY; shortTerm = -INFINITY; integrated = -INFINITY; truePeakMax = -INFINITY;
    resetRequested = false;
}

void LoudnessMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (resetRequested.load()) reset();
    const int nc = juce::jmin (2, numChannels);
    for (int i = 0; i < numSamples; ++i)
    {
        double sum = 0.0;
        for (int c = 0; c < nc; ++c)
        {
            const float x = channels[c][i];
            double y = kFilter[(size_t) c][0].process (x);
            y = kFilter[(size_t) c][1].process (y);
            sum += y * y;
            peakSinceBlock = std::max (peakSinceBlock, tp[(size_t) c].process (x));
        }
        blockSum += sum;
        if (++blockCount >= blockLen) finishBlock();
    }
}

void LoudnessMeter::finishBlock() noexcept
{
    blocks[(size_t) (blocksWritten % 30)] = blockSum / blockCount;
    ++blocksWritten;
    blockSum = 0.0; blockCount = 0;

    const int have = juce::jmin (blocksWritten, 30);
    auto meanOfLast = [this, have] (int n)
    {
        n = juce::jmin (n, have);
        double s = 0.0;
        for (int k = 0; k < n; ++k) s += blocks[(size_t) ((blocksWritten - 1 - k) % 30)];
        return s / n;
    };

    if (blocksWritten >= 4)
    {
        const double pm = meanOfLast (4);
        const float lm = powerToLufs (pm);
        momentary = lm;
        if (lm > -70.0f) // absolute gate
        {
            const int bin = juce::jlimit (0, numBins - 1, (int) ((lm + 70.0f) * 10.0f));
            ++binCount[(size_t) bin];
            binPower[(size_t) bin] += pm;
        }
    }
    shortTerm = blocksWritten >= 30 ? powerToLufs (meanOfLast (30)) : (blocksWritten >= 4 ? powerToLufs (meanOfLast (have)) : -INFINITY);

    const float tpDb = peakSinceBlock > 0.0f ? juce::Decibels::gainToDecibels (peakSinceBlock, -200.0f) : -INFINITY;
    if (tpDb > truePeakMax.load()) truePeakMax = tpDb;
    peakSinceBlock = 0.0f;

    if (++blocksSinceIntegrated >= 5) { blocksSinceIntegrated = 0; computeIntegrated(); }

    const float st = shortTerm.load();
    historyFifo.write (1).forEach ([&] (int idx) { historyBuf[(size_t) idx] = st; });
}

void LoudnessMeter::computeIntegrated() noexcept
{
    double p = 0.0; uint64_t n = 0;
    for (int b = 0; b < numBins; ++b) { p += binPower[(size_t) b]; n += binCount[(size_t) b]; }
    if (n == 0) { integrated = -INFINITY; return; }
    const float rel = powerToLufs (p / (double) n) - 10.0f;
    double p2 = 0.0; uint64_t n2 = 0;
    for (int b = 0; b < numBins; ++b)
    {
        const float binLo = -70.0f + (float) b * 0.1f;
        if (binLo + 0.1f > rel) { p2 += binPower[(size_t) b]; n2 += binCount[(size_t) b]; }
    }
    integrated = n2 ? powerToLufs (p2 / (double) n2) : -INFINITY;
}

int LoudnessMeter::popShortTermHistory (float* dest, int maxItems) noexcept
{
    int n = 0;
    historyFifo.read (juce::jmin (maxItems, historyFifo.getNumReady())).forEach ([&] (int idx) { dest[n++] = historyBuf[(size_t) idx]; });
    return n;
}

} // namespace aemon
