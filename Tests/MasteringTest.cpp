// Offline checks for the Open Mastering Studio DSP: loudness meter accuracy, limiter ceiling,
// latency alignment, null-transparency of a neutral chain, presets and NaN safety.
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "LabMeasurements.h"

//==============================================================================
// Allocation counter: replaces global new/delete for this test program. Allocations are counted
// only while countAllocations is set on the current thread (i.e. inside processBlock).
static thread_local bool countAllocations = false;
static std::atomic<long long> allocationCount { 0 };

static void* countedAlloc (std::size_t n)
{
    if (countAllocations) ++allocationCount;
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
// Over-aligned allocation: MSVC has no std::aligned_alloc, POSIX has posix_memalign.
static void* platformAlignedAlloc (std::size_t n, std::size_t align)
{
   #if JUCE_WINDOWS
    return _aligned_malloc (n, align);
   #else
    void* p = nullptr;
    return posix_memalign (&p, align < sizeof (void*) ? sizeof (void*) : align, n) == 0 ? p : nullptr;
   #endif
}
static void platformAlignedFree (void* p) noexcept
{
   #if JUCE_WINDOWS
    _aligned_free (p);
   #else
    std::free (p);
   #endif
}
static void* countedAllocAligned (std::size_t n, std::align_val_t a)
{
    if (countAllocations) ++allocationCount;
    if (void* p = platformAlignedAlloc (n == 0 ? 1 : n, (std::size_t) a)) return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n) { return countedAlloc (n); }
void* operator new[] (std::size_t n) { return countedAlloc (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { try { return countedAlloc (n); } catch (...) { return nullptr; } }
void* operator new (std::size_t n, std::align_val_t a) { return countedAllocAligned (n, a); }
void* operator new[] (std::size_t n, std::align_val_t a) { return countedAllocAligned (n, a); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { platformAlignedFree (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { platformAlignedFree (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { platformAlignedFree (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { platformAlignedFree (p); }

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static juce::AudioBuffer<float> makeMusicLike (double fs, double seconds, float levelDb, uint32_t seed = 1)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rnd ((juce::int64) seed);
    double ph[4] {};
    const double fr[4] { 55.0, 220.0, 1250.0, 5200.0 };
    float lp[2] {};
    const float amp = msdsp::dbToGain (levelDb);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const float env = (float) (0.55 + 0.45 * std::sin (2.0 * juce::MathConstants<double>::pi * 2.0 * t)); // pumping
        float tone = 0.0f;
        for (int k = 0; k < 4; ++k) { ph[k] += fr[k] / fs; tone += (float) std::sin (2.0 * juce::MathConstants<double>::pi * ph[k]) / (float) (k + 1); }
        for (int ch = 0; ch < 2; ++ch)
        {
            lp[ch] += 0.2f * ((rnd.nextFloat() * 2.0f - 1.0f) - lp[ch]);
            const float transient = (i % (int) (fs / 2) < 200) ? (rnd.nextFloat() * 2.0f - 1.0f) * 1.5f : 0.0f; // drum-like hits
            b.setSample (ch, i, amp * (env * (0.5f * tone + 0.4f * lp[ch]) + transient));
        }
    }
    return b;
}

static void processInBlocks (MasterChain& c, juce::AudioBuffer<float>& b, const ChainSettings& s, int block)
{
    juce::AudioBuffer<float> tmp (2, block);
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        const int n = std::min (block, b.getNumSamples() - pos);
        tmp.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) tmp.copyFrom (ch, 0, b, ch, pos, n);
        c.process (tmp, s);
        for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, pos, tmp, ch, 0, n);
    }
}

static float maxTruePeakDb (const juce::AudioBuffer<float>& b, int from = 0)
{
    msdsp::TruePeakDetector tp;
    float m = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = tp.push (b.getSample (0, i), b.getSample (1, i));
        if (i >= from + msdsp::TruePeakDetector::D) m = std::max (m, v);
    }
    return msdsp::gainToDb (m);
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}

// ---- Assistant test material: noise shaped to a 1/3-octave spectrum, with musical dynamics ----
static juce::AudioBuffer<float> makeShapedMixRaw (double, double, const assistant::Spectrum&, const assistant::Spectrum&, uint32_t);

// Generates, measures and pre-compensates (twice) so the test mix really has the requested balance
// despite the overlap of the band filters.
static juce::AudioBuffer<float> makeShapedMix (double fs, double seconds, const assistant::Spectrum& shape,
                                               const assistant::Spectrum& sideDb, uint32_t seed)
{
    auto drive = shape;
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto probe = makeShapedMixRaw (fs, std::min (seconds, 8.0), drive, sideDb, seed);
        const auto got = assistant::averageSpectrum (probe, fs);
        for (int b = 1; b < assistant::NumBands - 1; ++b) drive[(size_t) b] += shape[(size_t) b] - got[(size_t) b];
    }
    return makeShapedMixRaw (fs, seconds, drive, sideDb, seed);
}

static juce::AudioBuffer<float> makeShapedMixRaw (double fs, double seconds, const assistant::Spectrum& shape,
                                                  const assistant::Spectrum& sideDb, uint32_t seed)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    juce::Random rnd ((juce::int64) seed);
    std::vector<float> m ((size_t) n), s ((size_t) n);
    for (int b = 1; b < assistant::NumBands - 1; ++b)
    {
        const double fc = assistant::bandCentres()[(size_t) b];
        if (fc > fs * 0.45) break;
        msdsp::Svf hp, lp;
        hp.c = msdsp::makeSvf (msdsp::FilterType::HighPass, fs, fc * std::pow (2.0, -1.0 / 6.0), 0.7071, 0);
        lp.c = msdsp::makeSvf (msdsp::FilterType::LowPass, fs, fc * std::pow (2.0, 1.0 / 6.0), 0.7071, 0);
        const float g = std::sqrt (std::pow (10.0f, shape[(size_t) b] / 10.0f) * 1000.0f / (float) fc);
        const float gs = g * std::pow (10.0f, sideDb[(size_t) b] / 20.0f);
        for (int i = 0; i < n; ++i)
        {
            m[(size_t) i] += g * lp.process (0, hp.process (0, rnd.nextFloat() * 2.0f - 1.0f));
            s[(size_t) i] += gs * lp.process (1, hp.process (1, rnd.nextFloat() * 2.0f - 1.0f));
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double k = std::fmod (t, 0.5);
        const float env = (float) ((0.35 + 0.65 * (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.25 * t)))
                                   * (1.0 + 2.5 * std::exp (-k / 0.03)));
        out.setSample (0, i, env * (m[(size_t) i] + s[(size_t) i]));
        out.setSample (1, i, env * (m[(size_t) i] - s[(size_t) i]));
    }
    // normalise to about -20 LUFS
    msdsp::LoudnessMeter lm; lm.prepare (fs);
    lm.process (out.getReadPointer (0), out.getReadPointer (1), n);
    out.applyGain (msdsp::dbToGain (-20.0f - lm.integrated.load()));
    return out;
}

static assistant::Spectrum withBoost (assistant::Spectrum s, float fLo, float fHi, float db)
{
    for (int b = 0; b < assistant::NumBands; ++b)
        if (assistant::bandCentres()[(size_t) b] >= fLo && assistant::bandCentres()[(size_t) b] <= fHi) s[(size_t) b] += db;
    return s;
}

static float eqDbAt (const ChainSettings& s, double fs, float f)
{
    using FT = msdsp::FilterType;
    double m = msdsp::svfMagnitude (FT::LowShelf, fs, s.eq.lsFreq, 0.7071, s.eq.lsGain, f)
             * msdsp::svfMagnitude (FT::HighShelf, fs, s.eq.hsFreq, 0.7071, s.eq.hsGain, f);
    for (int k = 0; k < 3; ++k) m *= msdsp::svfMagnitude (FT::Bell, fs, s.eq.pf[k], s.eq.pq[k], s.eq.pg[k], f);
    return msdsp::gainToDb ((float) m);
}

static void assistantTests()
{
    using namespace assistant;
    std::cout << "\n== Master Assistant ==" << std::endl;
    const double fs = 48000.0;
    const Spectrum narrow = [] { Spectrum s {}; s.fill (-18.0f); return s; }();
    const Spectrum vocal = targetCurve (Curve::Vocal);
    ChainSettings base;

    // 0) Generic curves reproduce the published measurements they are built from
    {
        auto bandOf = [] (float f) { int best = 0; for (int b = 0; b < NumBands; ++b) if (std::abs (std::log (bandCentres()[(size_t) b] / f)) < std::abs (std::log (bandCentres()[(size_t) best] / f))) best = b; return best; };
        auto slopeAt = [&] (const Spectrum& s, float f) { const int b = bandOf (f); return (s[(size_t) b + 1] - s[(size_t) b - 1]) / (2.0f / 3.0f); };
        auto regSlope = [&] (const Spectrum& s)
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
            for (int b = bandOf (100); b <= bandOf (10000); ++b)
            {
                const double x = std::log2 (bandCentres()[(size_t) b] / 1000.0), y = s[(size_t) b];
                sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
            }
            return (float) ((n * sxy - sx * sy) / (n * sxx - sx * sx));
        };
        const auto pop = targetCurve (Curve::Pop);
        const float table[6][2] { { 200, -2.350f }, { 400, -3.668f }, { 800, -4.985f }, { 1600, -6.303f }, { 3200, -7.621f }, { 6400, -8.938f } };
        float worst = 0;
        for (auto& t : table) worst = std::max (worst, std::abs (slopeAt (pop, t[0]) - (t[1] + 3.01f)));
        check (worst < 0.35f, "Pop curve matches Elowsson & Friberg Table 1 slopes (+3 dB/oct band conversion), worst error " + juce::String (worst, 2) + " dB/oct");

        struct G { Curve c; float pestana; };
        const G genres[] { { Curve::Rock, -0.9793f }, { Curve::Electronic, -0.7461f }, { Curve::HipHop, -0.8141f }, { Curve::Jazz, -1.2929f },
                           { Curve::Acoustic, -1.1824f }, { Curve::RnB, -1.0320f } };
        float worstG = 0;
        for (auto& g : genres)
        {
            const float want = 0.6f * (g.pestana + 0.9433f) * 6.02f, got = regSlope (targetCurve (g.c)) - regSlope (pop);
            worstG = std::max (worstG, std::abs (want - got));
        }
        check (worstG < 0.25f, "genre tilts track 60% of Pestana et al. Table 2 differences, worst error " + juce::String (worstG, 2) + " dB/oct");

        auto at = [&] (Curve c, float f) { return targetCurve (c)[(size_t) bandOf (f)]; };
        check (at (Curve::HipHop, 40) > at (Curve::Pop, 40) + 2 && at (Curve::Pop, 40) > at (Curve::Jazz, 40) + 2
               && at (Curve::Electronic, 10000) > at (Curve::Pop, 10000) + 1 && at (Curve::Pop, 10000) > at (Curve::Jazz, 10000) + 1,
               "ordering: hip-hop > pop > jazz at 40 Hz, electronic > pop > jazz at 10 kHz");

        std::cout << "\n  Target curves (dB, 0 = 200 Hz-4 kHz average):\n  " << juce::String ("").paddedRight (' ', 24);
        const float fs_[] { 31.5f, 50, 100, 250, 1000, 4000, 8000, 12500 };
        for (float f : fs_) std::cout << juce::String (f >= 1000 ? juce::String (f / 1000, 1) + "k" : juce::String (f, f < 40 ? 1 : 0)).paddedLeft (' ', 7);
        std::cout << "\n";
        for (auto c : curveMenuOrder())
        {
            std::cout << "  " << juce::String (curveInfo (c).name).paddedRight (' ', 24);
            for (float f : fs_) std::cout << juce::String (at (c, f), 1).paddedLeft (' ', 7);
            std::cout << "\n";
        }
        std::cout << std::endl;
    }

    auto runOn =[&] (const juce::AudioBuffer<float>& mix, Request req)
    {
        const auto prof = analyse (mix, fs);
        return std::make_pair (prof, run (mix, fs, prof, req, base));
    };
    auto verify = [&] (const juce::AudioBuffer<float>& mix, const ChainSettings& s, float& I, float& tp)
    {
        MasterChain c; c.prepare (fs);
        auto b = mix; processInBlocks (c, b, s, 512);
        msdsp::LoudnessMeter m; m.prepare (fs);
        m.process (b.getReadPointer (0, c.getLatency()), b.getReadPointer (1, c.getLatency()), b.getNumSamples() - c.getLatency());
        I = m.integrated.load(); tp = m.truePeak.load();
    };

    // 1) A mix that already matches the vocal curve: little or no EQ
    {
        auto mix = makeShapedMix (fs, 20.0, vocal, narrow, 1);
        Request rq; rq.curve = Curve::Vocal; rq.style = Style::Balanced;
        auto [prof, r] = runOn (mix, rq);
        float maxG = 0; for (float g : { r.settings.eq.lsGain, r.settings.eq.pg[0], r.settings.eq.pg[1], r.settings.eq.pg[2], r.settings.eq.hsGain }) maxG = std::max (maxG, std::abs (g));
        check (r.error.isEmpty() && prof.isValid(), "assistant runs on a balanced mix (" + juce::String (prof.integrated, 1) + " LUFS in)");
        check (r.distanceBefore < 1.5f && maxG <= 1.5f, "balanced mix: distance " + juce::String (r.distanceBefore, 2) + " dB, largest EQ move " + juce::String (maxG, 1) + " dB");
        float I, tp; verify (mix, r.settings, I, tp);
        check (std::abs (I + 14.0f) < 0.7f && tp <= -0.9f, "Balanced style lands at " + juce::String (I, 1) + " LUFS (target -14), " + juce::String (tp, 2) + " dBTP");
        check (! r.settings.img.on, "narrow bass: stereo imager left off");
    }
    // 2) Dull mix: top end 6 dB down
    {
        auto mix = makeShapedMix (fs, 20.0, withBoost (vocal, 3000, 20000, -6.0f), narrow, 2);
        Request rq; rq.curve = Curve::Vocal;
        auto [prof, r] = runOn (mix, rq);
        const float lift = eqDbAt (r.settings, fs, 8000.0f);
        check (lift > 2.0f && r.distanceAfter < r.distanceBefore - 1.0f,
               "dull mix: EQ lifts 8 kHz by " + juce::String (lift, 1) + " dB, distance " + juce::String (r.distanceBefore, 1) + " -> " + juce::String (r.distanceAfter, 1));
    }
    // 3) Muddy mix: 250-500 Hz 6 dB up
    {
        auto mix = makeShapedMix (fs, 20.0, withBoost (vocal, 200, 500, 6.0f), narrow, 3);
        Request rq; rq.curve = Curve::Vocal;
        auto [prof, r] = runOn (mix, rq);
        const float cut = eqDbAt (r.settings, fs, 315.0f);
        check (cut < -2.0f && r.distanceAfter < r.distanceBefore - 1.0f,
               "muddy mix: EQ cuts 315 Hz by " + juce::String (cut, 1) + " dB, distance " + juce::String (r.distanceBefore, 1) + " -> " + juce::String (r.distanceAfter, 1));
    }
    // 4) Wide bass
    {
        Spectrum side = narrow;
        for (int b = 0; b <= 8; ++b) side[(size_t) b] = -2.0f;
        auto mix = makeShapedMix (fs, 20.0, vocal, side, 4);
        Request rq; rq.curve = Curve::Vocal;
        auto [prof, r] = runOn (mix, rq);
        check (r.settings.img.on && r.settings.img.wLow <= 60.0f, "wide bass (side " + juce::String (prof.sideRatio[0], 1) + " dB): low width set to "
               + juce::String (r.settings.img.wLow, 0) + "%");
    }
    // 5) Acoustic vs vocal target on the same mix: acoustic asks for less low end
    {
        auto mix = makeShapedMix (fs, 20.0, vocal, narrow, 5);
        Request ra; ra.curve = Curve::Acoustic;
        Request rv; rv.curve = Curve::Vocal;
        auto a = runOn (mix, ra).second;
        auto v = runOn (mix, rv).second;
        float want = 0, got = 0;
        const auto ta = targetCurve (Curve::Acoustic), tv = targetCurve (Curve::Vocal);
        for (int b = 3; b <= 6; ++b) // 40-80 Hz
        {
            want += (ta[(size_t) b] - tv[(size_t) b]) / 4.0f;
            got += (eqDbAt (a.settings, fs, bandCentres()[(size_t) b]) - eqDbAt (v.settings, fs, bandCentres()[(size_t) b])) / 4.0f;
        }
        // The fit deliberately shrinks small corrections (it prefers fewer, smaller moves), so expect a clear share, not all of it.
        check (got < 0.4f * want, "acoustic vs singer-songwriter at 40-80 Hz: curves differ by " + juce::String (want, 1) + " dB, EQ delivers "
               + juce::String (got, 1) + " dB");
    }
    // 6) Styles: Dynamic keeps limiting light, Loud is louder
    {
        auto mix = makeShapedMix (fs, 20.0, vocal, narrow, 6);
        Request rd; rd.style = Style::Dynamic;
        Request rl; rl.style = Style::Loud;
        auto d = runOn (mix, rd).second;
        auto l = runOn (mix, rl).second;
        check (d.avgLimiterGr <= 1.1f && l.integrated > d.integrated + 3.0f,
               "Dynamic " + juce::String (d.integrated, 1) + " LUFS (limiter " + juce::String (d.avgLimiterGr, 1) + " dB), Loud "
               + juce::String (l.integrated, 1) + " LUFS (limiter " + juce::String (l.avgLimiterGr, 1) + " dB)");
    }
    // 7) Reference matching: bright, loud reference
    {
        auto refAudio = makeShapedMix (fs, 20.0, withBoost (vocal, 4000, 20000, 4.0f), narrow, 7);
        refAudio.applyGain (msdsp::dbToGain (8.0f));
        { MasterChain lim; lim.prepare (fs); ChainSettings ls; ls.lim = { true, 4.0f, -1.0f, 80.0f, true }; processInBlocks (lim, refAudio, ls, 512); }
        auto refProf = analyse (refAudio, fs);
        auto mix = makeShapedMix (fs, 20.0, vocal, narrow, 8);
        Request rq; rq.curve = Curve::Reference; rq.references = { refProf }; rq.style = Style::Loud;
        auto [prof, r] = runOn (mix, rq);
        const float lift = eqDbAt (r.settings, fs, 8000.0f);
        check (r.error.isEmpty() && lift > 1.5f && r.distanceAfter < r.distanceBefore - 1.0f,
               "reference match: lifts 8 kHz by " + juce::String (lift, 1) + " dB, distance " + juce::String (r.distanceBefore, 1) + " -> " + juce::String (r.distanceAfter, 1));
        check (std::abs (r.integrated - r.targetLufs) < 0.7f, "reference loudness " + juce::String (refProf.integrated, 1) + " LUFS -> aim "
               + juce::String (r.targetLufs, 1) + ", got " + juce::String (r.integrated, 1));
        Request none; none.curve = Curve::Reference;
        check (run (mix, fs, prof, none, base).error.isNotEmpty(), "reference mode without references reports an error");

        const auto back = Profile::fromTree (refProf.toTree());
        bool same = back.isValid() && std::abs (back.integrated - refProf.integrated) < 0.01f;
        for (int b = 0; b < NumBands; ++b) same &= std::abs (back.spectrum[(size_t) b] - refProf.spectrum[(size_t) b]) < 0.01f;
        check (same, "reference profile save/load round trip");
    }
    // 7b) Long track (excerpt loop + full-length verification) and run time
    {
        auto mix = makeShapedMix (fs, 180.0, withBoost (vocal, 3000, 20000, -4.0f), narrow, 10);
        Request rq; rq.curve = Curve::Vocal; rq.style = Style::Balanced;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto prof = analyse (mix, fs);
        auto r = run (mix, fs, prof, rq, base);
        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        float I, tp; verify (mix, r.settings, I, tp);
        check (r.error.isEmpty() && std::abs (I + 14.0f) < 0.7f && tp <= -0.9f,
               "3-minute track: full-length result " + juce::String (I, 1) + " LUFS, " + juce::String (tp, 2) + " dBTP; analysis + run took "
               + juce::String (secs, 1) + " s");
    }
    // 7c) Every target through the processor, exactly as the Analyze & Apply button runs it
    {
        auto mix = makeShapedMix (fs, 12.0, vocal, narrow, 23);
        juce::StringArray bad;
        int ran = 0;
        for (auto c : curveMenuOrder())
        {
            MasteringAudioProcessor p;
            p.setPlayConfigDetails (2, 2, fs, 512);
            p.prepareToPlay (fs, 512);
            p.assistantOptions.curve = (int) c;
            p.startLearn();
            juce::MidiBuffer midi; juce::AudioBuffer<float> blk (2, 512);
            for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
            {
                for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, mix, ch, pos, 512);
                p.processBlock (blk, midi);
            }
            p.stopLearn();
            juce::String err;
            const bool started = p.startAssistant (err);
            std::unique_ptr<assistant::Result> res;
            for (int t = 0; t < 600 && res == nullptr && started; ++t) { juce::Thread::sleep (50); res = p.takeAssistantResult(); }
            if (! started || res == nullptr || res->error.isNotEmpty()) { bad.add (juce::String (curveInfo (c).name) + ": " + (err.isNotEmpty() ? err : res ? res->error : juce::String ("no result"))); continue; }
            p.applyAssistantResult (*res);
            const juce::String expectName = juce::String (curveInfo (c).name).upToFirstOccurrenceOf (" /", false, false);
            if (! p.currentPresetName.contains (expectName)) bad.add (juce::String (curveInfo (c).name) + " applied as \"" + p.currentPresetName + "\"");
            ++ran;
            p.releaseResources();
        }
        check (bad.isEmpty() && ran == (int) curveMenuOrder().size(), "all " + juce::String (ran) + " generic targets run from the processor without a reference track"
               + (bad.isEmpty() ? juce::String() : " - " + bad.joinIntoString ("; ")));
    }
    // 8) Processor: Learn -> analyse -> apply -> revert
    {
        MasteringAudioProcessor p;
        p.setPlayConfigDetails (2, 2, fs, 512);
        p.prepareToPlay (fs, 512);
        auto mix = makeShapedMix (fs, 15.0, withBoost (vocal, 3000, 20000, -6.0f), narrow, 9);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> blk (2, 512);
        p.startLearn();
        for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, mix, ch, pos, 512);
            p.processBlock (blk, midi);
        }
        p.stopLearn();
        const float hsBefore = p.makeSettings().eq.hsGain;
        juce::String err;
        const bool started = p.startAssistant (err);
        std::unique_ptr<assistant::Result> res;
        for (int t = 0; t < 600 && res == nullptr; ++t) { juce::Thread::sleep (100); res = p.takeAssistantResult(); }
        check (started && res != nullptr && res->error.isEmpty(), "Learn captured " + juce::String (p.getLearnedSeconds(), 1) + " s and the background job finished"
               + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
        if (res != nullptr && res->error.isEmpty())
        {
            p.applyAssistantResult (*res);
            const auto applied = p.makeSettings();
            check (std::abs (applied.lim.gain - res->settings.lim.gain) < 0.05f && std::abs (applied.inGain - res->settings.inGain) < 0.05f
                   && applied.bus.on, "result applied to the parameters (limiter +" + juce::String (applied.lim.gain, 1) + " dB)");
            p.revertAssistant();
            check (std::abs (p.makeSettings().eq.hsGain - hsBefore) < 1e-4f && std::abs (p.makeSettings().lim.gain) < 1e-4f, "Revert restores the previous settings");
        }
        p.releaseResources();
    }
}

