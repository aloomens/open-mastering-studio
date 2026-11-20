#pragma once
#include <juce_dsp/juce_dsp.h>
#if JUCE_USE_SSE_INTRINSICS
 #include <emmintrin.h>
#endif
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <vector>
#include "LinearPhase.h"

// Signal-processing building blocks for Open Mastering Studio.
namespace msdsp
{
inline float dbToGain (float db)  { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float coefFromMs (double fs, double ms) { return (float) std::exp (-1.0 / (std::max (ms, 0.001) * 0.001 * fs)); }

// Optional gain-reduction statistics (used by the offline assistant; off in real time).
struct GrStats
{
    bool enabled = false;
    double sum = 0.0;
    long long n = 0;
    void reset() { sum = 0.0; n = 0; }
    float mean() const { return n > 0 ? (float) (sum / (double) n) : 0.0f; }
};

// Raises an atomic to at least v (meter "max since last read").
inline void atomicMax (std::atomic<float>& a, float v)
{
    float cur = a.load (std::memory_order_relaxed);
    while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
}

//==============================================================================
// Zero-delay-feedback state-variable filter (Simper). Stable under fast modulation.
enum class FilterType { LowPass, HighPass, Bell, LowShelf, HighShelf };

struct SvfCoeffs { double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0; };

inline SvfCoeffs makeSvf (FilterType t, double fs, double f, double q, double gainDb)
{
    f = juce::jlimit (1.0, fs * 0.49, f);
    q = std::max (0.025, q);
    const double A = std::pow (10.0, gainDb / 40.0);
    double g = std::tan (juce::MathConstants<double>::pi * f / fs);
    double k = 1.0 / q;
    SvfCoeffs c;
    switch (t)
    {
        case FilterType::LowPass:   c.m0 = 0; c.m1 = 0; c.m2 = 1; break;
        case FilterType::HighPass:  c.m0 = 1; c.m1 = -k; c.m2 = -1; break;
        case FilterType::Bell:      k = 1.0 / (q * A); c.m0 = 1; c.m1 = k * (A * A - 1); c.m2 = 0; break;
        case FilterType::LowShelf:  g /= std::sqrt (A); c.m0 = 1; c.m1 = k * (A - 1); c.m2 = A * A - 1; break;
        case FilterType::HighShelf: g *= std::sqrt (A); c.m0 = A * A; c.m1 = k * (1 - A) * A; c.m2 = 1 - A * A; break;
    }
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

// Magnitude of the same filter at frequency f (exact for the bilinear-transformed prototype).
inline double svfMagnitude (FilterType t, double fs, double f0, double q, double gainDb, double f)
{
    using C = std::complex<double>;
    const double pi = juce::MathConstants<double>::pi;
    const double w = std::tan (pi * std::min (f, fs * 0.4999) / fs) / std::tan (pi * juce::jlimit (1.0, fs * 0.49, f0) / fs);
    const C s (0.0, w);
    const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
    q = std::max (0.025, q);
    C num, den;
    switch (t)
    {
        case FilterType::LowPass:   num = 1.0;           den = s * s + s / q + 1.0; break;
        case FilterType::HighPass:  num = s * s;         den = s * s + s / q + 1.0; break;
        case FilterType::Bell:      num = s * s + s * (A / q) + 1.0; den = s * s + s / (A * q) + 1.0; break;
        case FilterType::LowShelf:  num = A * (s * s + s * (sA / q) + A); den = A * s * s + s * (sA / q) + 1.0; break;
        case FilterType::HighShelf: num = A * (A * s * s + s * (sA / q) + 1.0); den = s * s + s * (sA / q) + A; break;
    }
    return std::abs (num / den);
}

struct Svf
{
    SvfCoeffs c;
    double ic1[2] {}, ic2[2] {};
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0; }

    // Glide to new coefficients over n samples (call advance() once per sample) so that
    // parameter changes never step the output.
    void glideTo (const SvfCoeffs& t, int n)
    {
        if (n <= 0) { c = t; left = 0; return; }
        target = t; left = n;
        const double k = 1.0 / n;
        step = { (t.a1 - c.a1) * k, (t.a2 - c.a2) * k, (t.a3 - c.a3) * k, (t.m0 - c.m0) * k, (t.m1 - c.m1) * k, (t.m2 - c.m2) * k };
    }
    inline void advance() noexcept
    {
        if (left <= 0) return;
        if (--left == 0) { c = target; return; }
        c.a1 += step.a1; c.a2 += step.a2; c.a3 += step.a3; c.m0 += step.m0; c.m1 += step.m1; c.m2 += step.m2;
    }

    inline float process (int ch, float x) noexcept
    {
        const double v0 = x;
        const double v3 = v0 - ic2[ch];
        const double v1 = c.a1 * ic1[ch] + c.a2 * v3;
        const double v2 = ic2[ch] + c.a2 * ic1[ch] + c.a3 * v3;
        ic1[ch] = 2.0 * v1 - ic1[ch];
        ic2[ch] = 2.0 * v2 - ic2[ch];
        return (float) (c.m0 * v0 + c.m1 * v1 + c.m2 * v2);
    }

private:
    SvfCoeffs target, step;
    int left = 0;
};

// One-pole parameter smoother (time constant in ms).
struct Smooth
{
    float value = 0.0f, coef = 0.0f; bool init = false;
    void prepare (double fs, double ms) { coef = 1.0f - (float) std::exp (-1.0 / (ms * 0.001 * fs)); init = false; }
    inline float next (float target) noexcept
    {
        if (! init) { value = target; init = true; return value; }
        value += coef * (target - value);
        return value;
    }
};

// Butterworth section Qs for cascaded 2nd-order stages.
inline const std::vector<double>& butterworthQs (int order)
{
    static const std::vector<double> q2 { 0.70710678 }, q4 { 0.54119610, 1.30656296 },
                                     q8 { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
    return order <= 2 ? q2 : order <= 4 ? q4 : q8;
}

// Smooths a mono value from 0..1 toward a 0/1 target (used for click-free switching).
struct Ramp
{
    float value = 0.0f, step = 0.001f; bool target = false;
    void prepare (double fs, double ms = 20.0) { step = (float) (1.0 / (ms * 0.001 * fs)); }
    void snap (bool on) { target = on; value = on ? 1.0f : 0.0f; }
    bool silent() const  { return ! target && value <= 0.0f; }
    bool full() const    { return target && value >= 1.0f; }
    inline float next() noexcept
    {
        value = target ? std::min (1.0f, value + step) : std::max (0.0f, value - step);
        return value;
    }
};

//==============================================================================
struct EqSettings
{
    bool on = true; int mode = 0;
    bool hpOn = false; float hpFreq = 25; int hpSlope = 1;
    float lsFreq = 100, lsGain = 0;
    float pf[3] { 200, 1000, 4000 }, pg[3] {}, pq[3] { 0.8f, 0.8f, 0.8f };
    float hsFreq = 10000, hsGain = 0;
    bool lpOn = false; float lpFreq = 20000;
};

class MasteringEQ
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        hpRamp.prepare (fs, 20.0); lpRamp.prepare (fs, 20.0); modeRamp.prepare (fs, 15.0);
        smoothA = 1.0f - (float) std::exp (-(double) Update / (0.015 * fs)); // ~15 ms parameter glide
        warmLen = (int) (0.02 * fs);
        first = true; reset();
    }
    void reset()
    {
        for (auto& f : hp) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : bands) f.reset();
    }

