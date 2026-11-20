#include "Assistant.h"
#include "Params.h"

namespace assistant
{
namespace
{
constexpr int NormLo = 10, NormHi = 22; // 200 Hz .. 4 kHz: reference region for spectral normalisation

float weightOf (int b)
{
    if (b <= 1 || b >= 30) return 0.0f;   // 20, 25 Hz and 20 kHz: too variable (codecs, subsonics)
    if (b == 2 || b == 29) return 0.5f;   // 31.5 Hz, 16 kHz
    return 1.0f;
}

void normalise (Spectrum& s)
{
    float m = 0.0f;
    for (int b = NormLo; b <= NormHi; ++b) m += s[(size_t) b];
    m /= (float) (NormHi - NormLo + 1);
    for (auto& v : s) v -= m;
}

float percentile (std::vector<float> v, float p)
{
    if (v.empty()) return -100.0f;
    const size_t k = (size_t) juce::jlimit (0.0, (double) v.size() - 1.0, std::floor (p * (double) (v.size() - 1)));
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

float round1 (float v) { return std::round (v * 10.0f) / 10.0f; }

juce::String region (float f)
{
    if (f < 60) return "sub-bass";
    if (f < 160) return "bass";
    if (f < 450) return "low-mids";
    if (f < 1600) return "mids";
    if (f < 5000) return "upper mids / presence";
    if (f < 10000) return "highs";
    return "air";
}

juce::String hzText (float f) { return ParamText::hz (f, 0); }
} // namespace

//==============================================================================
const std::array<float, NumBands>& bandCentres()
{
    static const std::array<float, NumBands> c = []
    {
        std::array<float, NumBands> a {};
        for (int i = 0; i < NumBands; ++i) a[(size_t) i] = 1000.0f * std::pow (2.0f, (float) (i - 17) / 3.0f);
        return a;
    }();
    return c;
}

juce::String bandLabel (int b)
{
    static const char* labels[NumBands] { "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160", "200", "250", "315", "400",
                                          "500", "630", "800", "1k", "1.25k", "1.6k", "2k", "2.5k", "3.15k", "4k", "5k", "6.3k",
                                          "8k", "10k", "12.5k", "16k", "20k" };
    return labels[juce::jlimit (0, NumBands - 1, b)];
}

juce::StringArray styleNames() { return { "Dynamic (-16 LUFS)", "Balanced (-14 LUFS)", "Loud (-10 LUFS)" }; }
float styleLufs (Style s) { return s == Style::Dynamic ? -16.0f : s == Style::Balanced ? -14.0f : -10.0f; }

//==============================================================================
// Generic target curves
//
// Base shape: mean long-term average spectrum of 12,345 popular-music tracks, Elowsson & Friberg,
// "Long-term Average Spectrum in Popular Music and its Relation to the Level of the Percussion",
// AES Convention Paper 9762 (2017), Table 1: slope -2.35, -3.67, -4.99, -6.30, -7.62, -8.94 dB/oct
// at 200 Hz ... 6.4 kHz. Those are per-FFT-bin (narrowband) slopes; a 1/3-octave band collects
// bandwidth proportional to frequency, so band levels are +3.01 dB/oct higher. The slope is linear
// in log-frequency (x = log2 (f / 1 kHz)): slope(x) = -2.40 - 1.32 x, i.e. level(x) = -2.40 x - 0.66 x^2.
//
// Percussion: in the same paper, more percussive tracks have more bass and more highs, pivoting so
// that the 89 Hz-4.5 kHz slope is unchanged; genre differences are "primarily a side-effect" of
// this. Modelled as a bowl equal to 1 at 89 Hz and 4.5 kHz, scaled by CurveInfo::percussion (dB).
//
// Genre tilt: Pestana, Ma, Reiss, Barbosa & Black, "Spectral Characteristics of Popular Commercial
// Recordings 1950-2010", AES 135th Convention (2013), Table 2, spectral slope (log-log, 100 Hz-10 kHz):
// pop -0.9433, rock -0.9793, electronic -0.7461, hip-hop -0.8141, jazz -1.2929, folk -1.1824,
// R&B -1.0336, soul -1.0303 -> x 6.02 dB/oct relative to pop. Only 60 % is used, because those
// genre averages also carry recording era (older material is duller; the authors note hip-hop's
// brightness and bass partly reflect post-2000 production), and the percussion bowl already
// supplies part of the difference.
namespace
{
const CurveInfo curveTable[NumCurves] = {
    //  name, description,                                                                    perc, slope, sub, gentle, lowcut, slowAtt, sat, tube, lowSide, monoHz, measured
    { "Acoustic / Folk", "Natural low end, little sub-bass, softer top: guitar, piano, strings, folk",
      -2.5f, -1.44f, -2.0f, true,  22.0f, true,  true,  true,  -20.0f, 120.0f, true },
    { "Singer-Songwriter", "Voice-led songs with light percussion: clear mids, moderate bass",
      -1.0f, -0.70f, -1.0f, false, 0.0f,  false, true,  false, -20.0f, 150.0f, true },
    { "Reference tracks", "Average of the reference tracks you add",
      0.0f, 0.0f, 0.0f, false, 0.0f, false, true, false, -20.0f, 150.0f, true },
    { "Pop", "Average modern popular music",
      0.0f, 0.0f, 0.0f, false, 0.0f, false, true, false, -20.0f, 150.0f, true },
    { "Rock", "Band music: guitars and drums, slightly darker than pop",
      0.5f, -0.22f, -1.0f, false, 0.0f, true, true, false, -20.0f, 150.0f, true },
    { "Hip-Hop / Trap", "Beat-driven: extended sub-bass, bright top, very mono low end",
      3.0f, 0.78f, 3.0f, false, 0.0f, false, true, false, -26.0f, 150.0f, true },
    { "R&B / Soul", "Smooth and warm: lighter low end than hip-hop, softer top than pop",
      0.5f, -0.54f, -1.0f, false, 0.0f, false, true, true, -22.0f, 150.0f, true },
    { "Electronic / EDM", "Club and electronic: deep bass, wide bright top, mono low end",
      3.0f, 1.18f, 2.0f, false, 0.0f, false, true, false, -26.0f, 150.0f, true },
    { "Jazz", "Acoustic ensembles: natural decay of the top end, light bottom, wide dynamics",
      -2.0f, -2.10f, -2.0f, true, 22.0f, true, true, true, -18.0f, 120.0f, true },
    { "Classical / Orchestral", "Extrapolated (no genre data): least percussion, natural stereo bass, no saturation",
      -4.0f, -2.10f, -2.0f, true, 18.0f, true, false, false, -12.0f, 100.0f, false },
};
} // namespace

const CurveInfo& curveInfo (Curve c) { return curveTable[juce::jlimit (0, NumCurves - 1, (int) c)]; }

std::vector<Curve> curveMenuOrder()
{
    return { Curve::Pop, Curve::Rock, Curve::HipHop, Curve::RnB, Curve::Electronic,
             Curve::Vocal, Curve::Acoustic, Curve::Jazz, Curve::Classical };
}

namespace
{
// The genre tilt is applied from ~250 Hz upward (the published slope differences are driven by the
// top end; Pestana et al. report *lighter* low end for the darker genres, so a 1 kHz pivot that
// raises the bass would contradict them). Low-end differences come from percussion and subBass.
float tiltShape (float x) { return juce::jlimit (-2.0f, 3.5f, x); }

// Regression slope (100 Hz-10 kHz, 1/3-octave points) of a curve shape, used to calibrate the tilt
// so the finished curve's slope difference matches the target.
template <typename Fn> float regressionSlope (Fn fn)
{
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
    for (int b = 7; b <= 27; ++b) // 100 Hz .. 10 kHz
    {
        const double x = std::log2 (bandCentres()[(size_t) b] / 1000.0), y = fn ((float) x);
        sx += x; sy += y; sxx += x * x; sxy += x * y; ++n;
    }
    return (float) ((n * sxy - sx * sy) / (n * sxx - sx * sx));
}

float bowlShape (float x) { const float u = (x + 0.66f) / 2.83f; return std::min (1.8f, u * u); } // 1 at 89 Hz and 4.5 kHz
} // namespace

Spectrum targetCurve (Curve c)
{
    const auto& info = curveInfo (c);
    static const float tiltSlope = regressionSlope (tiltShape), bowlSlope = regressionSlope (bowlShape);
    const float subSlope = regressionSlope ([&info] (float x) { const float f = 1000.0f * std::exp2 (x); return info.subBass / (1.0f + (f / 50.0f) * (f / 50.0f)); });
    // Total slope difference vs pop = 60 % of the published genre difference; the bowl and sub-bass
    // terms contribute part of it, the tilt supplies the rest.
    const float tilt = (0.6f * info.genreSlope - info.percussion * bowlSlope - subSlope) / tiltSlope;
    Spectrum s {};
    for (int b = 0; b < NumBands; ++b)
    {
        const float f = bandCentres()[(size_t) b];
        const float x = std::log2 (f / 1000.0f);
        const float base = -2.40f * x - 0.66f * x * x;
        const float sub = info.subBass / (1.0f + (f / 50.0f) * (f / 50.0f));
        s[(size_t) b] = base + info.percussion * bowlShape (x) + tilt * tiltShape (x) + sub;
    }
    normalise (s);
    return s;
}

float spectralDistance (const Spectrum& a, const Spectrum& b)
{
    double e = 0.0, w = 0.0;
    for (int i = 0; i < NumBands; ++i) { const float d = a[(size_t) i] - b[(size_t) i]; e += weightOf (i) * d * d; w += weightOf (i); }
    return (float) std::sqrt (e / w);
}

//==============================================================================
juce::ValueTree Profile::toTree() const
{
    juce::ValueTree t ("Reference");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("file", filePath, nullptr);
    t.setProperty ("sr", sampleRate, nullptr);
    t.setProperty ("seconds", seconds, nullptr);
    t.setProperty ("I", integrated, nullptr);
    t.setProperty ("lra", lra, nullptr);
    t.setProperty ("tp", truePeak, nullptr);
    t.setProperty ("plr", plr, nullptr);
    juce::StringArray sp, misc;
    for (float v : spectrum) sp.add (juce::String (v, 2));
    for (int b = 0; b < 3; ++b)
        for (float v : { bandP50[b], bandP90[b], bandP98[b], sideRatio[b], correlation[b] }) misc.add (juce::String (v, 3));
    misc.add (juce::String (fullP50, 3)); misc.add (juce::String (fullP90, 3)); misc.add (juce::String (crest, 3));
    t.setProperty ("spectrum", sp.joinIntoString (" "), nullptr);
    t.setProperty ("stats", misc.joinIntoString (" "), nullptr);
    return t;
}

Profile Profile::fromTree (const juce::ValueTree& t)
{
    Profile p;
    p.name = t.getProperty ("name").toString();
    p.filePath = t.getProperty ("file").toString();
    p.sampleRate = t.getProperty ("sr", 0.0);
    p.seconds = t.getProperty ("seconds", 0.0);
    p.integrated = t.getProperty ("I", -100.0f);
    p.lra = t.getProperty ("lra", 0.0f);
    p.truePeak = t.getProperty ("tp", -100.0f);
    p.plr = t.getProperty ("plr", 0.0f);
    auto sp = juce::StringArray::fromTokens (t.getProperty ("spectrum").toString(), " ", "");
    auto misc = juce::StringArray::fromTokens (t.getProperty ("stats").toString(), " ", "");
    if (sp.size() != NumBands || misc.size() != 18) { p.seconds = 0; return p; }
    for (int b = 0; b < NumBands; ++b) p.spectrum[(size_t) b] = sp[b].getFloatValue();
    for (int b = 0; b < 3; ++b)
    {
        p.bandP50[b] = misc[b * 5].getFloatValue(); p.bandP90[b] = misc[b * 5 + 1].getFloatValue();
        p.bandP98[b] = misc[b * 5 + 2].getFloatValue(); p.sideRatio[b] = misc[b * 5 + 3].getFloatValue();
        p.correlation[b] = misc[b * 5 + 4].getFloatValue();
    }
    p.fullP50 = misc[15].getFloatValue(); p.fullP90 = misc[16].getFloatValue(); p.crest = misc[17].getFloatValue();
    return p;
}

//==============================================================================
bool readAudio (juce::AudioFormatManager& fm, const juce::File& f, juce::AudioBuffer<float>& out, double& fs, double maxSeconds, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = "Can't read " + f.getFileName() + " - unsupported or damaged file."; return false; }
    fs = r->sampleRate;
    const auto len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (maxSeconds * fs));
    if (len < (int) (5.0 * fs)) { error = f.getFileName() + " is shorter than 5 seconds."; return false; }
    out.setSize (2, len);
    r->read (&out, 0, len, 0, true, true);
    if (r->numChannels == 1) out.copyFrom (1, 0, out, 0, 0, len);
    return true;
}