// ---- Click scan: jump each parameter while a smooth low-frequency signal plays through the whole chain.
// A click shows up as a burst above 6 kHz that is far larger than that band before or after the change.
struct ClickResult { juce::String what; float burstDb, steadyDb; };

static std::vector<ClickResult> clickScan (bool printAll)
{
    const double fs = 48000.0;
    constexpr int block = 256;
    const std::vector<std::pair<juce::String, float>> baseline {
        { ID::hpOn, 1 }, { ID::lpOn, 1 }, { ID::lpFreq, 16000 }, { ID::lsGain, 2 }, { ID::p2Gain, -2 },
        { ID::mbOn, 1 }, { ID::busOn, 1 }, { ID::busThr, -20 }, { ID::satOn, 1 }, { ID::satMix, 60 },
        { ID::imgOn, 1 }, { ID::imgLow, 50 }, { ID::limOn, 1 }, { ID::limGain, 6 } };

    auto render = [&] (const juce::String& id, float from, float to, float& burst, float& steady, bool limiterOff = false)
    {
        MasteringAudioProcessor p;
        for (auto& [bid, v] : baseline) if (auto* q = p.apvts.getParameter (bid)) q->setValueNotifyingHost (q->convertTo0to1 (v));
        if (limiterOff) p.apvts.getParameter (ID::limOn)->setValueNotifyingHost (0.0f);
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (from);
        p.setPlayConfigDetails (2, 2, fs, block);
        p.prepareToPlay (fs, block);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, block);
        msdsp::Svf hp[4];
        const auto& qs = msdsp::butterworthQs (8);
        for (int k = 0; k < 4; ++k) hp[k].c = msdsp::makeSvf (msdsp::FilterType::HighPass, fs, 6000.0, qs[(size_t) k], 0);
        const int total = (int) (1.1 * fs), change = ((int) (0.6 * fs) / block) * block; // on a block boundary
        float pre = 0, trans = 0, post = 0;
        double ph1 = 0, ph2 = 0;
        for (int pos = 0; pos < total; pos += block)
        {
            if (pos == change) prm->setValueNotifyingHost (to);
            for (int i = 0; i < block; ++i)
            {
                ph1 += 80.0 / fs; ph2 += 220.0 / fs;
                const float a = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * ph1);
                buf.setSample (0, i, a + 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * ph2));
                buf.setSample (1, i, a + 0.3f * (float) std::cos (2 * juce::MathConstants<double>::pi * ph2));
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < block; ++i)
            {
                const int n = pos + i;
                float h = 0;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float y = buf.getSample (ch, i);
                    for (int k = 0; k < 4; ++k) y = hp[k].process (ch, y);
                    h = std::max (h, std::abs (y));
                }
                if (n >= (int) (0.4 * fs) && n < change) pre = std::max (pre, h);
                else if (n >= change && n < change + (int) (0.03 * fs)) trans = std::max (trans, h);
                else if (n >= (int) (0.9 * fs)) post = std::max (post, h);
            }
        }
        burst = msdsp::gainToDb (trans);
        steady = msdsp::gainToDb (std::max (pre, post));
    };

    std::vector<ClickResult> clicks;
    MasteringAudioProcessor probe;
    for (auto* ap : probe.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap);
        if (rp == nullptr || rp->getParameterID() == ID::target) continue;
        const auto id = rp->getParameterID();
        std::vector<std::pair<float, float>> moves;
        if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr) moves = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
        else if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp))
        {
            const int n = c->choices.size();
            for (int a = 0; a < n; ++a)
                for (int b = 0; b < n; ++b)
                    if (a != b) moves.push_back ({ rp->convertTo0to1 ((float) a), rp->convertTo0to1 ((float) b) });
        }
        else moves = { { 0.2f, 0.8f }, { 0.8f, 0.2f } };

        for (auto [from, to] : moves)
        {
            float burst, steady;
            render (id, from, to, burst, steady);
            const juce::String what = rp->getName (40) + "  " + rp->getText (from, 0) + " -> " + rp->getText (to, 0);
            auto isClick = [] (float b, float st) { return b > -72.0f && b > st + 12.0f; };
            bool click = isClick (burst, steady);
            juce::String note;
            if (click && id != ID::limOn)
            {
                // Is it the control, or the limiter reacting to a sudden big level change?
                float b2, s2;
                render (id, from, to, b2, s2, true);
                if (! isClick (b2, s2)) { click = false; note = "  (limiter reacting to the level change; clean with limiter off: " + juce::String (b2, 1) + " dB)"; }
            }
            if (click) clicks.push_back ({ what, burst, steady });
            if (printAll || click || note.isNotEmpty())
                std::cout << (click ? "  CLICK " : "        ") << what.paddedRight (' ', 60) << " burst " << juce::String (burst, 1)
                          << " dB, steady " << juce::String (steady, 1) << " dB" << note << std::endl;
        }
    }
    return clicks;
}