    void process (juce::AudioBuffer<float>& buf, int start, int n, const EqSettings& s)
    {
        // Target vector: log-freqs, gains, Qs
        float tgt[NumP] { std::log (s.hpFreq), std::log (s.lsFreq), s.lsGain,
                          std::log (s.pf[0]), s.pg[0], s.pq[0], std::log (s.pf[1]), s.pg[1], s.pq[1],
                          std::log (s.pf[2]), s.pg[2], s.pq[2], std::log (s.hsFreq), s.hsGain, std::log (s.lpFreq) };
        if (first)
        {
            std::copy (tgt, tgt + NumP, cur);
            hpSlope = s.hpSlope; mode = s.mode;
            hpRamp.snap (s.hpOn); lpRamp.snap (s.lpOn); modeRamp.snap (true);
            first = false;
            updateCoeffs (0);
        }

        // Switching the channel mode or the low-cut slope: fade out, switch, let the filters settle, fade in.
        if (s.mode != mode) modeRamp.target = false;
        if (modeRamp.silent() && s.mode != mode) { mode = s.mode; reset(); modeWarm = warmLen; }
        if (modeWarm <= 0 && s.mode == mode) modeRamp.target = true;

        if (s.hpSlope != hpSlope) hpRamp.target = false;
        if (hpRamp.silent() && s.hpSlope != hpSlope) { hpSlope = s.hpSlope; for (auto& f : hp) f.reset(); updateCoeffs (0); hpWarm = warmLen; }
        if (s.hpSlope == hpSlope && hpWarm <= 0) hpRamp.target = s.hpOn;
        lpRamp.target = s.lpOn;

        auto* L = buf.getWritePointer (0, start);
        auto* R = buf.getWritePointer (1, start);
        const int numHp = (int) butterworthQs (hpSlope == 0 ? 2 : hpSlope == 1 ? 4 : 8).size();

        for (int i0 = 0; i0 < n; i0 += Update)
        {
            bool moved = false;
            for (int k = 0; k < NumP; ++k)
            {
                const float d = tgt[k] - cur[k];
                if (std::abs (d) > 1.0e-4f) { cur[k] += d * smoothA; moved = true; }
                else cur[k] = tgt[k];
            }
            if (moved) updateCoeffs (Update);

            const int i1 = std::min (n, i0 + Update);
            for (int i = i0; i < i1; ++i)
            {
                for (auto& f : hp) f.advance();
                for (auto& f : bands) f.advance();
                for (auto& f : lp) f.advance();

                const float dryL = L[i], dryR = R[i];
                float a = dryL, b = dryR;
                if (mode != 0) { const float m = 0.5f * (a + b), sd = 0.5f * (a - b); a = m; b = sd; }

                const float hpMix = hpRamp.next(), lpMix = lpRamp.next(), wet = modeRamp.next();
                for (int ch = 0; ch < 2; ++ch)
                {
                    if ((mode == 1 && ch == 1) || (mode == 2 && ch == 0)) continue;
                    float x = ch == 0 ? a : b;
                    float y = x;                                   // cut filters always run, so they are settled when switched in
                    for (int k = 0; k < numHp; ++k) y = hp[k].process (ch, y);
                    x += hpMix * (y - x);
                    for (auto& f : bands) x = f.process (ch, x);
                    y = lp[1].process (ch, lp[0].process (ch, x));
                    x += lpMix * (y - x);
                    (ch == 0 ? a : b) = x;
                }
                if (mode != 0) { const float l = a + b, r = a - b; a = l; b = r; }
                L[i] = dryL + wet * (a - dryL);
                R[i] = dryR + wet * (b - dryR);
            }
        }
        modeWarm -= n; hpWarm -= n;
    }

private:
    enum { NumP = 15, Update = 16 };
    void updateCoeffs (int glide)
    {
        const auto& qs = butterworthQs (hpSlope == 0 ? 2 : hpSlope == 1 ? 4 : 8);
        for (size_t k = 0; k < qs.size(); ++k) hp[k].glideTo (makeSvf (FilterType::HighPass, fs, std::exp (cur[0]), qs[k], 0), glide);
        bands[0].glideTo (makeSvf (FilterType::LowShelf, fs, std::exp (cur[1]), 0.7071, cur[2]), glide);
        for (int b = 0; b < 3; ++b)
            bands[1 + b].glideTo (makeSvf (FilterType::Bell, fs, std::exp (cur[3 + b * 3]), cur[5 + b * 3], cur[4 + b * 3]), glide);
        bands[4].glideTo (makeSvf (FilterType::HighShelf, fs, std::exp (cur[12]), 0.7071, cur[13]), glide);
        const auto& lq = butterworthQs (4);
        for (int k = 0; k < 2; ++k) lp[k].glideTo (makeSvf (FilterType::LowPass, fs, std::exp (cur[14]), lq[(size_t) k], 0), glide);
    }

    double fs = 44100;
    Svf hp[4], bands[5], lp[2];
    Ramp hpRamp, lpRamp, modeRamp;
    float cur[NumP] {}, smoothA = 0.03f;
    int hpSlope = 1, mode = 0, warmLen = 960, modeWarm = 0, hpWarm = 0;
    bool first = true;
};

//==============================================================================
// Feed-forward compressor gain computer with mean-square detector and soft knee.
struct CompCore
{
    float attC = 0.9f, relC = 0.999f, detC = 0.9f, ms = 0.0f, grDb = 0.0f, lastLevel = -100.0f;

    // Call once per block of n samples. Attack/release changes glide (log domain, ~60 ms) so that a
    // compressor that was lagging behind does not suddenly snap to its target.
    void setTimes (double fs, float attMs, float relMs, float detMs, int n = 0)
    {
        const float a = std::log (std::max (0.01f, attMs)), r = std::log (std::max (0.01f, relMs));
        if (n <= 0 || logAtt == 0.0f) { logAtt = a; logRel = r; }
        else
        {
            const float k = 1.0f - (float) std::exp (-(double) n / (0.06 * fs));
            logAtt += k * (a - logAtt); logRel += k * (r - logRel);
        }
        attC = coefFromMs (fs, std::exp (logAtt)); relC = coefFromMs (fs, std::exp (logRel)); detC = coefFromMs (fs, detMs);
        slowC = coefFromMs (fs, std::max (300.0, 5.0 * std::exp ((double) logRel)));
    }
    void reset() { ms = 0.0f; grDb = 0.0f; grSlow = 0.0f; }
    float logAtt = 0.0f, logRel = 0.0f;

    static float curve (float lvl, float thr, float ratio, float knee)
    {
        const float over = lvl - thr, slope = 1.0f / ratio - 1.0f;
        if (2.0f * over < -knee) return 0.0f;
        if (2.0f * std::abs (over) <= knee) { const float t = over + knee * 0.5f; return slope * t * t / (2.0f * knee); }
        return slope * over;
    }

    // x2 = detector input power. Returns gain change in dB (<= 0).
    // autoRelease: program-dependent (dual) release. A slow follower tracks the average reduction;
    // the result is whichever reduces more. After a short peak the gain recovers at the set release
    // time, but only back to the average, and from there slowly, so sustained compression does not
    // pump. The follower always runs, so switching it on or off never jumps.
    inline float process (float x2, float thr, float ratio, float knee = 6.0f, bool autoRelease = false) noexcept
    {
        ms = x2 + detC * (ms - x2);
        const float lvl = 10.0f * std::log10 (ms + 1.0e-12f) + 3.0103f; // sine peak-equivalent
        lastLevel = lvl;
        const float target = curve (lvl, thr, ratio, knee);
        const float c = target < grDb ? attC : relC;
        grDb = target + c * (grDb - target);
        grSlow = grDb + slowC * (grSlow - grDb);
        return autoRelease ? std::min (grDb, grSlow) : grDb;
    }
    float slowC = 0.9999f, grSlow = 0.0f;
};

//==============================================================================
struct BandSettings { float thr = -20, ratio = 2, att = 15, rel = 150, gain = 0; bool bypass = false, solo = false; };
struct MbSettings  { bool on = false; float xLow = 150, xHigh = 4000; BandSettings b[3]; bool linear = false, autoRelease = false; };

class ThreeBandSplit
{
public:
    void prepare (double fs)
    {
        juce::dsp::ProcessSpec spec { fs, 512, 2 };
        for (auto* f : { &x1, &x2, &ap2 }) { f->prepare (spec); }
        ap2.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        f1 = f2 = -1.0f;
    }
    void reset() { x1.reset(); x2.reset(); ap2.reset(); }
    void setFreqs (float lo, float hi)
    {
        hi = std::max (hi, lo * 1.5f);
        if (std::abs (lo - f1) > 0.001f) { x1.setCutoffFrequency (lo); f1 = lo; }
        if (std::abs (hi - f2) > 0.001f) { x2.setCutoffFrequency (hi); ap2.setCutoffFrequency (hi); f2 = hi; }
    }
    inline void split (int ch, float x, float& lo, float& mid, float& hi) noexcept
    {
        float rest;
        x1.processSample (ch, x, lo, rest);
        lo = ap2.processSample (ch, lo);
        x2.processSample (ch, rest, mid, hi);
    }
    float f1 = -1, f2 = -1;
private:
    juce::dsp::LinkwitzRileyFilter<float> x1, x2, ap2;
};

// Smoothly glides a pair of crossover frequencies (log domain, ~15 ms). Call every Update samples.
struct FreqGlide
{
    static constexpr int Update = 4;
    float lo = 0, hi = 0, k = 0.02f; bool init = false;
    void prepare (double fs) { k = 1.0f - (float) std::exp (-(double) Update / (0.015 * fs)); init = false; }
    bool step (float tLo, float tHi)
    {
        if (! init) { lo = tLo; hi = tHi; init = true; return true; }
        if (lo == tLo && hi == tHi) return false;
        lo *= std::pow (tLo / lo, k); hi *= std::pow (tHi / hi, k);
        if (std::abs (lo - tLo) < 0.01f) lo = tLo;
        if (std::abs (hi - tHi) < 0.01f) hi = tHi;
        return true;
    }
};

class MultibandComp
{
public:
    std::atomic<float> grMeter[3] {};

