// Aemon EQ: signal engine. AGPL-3.0-or-later.
// Header-only and free of JUCE so it can be tested on its own.
//
// Zero-latency mode uses magnitude-matched second-order sections: poles by impulse invariance and
// numerators fitted to the analogue prototype, either at DC, Nyquist and the band centre (after
// M. Vicanek, "Matched Second Order Digital Filters", 2016) or by least squares over 20 Hz-20 kHz,
// whichever is closer. Unlike the usual bilinear (RBJ) designs, bands do not "cramp" near Nyquist.
// Everything on the audio path is allocation-free.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace aemon::eq
{

constexpr double pi = 3.14159265358979323846;
constexpr int maxBands = 16;
constexpr int maxSections = 4;               // 96 dB/oct cut = 4 second-order sections

enum class Type { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tilt };
enum class Place { stereo = 0, left, right, mid, side };

struct BandParams
{
    bool on = false;
    Type type = Type::bell;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
    int slope = 12;                          // cut filters: 12, 24, 48, 96 dB/oct
    Place place = Place::stereo;
    bool dynamic = false;                    // gain only acts when the band's own level passes the threshold
    double thresholdDb = -24.0;
};

inline bool hasGain (Type t) { return t == Type::bell || t == Type::lowShelf || t == Type::highShelf || t == Type::tilt; }
inline bool isCut (Type t) { return t == Type::lowCut || t == Type::highCut; }

struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

//==============================================================================
// Analogue prototypes. x = f / f0. Each returns |H(jx)|^2 for one second-order section.
namespace proto
{
    inline double den (double x, double qd) { const double r = 1.0 - x * x; return r * r + (x / qd) * (x / qd); }
    inline double bell (double x, double A, double q)  { const double r = 1 - x * x; return (r * r + (x * A / q) * (x * A / q)) / (r * r + (x / (A * q)) * (x / (A * q))); }
    inline double lowShelf (double x, double A, double q)
    {
        const double s = std::sqrt (A) * x / q, n = A - x * x, d = 1 - A * x * x;
        return A * A * (n * n + s * s) / (d * d + s * s);
    }
    inline double highShelf (double x, double A, double q)
    {
        const double s = std::sqrt (A) * x / q, n = 1 - A * x * x, d = A - x * x;
        return A * A * (n * n + s * s) / (d * d + s * s);
    }
    inline double lp (double x, double q) { return 1.0 / den (x, q); }
    inline double hp (double x, double q) { return x * x * x * x / den (x, q); }
    inline double bp (double x, double q) { return (x / q) * (x / q) / den (x, q); }
    inline double notch (double x, double q) { const double r = 1 - x * x; return r * r / den (x, q); }
}

/** Butterworth section Qs for an even order, with the user's Q applied as extra resonance on the
    sharpest section (Q 0.707 = plain Butterworth). Returns the number of sections. */
inline int cutQs (int slope, double userQ, double* out)
{
    const int order = std::clamp (slope / 6, 2, 2 * maxSections), n = order / 2;
    for (int k = 1; k <= n; ++k) out[k - 1] = 1.0 / (2.0 * std::cos ((2.0 * k - 1.0) * pi / (2.0 * order)));
    std::sort (out, out + n);
    out[n - 1] *= userQ / std::sqrt (0.5);
    return n;
}

/** One section's analogue magnitude squared at frequency f. */
struct SectionProto
{
    enum Kind { bell, lowShelf, highShelf, lp, hp, bp, notch } kind = bell;
    double f0 = 1000, A = 1, q = 1;
    double mag2 (double f) const
    {
        const double x = f / f0;
        switch (kind)
        {
            case bell: return proto::bell (x, A, q);
            case lowShelf: return proto::lowShelf (x, A, q);
            case highShelf: return proto::highShelf (x, A, q);
            case lp: return proto::lp (x, q);
            case hp: return proto::hp (x, q);
            case bp: return proto::bp (x, q);
            case notch: return proto::notch (x, q);
        }
        return 1.0;
    }
    /** Pole frequency and Q of the denominator. */
    void poles (double& fp, double& qp) const
    {
        fp = f0; qp = q;
        if (kind == bell) qp = A * q;
        else if (kind == lowShelf) fp = f0 / std::sqrt (A);
        else if (kind == highShelf) fp = f0 * std::sqrt (A);
    }
};

struct Protos { std::array<SectionProto, maxSections> p {}; int n = 0; };

inline Protos prototypes (const BandParams& b)
{
    const double A = std::pow (10.0, b.gainDb / 40.0);
    const double q = std::clamp (b.q, 0.1, 40.0);
    Protos r;
    auto add = [&r] (SectionProto::Kind k, double f, double a, double qq) { if (r.n < maxSections) r.p[(size_t) r.n++] = { k, f, a, qq }; };
    switch (b.type)
    {
        case Type::bell: add (SectionProto::bell, b.freq, A, q); break;
        case Type::lowShelf: add (SectionProto::lowShelf, b.freq, A, q); break;
        case Type::highShelf: add (SectionProto::highShelf, b.freq, A, q); break;
        case Type::notch: add (SectionProto::notch, b.freq, 1, q); break;
        case Type::bandPass: add (SectionProto::bp, b.freq, 1, q); break;
        case Type::lowCut:
        case Type::highCut:
        {
            double qs[maxSections];
            const int n = cutQs (b.slope, q, qs);
            for (int k = 0; k < n; ++k) add (b.type == Type::lowCut ? SectionProto::hp : SectionProto::lp, b.freq, 1, qs[k]);
            break;
        }
        case Type::tilt:
        {   // gentle opposing shelves: -g/2 below, +g/2 above the centre
            const double At = std::pow (10.0, b.gainDb / 80.0);
            add (SectionProto::lowShelf, b.freq, 1.0 / At, 0.5);
            add (SectionProto::highShelf, b.freq, At, 0.5);
            break;
        }
    }
    return r;
}

/** Ideal (analogue) response of a band in dB: the reference the digital filters are tested against. */
inline double analogDb (const BandParams& b, double f)
{
    double m = 1.0;
    const auto pr = prototypes (b);
    for (int k = 0; k < pr.n; ++k) m *= pr.p[(size_t) k].mag2 (f);
    return 10.0 * std::log10 (std::max (m, 1e-30));
}

/** Frequencies used to fit each section, and their trig, computed once per sample rate. */
struct Grid
{
    static constexpr int N = 48;
    double fs = 0, f[N] {}, c1[N] {}, s1[N] {}, c2[N] {}, s2[N] {}, P[N][3] {};
    void build (double sampleRate)
    {
        fs = sampleRate;
        const double top = std::min (20000.0, fs * 0.47);
        for (int i = 0; i < N; ++i)
        {
            f[i] = 20.0 * std::pow (top / 20.0, i / double (N - 1));
            const double w = 2 * pi * f[i] / fs;
            c1[i] = std::cos (w); s1[i] = std::sin (w); c2[i] = std::cos (2 * w); s2[i] = std::sin (2 * w);
            const double h = std::cos (w * 0.5) * std::cos (w * 0.5);
            P[i][0] = h; P[i][1] = 1.0 - h; P[i][2] = 4.0 * h * (1.0 - h);
        }
    }
    /** |c0 + c1 z^-1 + c2 z^-2|^2 at grid point i. */
    double mag2 (int i, double k0, double k1, double k2) const
    {
        const double re = k0 + k1 * c1[i] + k2 * c2[i], im = k1 * s1[i] + k2 * s2[i];
        return re * re + im * im;
    }
};

/** Magnitude-matched digital section. */
inline Section matched (const SectionProto& p, const Grid& g)
{
    const double fs = g.fs;
    double fp, qp;
    p.poles (fp, qp);
    const double wp = std::min (2.0 * pi * fp / fs, pi * 0.98);
    const double z = 1.0 / (2.0 * qp);
    Section s;
    const double e = std::exp (-z * wp);
    s.a1 = z <= 1.0 ? -2.0 * e * std::cos (wp * std::sqrt (1.0 - z * z)) : -2.0 * e * std::cosh (wp * std::sqrt (z * z - 1.0));
    s.a2 = std::exp (-2.0 * z * wp);
    auto aMag2 = [&] (double w) { const double re = 1 + s.a1 * std::cos (w) + s.a2 * std::cos (2 * w), im = s.a1 * std::sin (w) + s.a2 * std::sin (2 * w); return re * re + im * im; };

    if (p.kind == SectionProto::hp)
    {   // matched high-pass: double zero at DC, gain matched at the cut-off
        const double fc = std::min (p.f0 / (fs * 0.5), 0.98);
        const double w0 = pi * fc, ph0 = std::cos (w0 * 0.5) * std::cos (w0 * 0.5), ph1 = 1.0 - ph0;
        const double A0 = (1 + s.a1 + s.a2) * (1 + s.a1 + s.a2), A1 = (1 - s.a1 + s.a2) * (1 - s.a1 + s.a2), A2 = -4.0 * s.a2;
        s.b0 = std::sqrt (std::max (0.0, A0 * ph0 + A1 * ph1 + A2 * 4.0 * ph0 * ph1)) * p.q / (4.0 * ph1);
        s.b1 = -2.0 * s.b0; s.b2 = s.b0;
        return s;
    }

    // Three-point match: DC, Nyquist and the centre (Vicanek).
    const double wm = std::min (2.0 * pi * p.f0 / fs, pi * 0.85);
    const double phi0 = std::cos (wm * 0.5) * std::cos (wm * 0.5), phi1 = 1.0 - phi0, phi2 = 4.0 * phi0 * phi1;
    double B0 = p.mag2 (0.0) * aMag2 (0.0), B1 = p.mag2 (fs * 0.5) * aMag2 (pi);
    double B2 = (p.mag2 (wm * fs / (2.0 * pi)) * aMag2 (wm) - B0 * phi0 - B1 * phi1) / phi2;

    auto factor = [] (double c0, double c1, double c2, double& b0, double& b1, double& b2)
    {
        const double q0 = std::sqrt (std::max (0.0, c0)), q1 = std::sqrt (std::max (0.0, c1)), W = 0.5 * (q0 + q1);
        c2 = std::max (c2, -W * W);
        b0 = 0.5 * (W + std::sqrt (W * W + c2)); b1 = 0.5 * (q0 - q1); b2 = b0 != 0.0 ? -c2 / (4.0 * b0) : 0.0;
    };

    // Least-squares alternative over the audible range (relative error). |B|^2 = sum Bk phik is
    // linear in Bk, so it is a 3x3 solve. Keep whichever of the two fits is more accurate.
    constexpr int N = Grid::N;
    double T[N], H[N], hMax = 0;
    for (int i = 0; i < N; ++i)
    {
        H[i] = p.mag2 (g.f[i]);
        T[i] = H[i] * g.mag2 (i, 1.0, s.a1, s.a2);
        hMax = std::max (hMax, H[i]);
    }
    double M[3][3] = {}, v[3] = {};
    for (int i = 0; i < N; ++i)
    {
        if (H[i] < hMax * 1e-4) continue;                           // ignore deep stop bands (< -40 dB)
        const double wgt = 1.0 / (T[i] * T[i]);
        for (int r = 0; r < 3; ++r) { v[r] += wgt * g.P[i][r] * T[i]; for (int c = 0; c < 3; ++c) M[r][c] += wgt * g.P[i][r] * g.P[i][c]; }
    }
    auto det3 = [] (const double A[3][3]) { return A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]); };
    const double det = det3 (M);
    if (std::abs (det) > 1e-300)
    {
        double x[3];
        for (int col = 0; col < 3; ++col)
        {
            double A[3][3];
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) A[r][c] = c == col ? v[r] : M[r][c];
            x[col] = det3 (A) / det;
        }
        auto worstDb = [&] (double c0, double c1, double c2)
        {
            double b0, b1, b2, err = 0;
            factor (c0, c1, c2, b0, b1, b2);
            for (int i = 0; i < N; ++i)
                if (H[i] >= hMax * 1e-4)
                    err = std::max (err, std::abs (10.0 * std::log10 (std::max (g.mag2 (i, b0, b1, b2), 1e-30) / T[i])));
            return err;
        };
        if (std::isfinite (x[0]) && std::isfinite (x[1]) && std::isfinite (x[2]) && x[0] >= 0 && x[1] >= 0
            && worstDb (x[0], x[1], x[2]) < worstDb (B0, B1, B2)) { B0 = x[0]; B1 = x[1]; B2 = x[2]; }
    }
    factor (B0, B1, B2, s.b0, s.b1, s.b2);
    return s;
}