// ---- Recall check: the same settings, reached different ways, must produce identical audio.
static juce::AudioBuffer<float> renderWith (MasteringAudioProcessor& p, const juce::AudioBuffer<float>& in, bool prepare = true)
{
    if (prepare) { p.setPlayConfigDetails (2, 2, 48000.0, 512); p.prepareToPlay (48000.0, 512); }
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> out (2, in.getNumSamples()), blk (2, 512);
    for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, in, ch, pos, 512);
        p.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, 512);
    }
    return out;
}

static float diffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
{
    double e = 0, r = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < a.getNumSamples(); ++i)
        {
            const double d = a.getSample (ch, i) - b.getSample (ch, i);
            e += d * d; r += (double) a.getSample (ch, i) * a.getSample (ch, i);
        }
    return e <= 0 ? -300.0f : (float) (10.0 * std::log10 (e / (r + 1e-30)));
}

static void recallTests()
{
    std::cout << "\n== Recalling settings ==" << std::endl;
    auto mix = makeMusicLike (48000.0, 4.0, -14.0f, 31);
    auto setUp = [] (MasteringAudioProcessor& p)
    {
        p.setCurrentProgram (2); // Loud & Punchy: every module type in use
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                 { ID::eqMode, 1 }, { ID::hpSlope, 2 }, { ID::p2Q, 2.2f }, { ID::satType, 3 }, { ID::imgOn, 1 }, { ID::imgHigh, 140 },
                 { ID::mbLinear, 1 }, { ID::mbAutoRel, 1 }, { ID::imgLinear, 1 }, { ID::imgMono, 1 }, { ID::imgMonoFreq, 90 }, { ID::imgSafe, 1 },
                 { ID::limStyle, 1 }, { ID::ditherShape, 1 }, { ID::ditherBits, 2 }, { ID::monitor, 0 } })
            if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
    };

    MasteringAudioProcessor a;
    setUp (a);
    const auto ref = renderWith (a, mix);
    juce::MemoryBlock state;
    a.getStateInformation (state);

    // 1) Project reopened: state restored before playback starts
    {
        MasteringAudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        const auto out = renderWith (b, mix);
        check (diffDb (ref, out) < -90.0f, "state restored before playback: difference " + juce::String (diffDb (ref, out), 1) + " dB");
    }
    // 2) Host prepares first, then restores state (common DAW order)
    {
        MasteringAudioProcessor b;
        b.setPlayConfigDetails (2, 2, 48000.0, 512); b.prepareToPlay (48000.0, 512);
        b.setStateInformation (state.getData(), (int) state.getSize());
        const auto out = renderWith (b, mix, false);
        check (diffDb (ref, out) < -90.0f, "state restored after prepare: difference " + juce::String (diffDb (ref, out), 1) + " dB");
    }
    // 3) User preset saved and loaded into a fresh instance
    {
        const juce::String name = "RecallTest " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        a.saveUserPreset (name);
        MasteringAudioProcessor b;
        b.loadUserPreset (b.getUserPresetFolder().getChildFile (name + ".mspreset"));
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { ID::ditherBits, 2 } }) // presets leave delivery settings alone
            if (auto* q = b.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
        const auto out = renderWith (b, mix);
        b.getUserPresetFolder().getChildFile (name + ".mspreset").deleteFile();
        check (diffDb (ref, out) < -90.0f, "user preset save/load: difference " + juce::String (diffDb (ref, out), 1) + " dB");
    }
    // 4) A/B: switch away and back
    {
        MasteringAudioProcessor b;
        setUp (b);
        b.selectAB (1); b.setCurrentProgram (0); b.selectAB (0);
        const auto out = renderWith (b, mix);
        check (diffDb (ref, out) < -90.0f, "A/B away and back: difference " + juce::String (diffDb (ref, out), 1) + " dB");
    }
    // 6) A session saved while listening in Compare + Mono reopens as the normal master
    {
        MasteringAudioProcessor c;
        setUp (c);
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { ID::bypass, 1 }, { ID::monitor, 1 } })
            if (auto* q = c.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
        juce::MemoryBlock st; c.getStateInformation (st);
        MasteringAudioProcessor b;
        b.setStateInformation (st.getData(), (int) st.getSize());
        const auto out = renderWith (b, mix);
        check (diffDb (ref, out) < -90.0f && b.apvts.getRawParameterValue (ID::monitor)->load() < 0.5f,
               "session saved in Compare + Mono reopens as the normal stereo master (difference " + juce::String (diffDb (ref, out), 1) + " dB)");
    }
    // 7) User presets recall output trim; A/B keeps the listening controls where they are
    {
        MasteringAudioProcessor c;
        setUp (c);
        c.apvts.getParameter (ID::outGain)->setValueNotifyingHost (c.apvts.getParameter (ID::outGain)->convertTo0to1 (-3.0f));
        const juce::String name = "TrimTest " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        c.saveUserPreset (name);
        c.apvts.getParameter (ID::outGain)->setValueNotifyingHost (c.apvts.getParameter (ID::outGain)->convertTo0to1 (0.0f));
        c.loadUserPreset (c.getUserPresetFolder().getChildFile (name + ".mspreset"));
        c.getUserPresetFolder().getChildFile (name + ".mspreset").deleteFile();
        check (std::abs (c.makeSettings().outGain + 3.0f) < 0.05f, "user preset recalls its output trim");

        c.selectAB (1);
        c.apvts.getParameter (ID::monitor)->setValueNotifyingHost (c.apvts.getParameter (ID::monitor)->convertTo0to1 (2));
        c.selectAB (0);
        check (juce::roundToInt (c.apvts.getRawParameterValue (ID::monitor)->load()) == 2, "switching A/B leaves the monitor mode alone");
    }
    // 8) Compare's level match survives a reopen
    {
        MasteringAudioProcessor c;
        c.meters.matchDb = 4.5f;
        juce::MemoryBlock st; c.getStateInformation (st);
        MasteringAudioProcessor b;
        b.setStateInformation (st.getData(), (int) st.getSize());
        check (std::abs (b.meters.matchDb.load() - 4.5f) < 1e-4f, "Compare level match restored with the session");
    }
    // 5) Every parameter value survives a save/restore exactly
    {
        MasteringAudioProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        juce::String bad;
        for (auto* ap : a.getParameters())
            if (auto* ra = dynamic_cast<juce::RangedAudioParameter*> (ap))
                if (auto* rb = b.apvts.getParameter (ra->getParameterID()))
                    if (std::abs (ra->getValue() - rb->getValue()) > 1e-6f) bad << ra->getParameterID() << " ";
        check (bad.isEmpty(), "all parameters restored exactly" + (bad.isEmpty() ? juce::String() : ": differ: " + bad));
    }
}

// ---- 1.7 upgrades: linear phase, mono bass, mono-safe, modeled saturation, Transparent limiter, dither shaping ----

// Hann-windowed DFT magnitude of one frequency, in dB re a full-scale sine.
static double toneDb (const float* x, int n, double f, double fs)
{
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n);
        const double ph = 2.0 * juce::MathConstants<double>::pi * f * i / fs;
        re += x[i] * w * std::cos (ph); im -= x[i] * w * std::sin (ph); wsum += w;
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / wsum + 1e-30);
}

// Renders a sine (both channels) through the chain; returns the settled output after the latency.
static std::vector<float> chainSine (const ChainSettings& s, double fs, double f, float levelDb, int len = 32768)
{
    MasterChain c; c.prepare (fs); c.configure (s);
    const int settle = (int) (0.5 * fs), total = settle + c.getLatency() + len;
    juce::AudioBuffer<float> b (2, total);
    const float a = msdsp::dbToGain (levelDb);
    for (int i = 0; i < total; ++i) { const float v = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / fs); b.setSample (0, i, v); b.setSample (1, i, v); }
    processInBlocks (c, b, s, 512);
    const float* p = b.getReadPointer (0, settle + c.getLatency());
    return std::vector<float> (p, p + len);
}

static double thdDb (const std::vector<float>& y, double f, double fs, int from = 2, int to = 10)
{
    const double fund = toneDb (y.data(), (int) y.size(), f, fs);
    double e = 0;
    for (int h = from; h <= to && h * f < fs * 0.45; ++h) e += std::pow (10.0, toneDb (y.data(), (int) y.size(), h * f, fs) / 10.0);
    return 10.0 * std::log10 (e + 1e-30) - fund;
}

static ChainSettings allOffSettings()
{
    ChainSettings s;
    s.eq.on = false; s.mb.on = false; s.bus.on = false; s.sat.on = false; s.img.on = false; s.lim.on = false;
    return s;
}

