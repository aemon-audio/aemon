// Aemon EQ: linear-phase mode. AGPL-3.0-or-later. Header-only, no JUCE.
//
// The EQ curve is turned into zero-phase FIR kernels built straight from the analogue band shapes
// (so there is no cramping at all), then applied with uniformly partitioned FFT convolution.
// Because bands can sit on L, R, Mid or Side, the whole EQ is a 2x2 matrix of filters
// (L->L, R->L, L->R, R->R); the cross terms are only computed when Mid and Side differ.
// Latency = kernel length / 2 + one partition, reported to the host.
#pragma once

#include "EqEngine.h"
#include <atomic>
#include <memory>

namespace aemon::eq
{

/** In-place iterative radix-2 complex FFT with precomputed twiddles. */
class FFT
{
public:
    explicit FFT (int size = 0) { init (size); }
    void init (int size)
    {
        n = size;
        if (n == 0) return;
        tw.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k) tw[(size_t) k] = std::polar (1.0, -2.0 * pi * k / n);
        rev.resize ((size_t) n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
    }
    int size() const { return n; }
    void forward (std::complex<double>* x) const { run (x, false); }
    void inverse (std::complex<double>* x) const { run (x, true); const double s = 1.0 / n; for (int i = 0; i < n; ++i) x[i] *= s; }

private:
    void run (std::complex<double>* x, bool inv) const
    {
        for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = tw[(size_t) (k * step)];
                    if (inv) w = std::conj (w);
                    const auto a = x[i + k], b = x[i + k + half] * w;
                    x[i + k] = a + b; x[i + k + half] = a - b;
                }
        }
    }
    int n = 0;
    std::vector<std::complex<double>> tw;
    std::vector<int> rev;
};

/** Frequency-domain partitions of the four kernels, ready for convolution. Built off the audio thread. */
struct KernelSet
{
    int partitions = 0, fftSize = 0;
    bool cross = false;                                          // L->R / R->L needed (Mid and Side differ)
    std::array<std::vector<std::complex<double>>, 4> part;       // [LL, RL, LR, RR] partitions * fftSize
    double outGain = 1.0;
};

/** Linear-phase resolution. High (default) keeps steep low cuts accurate; Medium halves the delay. */
enum class Resolution { medium = 0, high };
inline int linearKernelLength (double fs, Resolution r = Resolution::high)
{
    const int base = r == Resolution::high ? 16384 : 8192;     // at 44.1/48 kHz
    return fs <= 50000 ? base : (fs <= 100000 ? base * 2 : base * 4);
}
constexpr int linearPartition = 256;