    void prepare (double sampleRate)
    {
        fs = sampleRate; split.prepare (fs); glide.prepare (fs);
        lin.prepare (fs, 2, 2);
        for (int b = 0; b < 3; ++b) { thr[b].prepare (fs, 20.0); active[b].prepare (fs, 15.0); solo[b].prepare (fs, 15.0); gain[b].prepare (fs, 20.0); }
        reset();
    }
    void reset()
    {
        split.reset(); lin.reset();
        for (auto& c : comp) c.reset();
    }

    // Linear-phase crossovers (FIR, no phase shift at the band edges) instead of Linkwitz-Riley IIR.
    // Changes the latency, so the chain switches it between blocks (fading the output around it).
    void setLinear (bool on) { if (on != linear) { linear = on; reset(); } }
    bool isLinear() const { return linear; }
    int getLatency() const { return linear ? lin.getLatency() : 0; }
    int getLinearLatency() const { return lin.getLatency(); }   // what linear phase would add (same for the Imager)

    void process (juce::AudioBuffer<float>& buf, int start, int n, const MbSettings& s)
    {
        if (linear)
        {
            FirShape lo, hi;
            lo.p[0] = s.xLow; hi.p[0] = std::max (s.xHigh, s.xLow * 1.5f);
            lin.setTarget (0, lo); lin.setTarget (1, hi);
        }
        bool anySolo = false;
        for (auto& b : s.b) anySolo |= b.solo;
        float gainT[3], activeT[3], soloT[3];
        for (int b = 0; b < 3; ++b)
        {
            comp[b].setTimes (fs, s.b[b].att, s.b[b].rel, b == 0 ? 12.0f : b == 1 ? 5.0f : 2.0f, n);
            gainT[b] = s.b[b].bypass ? 1.0f : dbToGain (s.b[b].gain);
            activeT[b] = s.b[b].bypass ? 0.0f : 1.0f;            // bypass fades the compression out, not abruptly
            soloT[b] = (anySolo && ! s.b[b].solo) ? 0.0f : 1.0f; // solo fades the other bands
        }
        auto* L = buf.getWritePointer (0, start);
        auto* R = buf.getWritePointer (1, start);
        float grMax[3] {};

        for (int i0 = 0; i0 < n; i0 += FreqGlide::Update)
        {
            if (! linear && glide.step (s.xLow, s.xHigh)) split.setFreqs (glide.lo, glide.hi);
            const int i1 = std::min (n, i0 + FreqGlide::Update);
            for (int i = i0; i < i1; ++i)
            {
                float bl[3], br[3];
                if (linear)
                {
                    // bands from the FIR bank: low = LP1, mid = LP2 - LP1, high = delayed - LP2 (sums exactly)
                    const float dl = lin.delayed (0), dr = lin.delayed (1);
                    lin.write (0, L[i]); lin.write (1, R[i]);
                    const float l1 = lin.filtered (0, 0), l2 = lin.filtered (1, 0), r1 = lin.filtered (0, 1), r2 = lin.filtered (1, 1);
                    lin.advance();
                    bl[0] = l1; bl[1] = l2 - l1; bl[2] = dl - l2;
                    br[0] = r1; br[1] = r2 - r1; br[2] = dr - r2;
                }
                else
                {
                    split.split (0, L[i], bl[0], bl[1], bl[2]);
                    split.split (1, R[i], br[0], br[1], br[2]);
                }
                float outL = 0, outR = 0;
                for (int b = 0; b < 3; ++b)
                {
                    const float x2 = std::max (bl[b] * bl[b], br[b] * br[b]);
                    const float gr = comp[b].process (x2, thr[b].next (s.b[b].thr), s.b[b].ratio, 6.0f, s.autoRelease);
                    const float act = active[b].next (activeT[b]);
                    grMax[b] = std::max (grMax[b], -gr * act);
                    const float gDb = gr * act;
                    const float g = (gDb < -0.001f ? dbToGain (gDb) : 1.0f) * gain[b].next (gainT[b]) * solo[b].next (soloT[b]);
                    outL += bl[b] * g; outR += br[b] * g;
                }
                L[i] = outL; R[i] = outR;
            }
        }
        for (int b = 0; b < 3; ++b) { atomicMax (grMeter[b], grMax[b]); grLatest[b].store (grMax[b], std::memory_order_relaxed); }
    }
    std::atomic<float> grLatest[3] {};   // reduction in the last block, per band (for the Multiband view)

private:
    double fs = 44100;
    ThreeBandSplit split;
    LinPhaseBank lin;
    bool linear = false;
    FreqGlide glide;
    CompCore comp[3];
    Smooth thr[3], active[3], solo[3], gain[3];
};

//==============================================================================
struct BusSettings { bool on = false; float thr = -12, ratio = 2, att = 10, rel = 200, makeup = 0, mix = 100, hpf = 60; };

class BusComp
{
public:
    std::atomic<float> grMeter { 0.0f };
    GrStats stats;
    std::vector<float> grTrace, levelTrace;   // per sample of the last process() call, for the Glue view
    void prepare (double sampleRate, int maxBlock = 512)
    {
        fs = sampleRate; thr.prepare (fs, 20.0);
        grTrace.assign ((size_t) maxBlock, 0.0f); levelTrace.assign ((size_t) maxBlock, -100.0f);
        reset();
    }
    void reset() { comp.reset(); sc.reset(); makeup = -1000.0f; mix = -1.0f; }

    void process (juce::AudioBuffer<float>& buf, int start, int n, const BusSettings& s)
    {
        comp.setTimes (fs, s.att, s.rel, 1.0f, n);
        if (std::abs (s.hpf - scFreq) > 0.01f) { scFreq = s.hpf; sc.c = makeSvf (FilterType::HighPass, fs, scFreq, 0.7071, 0); }
        const float mkT = dbToGain (s.makeup), mixT = s.mix * 0.01f;
        if (makeup < -100.0f) { makeup = mkT; mix = mixT; }
        const float sm = 1.0f - coefFromMs (fs, 20.0);
        auto* L = buf.getWritePointer (0, start);
        auto* R = buf.getWritePointer (1, start);
        float grMax = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float sl = sc.process (0, L[i]), sr = sc.process (1, R[i]);
            const float gr = comp.process (std::max (sl * sl, sr * sr), thr.next (s.thr), s.ratio, 8.0f);
            grMax = std::max (grMax, -gr);
            if (stats.enabled) { stats.sum -= gr; ++stats.n; }
            if (i < (int) grTrace.size()) { grTrace[(size_t) i] = -gr; levelTrace[(size_t) i] = comp.lastLevel; }
            makeup += sm * (mkT - makeup); mix += sm * (mixT - mix);
            const float g = dbToGain (gr) * makeup;
            const float wet = mix * g + (1.0f - mix);
            L[i] *= wet; R[i] *= wet;
        }
        atomicMax (grMeter, grMax);
    }

private:
    double fs = 44100;
    CompCore comp;
    Svf sc;
    Smooth thr;
    float scFreq = -1.0f, makeup = -1000.0f, mix = -1.0f;
};

//==============================================================================
struct SatSettings { bool on = false; int type = 0; float drive = 6, mix = 100; };

// Saturation types. 0-2 are memoryless curves; 3-4 are models with memory (state per channel).
enum SatType { SatTape = 0, SatTube, SatSoftClip, SatTapeModel, SatTubeModel, NumSatTypes };

//------------------------------------------------------------------------------
// Magnetic tape: Jiles-Atherton hysteresis (as used for tape by J. Chowdhury, "Real-time Physical
// Modelling for Analog Tape Machines", DAFx 2019), solved per oversampled sample with a 2nd-order
// Runge-Kutta step in H. Around it: record pre-emphasis / playback de-emphasis (exact inverse
// shelves, so the highs saturate earlier, as on tape, while small signals stay flat) and a gentle
// head bump. Distortion depends on level, frequency and the signal's history.
struct TapeModel
{
    // Tape constants: Chowdhury's Ms, a, alpha and k; c (the reversible share) is raised from 0.17 to
    // 0.9 to stand in for AC bias, which on a real recorder linearises the tape so it follows the
    // anhysteretic curve with only a narrow loop. With the raw value the model is far too distorted
    // at normal levels and expands (gets louder) at medium drive, which tape does not do.
    static constexpr double Ms = 3.5e5, a = 2.2e4, alpha = 1.6e-3, k = 2.7e4, c = 0.9;
    static constexpr double Hscale = 2.2e4;   // input full scale at 0 dB drive -> H