static void upgradeTests()
{
    std::cout << "\n== 1.7 upgrades ==" << std::endl;
    const double fs = 48000.0;

    // --- Linear-phase crossover: LR4 magnitudes, symmetric (linear-phase) impulse response ---
    for (float fc : { 40.0f, 1000.0f, 12000.0f })
    {
        msdsp::LinPhaseBank bank; bank.prepare (fs, 1, 1);
        msdsp::FirShape sh; sh.p[0] = fc; bank.setTarget (0, sh);
        const int lat = bank.getLatency(), total = 2 * lat + 4096;
        std::vector<float> h ((size_t) total);
        for (int i = 0; i < total; ++i) { bank.write (0, i == 0 ? 1.0f : 0.0f); h[(size_t) i] = bank.filtered (0, 0); bank.advance(); }
        double worst = 0; juce::String at;
        for (double r : { 0.25, 0.5, 1.0, 2.0, 4.0 })
        {
            const double f = fc * r;
            if (f > fs * 0.45) continue;
            double re = 0, im = 0;
            for (int i = 0; i < total; ++i) { const double ph = 2.0 * juce::MathConstants<double>::pi * f * i / fs; re += h[(size_t) i] * std::cos (ph); im -= h[(size_t) i] * std::sin (ph); }
            const double meas = 20.0 * std::log10 (std::sqrt (re * re + im * im)), want = 20.0 * std::log10 (msdsp::FirShape::lr4Low (f, fc));
            if (want > -50.0 && std::abs (meas - want) > worst) { worst = std::abs (meas - want); at = juce::String (f, 0) + " Hz"; }
        }
        float peak = 0, asym = 0;
        for (auto v : h) peak = std::max (peak, std::abs (v));
        for (int k = 1; k < 2000; ++k) asym = std::max (asym, std::abs (h[(size_t) (lat + k)] - h[(size_t) (lat - k)]));
        check (worst < 0.25, "linear-phase crossover at " + juce::String ((int) fc) + " Hz follows the LR4 slope within " + juce::String (worst, 2) + " dB (worst at " + at + ")");
        check (asym < 1.0e-4f * peak, "linear-phase crossover at " + juce::String ((int) fc) + " Hz has a symmetric impulse response (no phase shift)");
    }

    // --- Multiband in linear phase: bands sum back to the delayed input exactly ---
    {
        msdsp::MultibandComp mb; mb.prepare (fs); mb.setLinear (true);
        msdsp::MbSettings s; s.on = true; s.linear = true; for (auto& b : s.b) b.bypass = true;
        auto in = makeMusicLike (fs, 3.0, -12.0f, 77);
        auto out = in;
        for (int pos = 0; pos < out.getNumSamples(); pos += 480) mb.process (out, pos, std::min (480, out.getNumSamples() - pos), s);
        const int L = mb.getLatency();
        double e = 0, r = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = L + 1000; i < in.getNumSamples(); ++i) { const double d = out.getSample (ch, i) - in.getSample (ch, i - L); e += d * d; r += (double) in.getSample (ch, i - L) * in.getSample (ch, i - L); }
        const double nullDb = 10.0 * std::log10 (e / r + 1e-30);
        check (nullDb < -100.0, "linear-phase multiband (bands bypassed) nulls against the delayed input: " + juce::String (nullDb, 1) + " dB (latency " + juce::String (L) + " = "
               + juce::String (1000.0 * L / fs, 1) + " ms)");
    }

    // --- Every latency-changing mode: reported latency == measured (impulse) ---
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        juce::String bad;
        for (int mode = 0; mode < 4; ++mode)
        {
            auto s = allOffSettings();
            s.mb.linear = mode == 1 || mode == 3; s.img.linear = mode == 2 || mode == 3; s.lim.style = mode == 3 ? msdsp::LimTransparent : msdsp::LimClassic;
            s.mb.on = s.mb.linear; for (auto& b : s.mb.b) b.bypass = true;
            s.img.on = s.img.linear;
            MasterChain c; c.prepare (rate); c.configure (s);
            const int lat = c.getLatency(), n = lat + 4096;
            juce::AudioBuffer<float> b (2, n); b.clear(); b.setSample (0, 100, 0.5f); b.setSample (1, 100, 0.5f);
            processInBlocks (c, b, s, 512);
            int best = 0; float bv = 0;
            for (int i = 0; i < n; ++i) if (std::abs (b.getSample (0, i)) > bv) { bv = std::abs (b.getSample (0, i)); best = i; }
            if (best - 100 != lat) bad << "mode " << mode << ": reported " << lat << ", measured " << (best - 100) << "; ";
        }
        check (bad.isEmpty(), "latency reported == measured for linear-phase multiband, linear-phase stereo, both + Transparent limiter at "
               + juce::String ((int) rate) + " Hz" + (bad.isEmpty() ? juce::String() : ": " + bad));
    }

    // --- Switching linear phase while playing: host told the new latency, no burst, audio resumes ---
    {
        MasteringAudioProcessor p;
        p.setPlayConfigDetails (2, 2, fs, 512); p.prepareToPlay (fs, 512);
        const int latBefore = p.getLatencySamples();
        auto mix = makeMusicLike (fs, 4.0, -18.0f, 12);
        juce::MidiBuffer midi; juce::AudioBuffer<float> blk (2, 512);
        float peakBefore = 0, peakSwitch = 0, peakAfter = 0; bool finite = true;
        for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
        {
            if (pos == 512 * 100) p.apvts.getParameter (ID::mbLinear)->setValueNotifyingHost (1.0f);
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, mix, ch, pos, 512);
            p.processBlock (blk, midi);
            finite &= allFinite (blk);
            const float m = blk.getMagnitude (0, 512);
            (pos < 512 * 100 ? peakBefore : pos < 512 * 130 ? peakSwitch : peakAfter) = std::max (pos < 512 * 100 ? peakBefore : pos < 512 * 130 ? peakSwitch : peakAfter, m);
        }
        const int latAfter = p.getLatencySamples();
        check (finite && latAfter > latBefore + 4000 && peakSwitch <= peakBefore * 1.05f && peakAfter > peakBefore * 0.5f,
               "switching MB Linear Phase while playing: latency " + juce::String (latBefore) + " -> " + juce::String (latAfter)
               + " reported to the host, no burst (peak " + juce::String (msdsp::gainToDb (peakSwitch), 1) + " vs " + juce::String (msdsp::gainToDb (peakBefore), 1)
               + " dBFS), audio resumes");
    }

    // --- Stereo: linear phase at neutral settings is a pure delay ---
    {
        msdsp::Imager img; img.prepare (fs); img.setLinear (true);
        msdsp::ImgSettings s; s.on = true; s.linear = true;
        auto in = makeMusicLike (fs, 3.0, -12.0f, 78);
        for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (1, i, 0.3f * in.getSample (1, i) + 0.5f * in.getSample (0, i));
        auto out = in;
        for (int pos = 0; pos < out.getNumSamples(); pos += 512) img.process (out, pos, std::min (512, out.getNumSamples() - pos), s);
        const int L = img.getLatency();
        double e = 0, r = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = L + 1000; i < in.getNumSamples(); ++i) { const double d = out.getSample (ch, i) - in.getSample (ch, i - L); e += d * d; r += (double) in.getSample (ch, i - L) * in.getSample (ch, i - L); }
        check (10.0 * std::log10 (e / r + 1e-30) < -100.0, "linear-phase stereo at 100% widths is a pure delay: " + juce::String (10.0 * std::log10 (e / r + 1e-30), 1) + " dB");
    }

    // --- Mono bass: side removed below the cutoff, kept above (both modes) ---
    for (bool linear : { false, true })
    {
        auto sideDb = [&] (double f)
        {
            msdsp::Imager img; img.prepare (fs); img.setLinear (linear);
            msdsp::ImgSettings s; s.on = true; s.linear = linear; s.monoBass = true; s.monoFreq = 120.0f;
            const int n = (int) fs * 2 + img.getLatency();
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / fs)); b.setSample (1, i, 0.0f); }
            for (int pos = 0; pos < n; pos += 512) img.process (b, pos, std::min (512, n - pos), s);
            std::vector<float> side ((size_t) 32768);
            for (int i = 0; i < 32768; ++i) side[(size_t) i] = 0.5f * (b.getSample (0, n - 32768 + i) - b.getSample (1, n - 32768 + i));
            return toneDb (side.data(), 32768, f, fs) - 20.0 * std::log10 (0.25);
        };
        const double lo = sideDb (30.0), hi = sideDb (1000.0);
        check (lo < -20.0 && std::abs (hi) < 0.1, juce::String (linear ? "linear-phase" : "IIR") + " mono bass (120 Hz): side at 30 Hz " + juce::String (lo, 1)
               + " dB, at 1 kHz " + juce::String (hi, 2) + " dB");
    }

    // --- Mono-safe width keeps the correlation from going negative when widening ---
    for (bool safe : { false, true })
    {
        msdsp::Imager img; img.prepare (fs);
        msdsp::ImgSettings s; s.on = true; s.wLow = s.wMid = s.wHigh = 200.0f; s.safe = safe;
        const int n = (int) (fs * 3);
        juce::AudioBuffer<float> b (2, n);
        juce::Random r (4);
        for (int i = 0; i < n; ++i) { const float a = 0.3f * (r.nextFloat() * 2 - 1), d = 0.21f * (r.nextFloat() * 2 - 1); b.setSample (0, i, a + d); b.setSample (1, i, a - d); }
        for (int pos = 0; pos < n; pos += 512) img.process (b, pos, std::min (512, n - pos), s);
        double lr = 0, ll = 0, rr = 0;
        for (int i = n / 2; i < n; ++i) { lr += (double) b.getSample (0, i) * b.getSample (1, i); ll += (double) b.getSample (0, i) * b.getSample (0, i); rr += (double) b.getSample (1, i) * b.getSample (1, i); }
        const double corr = lr / std::sqrt (ll * rr);
        if (safe) check (corr > -0.05, "Mono-Safe width: 200% widening of a wide mix keeps correlation at " + juce::String (corr, 2) + " (without it: below 0)");
        else check (corr < -0.15, "without Mono-Safe the same widening drives correlation to " + juce::String (corr, 2));
    }

    // --- Tape (Modeled): unity at low level, distortion grows with level and frequency, stays finite ---
    {
        auto sat = [] (int type, float drive) { auto s = allOffSettings(); s.sat = { true, type, drive, 100.0f }; return s; };
        const auto quiet = chainSine (sat (msdsp::SatTapeModel, 6.0f), fs, 1000.0, -40.0f);
        const double g = toneDb (quiet.data(), (int) quiet.size(), 1000.0, fs) + 40.0;
        check (std::abs (g) < 1.0, "Tape (Modeled) at -40 dBFS: gain " + juce::String (g, 2) + " dB (drive changes color, not level)");
        const double thdLo = thdDb (chainSine (sat (msdsp::SatTapeModel, 6.0f), fs, 1000.0, -24.0f), 1000.0, fs);
        const double thdHi = thdDb (chainSine (sat (msdsp::SatTapeModel, 6.0f), fs, 1000.0, -6.0f), 1000.0, fs);
        check (thdHi > thdLo + 12.0, "Tape (Modeled) distortion depends on level: THD " + juce::String (thdLo, 1) + " dB at -24 dBFS, " + juce::String (thdHi, 1) + " dB at -6 dBFS");
        const auto low = chainSine (sat (msdsp::SatTapeModel, 12.0f), fs, 200.0, -6.0f), high = chainSine (sat (msdsp::SatTapeModel, 12.0f), fs, 6000.0, -6.0f);
        const double gLow = toneDb (low.data(), (int) low.size(), 200.0, fs) + 6.0, gHigh = toneDb (high.data(), (int) high.size(), 6000.0, fs) + 6.0;
        check (gHigh < gLow - 0.5, "Tape (Modeled) saturates highs first (record EQ): level change at -6 dBFS, 200 Hz " + juce::String (gLow, 2) + " dB, 6 kHz " + juce::String (gHigh, 2) + " dB");
        // THE_LAB's high-note test: 6.3 and 10 kHz at -6 dBFS (drive 5, mix 60). Before 1.7.2, Tape (Modeled)
        // read -87.8 dB at 10 kHz (hysteresis loop tips) and both Tube types -78 dB at 6.3 kHz (the
        // oversampler's last filter let the 4th harmonic, 25.2 kHz, fold back).
        for (int type : { (int) msdsp::SatTape, (int) msdsp::SatTube, (int) msdsp::SatTapeModel, (int) msdsp::SatTubeModel })
        {
            juce::String res;
            double worstAll = -300;
            for (int k0 : { 8601, 13653 })
            {
                constexpr int N = 65536;
                auto s = sat (type, 5.0f); s.sat.mix = 60.0f;
                const auto y = chainSine (s, fs, k0 * fs / N, -6.0f, N);
                juce::dsp::FFT fft (16);
                std::vector<float> w ((size_t) 2 * N, 0.0f);
                for (int i = 0; i < N; ++i)
                {
                    const double t = 2.0 * juce::MathConstants<double>::pi * i / N;
                    w[(size_t) i] = y[(size_t) i] * (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
                }
                fft.performFrequencyOnlyForwardTransform (w.data());
                double worst = 0; int worstK = 0;
                for (int k = 10; k < N / 2; ++k)
                {
                    bool skip = std::abs (k - k0) <= 300;
                    for (int h = 2; h * k0 < N / 2 + 6; ++h) skip |= std::abs (k - h * k0) <= 6;   // true harmonics are not spurs
                    if (! skip && w[(size_t) k] > worst) { worst = w[(size_t) k]; worstK = k; }
                }
                const double db = 20.0 * std::log10 (worst / w[(size_t) k0] + 1e-30);
                worstAll = std::max (worstAll, db);
                res << juce::String (k0 * fs / N / 1000.0, 1) << " kHz: " << juce::String (db, 1) << " dB at " << juce::String (worstK * fs / N / 1000.0, 1) << " kHz  ";
            }
            static const char* names[] { "Tape", "Tube", "Soft Clip", "Tape (Modeled)", "Tube (Modeled)" };
            check (worstAll < -110.0, juce::String ("high-note test, ") + names[type] + ": " + res);
        }
        const double thdClassicLo = thdDb (chainSine (sat (msdsp::SatTape, 6.0f), fs, 1000.0, -24.0f), 1000.0, fs);
        std::cout << "        (classic Tape THD at -24 dBFS: " << juce::String (thdClassicLo, 1) << " dB)" << std::endl;

        auto h2Growth = [&] (int type)
        {
            const double lo = toneDb (chainSine (sat (type, 9.0f), fs, 1000.0, -24.0f).data(), 32768, 2000.0, fs) + 24.0;
            const double hi = toneDb (chainSine (sat (type, 9.0f), fs, 1000.0, -6.0f).data(), 32768, 2000.0, fs) + 6.0;
            return hi - lo;
        };
        const double gModel = h2Growth (msdsp::SatTubeModel), gFixed = h2Growth (msdsp::SatTube);
        check (gModel > gFixed + 3.0, "Tube (Modeled) even harmonics bloom with level: H2 rises " + juce::String (gModel, 1) + " dB from -24 to -6 dBFS (fixed-curve Tube: "
               + juce::String (gFixed, 1) + " dB)");

        for (int type : { (int) msdsp::SatTapeModel, (int) msdsp::SatTubeModel })
        {
            MasterChain c; c.prepare (fs);
            auto s = sat (type, 24.0f);
            juce::AudioBuffer<float> b (2, (int) fs * 2);
            juce::Random r (2);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, (r.nextFloat() * 2 - 1) * 1.5f);
            processInBlocks (c, b, s, 333);
            check (allFinite (b) && b.getMagnitude (0, b.getNumSamples()) < 8.0f, juce::String (type == msdsp::SatTapeModel ? "Tape" : "Tube")
                   + " (Modeled) at 24 dB drive with over-full-scale noise: finite, peak " + juce::String (msdsp::gainToDb (b.getMagnitude (0, b.getNumSamples())), 1) + " dBFS");
        }
    }

    // --- Transparent limiter: holds the ceiling, less distortion than Classic at the same settings ---
    {
        MasterChain c; c.prepare (fs);
        auto s = allOffSettings(); s.lim = { true, 12.0f, -1.0f, 60.0f, true, msdsp::LimTransparent };
        auto b = makeMusicLike (fs, 6.0, -10.0f, 7);
        c.configure (s);
        processInBlocks (c, b, s, 512);
        const float tp = maxTruePeakDb (b, c.getLatency());
        check (tp <= -0.95f, "Transparent limiter true peak " + juce::String (tp, 2) + " dBTP with -1.0 ceiling and 12 dB of gain");
        auto lim = [] (int style) { auto st = allOffSettings(); st.lim = { true, 10.0f, -1.0f, 30.0f, true, style }; return st; };
        const double thdC = thdDb (chainSine (lim (msdsp::LimClassic), fs, 80.0, -6.0f, 65536), 80.0, fs);
        const double thdT = thdDb (chainSine (lim (msdsp::LimTransparent), fs, 80.0, -6.0f, 65536), 80.0, fs);
        check (thdT < thdC - 6.0, "Transparent limiter distorts less on bass: THD on 80 Hz, 10 dB into the limiter, 30 ms release: Classic "
               + juce::String (thdC, 1) + " dB, Transparent " + juce::String (thdT, 1) + " dB");
    }

    // --- Dither noise shaping ---
    {
        msdsp::Dither d; d.prepare (44100.0);
        const double mid = d.shapingGainDb (3500.0, 44100.0), top = d.shapingGainDb (19000.0, 44100.0);
        check (mid < -8.0 && top > 10.0, "noise shaping at 44.1 kHz: noise " + juce::String (mid, 1) + " dB at 3.5 kHz, " + juce::String (top, 1) + " dB at 19 kHz (moved where hearing is least sensitive)");
        msdsp::Dither d96; d96.prepare (96000.0);
        check (d96.shapingGainDb (8000.0, 96000.0) < -6.0 && d96.shapingGainDb (40000.0, 96000.0) > 3.0, "noise shaping at 96 kHz moves noise above 20 kHz: "
               + juce::String (d96.shapingGainDb (8000.0, 96000.0), 1) + " dB at 8 kHz");
        // Measured on real output: dither noise in 1-6 kHz, shaped vs plain TPDF (16-bit, silence in)
        auto bandDb = [] (bool shape)
        {
            msdsp::Dither dd; dd.prepare (44100.0);
            const int n = 1 << 16;
            juce::AudioBuffer<float> b (2, n); b.clear();
            dd.process (b, 0, n, { 2, shape });
            juce::dsp::FFT fft (16);
            std::vector<float> w ((size_t) n * 2, 0.0f);
            for (int i = 0; i < n; ++i) w[(size_t) i] = b.getSample (0, i) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
            fft.performFrequencyOnlyForwardTransform (w.data());
            double e = 0; int cnt = 0;
            for (int k = (int) (1000.0 * n / 44100.0); k < (int) (6000.0 * n / 44100.0); ++k) { e += (double) w[(size_t) k] * w[(size_t) k]; ++cnt; }
            return 10.0 * std::log10 (e / cnt);
        };
        const double plain = bandDb (false), shaped = bandDb (true);
        check (shaped < plain - 8.0, "16-bit dither noise in 1-6 kHz: " + juce::String (shaped - plain, 1) + " dB vs plain TPDF");
        // A full-scale over with shaping on stays bounded (no run-away)
        msdsp::Dither dd; dd.prepare (44100.0);
        juce::AudioBuffer<float> b (2, 48000);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 48000; ++i) b.setSample (ch, i, (i / 100) % 2 ? 1.2f : -1.2f);
        dd.process (b, 0, 48000, { 2, true });
        juce::AudioBuffer<float> q (2, 4410); q.clear();
        dd.process (q, 0, 4410, { 2, true });
        check (allFinite (b) && q.getMagnitude (0, 4410) < 40.0f / 32768.0f, "shaped dither recovers right after full-scale overs (noise "
               + juce::String (q.getMagnitude (0, 4410) * 32768.0f, 1) + " LSB peak)");
    }

    // --- CPU of the new modes (information) ---
    {
        auto cost = [fs] (const ChainSettings& s)
        {
            MasterChain c; c.prepare (fs); c.configure (s);
            auto b = makeMusicLike (fs, 10.0, -14.0f, 3);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            processInBlocks (c, b, s, 512);
            return 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / 10000.0;
        };
        auto s1 = allOffSettings(); s1.sat = { true, msdsp::SatTapeModel, 6.0f, 100.0f };
        auto s2 = allOffSettings(); s2.mb.on = true; s2.mb.linear = true;
        auto s3 = allOffSettings(); s3.img.on = true; s3.img.linear = true;
        auto s4 = allOffSettings(); s4.lim.on = true; s4.lim.style = msdsp::LimTransparent;
        std::cout << "        CPU at 48 kHz (% of one core): Tape (Modeled) " << juce::String (cost (s1), 2) << ", linear-phase multiband " << juce::String (cost (s2), 2)
                  << ", linear-phase stereo " << juce::String (cost (s3), 2) << ", Transparent limiter " << juce::String (cost (s4), 2) << std::endl;
    }
}

