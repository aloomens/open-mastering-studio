// Plugin measurements in the spirit of THE_LAB: harmonic distortion, worst-case aliasing,
// true-peak output, latency (reported vs measured) and CPU cost per module.
// Run:  MasteringTest.exe --lab [report.md]
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "LabMeasurements.h"

namespace lab
{
namespace
{
constexpr double Fs = 48000.0;
constexpr int Order = 16, N = 1 << Order;     // 65536-point analysis
constexpr int Guard = 6;                       // bins either side of a tone (Blackman-Harris main lobe is +/-4)

double coherent (double f) { return std::round (f * N / Fs) * Fs / N; }

// Renders a steady sine through the chain and returns the analysed (post-latency, settled) segment.
std::vector<float> renderSine (const ChainSettings& s, double freq, float levelDb)
{
    MasterChain c;
    c.prepare (Fs);
    c.configure (s);
    const int settle = (int) (0.6 * Fs), total = settle + c.getLatency() + N;
    juce::AudioBuffer<float> buf (2, total);
    const float a = msdsp::dbToGain (levelDb);
    for (int i = 0; i < total; ++i)
    {
        const float v = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / Fs);
        buf.setSample (0, i, v); buf.setSample (1, i, v);
    }
    juce::AudioBuffer<float> blk (2, 512);
    for (int pos = 0; pos < total; pos += 512)
    {
        const int n = std::min (512, total - pos);
        blk.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, buf, ch, pos, n);
        c.process (blk, s);
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, pos, blk, ch, 0, n);
    }
    const float* p = buf.getReadPointer (0, settle + c.getLatency());
    return std::vector<float> (p, p + N);
}

// Power spectrum with a 4-term Blackman-Harris window.
std::vector<double> powerSpectrum (const std::vector<float>& x)
{
    static juce::dsp::FFT fft (Order);
    std::vector<float> d ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * i / (N - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        d[(size_t) i] = (float) (x[(size_t) i] * w);
    }
    fft.performFrequencyOnlyForwardTransform (d.data());
    std::vector<double> p ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) p[(size_t) k] = (double) d[(size_t) k] * d[(size_t) k];
    return p;
}

struct ToneAnalysis { double thdDb = -200, aliasDb = -200, worstAliasDb = -200, worstAliasHz = 0, audibleAliasDb = -200, audibleAliasHz = 0; };

// Splits the spectrum into fundamental, harmonics (n*f below Nyquist) and everything else
// (aliased products + noise). Aliasing is what remains outside DC, the fundamental and its harmonics.
ToneAnalysis analyse (const std::vector<float>& x, double freq)
{
    const auto p = powerSpectrum (x);
    const int k0 = (int) std::lround (freq * N / Fs);
    std::vector<char> used (p.size(), 0);
    auto band = [&] (int k) { double e = 0; for (int j = std::max (0, k - Guard); j <= std::min ((int) p.size() - 1, k + Guard); ++j) { e += p[(size_t) j]; used[(size_t) j] = 1; } return e; };
    for (int j = 0; j <= Guard * 2; ++j) used[(size_t) j] = 1; // DC
    const double fund = band (k0);
    double harm = 0;
    for (int h = 2; h * k0 + Guard < N / 2; ++h) harm += band (h * k0);
    double rest = 0, worst = 0, audible = 0; int worstK = 0, audibleK = 0;
    const size_t k15 = (size_t) (15000.0 * N / Fs);
    for (size_t k = 0; k < p.size(); ++k)
        if (! used[k])
        {
            rest += p[k];
            if (p[k] > worst) { worst = p[k]; worstK = (int) k; }
            if (k < k15 && p[k] > audible) { audible = p[k]; audibleK = (int) k; }
        }
    double fundPeak = 0;
    for (int j = k0 - Guard; j <= k0 + Guard; ++j) fundPeak = std::max (fundPeak, p[(size_t) j]);
    ToneAnalysis a;
    a.thdDb = 10.0 * std::log10 (harm / fund + 1e-30);
    a.aliasDb = 10.0 * std::log10 (rest / fund + 1e-30);
    a.worstAliasDb = 10.0 * std::log10 (worst / fundPeak + 1e-30);
    a.worstAliasHz = worstK * Fs / N;
    a.audibleAliasDb = 10.0 * std::log10 (audible / fundPeak + 1e-30);
    a.audibleAliasHz = audibleK * Fs / N;
    return a;
}