Spectrum averageSpectrum (const juce::AudioBuffer<float>& a, double fs, const Progress& progress)
{
    const int order = fs > 60000.0 ? 15 : 14;
    const int N = 1 << order, hop = N / 2;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    const double df = fs / N;

    // Bin -> band weights by overlap of each bin's frequency interval with the band edges.
    std::vector<std::vector<std::pair<int, float>>> map (NumBands);
    for (int b = 0; b < NumBands; ++b)
    {
        const double fc = bandCentres()[(size_t) b], lo = fc * std::pow (2.0, -1.0 / 6.0), hi = std::min (fc * std::pow (2.0, 1.0 / 6.0), fs * 0.5);
        for (int k = std::max (1, (int) (lo / df) - 1); k <= std::min (N / 2, (int) (hi / df) + 1); ++k)
        {
            const double o = std::min (hi, (k + 0.5) * df) - std::max (lo, (k - 0.5) * df);
            if (o > 0) map[(size_t) b].push_back ({ k, (float) (o / df) });
        }
    }

    const int len = a.getNumSamples();
    std::vector<float> buf ((size_t) N * 2);
    std::vector<std::array<double, NumBands>> frames;
    std::vector<double> totals;
    for (int start = 0; start == 0 || start + N <= len; start += hop)
    {
        std::array<double, NumBands> bands {};
        std::vector<double> power ((size_t) N / 2 + 1, 0.0);
        for (int ch = 0; ch < std::min (2, a.getNumChannels()); ++ch)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const int n = std::min (N, len - start);
            std::copy (a.getReadPointer (ch, start), a.getReadPointer (ch, start) + n, buf.begin());
            win.multiplyWithWindowingTable (buf.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = 0; k <= N / 2; ++k) power[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
        }
        double total = 0.0;
        for (int b = 0; b < NumBands; ++b)
        {
            for (auto [k, w] : map[(size_t) b]) bands[(size_t) b] += power[(size_t) k] * w;
            total += bands[(size_t) b];
        }
        frames.push_back (bands);
        totals.push_back (total);
        if (progress && (frames.size() % 64) == 0 && ! progress ((double) start / len)) return {};
        if (start + N >= len) break;
    }

    // Ignore quiet frames (intros, gaps): keep frames within 20 dB of the mean frame power.
    double mean = 0.0;
    for (double t : totals) mean += t;
    mean /= (double) std::max<size_t> (1, totals.size());
    std::array<double, NumBands> acc {};
    int used = 0;
    for (size_t i = 0; i < frames.size(); ++i)
        if (totals[i] > mean * 0.01)
        {
            for (int b = 0; b < NumBands; ++b) acc[(size_t) b] += frames[i][(size_t) b];
            ++used;
        }
    Spectrum s {};
    for (int b = 0; b < NumBands; ++b) s[(size_t) b] = (float) (10.0 * std::log10 (acc[(size_t) b] / std::max (1, used) + 1e-20));
    normalise (s);
    return s;
}