    void prepare (double upRate)
    {
        fsUp = upRate;
        rateK = 1.0 - std::exp (-1.0 / (0.02 * upRate));   // field-speed average: 20 ms
        pre.c  = makeSvf (FilterType::HighShelf, upRate, 3500.0, 0.6, 5.0);
        post.c = makeSvf (FilterType::HighShelf, upRate, 3500.0, 0.6, -5.0);
        bump.c = makeSvf (FilterType::Bell, upRate, 62.0, 1.1, 0.8);
        // Small-signal susceptibility (dM/dH for low-level audio), so that 0 dB drive at low level is unity gain.
        reset();
        double num = 0, den = 0;
        const double amp = 0.05 * Hscale;
        for (int i = 0; i < (int) (0.02 * upRate); ++i)
        {
            const double h = amp * std::sin (2.0 * juce::MathConstants<double>::pi * 500.0 * i / upRate);
            const double m = step (0, h);
            if (i > (int) (0.01 * upRate)) { num += m * h; den += h * h; }
        }
        chi = den > 0 ? std::max (1.0e-3, num / den) : 1.0;
        reset();
    }
    void reset()
    {
        for (int ch = 0; ch < 2; ++ch) { M[ch] = 0.0; Hp[ch] = 0.0; rate[ch] = 0.0; }
        pre.reset(); post.reset(); bump.reset();
    }

    inline float process (int ch, float x, float drive) noexcept
    {
        const float e = pre.process (ch, x);
        const double h = (double) e * drive * Hscale;
        const double m = step (ch, h);
        const float y = (float) (m / (chi * drive * Hscale));
        return bump.process (ch, post.process (ch, y));
    }

    // Static curve for the Saturation view: the anhysteretic magnetisation (normalised to unity slope).
    static float staticCurve (float x, float d) noexcept { return (float) (3.0 * langevin ((double) d * x) / d); }

private:
    static inline double langevin (double q) noexcept
    {
        if (std::abs (q) < 1.0e-3) return q / 3.0 - q * q * q / 45.0;
        return 1.0 / std::tanh (q) - 1.0 / q;
    }
    static inline double langevinD (double q) noexcept
    {
        if (std::abs (q) < 1.0e-3) return 1.0 / 3.0 - q * q / 15.0;
        const double ct = 1.0 / std::tanh (q);
        return 1.0 / (q * q) - ct * ct + 1.0;
    }
    // Langevin function L(q) and its slope L'(q) from one shared coth (one transcendental per call).
    static inline void langevinBoth (double q, double& l, double& ld) noexcept
    {
        if (std::abs (q) < 1.0e-3) { l = q / 3.0 - q * q * q / 45.0; ld = 1.0 / 3.0 - q * q / 15.0; return; }
        const double ct = 1.0 + 2.0 / (std::exp (2.0 * q) - 1.0), iq = 1.0 / q;   // coth (q); exp is cheaper than tanh
        l = ct - iq;
        ld = iq * iq - ct * ct + 1.0;
    }
    // dM/dH of the Jiles-Atherton model for the direction of travel delta (+1 / -1)
    static inline double dMdH (double m, double h, double delta, double irrWeight) noexcept
    {
        const double q = (h + alpha * m) / a;
        double l, ld;
        langevinBoth (q, l, ld);
        const double mAn = Ms * l, mDiff = mAn - m;
        // The irreversible part acts only while M lags behind the anhysteretic curve in the direction of
        // travel. A smooth gate (instead of an on/off switch at mDiff = 0) avoids a corner there; its
        // width scales with the magnetisation, so it behaves the same at every level. (x / sqrt(1 + x^2)
        // is as smooth as tanh and much cheaper.)
        const double gx = delta * mDiff / (0.05 * std::abs (mAn) + 1.0);
        const double deltaM = irrWeight * 0.5 * (1.0 + gx / std::sqrt (1.0 + gx * gx));
        const double irr = (1.0 - c) * deltaM * mDiff / ((1.0 - c) * delta * k - alpha * mDiff);
        const double rev = c * (Ms / a) * ld;
        return (irr + rev) / (1.0 - c * alpha * (Ms / a) * ld);
    }
    // Heun (2nd-order Runge-Kutta) in H. Large steps (loud, fast highs at high drive) are split so q
    // moves at most 0.1 per sub-step: one big step is inaccurate, and because the error depends on
    // where the samples fall on the waveform it would show up as non-harmonic spurs (aliasing-like).
    //
    // Rounded loop tips: in the textbook model the irreversible part switches off the instant the field
    // reverses, which leaves a corner in the output at every waveform peak. A corner has harmonics far
    // beyond what 8x oversampling can contain (THE_LAB found the 77th harmonic of a 10 kHz tone folded
    // to 2 kHz at -88 dB). Real loops have rounded tips, so the irreversible part fades out and back in
    // as the field slows to a stop and turns: weight = dh^2 / (dh^2 + eps^2), eps = 60 % of the average
    // field speed. Scale-free, so it rounds the tips the same way at every level and frequency.
    inline double step (int ch, double h) noexcept
    {
        const double dhTotal = h - Hp[ch];
        const double speed = std::abs (dhTotal);
        rate[ch] += rateK * (speed - rate[ch]);   // smooth average speed (no per-cycle jumps)
        if (dhTotal != 0.0)
        {
            const double delta = dhTotal > 0.0 ? 1.0 : -1.0;
            const double eps = 0.6 * rate[ch];   // ~0.4 x the peak speed of a sine
            const double w = speed * speed / (speed * speed + eps * eps + 1.0e-30);
            const int subs = std::min (MaxSubsteps, 1 + (int) (std::max (1.6 * rate[ch], 0.5 * speed) / (SubstepQ * a)));   // steady through a tone; transients still get enough
            const double dh = dhTotal / subs;
            double m = M[ch], hp = Hp[ch];
            for (int s = 0; s < subs; ++s)
            {
                const double k1 = dMdH (m, hp, delta, w);
                const double k2 = dMdH (m + dh * k1, hp + dh, delta, w);
                m = juce::jlimit (-Ms, Ms, m + 0.5 * dh * (k1 + k2));
                hp += dh;
            }
            M[ch] = m; Hp[ch] = h;
        }
        return M[ch];
    }

public:
    // Measured (1.7.4): Heun with steps of 0.03 in q, at most 4 per sample, keeps every high-note spur at
    // -122 dB; on music it costs ~1.8 us/sample and the worst case (full-scale bright noise) is bounded.
    // A 4th-order solver (1.7.2/1.7.3) was no cleaner and cost ~70 % more on normal material.
    static inline int MaxSubsteps = 4;
    static inline double SubstepQ = 0.03;
private:
    double fsUp = 384000.0, chi = 1.0, M[2] {}, Hp[2] {}, rate[2] {}, rateK = 1.0e-4;
    Svf pre, post, bump;
};

//------------------------------------------------------------------------------
// Tube stage whose operating point moves with the signal: the bias follows the envelope (as grid
// current shifts a real stage's bias), so louder passages get more asymmetry (more even harmonics)
// and a slight compression ("bloom"), quiet passages stay clean. Output is zero for zero input at
// any bias, so the moving bias does not thump.
struct TubeModel
{
    static constexpr float bias0 = 0.15f, biasRange = 0.35f;

    void prepare (double upRate)
    {
        attC = 1.0f - coefFromMs (upRate, 3.0);
        relC = 1.0f - coefFromMs (upRate, 80.0);
        const float t = std::tanh (bias0);
        norm = 1.0f / (1.0f - t * t);
        reset();
    }
    void reset() { env[0] = env[1] = 0.0f; }

    inline float process (int ch, float x, float d) noexcept
    {
        const float u = d * x;
        const float au = std::min (std::abs (u), 2.0f);
        env[ch] += (au > env[ch] ? attC : relC) * (au - env[ch]);
        const float b = bias0 + biasRange * env[ch];
        return (std::tanh (u + b) - std::tanh (b)) * norm / d;
    }

    static float staticCurve (float x, float d) noexcept
    {
        const float t = std::tanh (bias0);
        return (std::tanh (d * x + bias0) - t) / (d * (1.0f - t * t));
    }

private:
    float env[2] {}, attC = 0.01f, relC = 0.001f, norm = 1.0f;
};