// Independent true-peak estimate: 16x oversampling with JUCE's high-quality FIR (not the limiter's own detector).
float truePeak16x (const juce::AudioBuffer<float>& b, int from)
{
    juce::dsp::Oversampling<float> os (2, 4, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
    os.initProcessing (4096);
    float peak = 0;
    juce::AudioBuffer<float> blk (2, 4096);
    for (int pos = from; pos < b.getNumSamples(); pos += 4096)
    {
        const int n = std::min (4096, b.getNumSamples() - pos);
        blk.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, b, ch, pos, n);
        juce::dsp::AudioBlock<float> ab (blk);
        auto up = os.processSamplesUp (ab);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            for (size_t i = 0; i < up.getNumSamples(); ++i) peak = std::max (peak, std::abs (up.getSample ((int) ch, (int) i)));
    }
    return msdsp::gainToDb (peak);
}

// Ground truth: near-ideal band-limited reconstruction (Kaiser-windowed sinc, 256 taps, beta 10,
// 16 points between samples) evaluated around every sample within 3 dB of the loudest one.
float exactTruePeak (const juce::AudioBuffer<float>& b, int from)
{
    constexpr int Half = 128, Ph = 16;
    auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    static std::vector<float> table = [&]
    {
        std::vector<float> t ((size_t) Ph * 2 * Half);
        for (int p = 0; p < Ph; ++p)
            for (int k = 0; k < 2 * Half; ++k)
            {
                const double x = (double) (k - Half + 1) - (double) p / Ph; // sample offset relative to the interpolated point
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double u = x / (Half + 0.5);
                t[(size_t) (p * 2 * Half + k)] = (float) (std::abs (u) < 1 ? sinc * bessel0 (10.0 * std::sqrt (1 - u * u)) / bessel0 (10.0) : 0.0);
            }
        return t;
    }();
    float maxS = 0;
    for (int ch = 0; ch < 2; ++ch) maxS = std::max (maxS, b.getMagnitude (ch, from, b.getNumSamples() - from));
    const float thr = maxS * 0.708f;
    double peak = maxS;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* x = b.getReadPointer (ch);
        for (int n = std::max (from, Half); n < b.getNumSamples() - Half - 1; ++n)
        {
            if (std::max (std::abs (x[n]), std::abs (x[n + 1])) < thr) continue;
            for (int p = 1; p < Ph; ++p)              // point at n + p/Ph
            {
                const float* c = &table[(size_t) (p * 2 * Half)];
                double acc = 0;
                for (int k = 0; k < 2 * Half; ++k) acc += (double) c[k] * x[n - Half + 1 + k];
                peak = std::max (peak, std::abs (acc));
            }
        }
    }
    return msdsp::gainToDb ((float) peak);
}

} // namespace