Profile analyse (const juce::AudioBuffer<float>& a, double fs, const Progress& progress)
{
    Profile p;
    p.sampleRate = fs;
    p.seconds = a.getNumSamples() / fs;
    const int len = a.getNumSamples();
    const float* L = a.getReadPointer (0);
    const float* R = a.getReadPointer (std::min (1, a.getNumChannels() - 1));

    msdsp::LoudnessMeter meter;
    meter.prepare (fs);
    for (int i = 0; i < len; i += 4096) meter.process (L + i, R + i, std::min (4096, len - i));
    p.integrated = meter.integrated.load();
    p.lra = meter.lra.load();
    p.truePeak = meter.truePeak.load();
    p.plr = p.truePeak - p.integrated;
    if (progress && ! progress (0.15)) return p;

    // 100 ms block statistics per band
    msdsp::ThreeBandSplit split;
    split.prepare (fs);
    split.setFreqs (150.0f, 4000.0f);
    const int blk = (int) (0.1 * fs);
    std::vector<float> full, band[3], crests;
    double eM[3] {}, eS[3] {}, eLR[3] {}, eLL[3] {}, eRR[3] {};
    struct Block { float full; float band[3]; double m[3], s[3], lr[3], ll[3], rr[3]; float crest; };
    std::vector<Block> blocks;
    for (int start = 0; start + blk <= len; start += blk)
    {
        Block bk {};
        double ef = 0.0, bandE[3] {};
        float pk = 0.0f;
        for (int i = start; i < start + blk; ++i)
        {
            float l[3], r[3];
            split.split (0, L[i], l[0], l[1], l[2]);
            split.split (1, R[i], r[0], r[1], r[2]);
            ef += (double) L[i] * L[i] + (double) R[i] * R[i];
            pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
            for (int b = 0; b < 3; ++b)
            {
                bandE[b] += (double) l[b] * l[b] + (double) r[b] * r[b];
                const double m = 0.5 * (l[b] + r[b]), s = 0.5 * (l[b] - r[b]);
                bk.m[b] += m * m; bk.s[b] += s * s; bk.lr[b] += (double) l[b] * r[b];
                bk.ll[b] += (double) l[b] * l[b]; bk.rr[b] += (double) r[b] * r[b];
            }
        }
        const double msFull = ef / (2.0 * blk);
        bk.full = (float) (10.0 * std::log10 (msFull + 1e-12) + 3.0103);
        for (int b = 0; b < 3; ++b) bk.band[b] = (float) (10.0 * std::log10 (bandE[b] / (2.0 * blk) + 1e-12) + 3.0103);
        bk.crest = msdsp::gainToDb (pk) - (float) (10.0 * std::log10 (msFull + 1e-12));
        blocks.push_back (bk);
    }
    std::vector<float> allFull;
    for (auto& bk : blocks) allFull.push_back (bk.full);
    const float gate = std::max (-70.0f, percentile (allFull, 0.95f) - 30.0f);
    for (auto& bk : blocks)
    {
        if (bk.full < gate) continue;
        full.push_back (bk.full);
        crests.push_back (bk.crest);
        for (int b = 0; b < 3; ++b)
        {
            band[b].push_back (bk.band[b]);
            eM[b] += bk.m[b]; eS[b] += bk.s[b]; eLR[b] += bk.lr[b]; eLL[b] += bk.ll[b]; eRR[b] += bk.rr[b];
        }
    }
    p.fullP50 = percentile (full, 0.5f);
    p.fullP90 = percentile (full, 0.9f);
    p.crest = percentile (crests, 0.5f);
    for (int b = 0; b < 3; ++b)
    {
        p.bandP50[b] = percentile (band[b], 0.5f);
        p.bandP90[b] = percentile (band[b], 0.9f);
        p.bandP98[b] = percentile (band[b], 0.98f);
        p.sideRatio[b] = (float) (10.0 * std::log10 ((eS[b] + 1e-20) / (eM[b] + 1e-20)));
        const double den = std::sqrt (eLL[b] * eRR[b]);
        p.correlation[b] = den > 1e-20 ? (float) (eLR[b] / den) : 1.0f;
    }
    if (progress && ! progress (0.35)) return p;

    p.spectrum = averageSpectrum (a, fs, progress ? Progress ([&progress] (double f) { return progress (0.35 + 0.65 * f); }) : Progress());
    return p;
}

