#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Recent display frames from the audio thread (newest last), filled by the editor's timer.
class VizHistory
{
public:
    static constexpr int Size = 16384;            // ~22 s at 48 kHz (one frame per 64 samples)
    VizHistory() : ring ((size_t) Size), tmp (2048) {}
    void pullFrom (VizFifo& f)
    {
        int n;
        while ((n = f.pull (tmp.data(), (int) tmp.size())) > 0)
            for (int i = 0; i < n; ++i) { ring[(size_t) write] = tmp[(size_t) i]; write = (write + 1) % Size; filled = std::min (filled + 1, Size); }
    }
    int count() const { return filled; }
    // age 0 = newest frame
    const VizFrame& at (int age) const { return ring[(size_t) ((write - 1 - age + 2 * Size) % Size)]; }
    // Element-wise maximum over the newest `frames` frames.
    VizFrame recentMax (int frames) const
    {
        VizFrame m;
        for (int a = 0; a < std::min (frames, filled); ++a)
        {
            const auto& f = at (a);
            m.inPeak = std::max (m.inPeak, f.inPeak); m.satIn = std::max (m.satIn, f.satIn);
            m.busLevel = std::max (m.busLevel, f.busLevel); m.busGr = std::max (m.busGr, f.busGr);
            m.limGr = std::max (m.limGr, f.limGr); m.outPeak = std::max (m.outPeak, f.outPeak);
        }
        return m;
    }
private:
    std::vector<VizFrame> ring, tmp;
    int write = 0, filled = 0;
};

// Glue Comp: transfer curve with a live dot and draggable threshold, plus level/reduction history.
class GlueView : public juce::Component
{
public:
    GlueView (MasteringAudioProcessor&, const VizHistory&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
private:
    float pv (const char* id) const;
    MasteringAudioProcessor& proc;
    const VizHistory& hist;
    juce::Rectangle<float> curveArea, historyArea;
    bool dragging = false;
};

// Saturation: the curve being applied (type, drive and mix), where the signal hits it, and the
// harmonics it adds at that level.
class SaturationView : public juce::Component
{
public:
    SaturationView (MasteringAudioProcessor&, const VizHistory&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    float pv (const char* id) const;
    MasteringAudioProcessor& proc;
    const VizHistory& hist;
    juce::Rectangle<float> curveArea, harmArea;
    float shownPeak = 0.0f;
};

// Limiter: scrolling output waveform with the ceiling, and the gain reduction underneath.
class LimiterView : public juce::Component
{
public:
    LimiterView (MasteringAudioProcessor&, const VizHistory&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    float pv (const char* id) const;
    MasteringAudioProcessor& proc;
    const VizHistory& hist;
    juce::Rectangle<float> waveArea, grArea;
};