// Accuracy of the meters themselves: sines of known amplitude (-1 dBFS) across the band.
void calibrate()
{
    std::cout << "freq     exact   detector(48t,8x)" << std::endl;
    for (double f : { 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0, 21000.0, 22000.0, 23000.0, 23500.0 })
    {
        juce::AudioBuffer<float> b (2, (int) Fs);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = msdsp::dbToGain (-1.0f) * (float) std::sin (2 * juce::MathConstants<double>::pi * f * i / Fs + 0.37);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        msdsp::TruePeakDetector d; float pk = 0;
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = d.push (b.getSample (0, i), b.getSample (1, i)); if (i > 2000) pk = std::max (pk, v); }
        std::cout << juce::String (f, 0).paddedRight (' ', 8) << juce::String (exactTruePeak (b, 1000), 3).paddedRight (' ', 8)
                  << juce::String (msdsp::gainToDb (pk), 3) << std::endl;
    }

    // Bright bursts through the limiter: where do the overs come from?
    juce::AudioBuffer<float> bright (2, (int) (10 * Fs));
    juce::Random r (17);
    for (int i = 0; i < bright.getNumSamples(); ++i)
    {
        const float env = (i % 12000 < 300) ? 4.0f : 1.0f;
        for (int ch = 0; ch < 2; ++ch) bright.setSample (ch, i, 0.05f * env * (r.nextFloat() * 2 - 1));
    }
    for (bool guard : { false, true })
    {
        msdsp::Limiter lim; msdsp::TruePeakGuard g2; lim.prepare (Fs); g2.prepare (Fs, 0.5);
        juce::AudioBuffer<float> b (bright);
        int clips = 0;
        for (int pos = 0; pos < b.getNumSamples(); pos += 512)
        {
            const int n = std::min (512, b.getNumSamples() - pos);
            lim.process (b, pos, n, { true, 18.0f, -1.0f, 80.0f, true });
            if (guard) g2.process (b, pos, n, { true, 0.0f, -1.0f, 20.0f, true });
            for (int ch = 0; ch < 2; ++ch) for (int i = pos; i < pos + n; ++i) if (std::abs (b.getSample (ch, i)) >= msdsp::dbToGain (-1.0f) * 0.99999f) ++clips;
        }
        auto readWith = [&b] (auto det) { float pk = 0; for (int i = 0; i < b.getNumSamples(); ++i) { const float v = det.push (b.getSample (0, i), b.getSample (1, i)); if (i > 9600) pk = std::max (pk, v); } return msdsp::gainToDb (pk); };
        std::cout << (guard ? "with guard:    " : "main limiter:  ") << "exact " << juce::String (exactTruePeak (b, 9600), 2)
                  << "  detector48 " << juce::String (readWith (msdsp::TruePeakDetectorT<48, 60>()), 2)
                  << "  64/7 " << juce::String (readWith (msdsp::TruePeakDetectorT<64, 70>()), 2)
                  << "  96/8 " << juce::String (readWith (msdsp::TruePeakDetectorT<96, 80>()), 2)
                  << "  128/9 " << juce::String (readWith (msdsp::TruePeakDetectorT<128, 90>()), 2)
                  << "  samples at ceiling " << clips << std::endl;
    }
}

namespace
{
juce::AudioBuffer<float> processWhole (const ChainSettings& s, const juce::AudioBuffer<float>& in, int& latency)
{
    MasterChain c;
    c.prepare (Fs);
    c.configure (s);
    latency = c.getLatency();
    juce::AudioBuffer<float> out (2, in.getNumSamples() + latency), blk (2, 512);
    out.clear();
    for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, in, ch, 0, in.getNumSamples());
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        const int n = std::min (512, out.getNumSamples() - pos);
        blk.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, out, ch, pos, n);
        c.process (blk, s);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    return out;
}

juce::AudioBuffer<float> musicNoise (double seconds, float levelDb, uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const int n = (int) (seconds * Fs);
    juce::AudioBuffer<float> b (2, n);
    float lp[2] {};
    for (int i = 0; i < n; ++i)
    {
        const float env = (i % 24000 < 600) ? 3.0f : 1.0f; // transients
        for (int ch = 0; ch < 2; ++ch)
        {
            lp[ch] += 0.15f * ((r.nextFloat() * 2 - 1) - lp[ch]);
            b.setSample (ch, i, env * (lp[ch] + 0.15f * (r.nextFloat() * 2 - 1)));
        }
    }
    msdsp::LoudnessMeter m; m.prepare (Fs);
    m.process (b.getReadPointer (0), b.getReadPointer (1), n);
    b.applyGain (msdsp::dbToGain (levelDb - m.integrated.load()));
    return b;
}