class Saturator
{
public:
    // log2 of the oversampling factor (3 = 8x). Measured: 2x let high-frequency harmonics fold back audibly.
    static constexpr int OsStages = 3;

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        // Built stage by stage. JUCE's "max quality" preset gives the stage next to the host rate a
        // transition band of 0.06 (about 21-27 kHz at 48 kHz), so harmonics just above Nyquist were only
        // partly removed before folding back: THE_LAB measured the 4th harmonic of a 6.3 kHz tone
        // (25.2 kHz) at -78 dB, folded to 22.8 kHz, on both Tube types. That stage now has a 0.02
        // transition (about 23-25 kHz) and -110 dB stopband; the outer stages keep JUCE's values.
        os = std::make_unique<juce::dsp::Oversampling<float>> (2);
        for (int n = 0; n < OsStages; ++n)
        {
            const bool inner = n == 0;
            os->addOversamplingStage (juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
                                      inner ? 0.02f : 0.10f, inner ? -110.0f : -90.0f + 10.0f * (float) n,
                                      inner ? 0.02f : 0.12f, inner ? -110.0f : -75.0f + 10.0f * (float) n);
        }
        os->initProcessing ((size_t) maxBlock);
        latency = (int) std::lround (os->getLatencyInSamples());
        dry.setSize (2, maxBlock);
        delay.assign (2, std::vector<float> ((size_t) latency + 1, 0.0f));
        dcR = (float) (1.0 - 2.0 * juce::MathConstants<double>::pi * 3.0 / fs);
        mix.prepare (fs, 20.0);
        const double upRate = fs * (1 << OsStages);
        driveUp.prepare (upRate, 20.0);                   // runs at the oversampled rate
        typeStep = (float) (1.0 / (0.025 * upRate));      // 25 ms crossfade between curve types
        tape.prepare (upRate); tube.prepare (upRate);
        first = true;
        reset();
    }
    void reset()
    {
        if (os) os->reset();
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        dIdx = 0; dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
        tape.reset(); tube.reset();
    }
    int getLatency() const { return latency; }

    void process (juce::AudioBuffer<float>& buf, int start, int n, const SatSettings& s)
    {
        const float mixT = s.on ? s.mix * 0.01f : 0.0f;
        const float driveT = dbToGain (s.drive);
        const int type = juce::jlimit (0, NumSatTypes - 1, s.type);
        if (first) { curType = prevType = type; typeFade = 1.0f; first = false; }
        if (type != curType && typeFade >= 1.0f)
        {
            prevType = curType; curType = type; typeFade = 0.0f;
            if (type == SatTapeModel) tape.reset();        // a model fading in starts from rest
            if (type == SatTubeModel) tube.reset();
        }

        // Delayed dry path (keeps latency constant whether on or off)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* x = buf.getReadPointer (ch, start);
            auto* d = dry.getWritePointer (ch);
            auto& line = delay[(size_t) ch];
            int idx = dIdx;
            for (int i = 0; i < n; ++i)
            {
                line[(size_t) idx] = x[i];
                idx = (idx + 1) % (int) line.size();
                d[i] = line[(size_t) idx];
            }
        }
        dIdx = (dIdx + n) % (int) delay[0].size();

        // When saturation has been fully off for a while, rest the (costly) oversampled path and just pass
        // the latency-matched dry signal. On switch-on it restarts from a clean state while the mix
        // fades in from zero, so nothing jumps.
        if (mixT <= 0.0f && mix.value < 1.0e-5f)
        {
            idleSamples += n;
            if (idleSamples > (int) (0.5 * fs))
            {
                mix.value = 0.0f;
                for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, start, dry, ch, 0, n);
                resting = true;
                return;
            }
        }
        else idleSamples = 0;
        if (resting)
        {
            // Waking up: restart the oversampling filters and let them settle silently (mix held at
            // zero) for 10 ms before the wet signal fades in.
            os->reset(); dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
            tape.reset(); tube.reset();
            resting = false; warmLeft = (int) (0.01 * fs);
        }
        const float mixTarget = warmLeft > 0 ? 0.0f : mixT;
        warmLeft = std::max (0, warmLeft - n);

        juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), 2, (size_t) start, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int un = (int) up.getNumSamples();
        auto* xl = up.getChannelPointer (0);
        auto* xr = up.getChannelPointer (1);
        for (int i = 0; i < un; ++i)
        {
            const float d = driveUp.next (driveT);
            if (typeFade < 1.0f)
            {
                typeFade = std::min (1.0f, typeFade + typeStep);
                const float pl = run (prevType, 0, xl[i], d), pr = run (prevType, 1, xr[i], d);
                xl[i] = pl + typeFade * (run (curType, 0, xl[i], d) - pl);
                xr[i] = pr + typeFade * (run (curType, 1, xr[i], d) - pr);
            }
            else
            {
                xl[i] = run (curType, 0, xl[i], d);
                xr[i] = run (curType, 1, xr[i], d);
            }
        }
        os->processSamplesDown (block);

        auto* yl = buf.getWritePointer (0, start);
        auto* yr = buf.getWritePointer (1, start);
        auto* dl = dry.getReadPointer (0);
        auto* dr = dry.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float m = mix.next (mixTarget);
            float* y[2] { yl, yr };
            const float* d[2] { dl, dr };
            for (int ch = 0; ch < 2; ++ch)
            {
                // DC blocker on the wet path (asymmetric curves produce DC)
                const float w = y[ch][i];
                const float out = w - dcX[ch] + dcR * dcY[ch];
                dcX[ch] = w; dcY[ch] = out;
                y[ch][i] = d[ch][i] + m * (out - d[ch][i]);
            }
        }
    }

    // The saturation curves (public so the Saturation view can draw what is applied). For the two
    // models with memory this is their static (resting) curve.
    static inline float shape (int type, float x, float d) noexcept
    {
        switch (type)
        {
            case SatTape: // Tape: symmetric, odd harmonics, small-signal unity
                return std::tanh (d * x) / d;
            case SatTube: // Tube: biased (even harmonics), small-signal unity
            {
                constexpr float b = 0.25f;
                const float tb = std::tanh (b);
                return (std::tanh (d * x + b) - tb) / (d * (1.0f - tb * tb));
            }
            case SatTapeModel: return TapeModel::staticCurve (x, d);
            case SatTubeModel: return TubeModel::staticCurve (x, d);
            default: // Soft clip (cubic)
            {
                const float u = juce::jlimit (-1.0f, 1.0f, d * x * (2.0f / 3.0f));
                return (u - u * u * u / 3.0f) * 1.5f / d;
            }
        }
    }

private:
    inline float run (int type, int ch, float x, float d) noexcept
    {
        if (type == SatTapeModel) return tape.process (ch, x, d);
        if (type == SatTubeModel) return tube.process (ch, x, d);
        return shape (type, x, d);
    }
    TapeModel tape;
    TubeModel tube;
public:

private:
    double fs = 44100;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dry;
    std::vector<std::vector<float>> delay;
    int dIdx = 0, latency = 0;
    float dcR = 0.9995f, dcX[2] {}, dcY[2] {};
    Smooth mix, driveUp;
    int curType = 0, prevType = 0, idleSamples = 0, warmLeft = 0;
    float typeFade = 1.0f, typeStep = 0.001f;
    bool first = true, resting = false;
};

//==============================================================================
// 3-band stereo width on the side signal; mid gets the matching all-pass so both stay phase-aligned.
struct ImgSettings
{
    bool on = false; float xLow = 200, xHigh = 4000, wLow = 100, wMid = 100, wHigh = 100;
    bool linear = false;                  // linear-phase crossovers (FIR) instead of Linkwitz-Riley
    bool monoBass = false; float monoFreq = 120;   // remove the side signal below monoFreq (24 dB/oct)
    bool safe = false;                    // mono-safe: never let the side outweigh the mid (correlation >= 0)
};

