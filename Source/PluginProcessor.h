#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Params.h"
#include "MasterChain.h"
#include "Assistant.h"

// Lock-free mono sample FIFO feeding the spectrum analyser.
class AnalyzerFifo
{
public:
    void push (const float* l, const float* r, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) data[(size_t) (s1 + i)] = 0.5f * (l[i] + r[i]);
        for (int i = 0; i < n2; ++i) data[(size_t) (s2 + i)] = 0.5f * (l[n1 + i] + r[n1 + i]);
        fifo.finishedWrite (n1 + n2);
    }
    int pull (float* dest, int max)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (std::min (max, fifo.getNumReady()), s1, n1, s2, n2);
        std::copy (data.begin() + s1, data.begin() + s1 + n1, dest);
        std::copy (data.begin() + s2, data.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
private:
    static constexpr int Size = 1 << 15;
    juce::AbstractFifo fifo { Size };
    std::vector<float> data = std::vector<float> (Size, 0.0f);
};

// Lock-free stereo sample queue for the vectorscope (audio thread -> editor).
class StereoScopeFifo
{
public:
    void push (const float* l, const float* r, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { data[(size_t) (2 * (s1 + i))] = l[i]; data[(size_t) (2 * (s1 + i) + 1)] = r[i]; }
        for (int i = 0; i < n2; ++i) { data[(size_t) (2 * (s2 + i))] = l[n1 + i]; data[(size_t) (2 * (s2 + i) + 1)] = r[n1 + i]; }
        fifo.finishedWrite (n1 + n2);
    }
    // Pulls up to max stereo pairs into dest (interleaved L, R).
    int pull (float* dest, int max) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (std::min (max, fifo.getNumReady()), s1, n1, s2, n2);
        std::copy (data.begin() + 2 * s1, data.begin() + 2 * (s1 + n1), dest);
        std::copy (data.begin() + 2 * s2, data.begin() + 2 * (s2 + n2), dest + 2 * n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }
private:
    static constexpr int Size = 1 << 14;
    juce::AbstractFifo fifo { Size };
    std::vector<float> data = std::vector<float> (2 * Size, 0.0f);
};

struct Meters
{
    std::atomic<float> inPeak[2] {}, outPeak[2] {};
    std::atomic<float> correlation { 1.0f };
    std::atomic<float> matchDb { 0.0f };
    msdsp::LoudnessMeter inLoud, outLoud;
};

class MasteringAudioProcessor : public juce::AudioProcessor
{
public:
    MasteringAudioProcessor();
    ~MasteringAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return productName(); }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    static int getNumFactoryPrograms();
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Settings snapshot (safe from any thread) ----
    ChainSettings makeSettings() const;

    // ---- Presets ----
    static juce::StringArray factoryPresetNames();
    void applyFactoryPreset (int index);
    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File&);
    juce::String currentPresetName { "Default" };

    // ---- A/B ----
    void selectAB (int slot);
    void copyCurrentToOther();
    int getABSlot() const { return abSlot; }

    // ---- Loudness target helper: moves limiter gain so the output hits the target ----
    bool matchTarget (juce::String& message);

    juce::AudioProcessorValueTreeState apvts;
    Meters meters;
    AnalyzerFifo preFifo, postFifo;
    VizFifo vizFifo;              // module views: levels and gain reduction every 64 samples
    StereoScopeFifo scopeFifo;    // vectorscope
    float getMbGrLatest (int band) const { return chain.mb.grLatest[juce::jlimit (0, 2, band)].load (std::memory_order_relaxed); }
    std::atomic<float> grMb[3] {}, grBus { 0.0f }, grLim { 0.0f };

    // ---- Standalone file player ----
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    juce::AudioFormatManager formatManager;
    bool loadFile (const juce::File&);
    void ejectFile();
    juce::File getLoadedFile() const { return loadedFile; }
    juce::AudioTransportSource transport;
    std::atomic<bool> loopPlayback { true };
    void setLooping (bool shouldLoop) { loopPlayback = shouldLoop; if (readerSource) readerSource->setLooping (shouldLoop); }
    bool isFileLoaded() const { return fileLoaded.load(); }
    double getCurrentSampleRate() const { return currentSampleRate; }
    double getSampleRateNow() const { return currentSampleRate; }

    // Offline render of a file through a private copy of the chain (used by Export).
    // bits: 16/24 = dithered fixed point, 32 = float. progress() returns false to cancel.
    struct RenderResult { juce::String error; bool cancelled = false; float integrated = -100, truePeak = -100, lra = 0; };
    RenderResult renderFile (const juce::File& in, const juce::File& out, bool flac, int bits,
                             const std::function<bool (double)>& progress);

    // ---- Master Assistant ----
    struct AssistantOptions { int curve = 1, style = 1; float amount = 1.0f; bool referenceLoudness = true; };
    AssistantOptions assistantOptions;                       // message thread
    bool startAssistant (juce::String& error);
    void cancelAssistant() { cancelRequested = true; }
    bool isAssistantBusy() const { return pendingJobs.load() > 0; }
    float getAssistantProgress() const { return assistantProgress.load(); }
    juce::String getAssistantStatus() const;
    std::unique_ptr<assistant::Result> takeAssistantResult();
    void applyAssistantResult (const assistant::Result&);
    bool canRevertAssistant() const { return assistantUndo.isValid(); }
    void revertAssistant();
    const assistant::Result* getLastAssistantResult() const { return lastResult.get(); }

    // Learn: capture the incoming audio (plugin / live input) for analysis.
    void startLearn();
    void stopLearn() { capturing = false; }
    bool isLearning() const { return capturing.load(); }
    double getLearnedSeconds() const { return captureWritten.load() / currentSampleRate; }
    double getLearnCapacitySeconds() const { return capture.getNumSamples() / currentSampleRate; }

    // Reference tracks (analysed in the background; profiles are saved with the session).
    void addReferenceFile (const juce::File&);
    std::vector<assistant::Profile> getReferences() const;
    void removeReference (int index);
    bool setListenReference (int index, bool on);            // plays the reference at matched loudness
    bool isListeningToReference() const { return listenRef.load(); }