// Realistic program material: pink noise (Kellet filter) with the natural top-end roll-off of real
// mixes (gentle from ~12 kHz, steep above ~19 kHz), kick-like low bursts and snare-like broadband hits.
juce::AudioBuffer<float> realisticMaster (double seconds, uint32_t seed)
{
    juce::Random r ((juce::int64) seed);
    const int n = (int) (seconds * Fs);
    juce::AudioBuffer<float> b (2, n);
    for (int ch = 0; ch < 2; ++ch)
    {
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        msdsp::Svf shelf, lp1, lp2;
        shelf.c = msdsp::makeSvf (msdsp::FilterType::HighShelf, Fs, 12000, 0.7071, -4.0);
        const auto& q = msdsp::butterworthQs (4);
        lp1.c = msdsp::makeSvf (msdsp::FilterType::LowPass, Fs, 19000, q[0], 0);
        lp2.c = msdsp::makeSvf (msdsp::FilterType::LowPass, Fs, 19000, q[1], 0);
        double kick = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = r.nextFloat() * 2 - 1;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
            b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            const double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11; b6 = w * 0.115926;
            const int beat = i % 24000;
            if (beat == 0) kick = 0;
            const double kickV = beat < 6000 ? 0.9 * std::exp (-beat / 2500.0) * std::sin (2 * juce::MathConstants<double>::pi * (55.0 + 80.0 * std::exp (-beat / 600.0)) * beat / Fs) : 0.0;
            const double snare = (i + 12000) % 24000 < 2500 ? 0.6 * std::exp (-((i + 12000) % 24000) / 900.0) * w : 0.0;
            float v = (float) (pink + kickV + snare);
            v = shelf.process (0, v);
            v = lp2.process (0, lp1.process (0, v));
            b.setSample (ch, i, v);
        }
    }
    msdsp::LoudnessMeter m; m.prepare (Fs);
    m.process (b.getReadPointer (0), b.getReadPointer (1), n);
    b.applyGain (msdsp::dbToGain (-14.0f - m.integrated.load()));
    return b;
}

ChainSettings allOff()
{
    ChainSettings s;
    s.eq.on = false; s.mb.on = false; s.bus.on = false; s.sat.on = false; s.img.on = false; s.lim.on = false;
    s.dither = { 0, false };
    return s;
}

struct Config { juce::String name; ChainSettings s; };

std::vector<Config> configs()
{
    std::vector<Config> c;
    c.push_back ({ "Clean path (all modules off)", allOff() });
    const char* types[] { "Tape", "Tube", "Soft Clip", "Tape (Modeled)", "Tube (Modeled)" };
    for (int t = 0; t < 5; ++t)
        for (float drive : { 6.0f, 18.0f })
        {
            auto s = allOff(); s.sat = { true, t, drive, 100.0f };
            c.push_back ({ juce::String ("Saturation ") + types[t] + ", drive " + juce::String ((int) drive) + " dB", s });
        }
    { auto s = allOff(); s.bus = { true, -24.0f, 4.0f, 1.0f, 50.0f, 0.0f, 100.0f, 20.0f }; c.push_back ({ "Glue comp, fast (1 ms / 50 ms, 4:1)", s }); }
    { auto s = allOff(); s.bus = { true, -24.0f, 2.0f, 10.0f, 200.0f, 0.0f, 100.0f, 60.0f }; c.push_back ({ "Glue comp, typical (10 ms / 200 ms, 2:1)", s }); }
    { auto s = allOff(); s.mb.on = true; for (auto& b : s.mb.b) b = { -36.0f, 4.0f, 1.0f, 50.0f, 0.0f, false, false }; c.push_back ({ "Multiband, fast (all bands 4:1)", s }); }
    { auto s = allOff(); s.lim = { true, 6.0f, -1.0f, 100.0f, true }; c.push_back ({ "Limiter +6 dB, -1 dBTP", s }); }
    { auto s = allOff(); s.lim = { true, 12.0f, -1.0f, 50.0f, true }; c.push_back ({ "Limiter +12 dB, -1 dBTP, fast release", s }); }
    { auto s = allOff(); s.lim = { true, 18.0f, -1.0f, 20.0f, true }; c.push_back ({ "Limiter +18 dB, -1 dBTP, very fast release", s }); }
    { auto s = allOff(); s.lim = { true, 12.0f, -1.0f, 50.0f, true, msdsp::LimTransparent }; c.push_back ({ "Limiter Transparent +12 dB, -1 dBTP, fast release", s }); }
    { auto s = allOff(); s.lim = { true, 18.0f, -1.0f, 20.0f, true, msdsp::LimTransparent }; c.push_back ({ "Limiter Transparent +18 dB, -1 dBTP, very fast release", s }); }
    { auto s = allOff(); s.mb.on = true; s.mb.linear = true; for (auto& b : s.mb.b) b = { -36.0f, 4.0f, 1.0f, 50.0f, 0.0f, false, false }; c.push_back ({ "Multiband linear phase, fast (all bands 4:1)", s }); }
    {
        MasteringAudioProcessor p; p.setCurrentProgram (2);
        auto s = p.makeSettings(); s.dither = { 0, false };
        c.push_back ({ "Full chain: Loud & Punchy preset", s });
    }
    return c;
}