// --spur: THE_LAB-style high-note test (a 10 kHz tone at -6 dBFS through the whole plugin, Loud & Punchy
// with the 1.7 modes on), with each new mode switched off in turn to find where a spur comes from.
static void spurProbe()
{
    const double fs = 48000.0;
    constexpr int N = 65536;
    for (int k0 : { 8601, 13653 })   // odd bins: ~6299.6 Hz and ~9999.8 Hz, THE_LAB's two high notes
    {
    const double f = k0 * fs / N;
    std::cout << "-- " << juce::String (f, 1) << " Hz tone" << std::endl;
    struct Variant { const char* name; std::vector<std::pair<const char*, float>> v; };
    const std::vector<std::pair<const char*, float>> all { { ID::mbLinear, 1 }, { ID::mbAutoRel, 1 }, { ID::satType, 3 }, { ID::limStyle, 1 } };
    std::vector<Variant> vars {
        { "1.6-style (all new modes off)", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 0 }, { ID::limStyle, 0 } } },
        { "all four on (THE_LAB run)", all },
        { "without MB Linear Phase", { { ID::mbLinear, 0 } } },
        { "without MB Auto Release", { { ID::mbAutoRel, 0 } } },
        { "Tape (classic) instead of Tape (Modeled)", { { ID::satType, 0 } } },
        { "Classic limiter instead of Transparent", { { ID::limStyle, 0 } } },
        { "only Tape (Modeled)", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::limStyle, 0 } } },
        { "only MB Auto Release", { { ID::mbLinear, 0 }, { ID::satType, 0 }, { ID::limStyle, 0 } } },
        { "only Transparent limiter", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 0 } } },
        { "only Tube (classic)", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 1 }, { ID::limStyle, 0 } } },
        { "only Tube (Modeled)", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 4 }, { ID::limStyle, 0 } } },
        { "Tube (Modeled), drive 15", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 4 }, { ID::limStyle, 0 }, { ID::satDrive, 15 } } },
        { "Tube (classic), drive 15", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 1 }, { ID::limStyle, 0 }, { ID::satDrive, 15 } } },
        { "Tape (Modeled), drive 15", { { ID::mbLinear, 0 }, { ID::mbAutoRel, 0 }, { ID::satType, 3 }, { ID::limStyle, 0 }, { ID::satDrive, 15 } } },
    };
    for (auto& var : vars)
    {
        MasteringAudioProcessor p;
        p.setCurrentProgram (2);
        using List = std::vector<std::pair<const char*, float>>;
        for (const List* list : { (const List*) &all, (const List*) &var.v })
            for (auto [id, v] : *list) if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
        p.setPlayConfigDetails (2, 2, fs, 512); p.prepareToPlay (fs, 512);
        const int settle = (int) (3.0 * fs) + p.getLatencySamples(), total = settle + N;
        juce::AudioBuffer<float> out (2, total), blk (2, 512);
        juce::MidiBuffer midi;
        const float a = msdsp::dbToGain (-6.0f);
        for (int pos = 0; pos < total; pos += 512)
        {
            for (int i = 0; i < 512; ++i) { const float v = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * (pos + i) / fs); blk.setSample (0, i, v); blk.setSample (1, i, v); }
            p.processBlock (blk, midi);
            const int n = std::min (512, total - pos);
            out.copyFrom (0, pos, blk, 0, 0, n);
        }
        // Blackman-Harris power spectrum
        juce::dsp::FFT fft (16);
        std::vector<float> w ((size_t) 2 * N, 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const double t = 2.0 * juce::MathConstants<double>::pi * i / N;
            w[(size_t) i] = out.getSample (0, settle + i) * (float) (0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t));
        }
        fft.performFrequencyOnlyForwardTransform (w.data());
        const double tone = w[(size_t) k0];
        double worst = 0; int worstK = 0;
        for (int k = 10; k < N / 2; ++k)
        {
            if (std::abs (k - k0) <= 6 * 50) continue;   // tone (+/-5 % "level moving" zone)
            bool harmonic = false;                       // true harmonics below Nyquist are not spurs
            for (int h = 2; h * k0 < N / 2 + 6; ++h) harmonic |= std::abs (k - h * k0) <= 6;
            if (harmonic) continue;
            if (w[(size_t) k] > worst) { worst = w[(size_t) k]; worstK = k; }
        }
        std::cout << "  " << juce::String (var.name).paddedRight (' ', 44) << " worst spur " << juce::String (20.0 * std::log10 (worst / tone + 1e-30), 1)
                  << " dB at " << juce::String (worstK * fs / N, 0) << " Hz" << std::endl;
    }
    }
}

// --tapecpu: average and worst 512-sample buffer for the saturation types (loud bright noise and music).
static void tapeCpuProbe()
{
    const double fs = 48000.0;
    for (int type : { 0, 1, 3, 4 })
        for (float drive : { 5.0f, 15.0f })
        {
            msdsp::Saturator sat; sat.prepare (fs, 512);
            msdsp::SatSettings s { true, type, drive, 60.0f };
            juce::AudioBuffer<float> b (2, 512);
            juce::Random r (1);
            auto music = makeMusicLike (fs, 10.0, -6.0f, 9);
            for (int pass = 0; pass < 3; ++pass)
            {
                double total = 0, worst = 0; int blocks = 0;
                for (int pos = 0; pos + 512 <= (int) (fs * 10); pos += 512)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < 512; ++i)
                            b.setSample (ch, i, pass == 0 ? 0.7f * (r.nextFloat() * 2 - 1) : pass == 1 ? music.getSample (ch, pos + i)
                                                           : 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 999.76 * (pos + i) / fs));
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    sat.process (b, 0, 512, s);
                    const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0;
                    if (pos > (int) fs) { total += ms; worst = std::max (worst, ms); ++blocks; }
                }
                std::cout << "  type " << type << ", drive " << (int) drive << (pass == 0 ? ", loud noise: " : pass == 1 ? ", music:      " : ", 1 kHz tone: ") << "average "
                          << juce::String ((int) (total / blocks * 1.0e6 / 512.0)) << " ns/sample, worst buffer " << juce::String (worst, 2) << " ms" << std::endl;
            }
        }
}

