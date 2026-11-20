#pragma once
#include "DSP.h"

// The full mastering chain. One instance runs live in the processor; the exporter makes its own.
// Input -> EQ -> Multiband -> Glue -> Saturation -> Stereo -> Limiter -> Output -> Dither
struct ChainSettings
{
    float inGain = 0, outGain = 0;
    msdsp::EqSettings eq;
    msdsp::MbSettings mb;
    msdsp::BusSettings bus;
    msdsp::SatSettings sat;
    msdsp::ImgSettings img;
    msdsp::LimSettings lim;
    msdsp::DitherSettings dither;
};

// One display frame per 64 samples (about 1.3 ms at 48 kHz), for the module views.
struct VizFrame
{
    float inPeak = 0, satIn = 0, busLevel = -100, busGr = 0, limGr = 0, outPeak = 0;
};

// Lock-free single-producer / single-consumer queue of display frames (audio thread -> editor).
class VizFifo
{
public:
    static constexpr int Size = 16384;
    void push (const VizFrame& f) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) data[(size_t) s1] = f; else if (n2 > 0) data[(size_t) s2] = f;
        fifo.finishedWrite (n1 + n2);
    }
    int pull (VizFrame* dest, int max) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (std::min (max, fifo.getNumReady()), s1, n1, s2, n2);
        std::copy (data.begin() + s1, data.begin() + s1 + n1, dest);
        std::copy (data.begin() + s2, data.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
private:
    juce::AbstractFifo fifo { Size };
    std::vector<VizFrame> data = std::vector<VizFrame> (Size);
};

class MasterChain
{
public:
    static constexpr int MaxBlock = 512, VizChunk = 64;

    void setViz (VizFifo* f) { viz = f; }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        eq.prepare (fs); mb.prepare (fs); bus.prepare (fs);
        sat.prepare (fs, MaxBlock); img.prepare (fs); lim.prepare (fs); tpGuard.prepare (fs, 0.5);
        dither.prepare (fs);
        for (auto* r : { &eqR, &mbR, &busR, &imgR }) r->prepare (fs, 25.0);
        switchR.prepare (fs, 10.0); switchR.snap (true); holdLeft = 0;
        inG.prepare (fs, 50.0); outG.prepare (fs, 50.0);
        dry.setSize (2, MaxBlock);
        idle.setSize (2, MaxBlock);
        for (auto* d : { &mbDelay, &imgDelay }) d->prepare (mb.getLinearLatency() + 1);
        structure = {};
        mb.setLinear (false); img.setLinear (false); lim.setStyle (msdsp::LimClassic);
        inAbs.assign (MaxBlock, 0.0f); satAbs.assign (MaxBlock, 0.0f);
        acc = {}; accN = 0;
        first = true;
    }

    void reset()
    {
        eq.reset(); mb.reset(); bus.reset(); sat.reset(); img.reset(); lim.reset(); tpGuard.reset(); dither.reset();
        mbDelay.reset(); imgDelay.reset();
        first = true;
    }

    // Settings that change the latency: linear-phase crossovers and the limiter style.
    struct Structure
    {
        bool mbLinear = false, imgLinear = false; int limStyle = 0;
        bool operator== (const Structure& o) const { return mbLinear == o.mbLinear && imgLinear == o.imgLinear && limStyle == o.limStyle; }
        bool operator!= (const Structure& o) const { return ! (*this == o); }
    };
    static Structure structureOf (const ChainSettings& s) { return { s.mb.linear, s.img.linear, s.lim.style }; }

    // Applies the latency-changing settings at once (offline renders, and before playback starts).
    void configure (const ChainSettings& s)
    {
        if (structureOf (s) == structure) return;
        applyStructure (structureOf (s)); switchR.snap (true); holdLeft = 0;
    }

    int getLatency() const { return mb.getLatency() + sat.getLatency() + img.getLatency() + lim.getLatency() + tpGuard.getLatency(); }
    // The most it can be (both linear-phase modes on, Transparent limiter): sizes the processor's delay lines.
    int getMaxLatency() const { return 2 * mb.getLinearLatency() + sat.getLatency() + lim.getMaxLatency() + tpGuard.getLatency(); }

    void process (juce::AudioBuffer<float>& buf, const ChainSettings& s)
    {
        const int total = buf.getNumSamples();
        if (first)
        {
            eqR.snap (s.eq.on); mbR.snap (s.mb.on); busR.snap (s.bus.on); imgR.snap (s.img.on);
            if (structureOf (s) != structure) configure (s);
            first = false;
        }
        for (int start = 0; start < total; start += MaxBlock)
        {
            const int n = std::min (MaxBlock, total - start);
            // A latency-changing setting moved: fade the output out, switch, wait until audio from the
            // new path arrives, then fade back in (a short gap instead of a jump or click).
            const auto want = structureOf (s);
            if (want != structure) switchR.target = false;
            if (want != structure && switchR.silent()) { applyStructure (want); holdLeft = getLatency() + (int) (0.005 * fs); }
            if (want == structure && holdLeft <= 0) switchR.target = true;

            if (viz != nullptr) peaks (buf, start, n, inAbs);
            applyGain (buf, start, n, inG, msdsp::dbToGain (s.inGain));
            runSwitched (eq,  eqR,  buf, start, n, s.eq,  s.eq.on);
            runSwitchedDelayed (mb, mbR, mbDelay, buf, start, n, s.mb, s.mb.on);
            runSwitched (bus, busR, buf, start, n, s.bus, s.bus.on);
            if (viz != nullptr) peaks (buf, start, n, satAbs);
            sat.process (buf, start, n, s.sat);
            runSwitchedDelayed (img, imgR, imgDelay, buf, start, n, s.img, s.img.on);
            lim.process (buf, start, n, s.lim);
            // Second, gentle true-peak stage on the limiter's actual output: catches the small overs that
            // the main limiter's own gain changes create between samples (measured up to ~0.3 dB).
            tpGuard.process (buf, start, n, { s.lim.on && s.lim.truePeak, 0.0f, s.lim.ceiling, 20.0f, true });
            applyGain (buf, start, n, outG, msdsp::dbToGain (s.outGain));
            if (! switchR.full()) applySwitchFade (buf, start, n);
            holdLeft -= n;
            dither.process (buf, start, n, s.dither);
            if (viz != nullptr) collectViz (buf, start, n);
        }
    }

    const Structure& getStructure() const { return structure; }

    msdsp::MultibandComp mb;
    msdsp::BusComp bus;
    msdsp::Limiter lim;
    msdsp::TruePeakGuard tpGuard;