class Imager
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        split.prepare (fs);
        juce::dsp::ProcessSpec spec { fs, 512, 1 };
        for (auto* f : { &apM1, &apM2, &apMono, &hpMono }) f->prepare (spec);
        apM1.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        apM2.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        apMono.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        hpMono.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        glide.prepare (fs); monoGlide.prepare (fs); first = true;
        monoAmt.prepare (fs, 20.0); safeAmt.prepare (fs, 20.0);
        envC = 1.0f - coefFromMs (fs, 50.0);
        safeAtt = 1.0f - coefFromMs (fs, 20.0); safeRel = 1.0f - coefFromMs (fs, 300.0);
        lin.prepare (fs, 1, 1);
        midDelay.assign ((size_t) lin.getLatency() + 1, 0.0f);
        reset();
    }
    void reset()
    {
        split.reset(); apM1.reset(); apM2.reset(); apMono.reset(); hpMono.reset(); lin.reset();
        std::fill (midDelay.begin(), midDelay.end(), 0.0f); mdPos = 0;
        eMid = eSide = eIn = 0.0f; safeGain = 1.0f;
    }

    void setLinear (bool on) { if (on != linear) { linear = on; reset(); } }
    bool isLinear() const { return linear; }
    int getLatency() const { return linear ? lin.getLatency() : 0; }

    void process (juce::AudioBuffer<float>& buf, int start, int n, const ImgSettings& s)
    {
        const float wt[3] { s.wLow * 0.01f, s.wMid * 0.01f, s.wHigh * 0.01f };
        if (first) { std::copy (wt, wt + 3, w); first = false; }
        const float sm = 1.0f - coefFromMs (fs, 20.0);
        auto* L = buf.getWritePointer (0, start);
        auto* R = buf.getWritePointer (1, start);

        if (linear)
        {
            // One FIR shapes the side signal: the per-band widths and the mono-bass cut combined into a
            // single zero-phase response. The mid is only delayed, so nothing is phase-shifted.
            FirShape sh; sh.kind = FirShape::SideWidth;
            sh.p = { s.xLow, std::max (s.xHigh, s.xLow * 1.5f), wt[0], wt[1], wt[2], s.monoFreq, s.monoBass ? 1.0f : 0.0f };
            lin.setTarget (0, sh);
            const int dl = (int) midDelay.size();
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]);
                midDelay[(size_t) mdPos] = m;
                if (++mdPos == dl) mdPos = 0;
                const float mOut = midDelay[(size_t) mdPos];         // oldest = latency samples ago
                const float sIn = lin.delayed (0);
                lin.write (0, sd);
                float sOut = lin.filtered (0, 0);
                lin.advance();
                sOut *= safety (mOut, sOut, sIn, s.safe);
                L[i] = mOut + sOut; R[i] = mOut - sOut;
            }
            return;
        }

        for (int i0 = 0; i0 < n; i0 += FreqGlide::Update)
        {
            if (glide.step (s.xLow, s.xHigh))
            {
                split.setFreqs (glide.lo, glide.hi);
                if (std::abs (glide.lo - m1) > 0.001f) { apM1.setCutoffFrequency (glide.lo); m1 = glide.lo; }
                if (std::abs (split.f2 - m2) > 0.001f) { apM2.setCutoffFrequency (split.f2); m2 = split.f2; }
            }
            if (monoGlide.step (s.monoFreq, s.monoFreq) && std::abs (monoGlide.lo - mf) > 0.001f)
            {
                mf = monoGlide.lo; apMono.setCutoffFrequency (mf); hpMono.setCutoffFrequency (mf);
            }
            const int i1 = std::min (n, i0 + FreqGlide::Update);
            for (int i = i0; i < i1; ++i)
            {
                for (int k = 0; k < 3; ++k) w[k] += sm * (wt[k] - w[k]);
                const float m = 0.5f * (L[i] + R[i]), sd = 0.5f * (L[i] - R[i]);
                float mAp = apM2.processSample (0, apM1.processSample (0, m));
                float lo, mid, hi;
                split.split (0, sd, lo, mid, hi);
                float sOut = lo * w[0] + mid * w[1] + hi * w[2];
                // Mono bass: high-pass the side and give the mid the matching all-pass, so the two stay
                // aligned above the cutoff. Both filters always run; switching crossfades (exactly the
                // old signal when off).
                const float a = monoAmt.next (s.monoBass ? 1.0f : 0.0f);
                const float sHp = hpMono.processSample (0, sOut), mApMono = apMono.processSample (0, mAp);
                sOut += a * (sHp - sOut);
                mAp += a * (mApMono - mAp);
                sOut *= safety (mAp, sOut, sd, s.safe);
                L[i] = mAp + sOut; R[i] = mAp - sOut;
            }
        }
    }

private:
    // Mono-safe width: if the widened side carries more energy than the mid (L/R correlation below 0)
    // and more than the input already had, turn the side down until it doesn't. Smoothed (20 ms
    // down, 300 ms back), so it acts like a gentle width limiter rather than a switch.
    inline float safety (float mOut, float sOut, float sIn, bool on) noexcept
    {
        eMid += envC * (mOut * mOut - eMid);
        eSide += envC * (sOut * sOut - eSide);
        eIn += envC * (sIn * sIn - eIn);
        const float allowed = std::max (eMid, eIn);
        const float want = eSide > allowed && eSide > 1.0e-12f ? std::sqrt (allowed / eSide) : 1.0f;
        safeGain += (want < safeGain ? safeAtt : safeRel) * (want - safeGain);
        const float amt = safeAmt.next (on ? 1.0f : 0.0f);
        return 1.0f + amt * (safeGain - 1.0f);
    }

    double fs = 44100;
    ThreeBandSplit split;
    juce::dsp::LinkwitzRileyFilter<float> apM1, apM2, apMono, hpMono;
    FreqGlide glide, monoGlide;
    LinPhaseBank lin;
    std::vector<float> midDelay;
    int mdPos = 0;
    Smooth monoAmt, safeAmt;
    float w[3] { 1, 1, 1 }, m1 = -1, m2 = -1, mf = -1;
    float envC = 0.001f, safeAtt = 0.001f, safeRel = 0.0001f, eMid = 0, eSide = 0, eIn = 0, safeGain = 1.0f;
    bool first = true, linear = false;
};

//==============================================================================
// 8x polyphase interpolator for inter-sample (true) peak estimation: 48-tap Kaiser-windowed sinc
// per phase (BS.1770 asks for at least 4x; this is finer and far more accurate near the top of
// the band). Delay: D samples.
template <int TapCount, int BetaX10>
class TruePeakDetectorT
{
public:
    static constexpr int Taps = TapCount, Phases = 8, D = Taps / 2;

    TruePeakDetectorT()
    {
        auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        constexpr double beta = BetaX10 / 10.0;
        for (int p = 0; p < Phases; ++p)
        {
            const double f = (double) p / Phases;
            double sum = 0.0;
            for (int k = 0; k < Taps; ++k)
            {
                const double t = (double) (k - D) + f; // offset from the interpolated point, in samples
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const double u = t / (D + 0.5);
                const double win = std::abs (u) < 1.0 ? bessel0 (beta * std::sqrt (1.0 - u * u)) / bessel0 (beta) : 0.0;
                cd[p][k] = sinc * win; sum += cd[p][k];
            }
            double absSum = 0.0;
            for (int k = 0; k < Taps; ++k) { coef[p][k] = (float) (cd[p][k] / sum); absSum += std::abs (cd[p][k] / sum); }
            bound = std::max (bound, (float) absSum * 1.0001f);
        }
        reset();
    }
    void reset() { for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f); pos = 0; }

    // Push one sample per channel; returns the true-peak estimate for the sample D steps ago.
    // threshold: callers only care whether the peak exceeds it (limiter: ceiling; meter: running max).
    // If even the worst case (loudest sample in the window x the filter's absolute-sum gain) cannot
    // reach it, the interpolation is skipped - exact, not an approximation.
    inline float push (float l, float r, float threshold = 0.0f) noexcept
    {
        write (l, r);
        const float* h0 = hist[0] + pos;                 // h[k] = sample k steps ago, contiguous
        const float* h1 = hist[1] + pos;
        const float centre = std::max (std::abs (h0[D]), std::abs (h1[D]));
        if (threshold > 0.0f && windowMax (h0, h1) * bound <= threshold) return centre;

        float peak = centre;
        for (const float* h : { h0, h1 })
            for (int p = 1; p < Phases; ++p)
                peak = std::max (peak, std::abs (dot (coef[p], h)));
        return peak;
    }
    // Same as push but returns the plain sample peak D steps ago (keeps identical delay).
    inline float pushSamplePeak (float l, float r) noexcept
    {
        write (l, r);
        return std::max (std::abs (hist[0][pos + D]), std::abs (hist[1][pos + D]));
    }

private:
    static_assert (Taps % 8 == 0, "SIMD loops work in groups of 8");

    static inline float dot (const float* c, const float* h) noexcept
    {
       #if JUCE_USE_SSE_INTRINSICS
        __m128 a = _mm_setzero_ps(), b = _mm_setzero_ps();
        for (int k = 0; k < Taps; k += 8)
        {
            a = _mm_add_ps (a, _mm_mul_ps (_mm_load_ps (c + k),     _mm_loadu_ps (h + k)));
            b = _mm_add_ps (b, _mm_mul_ps (_mm_load_ps (c + k + 4), _mm_loadu_ps (h + k + 4)));
        }
        a = _mm_add_ps (a, b);
        a = _mm_add_ps (a, _mm_movehl_ps (a, a));
        a = _mm_add_ss (a, _mm_shuffle_ps (a, a, 1));
        return _mm_cvtss_f32 (a);
       #else
        float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        for (int k = 0; k < Taps; k += 4) { a0 += c[k] * h[k]; a1 += c[k + 1] * h[k + 1]; a2 += c[k + 2] * h[k + 2]; a3 += c[k + 3] * h[k + 3]; }
        return (a0 + a1) + (a2 + a3);
       #endif
    }

    static inline float windowMax (const float* h0, const float* h1) noexcept
    {
       #if JUCE_USE_SSE_INTRINSICS
        const __m128 signMask = _mm_castsi128_ps (_mm_set1_epi32 (0x7fffffff));
        __m128 m = _mm_setzero_ps();
        for (int k = 0; k < Taps; k += 4)
            m = _mm_max_ps (m, _mm_max_ps (_mm_and_ps (_mm_loadu_ps (h0 + k), signMask), _mm_and_ps (_mm_loadu_ps (h1 + k), signMask)));
        m = _mm_max_ps (m, _mm_movehl_ps (m, m));
        m = _mm_max_ss (m, _mm_shuffle_ps (m, m, 1));
        return _mm_cvtss_f32 (m);
       #else
        float m = 0;
        for (int k = 0; k < Taps; ++k) m = std::max (m, std::max (std::abs (h0[k]), std::abs (h1[k])));
        return m;
       #endif
    }

    // History stored twice so the newest Taps samples are always contiguous.
    inline void write (float l, float r) noexcept
    {
        pos = (pos + Taps - 1) % Taps;
        hist[0][pos] = hist[0][pos + Taps] = l;
        hist[1][pos] = hist[1][pos + Taps] = r;
    }
    float bound = 0.0f;
    double cd[Phases][Taps] {};
    alignas (16) float coef[Phases][Taps] {};
    alignas (16) float hist[2][2 * Taps] {};
    int pos = 0;
};