// --rate: gain at low frequencies at 48 vs 96 kHz, Loud & Punchy with each 1.7 mode alone (THE_LAB's sample-rate check).
static void rateProbe()
{
    MasteringAudioProcessor p;
    p.setCurrentProgram (2);
    const auto base = p.makeSettings();
    struct V { const char* name; std::function<void (ChainSettings&)> f; };
    std::vector<V> vars {
        { "1.6-style", [] (ChainSettings&) {} },
        { "MB Linear Phase", [] (ChainSettings& s) { s.mb.linear = true; } },
        { "MB Auto Release", [] (ChainSettings& s) { s.mb.autoRelease = true; } },
        { "Tape (Modeled)", [] (ChainSettings& s) { s.sat.type = msdsp::SatTapeModel; } },
        { "Transparent limiter", [] (ChainSettings& s) { s.lim.style = msdsp::LimTransparent; } },
        { "all four", [] (ChainSettings& s) { s.mb.linear = true; s.mb.autoRelease = true; s.sat.type = msdsp::SatTapeModel; s.lim.style = msdsp::LimTransparent; } },
    };
    for (auto& v : vars)
    {
        auto s = base; s.dither = { 0, false }; v.f (s);
        juce::String line = juce::String (v.name).paddedRight (' ', 22);
        for (double f : { 30.0, 50.0, 100.0 })
        {
            auto g = [&] (double fs) { auto y = chainSine (s, fs, f, -20.0f, 1 << 16); return toneDb (y.data(), (int) y.size(), f, fs) + 20.0; };
            line << "  " << juce::String ((int) f) << " Hz: 96k-48k " << juce::String (g (96000.0) - g (48000.0), 2) << " dB";
        }
        std::cout << line << std::endl;
    }
}

// ---- Layout: every Assistant control fits at the smallest window sizes ----
static void layoutTests()
{
    std::cout << "\n== Assistant panel layout ==" << std::endl;
    std::function<void (juce::Component*, std::vector<juce::Button*>&)> collect = [&] (juce::Component* c, std::vector<juce::Button*>& out)
    {
        for (auto* ch : c->getChildren())
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (ch)) out.push_back (b);
            collect (ch, out);
        }
    };
    for (bool standalone : { false, true })
    {
        if (standalone) juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        MasteringAudioProcessor p;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<MasteringAudioProcessorEditor*> (ed.get());
        e->showAssistant (true);
        const int minH = standalone ? 800 : 720;
        for (auto [w, h] : std::initializer_list<std::pair<int, int>> { { 1200, minH }, { 1320, standalone ? 880 : 800 }, { 1920, 1200 } })
        {
            e->setSize (w, h);
            std::vector<juce::Button*> buttons;
            collect (e, buttons);
            juce::StringArray bad;
            for (auto* b : buttons)
            {
                if (! b->isShowing() && ! (b->isVisible() && b->getParentComponent()->isVisible())) continue; // hidden on purpose (e.g. Cancel while idle)
                auto* parent = b->getParentComponent();
                if (dynamic_cast<AssistantPanel*> (parent) == nullptr) continue;
                if (b->getHeight() < 20 || b->getWidth() < 40 || ! parent->getLocalBounds().contains (b->getBounds()))
                    bad.add (b->getButtonText() + " " + b->getBounds().toString());
            }
            check (bad.isEmpty(), juce::String (standalone ? "standalone " : "plugin ") + juce::String (w) + "x" + juce::String (h)
                                  + ": every Assistant button fully visible" + (bad.isEmpty() ? juce::String() : " - clipped: " + bad.joinIntoString (", ")));
        }
    }
}

// ---- Module views: the data they show is correct, and each module shows its own view ----
static void moduleViewTests()
{
    std::cout << "\n== Module views ==" << std::endl;
    const double fs = 48000.0;
    auto mix = makeMusicLike (fs, 5.0, -14.0f, 51);
    auto runWith = [&] (std::function<void (MasteringAudioProcessor&)> setUp, VizHistory& hist)
    {
        MasteringAudioProcessor p;
        setUp (p);
        p.setPlayConfigDetails (2, 2, fs, 512); p.prepareToPlay (fs, 512);
        juce::MidiBuffer midi; juce::AudioBuffer<float> blk (2, 512);
        for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, mix, ch, pos, 512);
            p.processBlock (blk, midi);
            hist.pullFrom (p.vizFifo);
        }
        return std::array<float, 3> { p.getMbGrLatest (0), p.getMbGrLatest (1), p.getMbGrLatest (2) };
    };
    {
        VizHistory h;
        const auto mbGr = runWith ([] (MasteringAudioProcessor& p) { p.setCurrentProgram (2); }, h);
        const auto m = h.recentMax (h.count());
        const int expected = (int) (5.0 * fs / MasterChain::VizChunk);
        check (std::abs (h.count() - expected) <= 8, "display frames arrive every 64 samples (" + juce::String (h.count()) + " for 5 s)");
        check (m.limGr > 3.0f && m.limGr < 20.0f && m.busGr > 0.2f && m.outPeak <= msdsp::dbToGain (-1.0f) + 1e-3f && m.satIn > 0.0f,
               "Loud & Punchy shows limiting " + juce::String (m.limGr, 1) + " dB, glue " + juce::String (m.busGr, 1) + " dB, output peak "
               + juce::String (msdsp::gainToDb (m.outPeak), 2) + " dBFS (under the -1 ceiling)");
        check (std::max ({ mbGr[0], mbGr[1], mbGr[2] }) > 0.0f, "multiband reports gain reduction per band");
    }
    {
        VizHistory h;
        runWith ([] (MasteringAudioProcessor& p) { p.setCurrentProgram (2); p.apvts.getParameter (ID::limOn)->setValueNotifyingHost (0.0f); }, h);
        check (h.recentMax (h.count() / 2).limGr < 0.01f, "limiter switched off shows no limiting");
    }
    {
        MasteringAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<MasteringAudioProcessorEditor*> (ed.get());
        juce::StringArray seen;
        bool ok = true;
        const char* expect[] { "SpectrumView/EQ", "SpectrumView/EQ", "SpectrumView/Multiband", "GlueView", "SaturationView", "SpectrumView/Stereo", "LimiterView", "SpectrumView/EQ" };
        for (int m = 0; m < 8; ++m)
        {
            e->selectModule (m);
            juce::String shown;
            for (auto* c : e->getChildren())
                if (c->isVisible())
                {
                    if (auto* sv = dynamic_cast<SpectrumView*> (c)) shown = juce::String ("SpectrumView/") + (sv->getMode() == SpectrumView::Mode::EQ ? "EQ" : sv->getMode() == SpectrumView::Mode::Multiband ? "Multiband" : "Stereo");
                    else if (dynamic_cast<GlueView*> (c)) shown = "GlueView";
                    else if (dynamic_cast<SaturationView*> (c)) shown = "SaturationView";
                    else if (dynamic_cast<LimiterView*> (c)) shown = "LimiterView";
                }
            ok &= shown == expect[m];
            seen.add (juce::String (m) + "=" + shown);
        }
        check (ok, "each module shows its own chart view: " + seen.joinIntoString (", "));
    }
}

// ---- EQ chart points are labelled like the EQ panel sections they control ----
static void eqLabelTests()
{
    std::cout << "\n== EQ chart point labels ==" << std::endl;
    MasteringAudioProcessor p;
    SpectrumView view (p);
    juce::StringArray wrong, shown;
    for (int i = 0; i < view.getNumNodes(); ++i)
    {
        const auto name = view.getNodeName (i), label = SpectrumView::nodeLabel (i);
        // Expected: "Bell N" -> "N"; other sections -> their initials ("Low Shelf" -> "LS")
        juce::String expected;
        if (name.startsWith ("Bell ")) expected = name.fromLastOccurrenceOf (" ", false, false);
        else for (auto& w : juce::StringArray::fromTokens (name, " ", "")) expected << w.substring (0, 1);
        shown.add (name + "=" + label);
        if (label != expected || name != SpectrumView::nodeName (i)) wrong.add (name + " shows \"" + label + "\" (expected \"" + expected + "\")");
    }
    check (view.getNumNodes() == 7 && wrong.isEmpty(), "chart labels match the panel: " + shown.joinIntoString (", ")
                                                       + (wrong.isEmpty() ? juce::String() : " - WRONG: " + wrong.joinIntoString ("; ")));
}

// ---- The host is told which program (preset) is active ----
static void programReportingTests()
{
    std::cout << "\n== Program shown in the host ==" << std::endl;
    struct HostSpy : juce::AudioProcessorListener
    {
        int programChanges = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override { if (d.programChanged) ++programChanges; }
    };
    const int custom = MasteringAudioProcessor::getNumFactoryPrograms();

    MasteringAudioProcessor p;
    HostSpy host;
    p.addListener (&host);

    p.applyFactoryPreset (2);  // as the plugin's own preset menu does
    check (host.programChanges >= 1 && p.getCurrentProgram() == 2 && p.getProgramName (p.getCurrentProgram()) == "Loud & Punchy (-9)",
           "choosing a preset in the plugin tells the host: program " + juce::String (p.getCurrentProgram()) + " \"" + p.getProgramName (p.getCurrentProgram()) + "\"");

    // A user preset, an Assistant result or a recalled slot shows as Custom, not a stale factory name
    {
        const juce::String name = "ProgTest " + juce::String (juce::Random::getSystemRandom().nextInt (100000));
        p.saveUserPreset (name);
        const int before = host.programChanges;
        p.loadUserPreset (p.getUserPresetFolder().getChildFile (name + ".mspreset"));
        p.getUserPresetFolder().getChildFile (name + ".mspreset").deleteFile();
        check (host.programChanges > before && p.getCurrentProgram() == custom, "loading a user preset shows \"" + p.getProgramName (p.getCurrentProgram()) + "\" in the host");
    }

    // The program survives a session reload
    p.applyFactoryPreset (3);
    juce::MemoryBlock st; p.getStateInformation (st);
    {
        MasteringAudioProcessor q;
        HostSpy host2; q.addListener (&host2);
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.getCurrentProgram() == 3 && host2.programChanges >= 1, "reopened session reports its program to the host: " + q.getProgramName (q.getCurrentProgram()));
        q.removeListener (&host2);
    }

    // A host re-sending the current program after a reload must not wipe tweaks made since
    {
        auto* lsGain = p.apvts.getParameter (ID::lsGain);
        lsGain->setValueNotifyingHost (lsGain->convertTo0to1 (4.0f));
        p.setCurrentProgram (3);
        check (std::abs (p.makeSettings().eq.lsGain - 4.0f) < 0.05f, "host re-selecting the current program keeps your tweaks");
        p.setCurrentProgram (1);
        check (p.getCurrentProgram() == 1 && std::abs (p.makeSettings().eq.lsGain - 0.5f) < 0.05f, "host choosing another program loads it");
    }
    p.removeListener (&host);
}

