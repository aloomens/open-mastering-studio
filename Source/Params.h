#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs and the parameter layout for Open Mastering Studio.

// The product name users see (set once in CMakeLists.txt: MS_PRODUCT_NAME).
inline juce::String productName()
{
   #ifdef MS_PRODUCT_NAME
    return MS_PRODUCT_NAME;
   #else
    return "Open Mastering Studio";
   #endif
}
namespace ID
{
    // Global / I/O
    inline constexpr const char* inGain      = "inGain";
    inline constexpr const char* outGain     = "outGain";
    inline constexpr const char* bypass      = "bypass";
    inline constexpr const char* matchBypass = "matchBypass";
    inline constexpr const char* monitor     = "monitor";
    inline constexpr const char* target      = "target";

    // EQ
    inline constexpr const char* eqOn   = "eqOn";
    inline constexpr const char* eqMode = "eqMode";
    inline constexpr const char* hpOn   = "hpOn";
    inline constexpr const char* hpFreq = "hpFreq";
    inline constexpr const char* hpSlope= "hpSlope";
    inline constexpr const char* lsFreq = "lsFreq";
    inline constexpr const char* lsGain = "lsGain";
    inline constexpr const char* p1Freq = "p1Freq";
    inline constexpr const char* p1Gain = "p1Gain";
    inline constexpr const char* p1Q    = "p1Q";
    inline constexpr const char* p2Freq = "p2Freq";
    inline constexpr const char* p2Gain = "p2Gain";
    inline constexpr const char* p2Q    = "p2Q";
    inline constexpr const char* p3Freq = "p3Freq";
    inline constexpr const char* p3Gain = "p3Gain";
    inline constexpr const char* p3Q    = "p3Q";
    inline constexpr const char* hsFreq = "hsFreq";
    inline constexpr const char* hsGain = "hsGain";
    inline constexpr const char* lpOn   = "lpOn";
    inline constexpr const char* lpFreq = "lpFreq";

    // Multiband compressor
    inline constexpr const char* mbOn    = "mbOn";
    inline constexpr const char* mbXLow  = "mbXLow";
    inline constexpr const char* mbXHigh = "mbXHigh";
    inline juce::String mb (int band, const char* name) { return "mb" + juce::String (band) + name; } // Thr Ratio Att Rel Gain Byp Solo
    inline constexpr const char* mbLinear  = "mbLinear";
    inline constexpr const char* mbAutoRel = "mbAutoRel";

    // Bus (glue) compressor
    inline constexpr const char* busOn     = "busOn";
    inline constexpr const char* busThr    = "busThr";
    inline constexpr const char* busRatio  = "busRatio";
    inline constexpr const char* busAtt    = "busAtt";
    inline constexpr const char* busRel    = "busRel";
    inline constexpr const char* busMakeup = "busMakeup";
    inline constexpr const char* busMix    = "busMix";
    inline constexpr const char* busHpf    = "busHpf";

    // Saturation
    inline constexpr const char* satOn    = "satOn";
    inline constexpr const char* satType  = "satType";
    inline constexpr const char* satDrive = "satDrive";
    inline constexpr const char* satMix   = "satMix";

    // Stereo imager
    inline constexpr const char* imgOn    = "imgOn";
    inline constexpr const char* imgXLow  = "imgXLow";
    inline constexpr const char* imgXHigh = "imgXHigh";
    inline constexpr const char* imgLow   = "imgLow";
    inline constexpr const char* imgMid   = "imgMid";
    inline constexpr const char* imgHigh  = "imgHigh";
    inline constexpr const char* imgLinear   = "imgLinear";
    inline constexpr const char* imgMono     = "imgMono";
    inline constexpr const char* imgMonoFreq = "imgMonoFreq";
    inline constexpr const char* imgSafe     = "imgSafe";

    // Limiter
    inline constexpr const char* limOn      = "limOn";
    inline constexpr const char* limGain    = "limGain";
    inline constexpr const char* limCeiling = "limCeiling";
    inline constexpr const char* limRelease = "limRelease";
    inline constexpr const char* limTP      = "limTP";
    inline constexpr const char* limStyle   = "limStyle";

    // Dither
    inline constexpr const char* ditherBits  = "ditherBits";
    inline constexpr const char* ditherShape = "ditherShape";
}