using TruePeakDetector = TruePeakDetectorT<48, 60>;

//==============================================================================
struct LimSettings { bool on = true; float gain = 0, ceiling = -1, release = 100; bool truePeak = true; int style = 0; };

// Limiter styles. Classic: 1.5 ms look-ahead, one release, one smoothing stage (as in 1.6).
// Transparent: 3 ms look-ahead, program-dependent (dual) release and two cascaded smoothing stages.
enum LimStyle { LimClassic = 0, LimTransparent, NumLimStyles };

// Minimum of the last W values (monotonic queue): O(1) per sample instead of scanning the window.
// Returns exactly what a scan over a window pre-filled with 1.0 would.
class SlidingMin
{
public:
    void prepare (int maxWindow) { cap = maxWindow + 1; v.assign ((size_t) cap, 1.0f); t.assign ((size_t) cap, 0); reset (maxWindow); }
    void reset (int window) { W = window; head = 0; count = 0; now = 0; }
    inline float push (float x) noexcept
    {
        while (count > 0 && v[(size_t) back()] >= x) --count;
        const int slot = (head + count) % cap;
        v[(size_t) slot] = x; t[(size_t) slot] = now; ++count;
        while (t[(size_t) head] <= now - W) { head = (head + 1) % cap; --count; }
        ++now;
        return std::min (1.0f, v[(size_t) head]);
    }
private:
    int back() const noexcept { return (head + count - 1) % cap; }
    std::vector<float> v;
    std::vector<long long> t;
    int cap = 2, W = 1, head = 0, count = 0;
    long long now = 0;
};

// Look-ahead brickwall limiter: min-hold over the look-ahead window, release, then smoothing that
// spans the same window, so the gain is fully down before each peak arrives.
template <typename Detector>
class LimiterT
{
public:
    std::atomic<float> grMeter { 0.0f };
    GrStats stats;

    static constexpr double TransparentLookaheadMs = 3.0;

    void prepare (double sampleRate, double lookaheadMs = 1.5)
    {
        fs = sampleRate;
        classicL = std::max (1, (int) std::lround (lookaheadMs * 0.001 * fs));
        transparentL = std::max (classicL, (int) std::lround (TransparentLookaheadMs * 0.001 * fs));
        const int maxL = std::max (classicL, transparentL);
        minQ.prepare (maxL + 1);
        avgRing.assign ((size_t) maxL, 1.0f);
        avgRing2.assign ((size_t) maxL, 1.0f);
        gainTrace.assign (512, 1.0f);
        for (auto& d : delay) d.assign ((size_t) (maxL + Detector::D), 0.0f);
        ceilRing.assign ((size_t) (maxL + Detector::D), 1.0f);
        drive.prepare (fs, 40.0);
        ceiling.prepare (fs, 20.0);
        onAmt.prepare (fs, 20.0);
        applyStyle();
    }
    void reset()
    {
        minQ.reset (L + 1);
        std::fill (avgRing.begin(), avgRing.end(), 1.0f);
        std::fill (avgRing2.begin(), avgRing2.end(), 1.0f);
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (ceilRing.begin(), ceilRing.end(), 1.0f);
        tp.reset();
        sum = (double) L1; sum2 = (double) L2; r = rSlow = 1.0f; ai = ai2 = di = 0;
    }
    int getLatency() const { return L + Detector::D; }
    int getMaxLatency() const { return std::max (classicL, transparentL) + Detector::D; }

    // Switching style changes the latency, so the chain does it between blocks (with a fade).
    void setStyle (int s)
    {
        s = juce::jlimit (0, NumLimStyles - 1, s);
        if (s != style) { style = s; applyStyle(); }
    }
    int getStyle() const { return style; }

    void process (juce::AudioBuffer<float>& buf, int start, int n, const LimSettings& s)
    {
        // Gain, ceiling and on/off all glide, so moving them never jumps the level.
        const float driveT = s.on ? dbToGain (s.gain) : 1.0f;
        const float ceilT = dbToGain (s.ceiling), onT = s.on ? 1.0f : 0.0f;
        const float relC = coefFromMs (fs, s.release);
        const float slowC = coefFromMs (fs, juce::jlimit (250.0, 2500.0, 8.0 * s.release));
        const bool transparent = style == LimTransparent;
        auto* Lp = buf.getWritePointer (0, start);
        auto* Rp = buf.getWritePointer (1, start);
        float gMin = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const float dr = drive.next (driveT), ceil = ceiling.next (ceilT), on = onAmt.next (onT);
            const float l = Lp[i] * dr, rr = Rp[i] * dr;

            const float pk = s.truePeak ? tp.push (l, rr, ceil) : tp.pushSamplePeak (l, rr);
            const float req = pk > ceil ? ceil / pk : 1.0f;
            const float m = minQ.push (req);

            r = m < r ? m : m + relC * (r - m);
            float g;
            if (! transparent)
            {
                sum += (double) r - (double) avgRing[(size_t) ai];
                avgRing[(size_t) ai] = r;
                if (++ai == L1) { ai = 0; sum = 0.0; for (int k = 0; k < L1; ++k) sum += avgRing[(size_t) k]; } // avoid drift
                g = (float) (sum / L1);
            }
            else
            {
                // Program-dependent release: a slow follower holds the average reduction, so the gain
                // recovers quickly after a lone peak but does not pump on dense, sustained material.
                rSlow = r + slowC * (rSlow - r);
                const float rr2 = std::min (r, rSlow);
                // Two cascaded moving averages (together spanning the look-ahead): an S-shaped gain
                // curve with no corners, which puts far less distortion on the signal than one stage.
                sum += (double) rr2 - (double) avgRing[(size_t) ai];
                avgRing[(size_t) ai] = rr2;
                if (++ai == L1) { ai = 0; sum = 0.0; for (int k = 0; k < L1; ++k) sum += avgRing[(size_t) k]; }
                const float g1 = (float) (sum / L1);
                sum2 += (double) g1 - (double) avgRing2[(size_t) ai2];
                avgRing2[(size_t) ai2] = g1;
                if (++ai2 == L2) { ai2 = 0; sum2 = 0.0; for (int k = 0; k < L2; ++k) sum2 += avgRing2[(size_t) k]; }
                g = (float) (sum2 / L2);
            }
            if (on > 0.5f) gMin = std::min (gMin, g);
            if (i < (int) gainTrace.size()) gainTrace[(size_t) i] = 1.0f + on * (g - 1.0f);
            if (stats.enabled) { stats.sum += (g < 0.99999f && on > 0.5f) ? -gainToDb (g) : 0.0f; ++stats.n; }

            // Each sample carries the ceiling that was in force when it entered, so a moving
            // ceiling never clips what the limiter already planned for.
            const float dlL = delay[0][(size_t) di], dlR = delay[1][(size_t) di], dlC = ceilRing[(size_t) di];
            delay[0][(size_t) di] = l; delay[1][(size_t) di] = rr; ceilRing[(size_t) di] = ceil;
            if (++di == dlen) di = 0;

            const float wl = juce::jlimit (-dlC, dlC, dlL * g), wr = juce::jlimit (-dlC, dlC, dlR * g);
            Lp[i] = dlL + on * (wl - dlL);
            Rp[i] = dlR + on * (wr - dlR);
        }
        atomicMax (grMeter, -gainToDb (gMin));
    }

private:
    void applyStyle()
    {
        if (style == LimTransparent) { L = transparentL; L1 = std::max (1, L / 2); L2 = L - L1 + 1; }
        else { L = classicL; L1 = L; L2 = 1; }
        dlen = L + Detector::D;
        reset();
    }

    double fs = 44100;
    int style = LimClassic, classicL = 64, transparentL = 128, L = 64, L1 = 64, L2 = 1, dlen = 64, ai = 0, ai2 = 0, di = 0;
public:
    std::vector<float> gainTrace;   // applied gain per sample of the last process() call (Limiter view)
private:
    SlidingMin minQ;
    std::vector<float> avgRing, avgRing2, ceilRing;
    std::vector<float> delay[2];
    Detector tp;
    double sum = 0, sum2 = 0;
    float r = 1.0f, rSlow = 1.0f;
    Smooth drive, ceiling, onAmt;
};