/** Designs a band into fixed storage (real-time safe). Returns the number of sections. */
inline int design (const BandParams& b, const Grid& g, std::array<Section, maxSections>& out)
{
    const auto pr = prototypes (b);
    for (int k = 0; k < pr.n; ++k) out[(size_t) k] = matched (pr.p[(size_t) k], g);
    return pr.n;
}

/** Convenience version for tests and drawing (allocates). */
inline std::vector<Section> design (const BandParams& b, double fs)
{
    Grid g; g.build (fs);
    std::array<Section, maxSections> a;
    const int n = design (b, g, a);
    return { a.begin(), a.begin() + n };
}

/** Textbook bilinear design (RBJ cookbook), kept only for comparison in the tests. */
inline Section rbjBell (double f0, double gainDb, double q, double fs)
{
    const double A = std::pow (10.0, gainDb / 40.0), w = 2 * pi * f0 / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
    const double a0 = 1 + al / A;
    return { (1 + al * A) / a0, -2 * c / a0, (1 - al * A) / a0, -2 * c / a0, (1 - al / A) / a0 };
}

inline double digitalDb (const std::vector<Section>& secs, double f, double fs)
{
    const std::complex<double> zi = std::polar (1.0, -2.0 * pi * f / fs);
    double m = 1.0;
    for (auto& s : secs)
        m *= std::norm (s.b0 + s.b1 * zi + s.b2 * zi * zi) / std::norm (1.0 + s.a1 * zi + s.a2 * zi * zi);
    return 10.0 * std::log10 (std::max (m, 1e-30));
}