namespace ParamText
{
    inline juce::String hz (float v, int)  { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
    inline juce::String db (float v, int)  { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " dB"; }
    inline juce::String ms (float v, int)  { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    inline juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
    inline juce::String ratio (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + ":1"; }
    inline juce::String q (float v, int)   { return juce::String (v, 2); }
}

inline const juce::StringArray& targetNames()
{
    static const juce::StringArray names { "-8 LUFS (Club)", "-9 LUFS (Loud)", "-11 LUFS (CD)", "-14 LUFS (Streaming)",
                                            "-16 LUFS (Apple / Podcast)", "-18 LUFS (Dynamic)", "-23 LUFS (EBU R128)" };
    return names;
}
inline float targetLufs (int index)
{
    static const float v[] { -8.0f, -9.0f, -11.0f, -14.0f, -16.0f, -18.0f, -23.0f };
    return v[juce::jlimit (0, 6, index)];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = [] (float lo, float hi)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    };
    auto skewed = [] (float lo, float hi, float centre, float step = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    };

    using Fn = String (*) (float, int);
    auto addFloat = [&layout] (const String& id, const String& name, NormalisableRange<float> range, float def, Fn fn)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (fn)));
    };
    auto addBool = [&layout] (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addChoice = [&layout] (const String& id, const String& name, const StringArray& choices, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };

    // ---- I/O ----
    addFloat (ID::inGain,  "Input Gain",  { -24.0f, 24.0f, 0.1f }, 0.0f, ParamText::db);
    addFloat (ID::outGain, "Output Gain", { -24.0f, 0.0f, 0.1f },  0.0f, ParamText::db);
    addBool  (ID::bypass, "Compare (Bypass)", false);
    addBool  (ID::matchBypass, "Level-Matched Compare", true);
    addChoice (ID::monitor, "Monitor", { "Stereo", "Mono", "Side", "Left", "Right" }, 0);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ID::target, 1 }, "Loudness Target", targetNames(), 3,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));

    // ---- EQ ----
    addBool   (ID::eqOn, "EQ On", true);
    addChoice (ID::eqMode, "EQ Channels", { "Stereo", "Mid", "Side" }, 0);
    addBool   (ID::hpOn, "Low Cut On", false);
    addFloat  (ID::hpFreq, "Low Cut Freq", freqRange (10.0f, 500.0f), 25.0f, ParamText::hz);
    addChoice (ID::hpSlope, "Low Cut Slope (dB/oct)", { "12 dB", "24 dB", "48 dB" }, 1);
    addFloat  (ID::lsFreq, "Low Shelf Freq", freqRange (20.0f, 1000.0f), 100.0f, ParamText::hz);
    addFloat  (ID::lsGain, "Low Shelf Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, ParamText::db);
    const float bellDefaults[] { 200.0f, 1000.0f, 4000.0f };
    for (int i = 0; i < 3; ++i)
    {
        const String n = "Bell " + String (i + 1);
        const char* f = i == 0 ? ID::p1Freq : i == 1 ? ID::p2Freq : ID::p3Freq;
        const char* g = i == 0 ? ID::p1Gain : i == 1 ? ID::p2Gain : ID::p3Gain;
        const char* q = i == 0 ? ID::p1Q    : i == 1 ? ID::p2Q    : ID::p3Q;
        addFloat (f, n + " Freq", freqRange (20.0f, 20000.0f), bellDefaults[i], ParamText::hz);
        addFloat (g, n + " Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, ParamText::db);
        addFloat (q, n + " Q", skewed (0.1f, 10.0f, 1.0f), 0.8f, ParamText::q);
    }
    addFloat (ID::hsFreq, "High Shelf Freq", freqRange (1000.0f, 20000.0f), 10000.0f, ParamText::hz);
    addFloat (ID::hsGain, "High Shelf Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, ParamText::db);
    addBool  (ID::lpOn, "High Cut On", false);
    addFloat (ID::lpFreq, "High Cut Freq", freqRange (2000.0f, 22000.0f), 20000.0f, ParamText::hz);

    // ---- Multiband ----
    addBool  (ID::mbOn, "Multiband On", false);
    addFloat (ID::mbXLow,  "MB Crossover Low",  freqRange (40.0f, 1000.0f), 150.0f, ParamText::hz);
    addFloat (ID::mbXHigh, "MB Crossover High", freqRange (1000.0f, 16000.0f), 4000.0f, ParamText::hz);
    const char* bandNames[] { "Low", "Mid", "High" };
    const float attDef[] { 30.0f, 15.0f, 5.0f }, relDef[] { 250.0f, 150.0f, 100.0f };
    for (int b = 0; b < 3; ++b)
    {
        const String n = String ("MB ") + bandNames[b] + " ";
        addFloat (ID::mb (b, "Thr"),   n + "Threshold", { -60.0f, 0.0f, 0.1f }, -20.0f, ParamText::db);
        addFloat (ID::mb (b, "Ratio"), n + "Ratio",     skewed (1.0f, 10.0f, 3.0f, 0.01f), 2.0f, ParamText::ratio);
        addFloat (ID::mb (b, "Att"),   n + "Attack",    skewed (0.1f, 200.0f, 15.0f), attDef[b], ParamText::ms);
        addFloat (ID::mb (b, "Rel"),   n + "Release",   skewed (10.0f, 2000.0f, 200.0f), relDef[b], ParamText::ms);
        addFloat (ID::mb (b, "Gain"),  n + "Gain",      { -12.0f, 12.0f, 0.1f }, 0.0f, ParamText::db);
        addBool  (ID::mb (b, "Byp"),   n + "Bypass", false);
        addBool  (ID::mb (b, "Solo"),  n + "Solo", false);
    }
    addBool (ID::mbLinear,  "MB Linear Phase", false);
    addBool (ID::mbAutoRel, "MB Auto Release", false);

    // ---- Bus compressor ----
    addBool  (ID::busOn, "Glue On", false);
    addFloat (ID::busThr,    "Glue Threshold", { -40.0f, 0.0f, 0.1f }, -12.0f, ParamText::db);
    addFloat (ID::busRatio,  "Glue Ratio",     skewed (1.0f, 10.0f, 3.0f, 0.01f), 2.0f, ParamText::ratio);
    addFloat (ID::busAtt,    "Glue Attack",    skewed (0.1f, 100.0f, 10.0f), 10.0f, ParamText::ms);
    addFloat (ID::busRel,    "Glue Release",   skewed (20.0f, 2000.0f, 200.0f), 200.0f, ParamText::ms);
    addFloat (ID::busMakeup, "Glue Makeup",    { -6.0f, 18.0f, 0.1f }, 0.0f, ParamText::db);
    addFloat (ID::busMix,    "Glue Mix",       { 0.0f, 100.0f, 1.0f }, 100.0f, ParamText::pct);
    addFloat (ID::busHpf,    "Glue SC HPF",    freqRange (20.0f, 500.0f), 60.0f, ParamText::hz);

    // ---- Saturation ----
    addBool   (ID::satOn, "Saturation On", false);
    // New types go at the end so saved sessions keep their type.
    addChoice (ID::satType, "Saturation Type", { "Tape", "Tube", "Soft Clip", "Tape (Modeled)", "Tube (Modeled)" }, 0);
    addFloat  (ID::satDrive, "Saturation Drive", { 0.0f, 24.0f, 0.1f }, 6.0f, ParamText::db);
    addFloat  (ID::satMix,   "Saturation Mix",   { 0.0f, 100.0f, 1.0f }, 100.0f, ParamText::pct);

    // ---- Imager ----
    addBool  (ID::imgOn, "Stereo On", false);
    addFloat (ID::imgXLow,  "Stereo Crossover Low",  freqRange (50.0f, 1000.0f), 200.0f, ParamText::hz);
    addFloat (ID::imgXHigh, "Stereo Crossover High", freqRange (1000.0f, 12000.0f), 4000.0f, ParamText::hz);
    addFloat (ID::imgLow,  "Width Low",  { 0.0f, 200.0f, 1.0f }, 100.0f, ParamText::pct);
    addFloat (ID::imgMid,  "Width Mid",  { 0.0f, 200.0f, 1.0f }, 100.0f, ParamText::pct);
    addFloat (ID::imgHigh, "Width High", { 0.0f, 200.0f, 1.0f }, 100.0f, ParamText::pct);
    addBool  (ID::imgLinear, "Stereo Linear Phase", false);
    addBool  (ID::imgMono, "Mono Bass", false);
    addFloat (ID::imgMonoFreq, "Mono Bass Freq", freqRange (40.0f, 300.0f), 120.0f, ParamText::hz);
    addBool  (ID::imgSafe, "Mono-Safe Width", false);

    // ---- Limiter ----
    addBool  (ID::limOn, "Limiter On", true);
    addFloat (ID::limGain,    "Limiter Gain",    { 0.0f, 24.0f, 0.1f }, 0.0f, ParamText::db);
    addFloat (ID::limCeiling, "Limiter Ceiling", { -12.0f, 0.0f, 0.1f }, -1.0f, ParamText::db);
    addFloat (ID::limRelease, "Limiter Release", skewed (1.0f, 1000.0f, 100.0f), 100.0f, ParamText::ms);
    addBool  (ID::limTP, "True Peak", true);
    addChoice (ID::limStyle, "Limiter Style", { "Classic", "Transparent" }, 0);

    // ---- Dither ----
    addChoice (ID::ditherBits, "Dither", { "Off", "24-bit", "16-bit" }, 0);
    addBool   (ID::ditherShape, "Noise Shaping", false);

    return layout;
}