juce::String db1 (double v) { return v < -199 ? juce::String ("--") : juce::String (v, 1); }
} // namespace

//==============================================================================
Results run (juce::String& md, bool verbose)
{
    Results res;
    auto out = [&] (const juce::String& s) { md << s << "\n"; if (verbose) std::cout << s << std::endl; };
    const auto cfg = configs();

    out ("# Open Mastering Studio - plugin measurements\n");
    out ("Measured offline at 48 kHz on the processing chain (dither off). THD: harmonics 2 and up, relative to the fundamental. "
         "Aliasing: everything that is not DC, the tone or its harmonics (fold-back products plus noise), relative to the tone. "
         "Lower is better throughout.\n");

    // ---- THD vs level at 1 kHz ----
    out ("## Harmonic distortion (THD) at 1 kHz\n");
    const float levels[] { -24.0f, -12.0f, -6.0f, -1.0f };
    juce::String head = "| Configuration |", rule = "|---|";
    for (float l : levels) { head << " " << juce::String ((int) l) << " dBFS |"; rule << "---|"; }
    out (head); out (rule);
    const double f1k = coherent (1000.0);
    for (auto& c : cfg)
    {
        juce::String row = "| " + c.name + " |";
        for (float l : levels)
        {
            const auto a = analyse (renderSine (c.s, f1k, l), f1k);
            row << " " << db1 (a.thdDb) << " dB |";
            if (c.name.startsWith ("Clean")) res.cleanThd = std::max (res.cleanThd, (float) a.thdDb);
        }
        out (row);
    }
    out ("");

    // ---- Aliasing sweep ----
    out ("## Worst-case aliasing (sine sweep 2-19 kHz at -1 dBFS)\n");
    out ("Each tone is pushed through the module; the table shows the worst result over the sweep: total non-harmonic energy, "
         "the single largest fold-back product, and where it landed.\n");
    out ("| Configuration | Total aliasing + noise | Largest alias (anywhere) | Largest alias below 15 kHz |");
    out ("|---|---|---|---|");
    const double sweep[] { 2000, 5000, 8000, 11000, 14000, 17000, 19000 };
    for (auto& c : cfg)
    {
        ToneAnalysis worst, aud; double worstTone = 0, audTone = 0;
        for (double f : sweep)
        {
            const double fc = coherent (f);
            const auto a = analyse (renderSine (c.s, fc, -1.0f), fc);
            if (a.worstAliasDb > worst.worstAliasDb) { const double t = worst.aliasDb; worst = a; worst.aliasDb = std::max (t, a.aliasDb); worstTone = fc; }
            else worst.aliasDb = std::max (worst.aliasDb, a.aliasDb);
            if (a.audibleAliasDb > aud.audibleAliasDb) { aud = a; audTone = fc; }
        }
        auto where = [] (double db, double hz, double tone) { return db1 (db) + " dB at " + juce::String (hz / 1000.0, 2) + " kHz (" + juce::String (tone / 1000.0, 1) + " kHz tone)"; };
        out ("| " + c.name + " | " + db1 (worst.aliasDb) + " dB | " + where (worst.worstAliasDb, worst.worstAliasHz, worstTone) + " | "
             + where (aud.audibleAliasDb, aud.audibleAliasHz, audTone) + " |");
        res.aliasing.push_back ({ c.name, (float) aud.audibleAliasDb });
    }
    out ("");

    // ---- True peak ----
    out ("## True-peak output (limiter ceiling -1.0 dBTP)\n");
    out ("Measured independently of the limiter's own detector. *Exact* is near-ideal band-limited reconstruction "
         "(256-tap windowed sinc, 16 points between samples): the waveform a DAC or codec would actually rebuild. "
         "*JUCE 16x* is a general-purpose oversampler shown for comparison; its filters let some images through near Nyquist, "
         "so it can read high on bright material.\n");
    out ("| Input | True Peak on: exact | True Peak on: JUCE 16x | True Peak off: exact |");
    out ("|---|---|---|---|");
    {
        // fs/4 sine at 45 deg: samples sit at 0.707 of the waveform peak -> inter-sample peaks 3 dB above the samples
        juce::AudioBuffer<float> isp (2, (int) (3 * Fs));
        for (int i = 0; i < isp.getNumSamples(); ++i)
        {
            const float v = std::sin (juce::MathConstants<float>::pi * 0.5f * (float) i + juce::MathConstants<float>::pi * 0.25f) * 1.41f;
            isp.setSample (0, i, v); isp.setSample (1, i, v);
        }
        juce::AudioBuffer<float> bright (2, (int) (10 * Fs));
        {
            juce::Random r (17);
            for (int i = 0; i < bright.getNumSamples(); ++i)
            {
                const float env = (i % 12000 < 300) ? 4.0f : 1.0f;
                for (int ch = 0; ch < 2; ++ch) bright.setSample (ch, i, 0.05f * env * (r.nextFloat() * 2 - 1)); // white: full of near-Nyquist energy
            }
        }
        struct In { juce::String name; juce::AudioBuffer<float> b; float gain; };
        std::vector<In> inputs { { "Realistic master, +6 dB into limiter", realisticMaster (10.0, 7), 6.0f },
                                 { "Realistic master, +12 dB into limiter", realisticMaster (10.0, 7), 12.0f },
                                 { "Realistic master, +18 dB into limiter", realisticMaster (10.0, 7), 18.0f },
                                 { "Stress: noise with flat top end to 24 kHz, +12 dB", musicNoise (10.0, -14.0f, 3), 12.0f },
                                 { "Stress: white-noise bursts, +18 dB", bright, 18.0f },
                                 { "Extreme inter-sample-peak test (fs/4 sine, +3 dB ISP)", isp, 3.0f } };
        for (auto& in : inputs)
        {
            juce::String row = "| " + in.name + " |";
            for (bool tpOn : { true, false })
            {
                auto s = allOff(); s.lim = { true, in.gain, -1.0f, 80.0f, tpOn };
                int lat = 0;
                const auto o = processWhole (s, in.b, lat);
                const float exact = exactTruePeak (o, lat + (int) (0.2 * Fs));
                row << " " << juce::String (exact, 2) << " dBTP |";
                if (tpOn)
                {
                    row << " " << juce::String (truePeak16x (o, lat + (int) (0.2 * Fs)), 2) << " dBTP |";
                    if (! in.name.startsWith ("Stress")) res.worstTruePeak = std::max (res.worstTruePeak, exact);
                    else res.stressTruePeak = std::max (res.stressTruePeak, exact);
                }
            }
            out (row);
        }
    }
    out ("");

    // ---- Latency ----
    out ("## Latency: reported vs measured\n");
    out ("| Sample rate | Reported to host | Measured: impulse, all off | Measured: impulse, saturation wet path | Cross-correlation peak, Loud & Punchy |");
    out ("|---|---|---|---|---|");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        MasteringAudioProcessor p;
        p.setPlayConfigDetails (2, 2, fs, 512);
        p.prepareToPlay (fs, 512);
        const int reported = p.getLatencySamples();
        p.releaseResources();

        auto measure = [fs] (const ChainSettings& s, bool impulse)
        {
            MasterChain c; c.prepare (fs);
            const int n = (int) fs;
            juce::AudioBuffer<float> b (2, n); b.clear();
            juce::Random r (9);
            std::vector<float> src ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                src[(size_t) i] = impulse ? (i == 1000 ? 0.5f : 0.0f) : 0.03f * (r.nextFloat() * 2 - 1);
                b.setSample (0, i, src[(size_t) i]); b.setSample (1, i, src[(size_t) i]);
            }
            juce::AudioBuffer<float> blk (2, 512);
            for (int pos = 0; pos < n; pos += 512)
            {
                const int m = std::min (512, n - pos);
                blk.setSize (2, m, false, false, true);
                for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, b, ch, pos, m);
                c.process (blk, s);
                for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, pos, blk, ch, 0, m);
            }
            int best = 0; double bestV = -1;
            for (int lag = 0; lag < 2000; ++lag)
            {
                double acc = 0;
                if (impulse) acc = std::abs (b.getSample (0, 1000 + lag));
                else for (int i = 2000; i < n - 2000; i += 2) acc += (double) src[(size_t) i] * b.getSample (0, i + lag);
                if (acc > bestV) { bestV = acc; best = lag; }
            }
            return best;
        };
        MasteringAudioProcessor q; q.setCurrentProgram (2);
        auto loud = q.makeSettings(); loud.dither = { 0, false };
        auto satOnly = allOff(); satOnly.sat = { true, 0, 0.0f, 100.0f };   // wet path only (drive 0 dB = near-linear)
        const int mImp = measure (allOff(), true), mSat = measure (satOnly, true), mXc = measure (loud, false);
        out ("| " + juce::String ((int) fs) + " Hz | " + juce::String (reported) + " samples (" + juce::String (1000.0 * reported / fs, 2) + " ms) | "
             + juce::String (mImp) + " | " + juce::String (mSat) + " | " + juce::String (mXc) + " |");
        res.latencyError = std::max (res.latencyError, std::max (std::abs (mImp - reported), std::abs (mSat - reported)));
    }
    out ("");

    // ---- CPU ----
    out ("## CPU cost (single core, 256-sample blocks)\n");
    out ("| Module | 48 kHz | 96 kHz |");
    out ("|---|---|---|");
    {
        struct Row { juce::String name; double pct[2] {}; };
        std::vector<Row> rows { { "EQ (all bands active)" }, { "Multiband compressor" }, { "Glue compressor" }, { "Saturation (8x oversampled, while on)" },
                                { "Stereo imager" }, { "Limiter (true peak)" }, { "Loudness meters (in, and out with true peak)" },
                                { "Whole plugin (processBlock, Loud & Punchy)" } };
        int col = 0;
        for (double fs : { 48000.0, 96000.0 })
        {
            const double secs = 20.0;
            const int n = (int) (secs * fs);
            juce::AudioBuffer<float> src (2, n);
            juce::Random r (4);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) src.setSample (ch, i, 0.2f * (r.nextFloat() * 2 - 1));
            // Best of three runs: other programs on the machine can only make a run slower, never faster.
            auto timeIt = [&] (auto&& processChunk)
            {
                double best = 1.0e9;
                for (int run = 0; run < 3; ++run)
                {
                    juce::AudioBuffer<float> b (src);
                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int pos = 0; pos + 256 <= n; pos += 256) processChunk (b, pos, 256);
                    best = std::min (best, 100.0 * (juce::Time::getMillisecondCounterHiRes() - t0) / (secs * 1000.0));
                }
                return best;
            };
            msdsp::EqSettings eqs; eqs.hpOn = true; eqs.lpOn = true; eqs.lsGain = 2; eqs.pg[0] = -2; eqs.pg[1] = 1; eqs.pg[2] = 2; eqs.hsGain = 1;
            msdsp::MbSettings mbs; mbs.on = true;
            msdsp::BusSettings bus; bus.on = true;
            msdsp::SatSettings sat { true, 0, 6.0f, 100.0f };
            msdsp::ImgSettings img { true, 200, 4000, 50, 110, 120 };
            msdsp::LimSettings lim { true, 6.0f, -1.0f, 100.0f, true };
            { msdsp::MasteringEQ m; m.prepare (fs); rows[0].pct[col] = timeIt ([&] (auto& b, int p, int k) { m.process (b, p, k, eqs); }); }
            { msdsp::MultibandComp m; m.prepare (fs); rows[1].pct[col] = timeIt ([&] (auto& b, int p, int k) { m.process (b, p, k, mbs); }); }
            { msdsp::BusComp m; m.prepare (fs); rows[2].pct[col] = timeIt ([&] (auto& b, int p, int k) { m.process (b, p, k, bus); }); }
            { msdsp::Saturator m; m.prepare (fs, 512); rows[3].pct[col] = timeIt ([&] (auto& b, int p, int k) { m.process (b, p, k, sat); }); }
            { msdsp::Imager m; m.prepare (fs); rows[4].pct[col] = timeIt ([&] (auto& b, int p, int k) { m.process (b, p, k, img); }); }
            { msdsp::Limiter m; msdsp::TruePeakGuard g; m.prepare (fs); g.prepare (fs, 0.5);
              rows[5].pct[col] = timeIt ([&] (auto& b, int p, int k) { m.process (b, p, k, lim); g.process (b, p, k, { true, 0.0f, -1.0f, 20.0f, true }); }); }
            { msdsp::LoudnessMeter a, b2; a.measureTruePeak = false; a.prepare (fs); b2.prepare (fs);
              rows[6].pct[col] = timeIt ([&] (auto& b, int p, int k) { a.process (b.getReadPointer (0, p), b.getReadPointer (1, p), k); b2.process (b.getReadPointer (0, p), b.getReadPointer (1, p), k); }); }
            {
                MasteringAudioProcessor p; p.setCurrentProgram (2);
                p.setPlayConfigDetails (2, 2, fs, 256); p.prepareToPlay (fs, 256);
                juce::MidiBuffer midi; juce::AudioBuffer<float> blk (2, 256);
                rows[7].pct[col] = timeIt ([&] (auto& b, int pos, int k) { for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, b, ch, pos, k); p.processBlock (blk, midi); });
                if (col == 0) res.cpu48 = (float) rows[7].pct[0];
            }
            ++col;
        }
        for (auto& r : rows) out ("| " + r.name + " | " + juce::String (r.pct[0], 2) + " % | " + juce::String (r.pct[1], 2) + " % |");
    }
    out ("\nThe limiter row includes the second true-peak stage. Switched-off modules keep running in the background (so switching them on "
         "never clicks), except saturation, which rests after half a second off. The whole-plugin figure uses Loud & Punchy, which has saturation on.");
    return res;
}
void aliasProbe (const juce::String& filter)
{
    const double sweep[] { 2000, 5000, 8000, 11000, 14000, 17000, 19000 };
    for (auto& c : configs())
    {
        if (! c.name.contains (filter)) continue;
        ToneAnalysis aud; double audTone = 0;
        for (double f : sweep)
        {
            const double fc = coherent (f);
            const auto a = analyse (renderSine (c.s, fc, -1.0f), fc);
            if (a.audibleAliasDb > aud.audibleAliasDb) { aud = a; audTone = fc; }
        }
        std::cout << c.name << ": audible alias " << juce::String (aud.audibleAliasDb, 1) << " dB at " << juce::String (aud.audibleAliasHz, 0)
                  << " Hz (tone " << juce::String (audTone, 0) << ")" << std::endl;
    }
}} // namespace lab