// ---- Real-time safety: no memory allocation inside processBlock ----
static void allocationTests()
{
    std::cout << "\n== Audio thread: memory allocation ==" << std::endl;
    {
        // The counter must see what the old code did (build a parameter name as a String).
        const long long before = allocationCount.load();
        countAllocations = true;
        const juce::String name = ID::mb (1, "Thr");
        countAllocations = false;
        check (allocationCount.load() - before > 0 && name.isNotEmpty(), "allocation counter works (building a parameter name allocates "
               + juce::String (allocationCount.load() - before) + " time(s))");
    }
    const double fs = 48000.0;
    auto mix = makeMusicLike (fs, 12.0, -14.0f, 41);

    struct Case { juce::String name; std::function<void (MasteringAudioProcessor&)> setUp; bool moveControls; bool learn; };
    std::vector<Case> cases {
        { "Default (Clean)", [] (MasteringAudioProcessor&) {}, false, false },
        { "Loud & Punchy (every module type on)", [] (MasteringAudioProcessor& p) { p.setCurrentProgram (2); }, false, false },
        { "everything on + Compare + Mono monitor + dither", [] (MasteringAudioProcessor& p)
          {
              p.setCurrentProgram (2);
              for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                       { ID::imgOn, 1 }, { ID::hpOn, 1 }, { ID::lpOn, 1 }, { ID::eqMode, 1 }, { ID::bypass, 1 }, { ID::monitor, 1 }, { ID::ditherBits, 2 } })
                  if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
          }, false, false },
        { "Loud & Punchy while every control moves", [] (MasteringAudioProcessor& p) { p.setCurrentProgram (2); }, true, false },
        { "every 1.7 mode on (linear phase x2, Tape (Modeled), Transparent, mono bass, mono-safe, auto release, shaped dither)", [] (MasteringAudioProcessor& p)
          {
              p.setCurrentProgram (2);
              for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                       { ID::imgOn, 1 }, { ID::mbLinear, 1 }, { ID::mbAutoRel, 1 }, { ID::imgLinear, 1 }, { ID::imgMono, 1 }, { ID::imgSafe, 1 },
                       { ID::satType, 3 }, { ID::limStyle, 1 }, { ID::ditherBits, 2 }, { ID::ditherShape, 1 } })
                  if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
          }, false, false },
        { "every 1.7 mode on while every control moves", [] (MasteringAudioProcessor& p)
          {
              p.setCurrentProgram (2);
              for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                       { ID::imgOn, 1 }, { ID::mbLinear, 1 }, { ID::imgLinear, 1 }, { ID::satType, 4 }, { ID::limStyle, 1 } })
                  if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
          }, true, false },
        { "Learn (capturing audio)", [] (MasteringAudioProcessor& p) { p.setCurrentProgram (2); }, false, true },
        { "standalone file playback (44.1 kHz file resampled to 48 kHz)", [] (MasteringAudioProcessor& p) { p.setCurrentProgram (2); }, false, false },
    };
    // A 44.1 kHz test file for the playback case
    const auto playFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms_alloc_test.wav");
    {
        auto src = makeMusicLike (44100.0, 15.0, -14.0f, 8);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (playFile), 44100.0, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (src, 0, src.getNumSamples());
    }

    for (auto& c : cases)
    {
        MasteringAudioProcessor p;
        c.setUp (p);
        p.setPlayConfigDetails (2, 2, fs, 512);
        p.prepareToPlay (fs, 512);
        if (c.learn) p.startLearn();
        if (c.name.startsWith ("standalone"))
        {
            p.loadFile (playFile);
            p.transport.start();
            juce::Thread::sleep (300); // let the read-ahead thread buffer
        }
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> blk (2, 512);
        auto params = p.getParameters();
        juce::Random r (3);
        long long counted = 0; int buffersWithAlloc = 0, buffers = 0;
        for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
        {
            if (c.moveControls && (pos / 512) % 4 == 0)
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (params[r.nextInt (params.size())]))
                    if (rp->getParameterID() != ID::bypass && rp->getParameterID() != ID::monitor)
                        rp->setValueNotifyingHost (r.nextFloat());   // outside the measured window, like a host
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, mix, ch, pos, 512);
            const long long before = allocationCount.load();
            countAllocations = true;
            p.processBlock (blk, midi);
            countAllocations = false;
            const long long made = allocationCount.load() - before;
            if (pos >= (int) fs) // the first second is allowed for lazy setup (as in THE_LAB)
            {
                counted += made; ++buffers;
                if (made > 0) ++buffersWithAlloc;
            }
        }
        check (counted == 0, c.name + ": " + juce::String (counted) + " allocations in " + juce::String (buffersWithAlloc) + " of "
                             + juce::String (buffers) + " buffers");
        p.ejectFile();
        p.releaseResources();
    }
    playFile.deleteFile();
}

