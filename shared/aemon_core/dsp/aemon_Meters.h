// Aemon core. AGPL-3.0-or-later.
#pragma once

namespace aemon
{

/** Lock-free accumulator for one stereo point (input or output): peak, mean square and
    L*R products. The audio thread adds, the UI thread takes everything since the last read. */
class StereoLevelTap
{
public:
    struct Snapshot
    {
        float peak[2] { 0, 0 };
        double sumSq[2] { 0, 0 };
        double sumLR = 0;
        int count = 0;
        float meanSquare (int ch) const noexcept { return count ? (float) (sumSq[ch] / count) : 0.0f; }
        float correlation() const noexcept
        {
            const double d = std::sqrt (sumSq[0] * sumSq[1]);
            return d > 1.0e-12 ? (float) (sumLR / d) : 1.0f;
        }
    };

    void push (const float* l, const float* r, int n) noexcept
    {
        float pk0 = 0, pk1 = 0;
        double s0 = 0, s1 = 0, lr = 0;
        for (int i = 0; i < n; ++i)
        {
            const float a = l[i], b = r[i];
            pk0 = std::max (pk0, std::abs (a));
            pk1 = std::max (pk1, std::abs (b));
            s0 += (double) a * a; s1 += (double) b * b; lr += (double) a * b;
        }
        const juce::SpinLock::ScopedTryLockType lock (spin);
        if (! lock.isLocked()) return; // UI is reading; dropping one block is harmless for meters
        acc.peak[0] = std::max (acc.peak[0], pk0);
        acc.peak[1] = std::max (acc.peak[1], pk1);
        acc.sumSq[0] += s0; acc.sumSq[1] += s1; acc.sumLR += lr; acc.count += n;
    }

    Snapshot take() noexcept
    {
        const juce::SpinLock::ScopedLockType lock (spin);
        auto s = acc;
        acc = {};
        return s;
    }

private:
    juce::SpinLock spin;
    Snapshot acc;
};

/** Stereo samples for the vectorscope. Audio thread writes, UI reads. */
class ScopeFifo
{
public:
    void push (const float* l, const float* r, int n, int decimate) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            if (++skip < decimate) continue;
            skip = 0;
            if (fifo.getFreeSpace() < 1) return;
            fifo.write (1).forEach ([&] (int idx) { buf[(size_t) idx] = { l[i], r[i] }; });
        }
    }
    int pop (juce::Point<float>* dest, int max) noexcept
    {
        int n = 0;
        fifo.read (juce::jmin (max, fifo.getNumReady())).forEach ([&] (int idx) { dest[n++] = buf[(size_t) idx]; });
        return n;
    }

private:
    static constexpr int size = 8192;
    juce::AbstractFifo fifo { size };
    std::array<juce::Point<float>, size> buf {};
    int skip = 0;
};

/** Mono ring buffer for spectrum analysers. The audio thread writes (L+R)/2, the UI copies the most
    recent N samples whenever it redraws. A torn read only affects one analyser frame. */
class MonoRing
{
public:
    static constexpr int size = 16384, mask = size - 1;
    void push (const float* l, const float* r, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { buf[(size_t) w] = 0.5f * (l[i] + r[i]); w = (w + 1) & mask; }
        writePos.store (w, std::memory_order_release);
    }
    void latest (float* dest, int n) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) dest[i] = buf[(size_t) ((w - n + i) & mask)];
    }

private:
    std::array<float, size> buf {};
    std::atomic<int> writePos { 0 };
};

} // namespace aemon
