#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace msdsp
{
//==============================================================================
// Zero-phase magnitude that one FIR of a LinPhaseBank is designed to.
struct FirShape
{
    enum Kind { LowPass4, SideWidth };
    Kind kind = LowPass4;
    // LowPass4:  p[0] cutoff (Hz).
    // SideWidth: p[0] low crossover, p[1] high crossover (Hz), p[2..4] low/mid/high width (1 = unchanged),
    //            p[5] mono-bass frequency (Hz), p[6] mono-bass amount (0..1).
    std::array<float, 7> p {};

    // Magnitude of a 4th-order Linkwitz-Riley low-pass (a squared 2nd-order Butterworth).
    // Its complement 1 - lr4Low is exactly the LR4 high-pass magnitude, so a band split built from
    // it has the familiar LR4 slopes but no phase shift.
    static double lr4Low (double f, double fc) noexcept
    {
        const double r = f / fc, r2 = r * r;
        return 1.0 / (1.0 + r2 * r2);
    }

    double magnitude (double f) const noexcept
    {
        if (kind == LowPass4) return lr4Low (f, p[0]);
        const double l0 = lr4Low (f, p[0]), l1 = lr4Low (f, p[1]);
        const double w = p[2] * l0 + p[3] * (l1 - l0) + p[4] * (1.0 - l1);
        return w * (1.0 - p[6] * lr4Low (f, p[5]));
    }

    static bool isFrequency (Kind k, int i) noexcept { return k == LowPass4 ? i == 0 : (i == 0 || i == 1 || i == 5); }
    int numParams() const noexcept { return kind == LowPass4 ? 1 : 7; }
};

//==============================================================================
// Bank of linear-phase FIR filters run by uniformly partitioned FFT convolution (overlap-save).
// Every filter sees the same input channels; the bank also gives the input delayed by exactly the
// same latency, so "delayed input - low-pass" is the matching high-pass and the bands of a split
// add back to the delayed input exactly (to float rounding), whatever the filter shapes are.
//
// Real-time behavior: everything is allocated in prepare(). When a shape changes (a crossover moves,
// a width changes) the new FIR is designed on the audio thread without allocating, and the output
// crossfades from the old filter to the new one over one block, so moves are smooth and click-free.
// The output does not depend on the host's buffer size (fixed internal block of P samples).
class LinPhaseBank
{
public:
    static constexpr int MaxFilters = 2, MaxChannels = 2;

    void prepare (double sampleRate, int channels, int filters)
    {
        fs = sampleRate;
        nCh = juce::jlimit (1, MaxChannels, channels);
        nF = juce::jlimit (1, MaxFilters, filters);
        const int mult = fs > 100000.0 ? 4 : fs > 50000.0 ? 2 : 1;
        P = 256 * mult;                          // internal block (5.3 ms at 48 kHz)
        M = 8192 * mult;                         // design grid; FIR length N = M - 1 (odd, symmetric)
        N = M - 1;
        D = N / 2;                               // centre tap: the filter's own delay (85 ms at 48 kHz)
        K = (N + P - 1) / P;                     // partitions
        spec = 2 * P + 2;                        // interleaved complex bins 0..P

        int order = 0; while ((1 << order) < 2 * P) ++order;
        blockFft = std::make_unique<juce::dsp::FFT> (order);
        order = 0; while ((1 << order) < M) ++order;
        designFft = std::make_unique<juce::dsp::FFT> (order);

        for (int f = 0; f < MaxFilters; ++f)
            for (auto& h : H[f]) h.assign ((size_t) (K * spec), 0.0f);
        for (int ch = 0; ch < MaxChannels; ++ch)
        {
            fdl[ch].assign ((size_t) (K * spec), 0.0f);
            in[ch].assign ((size_t) (2 * P), 0.0f);
            ring[ch].assign ((size_t) (P + D), 0.0f);
            for (int f = 0; f < MaxFilters; ++f) out[f][ch].assign ((size_t) P, 0.0f);
        }
        work.assign ((size_t) (4 * P), 0.0f);
        workB.assign ((size_t) (4 * P), 0.0f);
        acc.assign ((size_t) spec, 0.0f);
        accB.assign ((size_t) spec, 0.0f);
        dzIn.assign ((size_t) M, {});
        dzOut.assign ((size_t) M, {});

        // Tukey window: flat over the middle 80 %, cosine taper on the outer 20 %. The responses have
        // decayed by more than 100 dB before the taper starts (even a 40 Hz crossover: 12 time constants
        // in), so the window only removes the truncation edge and leaves the designed shape intact.
        win.assign ((size_t) N, 0.0f);
        for (int j = 0; j < N; ++j)
        {
            const double u = std::abs ((double) (j - D) / (double) D);
            win[(size_t) j] = (float) (u < 0.8 ? 1.0 : 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * (u - 0.8) / 0.2));
        }
        // Crossfade curve for one block (raised cosine)
        fade.assign ((size_t) P, 0.0f);
        for (int i = 0; i < P; ++i) fade[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (i + 0.5) / P));

        glideK = (float) (1.0 - std::exp (-(double) P / (0.03 * fs)));   // ~30 ms parameter glide
        initialised = false;
        reset();
    }

    // Clears the audio history (filters keep their current design).
    void reset()
    {
        for (int ch = 0; ch < MaxChannels; ++ch)
        {
            std::fill (fdl[ch].begin(), fdl[ch].end(), 0.0f);
            std::fill (in[ch].begin(), in[ch].end(), 0.0f);
            std::fill (ring[ch].begin(), ring[ch].end(), 0.0f);
            for (int f = 0; f < MaxFilters; ++f) std::fill (out[f][ch].begin(), out[f][ch].end(), 0.0f);
        }
        pos = 0; rpos = 0; slot = 0; countdown = 0;
    }

    int getLatency() const noexcept { return P + D; }

    // Sets where filter f should glide to. The first call after prepare() designs it immediately.
    void setTarget (int f, const FirShape& s)
    {
        target[f] = s;
        if (! initialised || cur[f].kind != s.kind)
        {
            cur[f] = designed[f] = s;
            design (designed[f], H[f][active[f]].data());
            if (f == nF - 1) initialised = true;
        }
    }

    // Per-sample use (latency P + D):  d = delayed (ch); write (ch, x);  ... lowpass (f, ch) ...;  advance();
    inline float delayed (int ch) const noexcept      { return ring[ch][(size_t) rpos]; }
    inline void write (int ch, float x) noexcept      { ring[ch][(size_t) rpos] = x; in[ch][(size_t) (P + pos)] = x; }
    inline float filtered (int f, int ch) const noexcept { return out[f][ch][(size_t) pos]; }
    inline void advance() noexcept
    {
        if (++rpos == P + D) rpos = 0;
        if (++pos == P) { pos = 0; processBlock(); }
    }