// --snapshots <dir>: renders the editor with each module selected to PNGs (UI review without a window).
static int snapshots (const juce::File& dir)
{
    dir.createDirectory();
    MasteringAudioProcessor p;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.setCurrentProgram (2);
    for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { ID::imgOn, 1 }, { ID::imgLow, 40 }, { ID::imgHigh, 130 },
                                                                                   { ID::satType, 3 }, { ID::limStyle, 1 }, { ID::imgMono, 1 }, { ID::mbLinear, 1 } })
        if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v));
    juce::MidiBuffer midi;
    auto b = makeMusicLike (48000.0, 6.0, -14.0f, 3);
    juce::AudioBuffer<float> blk (2, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<MasteringAudioProcessorEditor*> (ed.get());
    for (int pos = 0; pos + 512 <= b.getNumSamples(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, b, ch, pos, 512);
        p.processBlock (blk, midi);
        if (pos % 4096 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    for (int m = 0; m < 8; ++m)
    {
        e->selectModule (m);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile ("module" + juce::String (m) + ".png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    // Assistant panel after a run on captured audio
    {
        auto mix = makeShapedMix (48000.0, 20.0, withBoost (assistant::targetCurve (assistant::Curve::Vocal), 3000, 20000, -5.0f),
                                  [] { assistant::Spectrum s {}; s.fill (-18.0f); for (int b = 0; b < 9; ++b) s[(size_t) b] = -3.0f; return s; }(), 12);
        p.startLearn();
        for (int pos = 0; pos + 512 <= mix.getNumSamples(); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, mix, ch, pos, 512);
            p.processBlock (blk, midi);
        }
        p.stopLearn();
        e->showAssistant (true);
        juce::String err;
        p.startAssistant (err);
        // snapshot while it is working (once progress is under way)
        for (int t = 0; t < 300 && p.isAssistantBusy() && p.getAssistantProgress() < 0.3f; ++t) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        {
            auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("working.png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        for (int t = 0; t < 300 && p.isAssistantBusy(); ++t) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        {
            auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("applied.png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile ("assistant.png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
        os.flush();
        e->showAssistant (false);
        e->selectModule (1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        auto img2 = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os2 (dir.getChildFile ("after_assistant.png"));
        os2.setPosition (0); os2.truncate();
        juce::PNGImageFormat().writeImageToStream (img2, os2);
    }
    // Standalone window: editor hosted in a DocumentWindow the way JUCE's standalone wrapper does it
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        MasteringAudioProcessor sp;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        juce::DocumentWindow w ("Mastering Studio", juce::Colours::darkgrey, juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton);
        w.setContentOwned (sp.createEditor(), true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        std::cout << "standalone window: maximise button " << (w.getMaximiseButton() != nullptr ? "present" : "MISSING")
                  << ", minimise " << (w.getMinimiseButton() != nullptr ? "present" : "missing")
                  << ", close " << (w.getCloseButton() != nullptr ? "present" : "missing") << std::endl;
        auto img = w.createComponentSnapshot ({ 0, 0, w.getWidth(), 60 }, true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile ("window_titlebar.png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
        w.clearContentComponent();
    }
    // Learning indicator
    {
        p.startLearn();
        for (int pos = 0; pos + 512 <= 48000 * 3; pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, b, ch, pos, 512);
            p.processBlock (blk, midi);
        }
        e->selectModule (3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile ("learning.png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
        p.stopLearn();
    }
    // Assistant panel at the plugin's minimum and default sizes
    {
        e->showAssistant (true);
        for (auto [w, h] : std::initializer_list<std::pair<int, int>> { { 1200, 720 }, { 1320, 800 } })
        {
            e->setSize (w, h);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
            auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("assistant_" + juce::String (w) + "x" + juce::String (h) + ".png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        e->showAssistant (false);
    }
    std::cout << "snapshots written to " << dir.getFullPathName() << std::endl;
    ed.reset();
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    // Tuning aid for the tape model's solver (MS_SUB = max sub-steps, MS_Q = step size in q)
    if (auto v = juce::SystemStats::getEnvironmentVariable ("MS_SUB", {}); v.isNotEmpty()) msdsp::TapeModel::MaxSubsteps = v.getIntValue();
    if (auto v = juce::SystemStats::getEnvironmentVariable ("MS_Q", {}); v.isNotEmpty()) msdsp::TapeModel::SubstepQ = v.getDoubleValue();
    if (argc > 2 && juce::String (argv[1]) == "--snapshots") return snapshots (juce::File (juce::String (argv[2])));
    if (argc > 1 && juce::String (argv[1]) == "--layout") { layoutTests(); eqLabelTests(); moduleViewTests(); return failures == 0 ? 0 : 1; }
    if (argc > 1 && juce::String (argv[1]) == "--programs") { programReportingTests(); return failures == 0 ? 0 : 1; }
    if (argc > 1 && juce::String (argv[1]) == "--alloc") { allocationTests(); return failures == 0 ? 0 : 1; }
    if (argc > 1 && juce::String (argv[1]) == "--tpcal") { lab::calibrate(); return 0; }
    if (argc > 2 && juce::String (argv[1]) == "--alias") { lab::aliasProbe (juce::String (argv[2])); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--lab")
    {
        juce::String md;
        lab::run (md, true);
        if (argc > 2) { juce::File (juce::String (argv[2])).replaceWithText (md); std::cout << "\nreport written to " << argv[2] << std::endl; }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--bench")
    {
        for (double fs : { 48000.0, 96000.0 })
        {
            MasteringAudioProcessor p;
            p.setCurrentProgram (2);
            auto in = makeMusicLike (fs, 30.0, -14.0f, 5);
            p.setPlayConfigDetails (2, 2, fs, 256); p.prepareToPlay (fs, 256);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            juce::MidiBuffer midi; juce::AudioBuffer<float> blk (2, 256);
            for (int pos = 0; pos + 256 <= in.getNumSamples(); pos += 256)
            {
                for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, in, ch, pos, 256);
                p.processBlock (blk, midi);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::cout << (int) fs << " Hz: 30 s of audio in " << juce::String (ms, 0) << " ms = " << juce::String (100.0 * ms / 30000.0, 2)
                      << " % of one CPU core" << std::endl;
        }
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "--recall") { recallTests(); return failures == 0 ? 0 : 1; }
    if (argc > 1 && juce::String (argv[1]) == "--upgrades") { upgradeTests(); return failures == 0 ? 0 : 1; }
    if (argc > 1 && juce::String (argv[1]) == "--spur") { spurProbe(); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--rate") { rateProbe(); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--tapecpu") { tapeCpuProbe(); return 0; }
    if (argc > 1 && juce::String (argv[1]) == "--clicks") { auto c = clickScan (argc > 2); std::cout << c.size() << " clicking change(s)" << std::endl; return 0; }

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        std::cout << "\n== Sample rate " << fs << " ==" << std::endl;

        // 1) Loudness meter: 1 kHz stereo sine at -20 dBFS reads -20 LUFS
        {
            msdsp::LoudnessMeter m; m.prepare (fs);
            const int n = (int) (fs * 5);
            std::vector<float> l ((size_t) n), r ((size_t) n);
            for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / fs);
            m.process (l.data(), r.data(), n);
            const float I = m.integrated.load();
            check (std::abs (I + 20.0f) < 0.1f, "1 kHz -20 dBFS sine reads " + juce::String (I, 2) + " LUFS (expect -20.0)");
            check (std::abs (m.truePeak.load() + 20.0f) < 0.1f, "true peak of that sine " + juce::String (m.truePeak.load(), 2) + " dBTP");
        }

        // 2) Neutral chain (limiter off) is transparent apart from latency
        {
            MasterChain c; c.prepare (fs);
            ChainSettings s; s.lim.on = false; s.eq.on = true;
            auto in = makeMusicLike (fs, 2.0, -12.0f);
            auto out = in;
            processInBlocks (c, out, s, 480);
            const int L = c.getLatency();
            double err = 0.0, ref = 0.0;
            for (int i = L + 2000; i < in.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double d = out.getSample (ch, i) - in.getSample (ch, i - L);
                    err += d * d; ref += (double) in.getSample (ch, i - L) * in.getSample (ch, i - L);
                }
            const double nullDb = 10.0 * std::log10 (err / ref + 1e-30);
            check (nullDb < -60.0, "neutral chain nulls against delayed input: " + juce::String (nullDb, 1) + " dB (latency " + juce::String (L) + ")");
        }

        // 3) Limiter holds the ceiling (true peak) with 12 dB of drive
        {
            MasterChain c; c.prepare (fs);
            ChainSettings s; s.lim = { true, 12.0f, -1.0f, 80.0f, true };
            auto b = makeMusicLike (fs, 6.0, -10.0f, 7);
            processInBlocks (c, b, s, 512);
            const float tp = maxTruePeakDb (b, c.getLatency());
            check (tp <= -0.85f, "limiter true peak " + juce::String (tp, 2) + " dBTP with -1.0 ceiling");
            check (b.getMagnitude (0, b.getNumSamples()) <= msdsp::dbToGain (-1.0f) + 1e-6f, "sample peak never exceeds ceiling");
        }

        // 4) Full chain with everything on: finite, ceiling held, odd block sizes
        {
            MasterChain c; c.prepare (fs);
            ChainSettings s;
            s.eq.hpOn = true; s.eq.lpOn = true; s.eq.pg[1] = 4.0f; s.eq.lsGain = 3.0f; s.eq.mode = 1;
            s.mb.on = true; for (auto& bs : s.mb.b) { bs.thr = -30; bs.ratio = 4; bs.gain = 2; }
            s.bus.on = true; s.bus.thr = -25; s.bus.ratio = 4; s.bus.makeup = 6;
            s.sat.on = true; s.sat.drive = 18; s.sat.type = 1;
            s.img.on = true; s.img.wLow = 0; s.img.wHigh = 200;
            s.lim = { true, 10.0f, -1.0f, 50.0f, true };
            s.dither = { 2, true };
            auto b = makeMusicLike (fs, 4.0, -6.0f, 3);
            juce::AudioBuffer<float> tmp (2, 1024);
            juce::Random r (5);
            for (int pos = 0; pos < b.getNumSamples();)
            {
                const int n = std::min (1 + r.nextInt (1023), b.getNumSamples() - pos);
                tmp.setSize (2, n, false, false, true);
                for (int ch = 0; ch < 2; ++ch) tmp.copyFrom (ch, 0, b, ch, pos, n);
                if (pos > b.getNumSamples() / 2) { s.eq.pf[1] = 3000; s.mb.xLow = 300; s.img.xHigh = 2000; s.sat.type = 2; }
                c.process (tmp, s);
                for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, pos, tmp, ch, 0, n);
                pos += n;
            }
            check (allFinite (b), "everything-on chain output is finite (random block sizes, live changes)");
            check (b.getMagnitude (0, b.getNumSamples()) <= msdsp::dbToGain (-1.0f) + 2.0f / 32768.0f, "everything-on chain stays under ceiling (incl. dither)");
            const int idx = b.getNumSamples() / 2;
            const float q = b.getSample (0, idx) * 32768.0f;
            check (std::abs (q - std::round (q)) < 1e-3f, "16-bit dither output lands on the 16-bit grid");
        }

        // 5) Multiband split sums flat when bands are bypassed
        {
            msdsp::MultibandComp mb; mb.prepare (fs);
            msdsp::MbSettings s; s.on = true; for (auto& bs : s.b) bs.bypass = true;
            const int n = (int) fs;
            juce::AudioBuffer<float> b (2, n);
            b.clear(); b.setSample (0, 100, 1.0f); b.setSample (1, 100, 1.0f);
            mb.process (b, 0, n, s);
            // energy of an allpass response equals the impulse energy
            double e = 0.0; for (int i = 0; i < n; ++i) e += (double) b.getSample (0, i) * b.getSample (0, i);
            check (std::abs (10.0 * std::log10 (e)) < 0.05, "multiband (bypassed bands) is all-pass: energy " + juce::String (10.0 * std::log10 (e), 3) + " dB");
        }
    }

    // 6) Processor: every factory preset renders cleanly and the limiter holds the ceiling
    {
        std::cout << "\n== Processor / presets ==" << std::endl;
        MasteringAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < MasteringAudioProcessor::getNumFactoryPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            auto b = makeMusicLike (48000.0, 4.0, -14.0f, (uint32_t) (11 + i));
            juce::AudioBuffer<float> blk (2, 512);
            float peak = 0.0f; bool finite = true;
            for (int pos = 0; pos + 512 <= b.getNumSamples(); pos += 512)
            {
                for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, b, ch, pos, 512);
                p.processBlock (blk, midi);
                finite &= allFinite (blk);
                if (pos > 48000) peak = std::max (peak, blk.getMagnitude (0, 512));
            }
            const float ceil = p.makeSettings().lim.ceiling;
            check (finite && msdsp::gainToDb (peak) <= ceil + 0.01f,
                   "preset '" + p.getProgramName (i) + "': peak " + juce::String (msdsp::gainToDb (peak), 2) + " dBFS, short-term "
                   + juce::String (p.meters.outLoud.shortTerm.load(), 1) + " LUFS");
        }

        // state round trip
        p.setCurrentProgram (2);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        const float before = p.makeSettings().lim.gain;
        p.setCurrentProgram (0);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (p.makeSettings().lim.gain - before) < 1e-4f && p.currentPresetName == p.getProgramName (2), "state save/restore round trip");

        // A/B
        p.setCurrentProgram (1);
        p.selectAB (1); p.setCurrentProgram (2);
        const float bGain = p.makeSettings().lim.gain;
        p.selectAB (0);
        const float aGain = p.makeSettings().lim.gain;
        p.selectAB (1);
        check (std::abs (p.makeSettings().lim.gain - bGain) < 1e-4f && std::abs (aGain - bGain) > 0.1f, "A/B slots keep separate settings");
        p.releaseResources();
    }

    // 7) File player and offline export round trip
    {
        std::cout << "\n== File player / export ==" << std::endl;
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MasteringStudioTest");
        dir.deleteRecursively(); dir.createDirectory();
        const auto src = dir.getChildFile ("mix.wav");
        const double fs = 44100.0;
        auto mix = makeMusicLike (fs, 6.0, -14.0f, 21);
        juce::AudioBuffer<float> mono (1, mix.getNumSamples());
        mono.copyFrom (0, 0, mix, 0, 0, mix.getNumSamples());
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (src), fs, 1, 24, {}, 0));
            w->writeFromAudioSampleBuffer (mono, 0, mono.getNumSamples());
        }

        MasteringAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        check (p.loadFile (src), "player loads a mono 24-bit WAV");
        p.transport.start();
        juce::Thread::sleep (300); // let the read-ahead thread buffer
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> blk (2, 512);
        float peak = 0.0f; bool same = true;
        for (int k = 0; k < 200; ++k)
        {
            blk.clear();
            p.processBlock (blk, midi);
            peak = std::max (peak, blk.getMagnitude (0, 512));
            for (int i = 0; i < 512; ++i) same &= blk.getSample (0, i) == blk.getSample (1, i);
        }
        check (peak > 0.01f && same, "playback replaces input with the file (resampled 44.1k -> 48k), mono on both channels");
        p.ejectFile();

        // Neutral render: output must line up sample-for-sample with the source (latency trimmed)
        p.setCurrentProgram (0);
        p.apvts.getParameter (ID::limOn)->setValueNotifyingHost (0.0f);
        const auto neutral = dir.getChildFile ("neutral.wav");
        auto r1 = p.renderFile (src, neutral, false, 32, {});
        check (r1.error.isEmpty() && ! r1.cancelled, "neutral export ran: " + (r1.error.isEmpty() ? juce::String ("ok") : r1.error));
        {
            std::unique_ptr<juce::AudioFormatReader> rd (p.formatManager.createReaderFor (neutral));
            juce::AudioBuffer<float> back (2, (int) rd->lengthInSamples);
            rd->read (&back, 0, back.getNumSamples(), 0, true, true);
            float maxDiff = 0.0f;
            for (int i = 0; i < back.getNumSamples(); ++i) maxDiff = std::max (maxDiff, std::abs (back.getSample (1, i) - mono.getSample (0, i)));
            check (rd->lengthInSamples == mono.getNumSamples() && maxDiff < 1.0e-4f,
                   "neutral export is the same length and sample-aligned (max diff " + juce::String (maxDiff, 7) + ")");
        }

        // Loud preset to 16-bit FLAC
        p.setCurrentProgram (2);
        const auto loud = dir.getChildFile ("loud.flac");
        auto r2 = p.renderFile (src, loud, true, 16, {});
        {
            std::unique_ptr<juce::AudioFormatReader> rd (p.formatManager.createReaderFor (loud));
            check (rd != nullptr && rd->bitsPerSample == 16 && rd->lengthInSamples == mono.getNumSamples(), "FLAC 16-bit export readable, correct length");
            juce::AudioBuffer<float> back (2, (int) rd->lengthInSamples);
            rd->read (&back, 0, back.getNumSamples(), 0, true, true);
            const float tp = maxTruePeakDb (back);
            check (tp <= -0.8f, "exported master true peak " + juce::String (tp, 2) + " dBTP (ceiling -1)");
            check (r2.integrated > -16.0f && r2.integrated < -6.0f, "export reports loudness " + juce::String (r2.integrated, 1) + " LUFS");
        }
        int calls = 0;
        auto r3 = p.renderFile (src, dir.getChildFile ("cancel.wav"), false, 24, [&calls] (double) { return ++calls < 3; });
        check (r3.cancelled && ! dir.getChildFile ("cancel.wav").exists(), "cancelled export leaves no file");
        p.releaseResources();
        dir.deleteRecursively();
    }

    assistantTests();
    upgradeTests();
    recallTests();
    programReportingTests();
    layoutTests();
    eqLabelTests();
    moduleViewTests();
    allocationTests();

    {
        std::cout << "\n== Measurements (THD, aliasing, true peak, latency, CPU) ==" << std::endl;
        juce::String md;
        const auto r = lab::run (md, false);
        check (r.cleanThd < -120.0f, "clean path adds no distortion: THD " + juce::String (r.cleanThd, 1) + " dB");
        check (r.latencyError == 0, "latency reported to the host matches the measured delay exactly (44.1/48/96 kHz)");
        // Measured 7 % on a quiet machine (every module on, 8x saturation); the margin absorbs load from other programs.
        check (r.cpu48 < 15.0f, "whole plugin uses " + juce::String (r.cpu48, 2) + " % of one core at 48 kHz");
        check (r.worstTruePeak <= -0.95f, "realistic masters stay under the -1 dBTP ceiling with 6-18 dB of limiting: "
                                          + juce::String (r.worstTruePeak, 2) + " dBTP (exact reconstruction)");
        check (r.stressTruePeak <= -0.3f, "stress test (noise flat to 24 kHz) stays within 0.7 dB of the ceiling: " + juce::String (r.stressTruePeak, 2) + " dBTP");
        for (auto& [name, db] : r.aliasing)
        {
            float limit = 0;
            if (name.startsWith ("Saturation")) limit = name.contains ("drive 6") ? -75.0f : -40.0f;
            else if (name.startsWith ("Limiter")) limit = -65.0f;
            else continue;
            check (db <= limit, "audible aliasing (below 15 kHz), " + name + ": " + juce::String (db, 1) + " dB (limit " + juce::String ((int) limit) + ")");
        }
    }
    {
        std::cout << "\n== Click-free controls ==" << std::endl;
        const auto clicks = clickScan (false);
        check (clicks.empty(), "every parameter jump (full range, every switch and choice) is click-free: "
                               + juce::String ((int) clicks.size()) + " clicking change(s)");
    }

    // Standalone window gets a maximise button and icon; a plugin instance leaves its host window alone
    {
        std::cout << "\n== Standalone window ==" << std::endl;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        auto sp = std::make_unique<MasteringAudioProcessor>();
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        juce::DocumentWindow w ("Mastering Studio", juce::Colours::darkgrey, juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton);
        w.setContentOwned (sp->createEditor(), true);
        check (w.getMaximiseButton() != nullptr && w.getMinimiseButton() != nullptr && w.getCloseButton() != nullptr,
               "standalone window has minimise, maximise and close buttons");
        check (versionText() == juce::String ("v") + MS_VERSION && versionText() != "v?" && w.getName() == productName() + " " + MS_VERSION && productName() == "Open Mastering Studio",
               "version shown on screen (" + versionText() + ") and in the window title (\"" + w.getName() + "\") match the build version " + MS_VERSION + " and the product name");
        w.clearContentComponent();
        {
            const auto about = MasteringAudioProcessorEditor::aboutText();
            check (about.contains (versionText()) && about.contains ("GNU Affero General Public License") && about.contains ("WITHOUT ANY WARRANTY")
                   && about.contains ("source code") && about.contains ("OpenMasteringStudio-<version>-source.zip")
                   && about.contains ("Copyright (C) 2026 Al Oomens") && about.startsWith ("Open Mastering Studio")
                   && about.contains ("openstudio.plugins@gmail.com or GitHub Discussions"),
                   "About box (click the title) shows name, version, copyright, AGPLv3 license, no-warranty notice, where the source is and the contact");
            check (sp->getName() == "Open Mastering Studio", "the DAW sees the plugin as \"" + sp->getName() + "\"");
        }
        // Top bar: at the smallest and the default window size no control overlaps another or the title
        for (auto [width, height] : { std::pair<int, int> { 1200, 720 }, { 1320, 800 } })
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (sp->createEditor());
            ed->setSize (width, height);
            std::vector<juce::Rectangle<int>> boxes;
            for (auto* c : ed->getChildren())
                if (c->isVisible() && c->getY() < 48 && c->getBottom() <= 48) boxes.push_back (c->getBounds());
            bool ok = boxes.size() >= 10;
            for (size_t a = 0; a < boxes.size(); ++a)
            {
                ok &= ! boxes[a].intersects (MasteringAudioProcessorEditor::titleArea()) && boxes[a].getRight() <= width;
                for (size_t b = a + 1; b < boxes.size(); ++b) ok &= ! boxes[a].intersects (boxes[b]);
            }
            check (ok, "top bar at " + juce::String (width) + "x" + juce::String (height) + ": " + juce::String ((int) boxes.size())
                       + " controls, none overlapping each other or the \"OPEN MASTERING STUDIO\" title (" + juce::String (MasteringAudioProcessorEditor::titleArea().getWidth()) + " px)");
        }

        MasteringAudioProcessor plug; // wrapperType undefined, like a plugin host
        juce::DocumentWindow host ("Host", juce::Colours::darkgrey, juce::DocumentWindow::closeButton);
        host.setContentOwned (plug.createEditor(), true);
        check (host.getMaximiseButton() == nullptr, "plugin editor does not change its host's window buttons");
        host.clearContentComponent();
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