//==============================================================================
/** Up to 16 bands, each on stereo, left, right, mid or side. Parameter changes glide (about 15 ms)
    and coefficients are recomputed every 32 samples; switching a band, its shape or its placement
    crossfades. Neutral settings pass audio through bit-exact. */
class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        grid.build (fs);
        glide = 1.0 - std::exp (-32.0 / (0.015 * fs));
        atk = 1.0 - std::exp (-1.0 / (0.005 * fs));
        rel = 1.0 - std::exp (-1.0 / (0.12 * fs));
        fadeStep = 32.0 / (0.02 * fs);
        for (auto& b : bands) { b.cur = b.target; b.mix = b.target.on ? 1.0 : 0.0; b.pending = false; b.recompute (grid); b.clear(); }
        outCur = outTarget;
    }

    void setBand (int i, const BandParams& p)
    {
        auto& b = bands[(size_t) i];
        const bool structural = p.type != b.target.type || p.slope != b.target.slope || p.place != b.target.place;
        if (structural && b.mix > 0.0) { b.pending = true; b.next = p; return; }  // fade out, switch, fade in
        if (structural) { b.cur = p; b.clear(); b.recompute (grid); }
        b.target = p;
        b.pending = false;
    }
    void setOutputGainDb (double db) { outTarget = std::pow (10.0, db / 20.0); }

    void process (float* L, float* R, int n)
    {
        if (isNeutral()) return;
        for (int start = 0; start < n; start += 32)
        {
            const int len = std::min (32, n - start);
            for (auto& b : bands) b.advance (grid, glide, fadeStep);
            const double g0 = outCur;
            outCur += (outTarget - outCur) * glide;
            if (std::abs (outCur - outTarget) < 1e-9) outCur = outTarget;
            for (int i = start; i < start + len; ++i)
            {
                double l = L[i], r = R[i];
                for (auto& b : bands)
                {
                    if (b.mix <= 0.0) continue;
                    if (b.dyn)
                    {
                        const double d = b.cur.place == Place::left ? l : b.cur.place == Place::right ? r
                                       : b.cur.place == Place::side ? 0.5 * (l - r) : 0.5 * (l + r);
                        const double e = std::abs (b.detect (d));
                        b.env += (e > b.env ? atk : rel) * (e - b.env);
                    }
                    switch (b.cur.place)
                    {
                        case Place::stereo: { const double fl = b.run (0, l), fr = b.run (1, r); l += b.mix * (fl - l); r += b.mix * (fr - r); break; }
                        case Place::left:   { const double fl = b.run (0, l); l += b.mix * (fl - l); break; }
                        case Place::right:  { const double fr = b.run (1, r); r += b.mix * (fr - r); break; }
                        case Place::mid:
                        case Place::side:
                        {
                            double m = 0.5 * (l + r), s = 0.5 * (l - r);
                            double& x = b.cur.place == Place::mid ? m : s;
                            const double y = b.run (0, x);
                            x += b.mix * (y - x);
                            l = m + s; r = m - s;
                            break;
                        }
                    }
                }
                const double g = g0 + (outCur - g0) * double (i - start + 1) / len;
                L[i] = (float) (l * g); R[i] = (float) (r * g);
            }
        }
    }

    /** Combined response of all bands for a channel view (for the UI curve), in dB. */
    double responseDb (double f, Place view = Place::stereo) const
    {
        double db = 20.0 * std::log10 (outTarget);
        for (auto& b : bands)
            if (b.target.on && (b.target.place == view || b.target.place == Place::stereo || view == Place::stereo))
                db += digitalDb (design (b.target, fs), f, fs);
        return db;
    }