// Main limiter (48-tap detector) and the final true-peak stage (longer, more accurate detector).
using Limiter = LimiterT<TruePeakDetector>;
using TruePeakGuard = LimiterT<TruePeakDetectorT<96, 80>>;

//==============================================================================
struct DitherSettings { int bits = 0; bool shape = false; };

class Dither
{
public:
    static constexpr int MaxOrder = 9;

    // Noise shaping (error feedback, noise transfer 1 - sum h[k] z^-k):
    //  44.1/48 kHz: Wannamaker's 9-coefficient F-weighted filter (psychoacoustically optimal: the
    //               noise is pushed out of the 1-6 kHz region where hearing is most sensitive).
    //  88.2 kHz and up: 2nd-order high-pass shaping, (1 - z^-1)^2, which moves the noise above 20 kHz.
    void prepare (double sampleRate)
    {
        static const double fWeighted[MaxOrder] { 2.412, -3.370, 3.937, -4.174, 3.353, -2.205, 1.281, -0.569, 0.0847 };
        static const double secondOrder[2] { 2.0, -1.0 };
        const double* src = sampleRate <= 50000.0 ? fWeighted : secondOrder;
        order = sampleRate <= 50000.0 ? MaxOrder : 2;
        for (int k = 0; k < MaxOrder; ++k) h[k] = k < order ? (float) src[k] : 0.0f;
        reset();
    }
    void reset() { for (auto& e : err) std::fill (std::begin (e), std::end (e), 0.0f); ePos = 0; }

    // Noise-transfer magnitude at frequency f (for the tests and docs).
    double shapingGainDb (double f, double fs) const
    {
        std::complex<double> s (1.0, 0.0);
        for (int k = 0; k < order; ++k) s -= (double) h[k] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * f / fs * (k + 1));
        return 20.0 * std::log10 (std::abs (s));
    }

    void process (juce::AudioBuffer<float>& buf, int start, int n, const DitherSettings& s)
    {
        if (s.bits == 0) return;
        const int bits = s.bits == 1 ? 24 : 16;
        const float scale = (float) (1 << (bits - 1)), lsb = 1.0f / scale;
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buf.getWritePointer (ch, start + i);
                float fb = 0.0f;
                if (s.shape)
                    for (int k = 0; k < order; ++k) fb += h[k] * err[ch][(ePos + k) % MaxOrder];   // err[ePos] = newest
                const float w = *x - fb;
                const float d = (rnd() - rnd()) * lsb; // TPDF, +/-1 LSB
                const float y = std::round ((w + d) * scale) * lsb;
                // The fed-back error excludes clipping and is bounded, so a full-scale over can never
                // make the shaping filter run away.
                const float e = s.shape ? juce::jlimit (-4.0f * lsb, 4.0f * lsb, y - w) : 0.0f;
                err[ch][(ePos + MaxOrder - 1) % MaxOrder] = e;
                *x = juce::jlimit (-1.0f, 1.0f - lsb, y);
            }
            ePos = (ePos + MaxOrder - 1) % MaxOrder;
        }
    }
private:
    inline float rnd() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return (float) state * (1.0f / 4294967296.0f); }
    uint32_t state = 0x9E3779B9u;
    float h[MaxOrder] {}, err[2][MaxOrder] {};
    int order = MaxOrder, ePos = 0;
};

//==============================================================================
// ITU-R BS.1770-4 / EBU R128 loudness meter (momentary, short-term, integrated, LRA, true peak).
class LoudnessMeter
{
public:
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f }, lra { 0.0f }, truePeak { -100.0f };
    std::atomic<bool> resetRequest { false };
    bool measureTruePeak = true;   // the input meter does not show true peak, so it can skip it

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        // Stage 1: high shelf
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        // Stage 2: high pass
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        subLen = std::max (1, (int) std::lround (0.1 * fs));
        reset();
    }

    void reset()
    {
        for (auto& z : zs) z = {};
        acc = 0.0; accN = 0; ringCount = 0; ringPos = 0;
        std::fill (std::begin (ring), std::end (ring), 0.0);
        std::fill (std::begin (iCount), std::end (iCount), 0u);
        std::fill (std::begin (iEnergy), std::end (iEnergy), 0.0);
        std::fill (std::begin (sCount), std::end (sCount), 0u);
        std::fill (std::begin (sEnergy), std::end (sEnergy), 0.0);
        tpd.reset(); tpMax = 0.0f;
        momentary = -100.0f; shortTerm = -100.0f; integrated = -100.0f; lra = 0.0f; truePeak = -100.0f;
    }

    void process (const float* l, const float* r, int n)
    {
        if (resetRequest.exchange (false)) reset();
        for (int i = 0; i < n; ++i)
        {
            const double kl = biq (s2, zs[2], biq (s1, zs[0], l[i]));
            const double kr = biq (s2, zs[3], biq (s1, zs[1], r[i]));
            acc += kl * kl + kr * kr;
            if (measureTruePeak) tpMax = std::max (tpMax, tpd.push (l[i], r[i], tpMax));
            if (++accN >= subLen) endSubBlock();
        }
        truePeak = gainToDb (tpMax);
    }

    // Returns the integrated loudness gated as BS.1770 (or -100 when nothing measured).
    static float lufs (double z) { return z > 0 ? (float) (-0.691 + 10.0 * std::log10 (z)) : -100.0f; }

private:
    struct Bq { double b0, b1, b2, a1, a2; };
    struct Z { double z1 = 0, z2 = 0; };
    static inline double biq (const Bq& c, Z& z, double x) noexcept
    {
        const double y = c.b0 * x + z.z1;
        z.z1 = c.b1 * x - c.a1 * y + z.z2;
        z.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    static constexpr int NumBins = 800; // -70 .. +10 LUFS, 0.1 LU
    static int binOf (float lu) { return juce::jlimit (0, NumBins - 1, (int) ((lu + 70.0f) * 10.0f)); }

    void endSubBlock()
    {
        ring[ringPos] = acc / accN;
        ringPos = (ringPos + 1) % 30;
        ringCount = std::min (ringCount + 1, 30);
        acc = 0.0; accN = 0;

        auto meanLast = [this] (int count)
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k) s += ring[(ringPos - k + 30) % 30];
            return s / count;
        };

        const double zM = meanLast (std::min (ringCount, 4));
        momentary = lufs (zM);
        const double zS = meanLast (ringCount);
        shortTerm = lufs (zS);

        if (ringCount >= 4)
        {
            const float lu = lufs (zM);
            if (lu > -70.0f) { const int b = binOf (lu); ++iCount[b]; iEnergy[b] += zM; }
            updateIntegrated();
        }
        if (ringCount >= 30)
        {
            const float lu = lufs (zS);
            if (lu > -70.0f) { const int b = binOf (lu); ++sCount[b]; sEnergy[b] += zS; }
            updateLra();
        }
    }

    void updateIntegrated()
    {
        double e = 0.0; uint64_t c = 0;
        for (int b = 0; b < NumBins; ++b) { e += iEnergy[b]; c += iCount[b]; }
        if (c == 0) return;
        const float gate = lufs (e / (double) c) - 10.0f;
        e = 0.0; c = 0;
        for (int b = binOf (gate); b < NumBins; ++b) { e += iEnergy[b]; c += iCount[b]; }
        if (c > 0) integrated = lufs (e / (double) c);
    }

    void updateLra()
    {
        double e = 0.0; uint64_t c = 0;
        for (int b = 0; b < NumBins; ++b) { e += sEnergy[b]; c += sCount[b]; }
        if (c == 0) return;
        const int g = binOf (lufs (e / (double) c) - 20.0f);
        uint64_t total = 0;
        for (int b = g; b < NumBins; ++b) total += sCount[b];
        if (total < 2) return;
        const uint64_t lo = (uint64_t) (0.10 * (double) total), hi = (uint64_t) (0.95 * (double) total);
        uint64_t run = 0; float l10 = 0, l95 = 0; bool got10 = false;
        for (int b = g; b < NumBins; ++b)
        {
            run += sCount[b];
            if (! got10 && run > lo) { l10 = -70.0f + b * 0.1f; got10 = true; }
            if (run > hi) { l95 = -70.0f + b * 0.1f; break; }
        }
        lra = std::max (0.0f, l95 - l10);
    }

    double fs = 44100;
    Bq s1 {}, s2 {};
    TruePeakDetectorT<96, 80> tpd; // same accuracy as the final true-peak stage, so the meter is honest
    Z zs[4];
    double acc = 0.0; int accN = 0, subLen = 4410;
    double ring[30] {}; int ringPos = 0, ringCount = 0;
    uint32_t iCount[NumBins] {}; double iEnergy[NumBins] {};
    uint32_t sCount[NumBins] {}; double sEnergy[NumBins] {};
    float tpMax = 0.0f;
};
} // namespace msdsp