//==============================================================================
namespace
{
struct StyleSpec { float lufs, minPlr, maxLimGr, glueGr, limRelease; };
StyleSpec styleSpec (Style s)
{
    switch (s)
    {
        case Style::Dynamic:  return { -16.0f, 11.0f, 0.8f, 1.0f, 200.0f };
        case Style::Balanced: return { -14.0f,  9.0f, 1.6f, 1.5f, 120.0f };
        default:              return { -10.0f,  7.0f, 3.0f, 2.5f,  70.0f };
    }
}

// Response of the EQ's tone sections at the band centres, in dB.
struct EqSection { msdsp::FilterType type; float f, g, q; };

float sectionDb (const EqSection& s, double fs, float f)
{
    return msdsp::gainToDb ((float) msdsp::svfMagnitude (s.type, fs, s.f, s.q, s.g, f));
}

// Fits low shelf, three bells and high shelf to the wanted correction (coordinate descent).
std::array<EqSection, 5> fitEq (const Spectrum& want, const std::array<float, NumBands>& w, double fs, float maxGain)
{
    using FT = msdsp::FilterType;
    std::array<EqSection, 5> sec { EqSection { FT::LowShelf, 100, 0, 0.7071f }, { FT::Bell, 250, 0, 0.9f }, { FT::Bell, 1000, 0, 0.9f },
                                   { FT::Bell, 4000, 0, 0.9f }, { FT::HighShelf, 10000, 0, 0.7071f } };
    struct Range { float lo, hi; int steps; std::vector<float> qs; };
    const Range ranges[5] { { 60, 160, 6, { 0.7071f } }, { 80, 500, 14, { 0.6f, 0.9f, 1.4f } }, { 400, 2500, 14, { 0.6f, 0.9f, 1.4f } },
                            { 2000, 9000, 14, { 0.6f, 0.9f, 1.4f } }, { 5000, 14000, 6, { 0.7071f } } };
    constexpr float lambda = 0.6f; // prefers fewer, smaller moves

    std::array<std::array<float, NumBands>, 5> resp {};
    const auto& fc = bandCentres();
    for (int pass = 0; pass < 6; ++pass)
        for (int k = 0; k < 5; ++k)
        {
            std::array<float, NumBands> r {};
            for (int b = 0; b < NumBands; ++b)
            {
                float others = 0.0f;
                for (int j = 0; j < 5; ++j) if (j != k) others += resp[(size_t) j][(size_t) b];
                r[(size_t) b] = want[(size_t) b] - others;
            }
            float bestErr = 0.0f;
            for (int b = 0; b < NumBands; ++b) bestErr += w[(size_t) b] * r[(size_t) b] * r[(size_t) b];
            EqSection best = sec[(size_t) k]; best.g = 0.0f;
            const auto& rg = ranges[k];
            for (int i = 0; i < rg.steps; ++i)
            {
                const float f = rg.lo * std::pow (rg.hi / rg.lo, rg.steps > 1 ? (float) i / (float) (rg.steps - 1) : 0.0f);
                for (float q : rg.qs)
                {
                    EqSection c { sec[(size_t) k].type, f, 3.0f, q };
                    double num = 0.0, den = lambda;
                    for (int b = 0; b < NumBands; ++b)
                    {
                        const float s = sectionDb (c, fs, fc[(size_t) b]) / 3.0f;
                        num += w[(size_t) b] * r[(size_t) b] * s; den += w[(size_t) b] * s * s;
                    }
                    c.g = juce::jlimit (-maxGain, maxGain, (float) (num / den));
                    float err = lambda * c.g * c.g;
                    for (int b = 0; b < NumBands; ++b)
                    {
                        const float d = r[(size_t) b] - sectionDb (c, fs, fc[(size_t) b]);
                        err += w[(size_t) b] * d * d;
                    }
                    if (err < bestErr) { bestErr = err; best = c; }
                }
            }
            sec[(size_t) k] = best;
            for (int b = 0; b < NumBands; ++b) resp[(size_t) k][(size_t) b] = sectionDb (best, fs, fc[(size_t) b]);
        }
    for (auto& s : sec) { s.g = round1 (s.g); if (std::abs (s.g) < 0.3f) s.g = 0.0f; }
    return sec;
}

struct RenderStats { float integrated = -100, truePeak = -100, limGr = 0, glueGr = 0; };

RenderStats render (const juce::AudioBuffer<float>& in, double fs, const ChainSettings& s, juce::AudioBuffer<float>* keep, const Progress& progress)
{
    MasterChain chain;
    chain.prepare (fs);
    chain.configure (s);
    chain.lim.stats.enabled = true;
    chain.bus.stats.enabled = true;
    msdsp::LoudnessMeter meter;
    meter.prepare (fs);
    const int latency = chain.getLatency(), len = in.getNumSamples();
    if (keep != nullptr) keep->setSize (2, len);
    juce::AudioBuffer<float> buf (2, 4096);
    int skip = latency, written = 0;
    for (int pos = 0; pos < len + latency; pos += 4096)
    {
        if (progress && ! progress ((double) pos / (len + latency))) return {};
        const int n = std::min (4096, len + latency - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        if (pos < len)
            for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), pos, std::min (n, len - pos));
        chain.process (buf, s);
        const int off = std::min (skip, n);
        skip -= off;
        const int m = std::min (n - off, len - written);
        if (m > 0)
        {
            meter.process (buf.getReadPointer (0, off), buf.getReadPointer (1, off), m);
            if (keep != nullptr) for (int ch = 0; ch < 2; ++ch) keep->copyFrom (ch, written, buf, ch, off, m);
            written += m;
        }
    }
    return { meter.integrated.load(), meter.truePeak.load(), chain.lim.stats.mean(), chain.bus.stats.mean() };
}

