#pragma once
#include "MasterChain.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <functional>

// Master Assistant: analyses a mix (and optional reference tracks), proposes chain settings,
// then renders the result offline and refines it until loudness/dynamics targets are met.
namespace assistant
{
constexpr int NumBands = 31; // ISO 1/3-octave bands, 20 Hz .. 20 kHz

const std::array<float, NumBands>& bandCentres();   // exact centres (1000 * 2^(n/3))
juce::String bandLabel (int band);                  // nominal label ("31.5", "1k", ...)
using Spectrum = std::array<float, NumBands>;

// Everything the assistant needs to know about a piece of audio. Small enough to store in presets/state.
struct Profile
{
    juce::String name, filePath;
    double sampleRate = 0, seconds = 0;
    float integrated = -100, lra = 0, truePeak = -100, plr = 0; // plr = true peak - integrated
    Spectrum spectrum {};                     // long-term average, dB, 0 = mean of 200 Hz..4 kHz
    float bandP50[3] {}, bandP90[3] {}, bandP98[3] {}; // 100 ms levels per band (<150 Hz, mid, >4 kHz), dB peak-equivalent
    float fullP50 = -100, fullP90 = -100;
    float sideRatio[3] {};                    // side/mid energy per band, dB
    float correlation[3] {};
    float crest = 0;                          // median 100 ms peak-to-RMS, dB

    bool isValid() const { return seconds >= 5.0 && integrated > -70.0f; }
    juce::ValueTree toTree() const;
    static Profile fromTree (const juce::ValueTree&);
};

// Values are stored in sessions, so existing numbers never change; new curves are appended.
enum class Curve { Acoustic = 0, Vocal = 1, Reference = 2, Pop = 3, Rock = 4, HipHop = 5, RnB = 6, Electronic = 7, Jazz = 8, Classical = 9 };
constexpr int NumCurves = 10;
enum class Style { Dynamic = 0, Balanced = 1, Loud = 2 };

// How each generic curve is built and how the assistant treats that kind of music.
struct CurveInfo
{
    const char* name;
    const char* description;
    float percussion;      // dB at 89 Hz and 4.5 kHz relative to the mids (Elowsson & Friberg 2017 pivot)
    float genreSlope;      // dB/oct relative to pop, 100 Hz-10 kHz (Pestana et al. 2013, Table 2)
    float subBass;         // extra dB below ~50 Hz
    bool gentle;           // lighter compression, slower release
    float lowCutHz;        // always-on subsonic filter (0 = only when needed)
    bool slowAttack;       // glue lets transients through
    bool allowSaturation;  // Loud style may add saturation
    bool tubeSaturation;
    float lowSideTarget;   // side/mid energy below the bass crossover the assistant aims for (dB)
    float bassMonoHz;      // crossover used when narrowing the bass
    bool measured;         // genre slope comes from published data (false = extrapolated)
};
const CurveInfo& curveInfo (Curve);
std::vector<Curve> curveMenuOrder();          // generic curves in menu order (Reference is added separately)
juce::StringArray styleNames();
Spectrum targetCurve (Curve);                 // generic targets, normalised like Profile::spectrum
float styleLufs (Style);

using Progress = std::function<bool (double)>; // return false to cancel

bool readAudio (juce::AudioFormatManager&, const juce::File&, juce::AudioBuffer<float>& out, double& sampleRate,
                double maxSeconds, juce::String& error);
Profile analyse (const juce::AudioBuffer<float>&, double sampleRate, const Progress& = {});
Spectrum averageSpectrum (const juce::AudioBuffer<float>&, double sampleRate, const Progress& = {});
float spectralDistance (const Spectrum& a, const Spectrum& b); // weighted RMS difference in dB

struct Request
{
    Curve curve = Curve::Vocal;
    Style style = Style::Balanced;
    float amount = 1.0f;            // 0..1: how far to move tone and width toward the target
    bool useReferenceLoudness = true;
    std::vector<Profile> references;
};

struct Result
{
    bool cancelled = false;
    juce::String error;
    ChainSettings settings;
    juce::StringArray notes;
    Spectrum mixSpectrum {}, targetSpectrum {}, resultSpectrum {};
    float targetLufs = -14, integrated = -100, truePeak = -100, avgLimiterGr = 0, avgGlueGr = 0;
    float distanceBefore = 0, distanceAfter = 0;
    juce::String summary;
};

Result run (const juce::AudioBuffer<float>& mix, double sampleRate, const Profile& mixProfile,
            const Request&, const ChainSettings& current, const Progress& = {});

// All processing parameters (not output trim, monitoring or dither) as parameter-ID/value pairs.
std::vector<std::pair<juce::String, float>> toParams (const ChainSettings&);
} // namespace assistant