private:
    void glide()
    {
        for (int f = 0; f < nF; ++f)
        {
            auto& c = cur[f]; const auto& t = target[f];
            for (int i = 0; i < c.numParams(); ++i)
            {
                if (c.p[(size_t) i] == t.p[(size_t) i]) continue;
                if (FirShape::isFrequency (c.kind, i))
                {
                    const float r = t.p[(size_t) i] / c.p[(size_t) i];
                    c.p[(size_t) i] *= std::pow (r, glideK);
                    if (std::abs (r - 1.0f) < 0.002f) c.p[(size_t) i] = t.p[(size_t) i];
                }
                else
                {
                    c.p[(size_t) i] += glideK * (t.p[(size_t) i] - c.p[(size_t) i]);
                    if (std::abs (t.p[(size_t) i] - c.p[(size_t) i]) < 0.002f) c.p[(size_t) i] = t.p[(size_t) i];
                }
            }
        }
    }

    void processBlock() noexcept
    {
        glide();
        // At most one redesign every two blocks (~10 ms) keeps the cost of moving a control bounded.
        int upd = -1;
        if (--countdown <= 0)
            for (int k = 0; k < nF && upd < 0; ++k)
            {
                const int f = (lastUpdated + 1 + k) % nF;
                if (cur[f].p != designed[f].p) upd = f;
            }
        if (upd >= 0)
        {
            designed[upd] = cur[upd];
            design (designed[upd], H[upd][1 - active[upd]].data());
            lastUpdated = upd; countdown = 2;
        }

        slot = (slot + K - 1) % K;                      // newest spectrum goes here
        for (int ch = 0; ch < nCh; ++ch)
        {
            std::copy (in[ch].begin(), in[ch].end(), work.begin());
            blockFft->performRealOnlyForwardTransform (work.data(), true);
            std::copy (work.begin(), work.begin() + spec, fdl[ch].begin() + slot * spec);
            std::copy (in[ch].begin() + P, in[ch].end(), in[ch].begin());   // this block becomes the overlap
        }

        for (int f = 0; f < nF; ++f)
            for (int ch = 0; ch < nCh; ++ch)
            {
                convolve (ch, H[f][active[f]].data(), acc.data(), work);
                float* o = out[f][ch].data();
                if (f == upd)
                {
                    convolve (ch, H[f][1 - active[f]].data(), accB.data(), workB);
                    for (int i = 0; i < P; ++i) o[i] = work[(size_t) (P + i)] + fade[(size_t) i] * (workB[(size_t) (P + i)] - work[(size_t) (P + i)]);
                }
                else std::copy (work.begin() + P, work.begin() + 2 * P, o);
            }
        if (upd >= 0) active[upd] = 1 - active[upd];
    }

    // Sum of (input spectrum k blocks ago) x (filter partition k), then back to time; result in w[P..2P).
    void convolve (int ch, const float* h, float* a, std::vector<float>& w) noexcept
    {
        std::fill (a, a + spec, 0.0f);
        for (int k = 0; k < K; ++k)
        {
            const float* x = fdl[ch].data() + ((slot + k) % K) * spec;
            const float* hk = h + k * spec;
            for (int b = 0; b < spec; b += 2)
            {
                const float xr = x[b], xi = x[b + 1], hr = hk[b], hi = hk[b + 1];
                a[b]     += xr * hr - xi * hi;
                a[b + 1] += xr * hi + xi * hr;
            }
        }
        std::copy (a, a + spec, w.begin());
        blockFft->performRealOnlyInverseTransform (w.data());
    }

    // Frequency-sampling design: zero-phase magnitude on M bins -> inverse FFT -> centre, window,
    // then the spectrum of each P-sample partition (zero-padded to 2P). No allocation.
    void design (const FirShape& s, float* dst) noexcept
    {
        for (int k = 0; k < M; ++k)
        {
            const int kk = k <= M / 2 ? k : M - k;
            dzIn[(size_t) k] = { (float) s.magnitude ((double) kk * fs / M), 0.0f };
        }
        designFft->perform (dzIn.data(), dzOut.data(), true);        // JUCE scales the inverse by 1/M
        for (int p = 0; p < K; ++p)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            for (int i = 0; i < P; ++i)
            {
                const int j = p * P + i;
                if (j >= N) break;
                work[(size_t) i] = dzOut[(size_t) ((j - D + M) % M)].real() * win[(size_t) j];
            }
            blockFft->performRealOnlyForwardTransform (work.data(), true);
            std::copy (work.begin(), work.begin() + spec, dst + p * spec);
        }
    }

    double fs = 48000.0;
    int nCh = 1, nF = 1, P = 256, M = 8192, N = 8191, D = 4095, K = 32, spec = 514;
    std::unique_ptr<juce::dsp::FFT> blockFft, designFft;
    std::vector<float> H[MaxFilters][2], fdl[MaxChannels], in[MaxChannels], ring[MaxChannels], out[MaxFilters][MaxChannels];
    std::vector<float> work, workB, acc, accB, win, fade;
    std::vector<juce::dsp::Complex<float>> dzIn, dzOut;
    FirShape target[MaxFilters], cur[MaxFilters], designed[MaxFilters];
    int active[MaxFilters] {}, pos = 0, rpos = 0, slot = 0, countdown = 0, lastUpdated = 0;
    float glideK = 0.15f;
    bool initialised = false;
};
} // namespace msdsp