private:
    void resetAllToDefaults();
    void replaceStateKeepingListening (const juce::ValueTree&);
    void cacheParameterPointers();
    void setProgramIndex (int index);   // records the program and tells the host
    int undoProgram = 0;

    // Direct pointers to every parameter value, resolved once (the audio thread never looks up by name).
    using P = std::atomic<float>*;
    struct CachedParams
    {
        P inGain {}, outGain {}, bypass {}, matchBypass {}, monitor {}, target {};
        P eqOn {}, eqMode {}, hpOn {}, hpFreq {}, hpSlope {}, lsFreq {}, lsGain {}, pf[3] {}, pg[3] {}, pq[3] {}, hsFreq {}, hsGain {}, lpOn {}, lpFreq {};
        P mbOn {}, mbXLow {}, mbXHigh {}, mbThr[3] {}, mbRatio[3] {}, mbAtt[3] {}, mbRel[3] {}, mbGain[3] {}, mbByp[3] {}, mbSolo[3] {};
        P mbLinear {}, mbAutoRel {};
        P busOn {}, busThr {}, busRatio {}, busAtt {}, busRel {}, busMakeup {}, busMix {}, busHpf {};
        P satOn {}, satType {}, satDrive {}, satMix {};
        P imgOn {}, imgXLow {}, imgXHigh {}, imgLow {}, imgMid {}, imgHigh {};
        P imgLinear {}, imgMono {}, imgMonoFreq {}, imgSafe {};
        P limOn {}, limGain {}, limCeiling {}, limRelease {}, limTP {}, limStyle {};
        P ditherBits {}, ditherShape {};
    } cp;
    void setStatus (const juce::String&);
    void setParam (const juce::String& id, float value);

    MasterChain chain;
    double currentSampleRate = 44100.0;
    int currentProgram = 0;

    std::vector<float> dryDelay[2];
    int dryPos = 0, reportedLatency = 0;
    std::atomic<bool> snapStructure { false };
    msdsp::Ramp bypassRamp;
    int monitorMode = 0, monitorPrev = 0;
    float monitorFade = 1.0f;
    float matchGainCur = 1.0f;
    double corrLR = 0, corrLL = 0, corrRR = 0;

    juce::ValueTree abStates[2];
    int abSlot = 0;

    juce::TimeSliceThread readAhead { "Open Mastering Studio file read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::File loadedFile;
    int fileChannels = 2;
    std::atomic<bool> fileLoaded { false };
    juce::AudioBuffer<float> playBuf;

    // Assistant state
    juce::ThreadPool jobs { 1 };
    std::atomic<int> pendingJobs { 0 };
    std::atomic<bool> cancelRequested { false };
    std::atomic<float> assistantProgress { 0.0f };
    juce::CriticalSection assistantLock;
    juce::String assistantStatus;
    std::unique_ptr<assistant::Result> pendingResult, lastResult;
    std::vector<assistant::Profile> references;               // guarded by assistantLock
    juce::ValueTree assistantUndo;
    juce::String undoPresetName;

    juce::AudioBuffer<float> capture;
    std::atomic<int> captureWritten { 0 };
    std::atomic<bool> capturing { false };

    juce::AudioTransportSource refTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> refSource;
    juce::String refLoadedPath;
    int refChannels = 2;
    std::atomic<bool> listenRef { false }, refLoaded { false };
    std::atomic<float> refGainDb { 0.0f };
    msdsp::Ramp refRamp;
    juce::AudioBuffer<float> refBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringAudioProcessor)
};