// For long tracks, eight 6-second slices spread through the song (cross-faded) stand in for the whole.
juce::AudioBuffer<float> makeExcerpt (const juce::AudioBuffer<float>& a, double fs)
{
    const int len = a.getNumSamples();
    if (len <= (int) (90.0 * fs)) return a;
    const int seg = (int) (6.0 * fs), fade = (int) (0.02 * fs), count = 8;
    juce::AudioBuffer<float> out (2, count * (seg - fade) + fade);
    out.clear();
    int w = 0;
    for (int i = 0; i < count; ++i)
    {
        const int start = juce::jlimit (0, len - seg, (int) ((i + 0.5) * len / count) - seg / 2);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = a.getReadPointer (std::min (ch, a.getNumChannels() - 1), start);
            float* dst = out.getWritePointer (ch, w);
            for (int k = 0; k < seg; ++k)
            {
                float g = 1.0f;
                if (i > 0 && k < fade) g = (float) k / fade;
                if (i < count - 1 && k >= seg - fade) g = (float) (seg - k) / fade;
                dst[k] += src[k] * g;
            }
        }
        w += seg - fade;
    }
    return out;
}
} // namespace

//==============================================================================
Result run (const juce::AudioBuffer<float>& mix, double fs, const Profile& mp, const Request& req, const ChainSettings& current, const Progress& progress)
{
    Result res;
    auto step = [&progress, &res] (double from, double span)
    {
        return Progress ([&progress, &res, from, span] (double f)
        {
            if (progress && ! progress (from + span * f)) { res.cancelled = true; return false; }
            return true;
        });
    };
    if (! mp.isValid()) { res.error = "Not enough audio to analyze (need at least 5 seconds of music)."; return res; }

    // ---- Target ----
    const bool useRef = req.curve == Curve::Reference;
    Spectrum target {};
    float refLufs = 0, refPlr = 0, refSide[3] {};
    std::vector<const Profile*> refs;
    for (auto& r : req.references) if (r.isValid()) refs.push_back (&r);
    if (useRef)
    {
        if (refs.empty()) { res.error = "Add at least one reference track, or pick a generic target curve."; return res; }
        for (auto* r : refs)
        {
            for (int b = 0; b < NumBands; ++b) target[(size_t) b] += r->spectrum[(size_t) b] / (float) refs.size();
            refLufs += r->integrated / (float) refs.size();
            refPlr += r->plr / (float) refs.size();
            for (int b = 0; b < 3; ++b) refSide[b] += r->sideRatio[b] / (float) refs.size();
        }
        normalise (target);
    }
    else target = targetCurve (req.curve);

    const auto st = styleSpec (req.style);
    float targetLufs = st.lufs, minPlr = st.minPlr;
    if (useRef && req.useReferenceLoudness)
    {
        targetLufs = refLufs;
        minPlr = std::min (st.minPlr, refPlr);
        res.notes.add ("Loudness target taken from the reference" + juce::String (refs.size() > 1 ? "s" : "") + ": "
                       + juce::String (refLufs, 1) + " LUFS (peak-to-loudness " + juce::String (refPlr, 1) + " dB).");
    }
    targetLufs = juce::jlimit (-24.0f, -6.0f, targetLufs);
    const auto& ci = curveInfo (req.curve);
    const bool gentle = ci.gentle || req.style == Style::Dynamic || (useRef && refPlr > 10.5f);
    const float amount = juce::jlimit (0.0f, 1.0f, req.amount);

    // ---- Start from a clean chain (keep output trim and dither) ----
    ChainSettings s = current;
    s.eq = {}; s.mb = {}; s.bus = {}; s.sat = {}; s.img = {}; s.lim = {};
    s.inGain = round1 (juce::jlimit (-12.0f, 12.0f, -18.0f - mp.integrated));
    if (std::abs (s.inGain) >= 1.0f)
        res.notes.add ("Input gain " + ParamText::db (s.inGain, 0) + " so the mix enters the chain around -18 LUFS (it measured "
                       + juce::String (mp.integrated, 1) + " LUFS).");
    const float shift = s.inGain;

    // ---- Tone ----
    res.mixSpectrum = mp.spectrum;
    res.targetSpectrum = target;
    res.distanceBefore = spectralDistance (mp.spectrum, target);
    std::array<float, NumBands> w {};
    Spectrum want {};
    for (int b = 0; b < NumBands; ++b) { w[(size_t) b] = weightOf (b); want[(size_t) b] = target[(size_t) b] - mp.spectrum[(size_t) b]; }
    {
        double m = 0, ws = 0;
        for (int b = 3; b <= 28; ++b) { m += want[(size_t) b] * w[(size_t) b]; ws += w[(size_t) b]; }
        for (auto& v : want) v -= (float) (m / ws);
        Spectrum sm = want;
        for (int b = 1; b < NumBands - 1; ++b) sm[(size_t) b] = 0.25f * want[(size_t) b - 1] + 0.5f * want[(size_t) b] + 0.25f * want[(size_t) b + 1];
        for (int b = 0; b < NumBands; ++b) want[(size_t) b] = juce::jlimit (-6.0f, 6.0f, sm[(size_t) b]) * amount;
    }

    // Subsonic / rumble: the low cut handles excess below ~40 Hz better than a shelf.
    {
        int hpBand = -1;
        for (int b = 2; b <= 5; ++b) if (want[(size_t) b] < -3.0f * amount && want[(size_t) b] < -1.5f) hpBand = b;
        if (hpBand >= 0)
        {
            s.eq.hpOn = true;
            s.eq.hpFreq = juce::jlimit (22.0f, 45.0f, bandCentres()[(size_t) hpBand]);
            s.eq.hpSlope = 1;
            for (int b = 0; b <= hpBand; ++b) w[(size_t) b] = 0.0f;
            res.notes.add ("Low cut at " + hzText (s.eq.hpFreq) + ": the mix has more sub-bass/rumble than the target.");
        }
        else if (ci.lowCutHz > 0.0f && ! useRef)
        {
            s.eq.hpOn = true; s.eq.hpFreq = ci.lowCutHz; s.eq.hpSlope = 1; // inaudible subsonic cleanup for acoustic material
        }
    }

    const auto sec = fitEq (want, w, fs, 3.0f);
    s.eq.lsFreq = sec[0].f; s.eq.lsGain = sec[0].g;
    for (int k = 0; k < 3; ++k) { s.eq.pf[k] = sec[(size_t) k + 1].f; s.eq.pg[k] = sec[(size_t) k + 1].g; s.eq.pq[k] = sec[(size_t) k + 1].q; }
    s.eq.hsFreq = sec[4].f; s.eq.hsGain = sec[4].g;
    for (int k = 0; k < 5; ++k)
    {
        const auto& e = sec[(size_t) k];
        if (e.g == 0.0f) continue;
        const juce::String what = k == 0 ? "Low shelf" : k == 4 ? "High shelf" : "Bell";
        res.notes.add (what + " " + ParamText::db (e.g, 0) + " at " + hzText (e.f) + ": " + (e.g > 0 ? "lift the " : "tame the ")
                       + region (e.f) + (e.g > 0 ? " (thinner than the target)." : " (heavier than the target)."));
    }
    if (std::all_of (sec.begin(), sec.end(), [] (const EqSection& e) { return e.g == 0.0f; }))
        res.notes.add ("Tonal balance already close to the target - EQ left flat.");
    auto eqDbAt = [&sec, fs] (float f) { float d = 0; for (auto& e : sec) d += sectionDb (e, fs, f); return d; };

    // ---- Multiband: only where a band's dynamics are unruly ----
    const float lowSpread = mp.bandP98[0] - mp.bandP50[0], highSpread = mp.bandP98[2] - mp.bandP50[2];
    const bool boomy = lowSpread > 12.0f, harsh = highSpread > 14.0f && ! ci.gentle;
    if (boomy || harsh || req.style == Style::Loud)
    {
        s.mb.on = true; s.mb.xLow = 150.0f; s.mb.xHigh = 4000.0f;
        auto& lo = s.mb.b[0]; auto& md = s.mb.b[1]; auto& hi = s.mb.b[2];
        lo = { mp.bandP90[0] + shift + eqDbAt (90.0f) + (boomy ? 0.0f : 2.0f), gentle ? 1.8f : 2.5f, 30.0f, 200.0f, 0.0f, false, false };
        md = { mp.bandP98[1] + shift + eqDbAt (1000.0f) + 2.0f, 1.5f, 15.0f, 150.0f, 0.0f, false, false };
        hi = { (harsh ? mp.bandP90[2] : mp.bandP98[2] + 2.0f) + shift + eqDbAt (6000.0f), 2.0f, 3.0f, 80.0f, 0.0f, false, false };
        for (auto* b : { &lo, &md, &hi }) b->thr = round1 (juce::jlimit (-60.0f, 0.0f, b->thr));
        if (boomy) res.notes.add ("Multiband: low band controls uneven bass notes (they vary by " + juce::String (juce::roundToInt (lowSpread)) + " dB).");
        if (harsh) res.notes.add ("Multiband: fast high band catches harsh peaks and sibilance.");
        if (! boomy && ! harsh) res.notes.add ("Multiband: light, peak-only control in each band to help density for a loud master.");
    }

    // ---- Glue compression ----
    s.bus.on = true;
    s.bus.ratio = gentle ? 1.5f : 2.0f;
    s.bus.att = (ci.slowAttack || mp.crest > 16.0f) ? 30.0f : 15.0f;
    s.bus.rel = gentle ? 300.0f : 150.0f;
    s.bus.mix = 100.0f; s.bus.hpf = 90.0f;
    s.bus.thr = round1 (juce::jlimit (-40.0f, 0.0f, mp.fullP90 + shift - st.glueGr / (1.0f - 1.0f / s.bus.ratio)));
    s.bus.makeup = st.glueGr;
    res.notes.add (juce::String ("Glue: ") + ParamText::ratio (s.bus.ratio, 0) + ", " + ParamText::ms (s.bus.att, 0) + " attack"
                   + (s.bus.att >= 30.0f ? " (lets transients through)" : "") + ", aiming for about " + juce::String (st.glueGr, 1) + " dB of reduction.");

    // ---- Saturation (loud style only) ----
    if (req.style == Style::Loud && ci.allowSaturation)
    {
        const bool tube = ci.tubeSaturation;
        s.sat = { true, tube ? 1 : 0, tube ? 3.0f : 4.5f, tube ? 25.0f : 35.0f };
        res.notes.add (juce::String ("Saturation: subtle ") + (tube ? "tube" : "tape") + " for density, so the limiter has less to do.");
    }

    // ---- Stereo ----
    {
        const float lowTarget = useRef ? refSide[0] : ci.lowSideTarget;
        if (mp.sideRatio[0] > lowTarget + 6.0f)
        {
            s.img.on = true; s.img.xLow = ci.bassMonoHz;
            s.img.wLow = std::round (juce::jlimit (0.0f, 100.0f, 100.0f * std::pow (10.0f, (lowTarget - mp.sideRatio[0]) / 20.0f)) / 5.0f) * 5.0f;
            res.notes.add ("Stereo: bass narrowed to " + juce::String ((int) s.img.wLow) + "% below " + hzText (s.img.xLow)
                           + " (wide bass, side at " + juce::String (juce::roundToInt (mp.sideRatio[0])) + " dB) - tighter low end, mono-safe.");
        }
        if (useRef)
            for (int b = 1; b < 3; ++b)
            {
                const float d = (refSide[b] - mp.sideRatio[b]) * amount;
                if (std::abs (d) > 1.5f && mp.correlation[b] > 0.2f)
                {
                    float& wv = b == 1 ? s.img.wMid : s.img.wHigh;
                    wv = std::round (juce::jlimit (75.0f, 135.0f, 100.0f * std::pow (10.0f, d / 20.0f)));
                    s.img.on = true;
                    res.notes.add (juce::String ("Stereo: ") + (b == 1 ? "mids" : "highs") + " width " + juce::String ((int) wv) + "% to match the reference.");
                }
            }
    }

    // ---- Limiter + render loop ----
    s.lim = { true, 0.0f, -1.0f, gentle ? st.limRelease * 1.5f : st.limRelease, true, current.lim.style };   // keeps your limiter style
    float tEff = std::min (targetLufs, s.lim.ceiling - minPlr);
    if (tEff < targetLufs - 0.05f)
        res.notes.add ("Loudness capped at " + juce::String (tEff, 1) + " LUFS to keep at least " + juce::String (minPlr, 1) + " dB between peaks and loudness.");
    s.lim.gain = juce::jlimit (0.0f, 18.0f, tEff + 18.0f - st.glueGr * 0.5f);

    const auto excerpt = makeExcerpt (mix, fs);
    const bool longTrack = excerpt.getNumSamples() != mix.getNumSamples();
    juce::AudioBuffer<float> out;
    RenderStats rs;
    bool capped = false;
    const int maxIter = 8;
    for (int it = 0; it < maxIter; ++it)
    {
        rs = render (excerpt, fs, s, &out, step (0.05 + 0.6 * it / maxIter, 0.6 / maxIter));
        if (res.cancelled) return res;
        bool settled = true;
        if (std::abs (rs.glueGr - st.glueGr) > 0.3f)
        {
            s.bus.thr = round1 (juce::jlimit (-40.0f, 0.0f, s.bus.thr + (rs.glueGr - st.glueGr) * 1.3f));
            settled = false;
        }
        if (rs.limGr > st.maxLimGr + 0.2f && rs.integrated - (rs.limGr - st.maxLimGr) < tEff)
        {
            tEff = rs.integrated - (rs.limGr - st.maxLimGr);
            capped = true;
        }
        const float d = tEff - rs.integrated;
        if (s.lim.gain <= 0.0f && d < -0.3f) { s.inGain = round1 (std::max (-24.0f, s.inGain + d)); settled = false; }
        else if (std::abs (d) > 0.25f) { s.lim.gain = round1 (juce::jlimit (0.0f, 18.0f, s.lim.gain + d)); settled = false; }
        if (settled) break;
    }
    if (longTrack)
    {
        for (int pass = 0; pass < 2; ++pass)
        {
            auto full = render (mix, fs, s, nullptr, step (0.7 + 0.15 * pass, 0.15));
            if (res.cancelled) return res;
            rs.integrated = full.integrated; rs.truePeak = full.truePeak; rs.limGr = full.limGr; rs.glueGr = full.glueGr;
            const float d = tEff - full.integrated;
            if (std::abs (d) <= 0.3f || s.lim.gain + d < 0.0f || s.lim.gain + d > 18.0f) break;
            s.lim.gain = round1 (s.lim.gain + d);
        }
    }
    if (capped)
        res.notes.add ("Loudness held at " + juce::String (tEff, 1) + " LUFS: going louder would need more than "
                       + juce::String (st.maxLimGr, 1) + " dB of average limiting for this style. Choose Loud to push further.");
    res.notes.add ("Limiter: +" + juce::String (s.lim.gain, 1) + " dB into a -1.0 dBTP true-peak ceiling, " + ParamText::ms (s.lim.release, 0) + " release.");

    res.settings = s;
    res.resultSpectrum = averageSpectrum (out, fs);
    res.distanceAfter = spectralDistance (res.resultSpectrum, target);
    res.targetLufs = tEff;
    res.integrated = rs.integrated; res.truePeak = rs.truePeak; res.avgLimiterGr = rs.limGr; res.avgGlueGr = rs.glueGr;
    res.summary = "Result: " + juce::String (rs.integrated, 1) + " LUFS, " + juce::String (rs.truePeak, 1) + " dBTP, limiter "
                + juce::String (rs.limGr, 1) + " dB / glue " + juce::String (rs.glueGr, 1) + " dB average reduction. Distance to target "
                + juce::String (res.distanceBefore, 1) + " -> " + juce::String (res.distanceAfter, 1) + " dB.";
    if (progress) progress (1.0);
    return res;
}