/** Builds the four zero-phase kernels for a set of bands (any thread). */
inline std::shared_ptr<KernelSet> buildKernels (const std::array<BandParams, maxBands>& bands, double outGainDb, double fs,
                                                 Resolution res = Resolution::high)
{
    const int L = linearKernelLength (fs, res), P = linearPartition, F = 2 * P;
    FFT big (L), small (F);
    auto ks = std::make_shared<KernelSet>();
    ks->partitions = L / P; ks->fftSize = F;
    ks->outGain = std::pow (10.0, outGainDb / 20.0);

    // Per-bin magnitude of each path: stereo/left bands act on L, stereo/right on R, then mid/side.
    std::vector<double> hL ((size_t) L / 2 + 1, 1.0), hR (hL), hM (hL), hS (hL);
    for (auto& b : bands)
    {
        if (! b.on) continue;
        for (int k = 0; k <= L / 2; ++k)
        {
            const double f = std::max (1.0, k * fs / L);
            const double m = std::pow (10.0, analogDb (b, f) / 20.0);
            switch (b.place)
            {
                case Place::stereo: hL[(size_t) k] *= m; hR[(size_t) k] *= m; break;
                case Place::left: hL[(size_t) k] *= m; break;
                case Place::right: hR[(size_t) k] *= m; break;
                case Place::mid: hM[(size_t) k] *= m; break;
                case Place::side: hS[(size_t) k] *= m; break;
            }
        }
    }
    // Total 2x2 response: out = MS^-1 * diag(hM, hS) * MS * diag(hL, hR) * in
    //   L' = 0.5(hM+hS) hL L + 0.5(hM-hS) hR R ;  R' = 0.5(hM-hS) hL L + 0.5(hM+hS) hR R
    ks->cross = false;
    for (int k = 0; k <= L / 2; ++k) if (std::abs (hM[(size_t) k] - hS[(size_t) k]) > 1e-9) { ks->cross = true; break; }

    // Hann window: measured best here for both accuracy and stop-band depth (see tests)
    std::vector<double> win ((size_t) L);
    for (int i = 0; i < L; ++i)
    {
        const double t = 2 * pi * i / (L - 1);
        win[(size_t) i] = 0.5 - 0.5 * std::cos (t);
    }

    std::vector<std::complex<double>> spec ((size_t) L), buf ((size_t) F);
    auto makeKernel = [&] (int which)
    {
        for (int k = 0; k <= L / 2; ++k)
        {
            const double sum = 0.5 * (hM[(size_t) k] + hS[(size_t) k]), diff = 0.5 * (hM[(size_t) k] - hS[(size_t) k]);
            double v = 0;
            switch (which) { case 0: v = sum * hL[(size_t) k]; break; case 1: v = diff * hR[(size_t) k]; break; case 2: v = diff * hL[(size_t) k]; break; case 3: v = sum * hR[(size_t) k]; break; }
            spec[(size_t) k] = v;
            if (k > 0 && k < L / 2) spec[(size_t) (L - k)] = v;
        }
        big.inverse (spec.data());                       // real, zero-phase, centred at 0
        std::vector<double> h ((size_t) L);
        for (int i = 0; i < L; ++i) h[(size_t) i] = spec[(size_t) ((i + L / 2) % L)].real() * win[(size_t) i];
        auto& out = ks->part[(size_t) which];
        out.assign ((size_t) (ks->partitions * F), {});
        for (int p = 0; p < ks->partitions; ++p)
        {
            std::fill (buf.begin(), buf.end(), std::complex<double> {});
            for (int i = 0; i < P; ++i) buf[(size_t) i] = h[(size_t) (p * P + i)];
            small.forward (buf.data());
            std::copy (buf.begin(), buf.end(), out.begin() + p * F);
        }
    };
    makeKernel (0); makeKernel (3);
    if (ks->cross) { makeKernel (1); makeKernel (2); }
    return ks;
}

/** Uniformly partitioned overlap-save convolver for the 2x2 kernel set, with a crossfade when the
    kernels change. Audio thread only, apart from setKernels() which is lock-free. */
class LinearPhaseEngine
{
public:
    LinearPhaseEngine() = default;
    ~LinearPhaseEngine() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete pendingTrash; }
    LinearPhaseEngine (const LinearPhaseEngine&) = delete;
    LinearPhaseEngine& operator= (const LinearPhaseEngine&) = delete;
    void prepare (double sampleRate, Resolution r = Resolution::high)
    {
        fs = sampleRate; res = r;
        const int L = linearKernelLength (fs, res);
        nParts = L / P;
        fft.init (F);
        for (auto& h : hist) h.assign ((size_t) (nParts * F), {});
        for (auto& b : inBuf) b.assign ((size_t) F, 0.0);
        for (auto& o : outBuf) o.assign ((size_t) P, 0.0);
        for (auto& o : outOld) o.assign ((size_t) P, 0.0);
        acc.assign ((size_t) F, {});
        fill = 0; head = 0;
        current.reset();
        delete pending.exchange (nullptr);
        delete trash.exchange (nullptr);
        delete pendingTrash; pendingTrash = nullptr;
    }
    int latency() const { return linearKernelLength (fs, res) / 2 + P; }
    Resolution resolution() const { return res; }

    /** Hand over new kernels (from any thread). Ownership is shared; the old set is released on the audio thread. */
    void setKernels (std::shared_ptr<KernelSet> k)
    {
        auto* holder = new std::shared_ptr<KernelSet> (std::move (k));
        delete pending.exchange (holder);
    }

    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            inBuf[0][(size_t) (P + fill)] = L[i];
            inBuf[1][(size_t) (P + fill)] = R[i];
            L[i] = (float) outBuf[0][(size_t) fill];
            R[i] = (float) outBuf[1][(size_t) fill];
            if (++fill == P) { fill = 0; block(); }
        }
    }