private:
    // Stereo delay line for the dry path of a module that has latency of its own (linear phase).
    struct DryDelay
    {
        std::vector<float> line[2];
        int pos = 0;
        void prepare (int maxDelay) { for (auto& l : line) l.assign ((size_t) maxDelay + 1, 0.0f); pos = 0; }
        void reset() { for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f); pos = 0; }
        // Writes the input and puts the input from `delay` samples ago into out.
        void process (const juce::AudioBuffer<float>& in, int start, int n, juce::AudioBuffer<float>& out, int delay) noexcept
        {
            const int size = (int) line[0].size();
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* x = in.getReadPointer (ch, start);
                auto* y = out.getWritePointer (ch);
                auto& l = line[ch];
                int p = pos;
                for (int i = 0; i < n; ++i)
                {
                    l[(size_t) p] = x[i];
                    int r = p - delay; if (r < 0) r += size;
                    y[i] = l[(size_t) r];
                    if (++p == size) p = 0;
                }
            }
            pos = (pos + n) % size;
        }
    };

    void applyStructure (const Structure& st)
    {
        mb.setLinear (st.mbLinear);
        img.setLinear (st.imgLinear);
        lim.setStyle (st.limStyle);
        mbDelay.reset(); imgDelay.reset();
        structure = st;
    }

    // Raised-cosine fade used while switching structure.
    void applySwitchFade (juce::AudioBuffer<float>& buf, int start, int n) noexcept
    {
        auto* L = buf.getWritePointer (0, start); auto* R = buf.getWritePointer (1, start);
        for (int i = 0; i < n; ++i)
        {
            const float v = switchR.next();
            const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * v);
            L[i] *= g; R[i] *= g;
        }
    }

    // Like runSwitched, for a module that may delay its output (linear-phase mode): the dry path is
    // delayed by the same amount, so switching the module on or off never shifts the timing.
    template <typename Module, typename Settings>
    void runSwitchedDelayed (Module& m, msdsp::Ramp& ramp, DryDelay& dl, juce::AudioBuffer<float>& buf, int start, int n, const Settings& s, bool on)
    {
        const int lat = m.getLatency();
        if (lat == 0) { runSwitched (m, ramp, buf, start, n, s, on); return; }
        dl.process (buf, start, n, dry, lat);
        ramp.target = on;
        if (ramp.silent())
        {
            for (int ch = 0; ch < 2; ++ch) idle.copyFrom (ch, 0, buf, ch, start, n);
            m.process (idle, 0, n, s);
            for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, start, dry, ch, 0, n);
            return;
        }
        m.process (buf, start, n, s);
        if (ramp.full()) return;
        auto* L = buf.getWritePointer (0, start); auto* R = buf.getWritePointer (1, start);
        auto* dL = dry.getReadPointer (0); auto* dR = dry.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = ramp.next();
            L[i] = dL[i] + g * (L[i] - dL[i]);
            R[i] = dR[i] + g * (R[i] - dR[i]);
        }
    }

    template <typename Module, typename Settings>
    void runSwitched (Module& m, msdsp::Ramp& ramp, juce::AudioBuffer<float>& buf, int start, int n, const Settings& s, bool on)
    {
        ramp.target = on;
        if (ramp.silent())
        {
            // Off: keep the module running on a copy, so its filters and detectors are settled
            // (no click) the moment it is switched back on.
            for (int ch = 0; ch < 2; ++ch) idle.copyFrom (ch, 0, buf, ch, start, n);
            m.process (idle, 0, n, s);
            return;
        }
        if (ramp.full()) { m.process (buf, start, n, s); return; }

        for (int ch = 0; ch < 2; ++ch) dry.copyFrom (ch, 0, buf, ch, start, n);
        m.process (buf, start, n, s);
        auto* L = buf.getWritePointer (0, start); auto* R = buf.getWritePointer (1, start);
        auto* dL = dry.getReadPointer (0); auto* dR = dry.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = ramp.next();
            L[i] = dL[i] + g * (L[i] - dL[i]);
            R[i] = dR[i] + g * (R[i] - dR[i]);
        }
    }

    static void peaks (const juce::AudioBuffer<float>& buf, int start, int n, std::vector<float>& out) noexcept
    {
        auto* L = buf.getReadPointer (0, start); auto* R = buf.getReadPointer (1, start);
        for (int i = 0; i < n; ++i) out[(size_t) i] = std::max (std::abs (L[i]), std::abs (R[i]));
    }

    // Condenses this sub-block into 64-sample display frames (no allocation; the queue is pre-sized).
    void collectViz (const juce::AudioBuffer<float>& buf, int start, int n) noexcept
    {
        auto* L = buf.getReadPointer (0, start); auto* R = buf.getReadPointer (1, start);
        for (int i = 0; i < n; ++i)
        {
            acc.inPeak = std::max (acc.inPeak, inAbs[(size_t) i]);
            acc.satIn = std::max (acc.satIn, satAbs[(size_t) i]);
            acc.busLevel = std::max (acc.busLevel, bus.levelTrace[(size_t) i]);
            acc.busGr = std::max (acc.busGr, bus.grTrace[(size_t) i]);
            accMinGain = std::min (accMinGain, lim.gainTrace[(size_t) i]);
            acc.outPeak = std::max (acc.outPeak, std::max (std::abs (L[i]), std::abs (R[i])));
            if (++accN == VizChunk)
            {
                acc.limGr = accMinGain < 0.99999f ? -msdsp::gainToDb (accMinGain) : 0.0f;
                viz->push (acc);
                acc = {}; accN = 0; accMinGain = 1.0f;
            }
        }
    }

    // Per-sample gain glide (~30 ms), so big gain moves never step the waveform.
    static void applyGain (juce::AudioBuffer<float>& buf, int start, int n, msdsp::Smooth& g, float target)
    {
        auto* L = buf.getWritePointer (0, start); auto* R = buf.getWritePointer (1, start);
        if (g.init && std::abs (g.value - target) < 1.0e-6f)
        {
            g.value = target;
            if (target != 1.0f) for (int i = 0; i < n; ++i) { L[i] *= target; R[i] *= target; }
            return;
        }
        for (int i = 0; i < n; ++i) { const float v = g.next (target); L[i] *= v; R[i] *= v; }
    }

    double fs = 44100;
    msdsp::MasteringEQ eq;
    msdsp::Saturator sat;
    msdsp::Imager img;
    msdsp::Dither dither;
    msdsp::Ramp eqR, mbR, busR, imgR, switchR;
    DryDelay mbDelay, imgDelay;
    Structure structure;
    int holdLeft = 0;
    juce::AudioBuffer<float> dry, idle;
    msdsp::Smooth inG, outG;
    bool first = true;

    VizFifo* viz = nullptr;
    std::vector<float> inAbs, satAbs;
    VizFrame acc;
    float accMinGain = 1.0f;
    int accN = 0;
};