private:
    struct Band
    {
        BandParams target, cur, next;
        bool pending = false;
        double mix = 0.0;
        // dynamic: band-pass detector at the band's frequency, envelope, and the gain actually applied
        bool dyn = false;
        Section det {};
        std::array<double, 4> detZ {};
        double env = 0.0, appliedGain = 0.0;
        inline double detect (double x)
        {
            const double y = det.b0 * x + det.b1 * detZ[0] + det.b2 * detZ[1] - det.a1 * detZ[2] - det.a2 * detZ[3];
            detZ[1] = detZ[0]; detZ[0] = x; detZ[3] = detZ[2]; detZ[2] = y;
            return y;
        }
        std::array<Section, maxSections> sec {};
        int nsec = 0;
        // DF1 state: [channel][section] = x1, x2, y1, y2
        std::array<std::array<std::array<double, 4>, maxSections>, 2> st {};

        void clear() { for (auto& c : st) for (auto& s : c) s.fill (0.0); detZ.fill (0.0); env = 0.0; }
        void recompute (const Grid& g)
        {
            dyn = cur.dynamic && hasGain (cur.type);
            BandParams eff = cur;
            if (dyn)
            {
                const double envDb = 20.0 * std::log10 (std::max (env, 1e-9));
                const double amount = std::clamp ((envDb - cur.thresholdDb) / 6.0, 0.0, 1.0);   // 6 dB soft knee
                eff.gainDb = cur.gainDb * amount;
                const double w = 2 * pi * cur.freq / g.fs, al = std::sin (w) / (2 * std::max (cur.q, 0.3)), c = std::cos (w), a0 = 1 + al;
                det = { al / a0, 0.0, -al / a0, -2 * c / a0, (1 - al) / a0 };
            }
            appliedGain = eff.gainDb;
            nsec = design (eff, g, sec);
        }
        void advance (const Grid& g, double glide, double fadeStep)
        {
            if (pending)
            {
                mix = std::max (0.0, mix - fadeStep);
                if (mix <= 0.0) { pending = false; target = next; cur = next; clear(); recompute (g); }
                return;
            }
            const double want = target.on ? 1.0 : 0.0;
            if (mix != want) mix = want > mix ? std::min (want, mix + fadeStep) : std::max (want, mix - fadeStep);
            if (mix <= 0.0) { if (cur.freq != target.freq || cur.gainDb != target.gainDb || cur.q != target.q) { cur = target; recompute (g); } clear(); return; }
            bool moved = false;
            auto toward = [&] (double& c, double t, bool logScale)
            {
                if (c == t) return;
                if (logScale) c = std::exp (std::log (c) + (std::log (t) - std::log (c)) * glide);
                else c += (t - c) * glide;
                if (std::abs (c - t) < (logScale ? t * 1e-5 : 1e-4)) c = t;
                moved = true;
            };
            toward (cur.freq, target.freq, true);
            toward (cur.gainDb, target.gainDb, false);
            toward (cur.q, target.q, true);
            cur.on = target.on;
            cur.dynamic = target.dynamic; cur.thresholdDb = target.thresholdDb;
            if (moved || nsec == 0 || dyn != (cur.dynamic && hasGain (cur.type))) recompute (g);
            else if (dyn)
            {   // follow the detector: recompute when the applied gain would move by more than 0.05 dB
                const double envDb = 20.0 * std::log10 (std::max (env, 1e-9));
                const double want = cur.gainDb * std::clamp ((envDb - cur.thresholdDb) / 6.0, 0.0, 1.0);
                if (std::abs (want - appliedGain) > 0.05) recompute (g);
            }
        }
        inline double run (int ch, double x)
        {
            for (int k = 0; k < nsec; ++k)
            {
                auto& s = sec[(size_t) k];
                auto& z = st[(size_t) ch][(size_t) k];
                const double y = s.b0 * x + s.b1 * z[0] + s.b2 * z[1] - s.a1 * z[2] - s.a2 * z[3];
                z[1] = z[0]; z[0] = x; z[3] = z[2]; z[2] = y;
                x = y;
            }
            return x;
        }
    };

    bool isNeutral() const
    {
        if (outCur != 1.0 || outTarget != 1.0) return false;
        for (auto& b : bands) if (b.mix > 0.0 || b.target.on || b.pending) return false;
        return true;
    }

    double fs = 48000.0, glide = 0.1, fadeStep = 0.03, atk = 0.01, rel = 0.001;
    Grid grid;
    std::array<Band, maxBands> bands {};
    double outCur = 1.0, outTarget = 1.0;
};

} // namespace aemon::eq