//==============================================================================
std::vector<std::pair<juce::String, float>> toParams (const ChainSettings& s)
{
    std::vector<std::pair<juce::String, float>> p {
        { ID::inGain, s.inGain },
        { ID::eqOn, s.eq.on ? 1.0f : 0.0f }, { ID::eqMode, (float) s.eq.mode }, { ID::hpOn, s.eq.hpOn ? 1.0f : 0.0f }, { ID::hpFreq, s.eq.hpFreq }, { ID::hpSlope, (float) s.eq.hpSlope },
        { ID::lsFreq, s.eq.lsFreq }, { ID::lsGain, s.eq.lsGain },
        { ID::p1Freq, s.eq.pf[0] }, { ID::p1Gain, s.eq.pg[0] }, { ID::p1Q, s.eq.pq[0] },
        { ID::p2Freq, s.eq.pf[1] }, { ID::p2Gain, s.eq.pg[1] }, { ID::p2Q, s.eq.pq[1] },
        { ID::p3Freq, s.eq.pf[2] }, { ID::p3Gain, s.eq.pg[2] }, { ID::p3Q, s.eq.pq[2] },
        { ID::hsFreq, s.eq.hsFreq }, { ID::hsGain, s.eq.hsGain }, { ID::lpOn, s.eq.lpOn ? 1.0f : 0.0f }, { ID::lpFreq, s.eq.lpFreq },
        { ID::mbOn, s.mb.on ? 1.0f : 0.0f }, { ID::mbXLow, s.mb.xLow }, { ID::mbXHigh, s.mb.xHigh },
        { ID::busOn, s.bus.on ? 1.0f : 0.0f }, { ID::busThr, s.bus.thr }, { ID::busRatio, s.bus.ratio }, { ID::busAtt, s.bus.att }, { ID::busRel, s.bus.rel },
        { ID::busMakeup, s.bus.makeup }, { ID::busMix, s.bus.mix }, { ID::busHpf, s.bus.hpf },
        { ID::satOn, s.sat.on ? 1.0f : 0.0f }, { ID::satType, (float) s.sat.type }, { ID::satDrive, s.sat.drive }, { ID::satMix, s.sat.mix },
        { ID::imgOn, s.img.on ? 1.0f : 0.0f }, { ID::imgXLow, s.img.xLow }, { ID::imgXHigh, s.img.xHigh }, { ID::imgLow, s.img.wLow },
        { ID::imgMid, s.img.wMid }, { ID::imgHigh, s.img.wHigh },
        { ID::limOn, s.lim.on ? 1.0f : 0.0f }, { ID::limGain, s.lim.gain }, { ID::limCeiling, s.lim.ceiling }, { ID::limRelease, s.lim.release }, { ID::limTP, s.lim.truePeak ? 1.0f : 0.0f },
    };
    for (int b = 0; b < 3; ++b)
    {
        const auto& bs = s.mb.b[b];
        for (auto& kv : std::initializer_list<std::pair<const char*, float>> { { "Thr", bs.thr }, { "Ratio", bs.ratio }, { "Att", bs.att },
                                                                               { "Rel", bs.rel }, { "Gain", bs.gain }, { "Byp", bs.bypass ? 1.0f : 0.0f },
                                                                               { "Solo", bs.solo ? 1.0f : 0.0f } })
            p.push_back ({ ID::mb (b, kv.first), kv.second });
    }
    return p;
}
} // namespace assistant