private:
    void block()
    {
        // pick up new kernels at a block boundary; the old ones fade out over this block, then go to
        // the trash slot so they are freed by collectGarbage() on another thread (no free() here)
        std::shared_ptr<KernelSet>* oldHolder = nullptr;
        if (auto* h = pending.exchange (nullptr)) { std::swap (current, *h); oldHolder = h; }
        const std::shared_ptr<KernelSet>& old = oldHolder ? *oldHolder : noKernels;

        // FFT of the last 2P input samples per channel, stored in the frequency-domain delay line
        head = (head - 1 + nParts) % nParts;
        for (int c = 0; c < 2; ++c)
        {
            auto* slot = hist[(size_t) c].data() + head * F;
            for (int i = 0; i < F; ++i) slot[i] = inBuf[(size_t) c][(size_t) i];
            fft.forward (slot);
            std::copy (inBuf[(size_t) c].begin() + P, inBuf[(size_t) c].end(), inBuf[(size_t) c].begin()); // slide
        }
        render (current, outBuf);
        if (oldHolder && old)
        {
            render (old, outOld);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < P; ++i)
                {
                    const double t = (i + 0.5) / P;
                    outBuf[(size_t) c][(size_t) i] = t * outBuf[(size_t) c][(size_t) i] + (1 - t) * outOld[(size_t) c][(size_t) i];
                }
        }
        if (oldHolder)
        {
            if (trash.load() == nullptr) trash.store (oldHolder);
            else { pendingTrash = oldHolder; }                  // rare: collected next time
        }
        else if (pendingTrash && trash.load() == nullptr) { trash.store (pendingTrash); pendingTrash = nullptr; }
    }

public:
    /** Frees kernel sets the audio thread has finished with. Call from any non-audio thread. */
    void collectGarbage() { delete trash.exchange (nullptr); }

private:

    void render (const std::shared_ptr<KernelSet>& k, std::array<std::vector<double>, 2>& out)
    {
        for (int oc = 0; oc < 2; ++oc)
        {
            if (! k) { for (int i = 0; i < P; ++i) out[(size_t) oc][(size_t) i] = 0.0; continue; }
            std::fill (acc.begin(), acc.end(), std::complex<double> {});
            for (int ic = 0; ic < 2; ++ic)
            {
                const int kIdx = oc == 0 ? (ic == 0 ? 0 : 1) : (ic == 0 ? 2 : 3);
                if ((kIdx == 1 || kIdx == 2) && ! k->cross) continue;
                const auto& kp = k->part[(size_t) kIdx];
                const auto& hs = hist[(size_t) ic];
                for (int p = 0; p < nParts; ++p)
                {
                    const auto* x = hs.data() + ((head + p) % nParts) * F;
                    const auto* h = kp.data() + p * F;
                    for (int i = 0; i < F; ++i) acc[(size_t) i] += x[i] * h[i];
                }
            }
            fft.inverse (acc.data());
            for (int i = 0; i < P; ++i) out[(size_t) oc][(size_t) i] = acc[(size_t) (P + i)].real() * k->outGain;
        }
    }

    static constexpr int P = linearPartition, F = 2 * linearPartition;
    double fs = 48000;
    Resolution res = Resolution::high;
    int nParts = 0, fill = 0, head = 0;
    FFT fft;
    std::array<std::vector<std::complex<double>>, 2> hist;
    std::array<std::vector<double>, 2> inBuf, outBuf, outOld;
    std::vector<std::complex<double>> acc;
    std::shared_ptr<KernelSet> current, noKernels;
    std::atomic<std::shared_ptr<KernelSet>*> pending { nullptr }, trash { nullptr };
    std::shared_ptr<KernelSet>* pendingTrash = nullptr;
};

} // namespace aemon::eq
