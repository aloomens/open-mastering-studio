#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct PresetDef
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;
};

const std::vector<PresetDef>& factoryPresets()
{
    using namespace ID;
    static const std::vector<PresetDef> presets {
        { "Default (Clean)", {} },
        { "Streaming Ready (-14)", {
            { hpOn, 1 }, { hpFreq, 25 }, { lsFreq, 90 }, { lsGain, 0.5f }, { hsFreq, 11000 }, { hsGain, 1.0f },
            { busOn, 1 }, { busThr, -16 }, { busRatio, 2 }, { busAtt, 10 }, { busRel, 200 }, { busMakeup, 1.5f }, { busMix, 100 },
            { limOn, 1 }, { limGain, 4 }, { limCeiling, -1 }, { limRelease, 120 }, { limTP, 1 } } },
        { "Loud & Punchy (-9)", {
            { hpOn, 1 }, { hpFreq, 28 }, { lsFreq, 70 }, { lsGain, 1.0f }, { p2Freq, 350 }, { p2Gain, -1.0f }, { p2Q, 0.9f },
            { hsFreq, 10000 }, { hsGain, 1.5f },
            { mbOn, 1 }, { mbXLow, 140 }, { mbXHigh, 4500 },
            { mb (0, "Thr"), -22 }, { mb (0, "Ratio"), 3 }, { mb (0, "Att"), 25 }, { mb (0, "Rel"), 180 }, { mb (0, "Gain"), 1 },
            { mb (1, "Thr"), -20 }, { mb (1, "Ratio"), 2 }, { mb (1, "Att"), 12 }, { mb (1, "Rel"), 120 }, { mb (1, "Gain"), 1 },
            { mb (2, "Thr"), -24 }, { mb (2, "Ratio"), 2.5f }, { mb (2, "Att"), 4 }, { mb (2, "Rel"), 80 }, { mb (2, "Gain"), 1 },
            { busOn, 1 }, { busThr, -14 }, { busRatio, 4 }, { busAtt, 30 }, { busRel, 150 }, { busMakeup, 2 }, { busMix, 70 },
            { satOn, 1 }, { satType, 0 }, { satDrive, 5 }, { satMix, 60 },
            { limOn, 1 }, { limGain, 9 }, { limCeiling, -1 }, { limRelease, 60 }, { limTP, 1 } } },
        { "Warm Analog", {
            { hpOn, 1 }, { hpFreq, 22 }, { lsFreq, 120 }, { lsGain, 1.5f }, { p3Freq, 3500 }, { p3Gain, -0.8f }, { p3Q, 0.7f },
            { hsFreq, 14000 }, { hsGain, -1.0f },
            { busOn, 1 }, { busThr, -14 }, { busRatio, 2 }, { busAtt, 30 }, { busRel, 300 }, { busMakeup, 1 }, { busMix, 80 },
            { satOn, 1 }, { satType, 1 }, { satDrive, 9 }, { satMix, 65 },
            { limOn, 1 }, { limGain, 4 }, { limCeiling, -1 }, { limRelease, 150 }, { limTP, 1 } } },
        { "Wide & Airy", {
            { hpOn, 1 }, { hpFreq, 25 }, { p3Freq, 3200 }, { p3Gain, -1.5f }, { p3Q, 1.2f }, { hsFreq, 12000 }, { hsGain, 2.0f },
            { imgOn, 1 }, { imgXLow, 180 }, { imgXHigh, 5000 }, { imgLow, 0 }, { imgMid, 120 }, { imgHigh, 140 },
            { limOn, 1 }, { limGain, 4 }, { limCeiling, -1 }, { limRelease, 100 }, { limTP, 1 } } },
        { "Bass Control", {
            { hpOn, 1 }, { hpFreq, 30 }, { hpSlope, 2 },
            { mbOn, 1 }, { mbXLow, 120 }, { mbXHigh, 5000 },
            { mb (0, "Thr"), -26 }, { mb (0, "Ratio"), 3.5f }, { mb (0, "Att"), 20 }, { mb (0, "Rel"), 150 },
            { mb (1, "Thr"), -12 }, { mb (1, "Ratio"), 1.5f }, { mb (2, "Thr"), -12 }, { mb (2, "Ratio"), 1.5f },
            { imgOn, 1 }, { imgXLow, 150 }, { imgLow, 0 },
            { limOn, 1 }, { limGain, 5 }, { limCeiling, -1 }, { limRelease, 100 }, { limTP, 1 } } },
        { "Gentle Glue", {
            { busOn, 1 }, { busThr, -18 }, { busRatio, 1.5f }, { busAtt, 30 }, { busRel, 400 }, { busMakeup, 1 }, { busMix, 100 }, { busHpf, 90 },
            { limOn, 1 }, { limGain, 2 }, { limCeiling, -1 }, { limRelease, 200 }, { limTP, 1 } } },
        { "Podcast / Voice (-16)", {
            { hpOn, 1 }, { hpFreq, 80 }, { hpSlope, 1 }, { p1Freq, 300 }, { p1Gain, -2 }, { p1Q, 1.0f },
            { p2Freq, 3000 }, { p2Gain, 2 }, { p2Q, 0.8f }, { p3Freq, 7000 }, { p3Gain, -1.5f }, { p3Q, 2.0f },
            { busOn, 1 }, { busThr, -20 }, { busRatio, 3 }, { busAtt, 5 }, { busRel, 150 }, { busMakeup, 3 }, { busHpf, 100 },
            { imgOn, 1 }, { imgLow, 0 }, { imgMid, 60 }, { imgHigh, 60 },
            { limOn, 1 }, { limGain, 6 }, { limCeiling, -1.5f }, { limRelease, 80 }, { limTP, 1 } } },
    };
    return presets;
}

// Listening aids: never changed by recalling a project, preset or A/B slot (a session saved
// while in Compare or Mono must not reopen sounding different).
bool isListeningControl (const juce::String& id)
{
    return id == ID::bypass || id == ID::matchBypass || id == ID::monitor;
}

// Factory presets also leave your delivery settings (output trim, loudness target, dither) alone.
// User presets store and recall them, so a preset sounds exactly as it did when saved.
bool isPresetExempt (const juce::String& id)
{
    return isListeningControl (id) || id == ID::outGain || id == ID::target || id == ID::ditherBits || id == ID::ditherShape;
}
} // namespace

//==============================================================================
MasteringAudioProcessor::MasteringAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MasteringStudio", createParameterLayout())
{
    formatManager.registerBasicFormats();
    apvts.state.setProperty ("preset", currentPresetName, nullptr);
    cacheParameterPointers();
    chain.setViz (&vizFifo);
}

MasteringAudioProcessor::~MasteringAudioProcessor()
{
    cancelRequested = true;
    jobs.removeAllJobs (true, 20000);
    refTransport.setSource (nullptr);
    transport.setSource (nullptr);
    readAhead.stopThread (2000);
}

//==============================================================================
// Every parameter is looked up once here; the audio thread then only reads atomics through these
// pointers (looking parameters up by name built strings and allocated memory in every block).
void MasteringAudioProcessor::cacheParameterPointers()
{
    auto p = [this] (const juce::String& id)
    {
        auto* v = apvts.getRawParameterValue (id);
        jassert (v != nullptr);
        return v;
    };
    auto& c = cp;
    c.inGain = p (ID::inGain); c.outGain = p (ID::outGain);
    c.bypass = p (ID::bypass); c.matchBypass = p (ID::matchBypass); c.monitor = p (ID::monitor); c.target = p (ID::target);
    c.eqOn = p (ID::eqOn); c.eqMode = p (ID::eqMode); c.hpOn = p (ID::hpOn); c.hpFreq = p (ID::hpFreq); c.hpSlope = p (ID::hpSlope);
    c.lsFreq = p (ID::lsFreq); c.lsGain = p (ID::lsGain);
    const char* pf[] { ID::p1Freq, ID::p2Freq, ID::p3Freq }, *pg[] { ID::p1Gain, ID::p2Gain, ID::p3Gain }, *pq[] { ID::p1Q, ID::p2Q, ID::p3Q };
    for (int k = 0; k < 3; ++k) { c.pf[k] = p (pf[k]); c.pg[k] = p (pg[k]); c.pq[k] = p (pq[k]); }
    c.hsFreq = p (ID::hsFreq); c.hsGain = p (ID::hsGain); c.lpOn = p (ID::lpOn); c.lpFreq = p (ID::lpFreq);
    c.mbOn = p (ID::mbOn); c.mbXLow = p (ID::mbXLow); c.mbXHigh = p (ID::mbXHigh);
    for (int k = 0; k < 3; ++k)
    {
        c.mbThr[k] = p (ID::mb (k, "Thr")); c.mbRatio[k] = p (ID::mb (k, "Ratio")); c.mbAtt[k] = p (ID::mb (k, "Att"));
        c.mbRel[k] = p (ID::mb (k, "Rel")); c.mbGain[k] = p (ID::mb (k, "Gain")); c.mbByp[k] = p (ID::mb (k, "Byp")); c.mbSolo[k] = p (ID::mb (k, "Solo"));
    }
    c.mbLinear = p (ID::mbLinear); c.mbAutoRel = p (ID::mbAutoRel);
    c.imgLinear = p (ID::imgLinear); c.imgMono = p (ID::imgMono); c.imgMonoFreq = p (ID::imgMonoFreq); c.imgSafe = p (ID::imgSafe);
    c.limStyle = p (ID::limStyle);
    c.busOn = p (ID::busOn); c.busThr = p (ID::busThr); c.busRatio = p (ID::busRatio); c.busAtt = p (ID::busAtt);
    c.busRel = p (ID::busRel); c.busMakeup = p (ID::busMakeup); c.busMix = p (ID::busMix); c.busHpf = p (ID::busHpf);
    c.satOn = p (ID::satOn); c.satType = p (ID::satType); c.satDrive = p (ID::satDrive); c.satMix = p (ID::satMix);
    c.imgOn = p (ID::imgOn); c.imgXLow = p (ID::imgXLow); c.imgXHigh = p (ID::imgXHigh);
    c.imgLow = p (ID::imgLow); c.imgMid = p (ID::imgMid); c.imgHigh = p (ID::imgHigh);
    c.limOn = p (ID::limOn); c.limGain = p (ID::limGain); c.limCeiling = p (ID::limCeiling); c.limRelease = p (ID::limRelease); c.limTP = p (ID::limTP);
    c.ditherBits = p (ID::ditherBits); c.ditherShape = p (ID::ditherShape);
}

ChainSettings MasteringAudioProcessor::makeSettings() const
{
    auto v = [] (const std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto b = [&v] (const std::atomic<float>* a) { return v (a) > 0.5f; };
    auto i = [&v] (const std::atomic<float>* a) { return juce::roundToInt (v (a)); };
    const auto& c = cp;

    ChainSettings s;
    s.inGain = v (c.inGain); s.outGain = v (c.outGain);

    auto& e = s.eq;
    e.on = b (c.eqOn); e.mode = i (c.eqMode);
    e.hpOn = b (c.hpOn); e.hpFreq = v (c.hpFreq); e.hpSlope = i (c.hpSlope);
    e.lsFreq = v (c.lsFreq); e.lsGain = v (c.lsGain);
    for (int k = 0; k < 3; ++k) { e.pf[k] = v (c.pf[k]); e.pg[k] = v (c.pg[k]); e.pq[k] = v (c.pq[k]); }
    e.hsFreq = v (c.hsFreq); e.hsGain = v (c.hsGain);
    e.lpOn = b (c.lpOn); e.lpFreq = v (c.lpFreq);

    s.mb.on = b (c.mbOn); s.mb.xLow = v (c.mbXLow); s.mb.xHigh = v (c.mbXHigh);
    for (int k = 0; k < 3; ++k)
    {
        auto& bs = s.mb.b[k];
        bs.thr = v (c.mbThr[k]); bs.ratio = v (c.mbRatio[k]); bs.att = v (c.mbAtt[k]); bs.rel = v (c.mbRel[k]);
        bs.gain = v (c.mbGain[k]); bs.bypass = b (c.mbByp[k]); bs.solo = b (c.mbSolo[k]);
    }
    s.mb.linear = b (c.mbLinear); s.mb.autoRelease = b (c.mbAutoRel);

    s.bus = { b (c.busOn), v (c.busThr), v (c.busRatio), v (c.busAtt), v (c.busRel), v (c.busMakeup), v (c.busMix), v (c.busHpf) };
    s.sat = { b (c.satOn), i (c.satType), v (c.satDrive), v (c.satMix) };
    s.img = { b (c.imgOn), v (c.imgXLow), v (c.imgXHigh), v (c.imgLow), v (c.imgMid), v (c.imgHigh) };
    s.img.linear = b (c.imgLinear); s.img.monoBass = b (c.imgMono); s.img.monoFreq = v (c.imgMonoFreq); s.img.safe = b (c.imgSafe);
    s.lim = { b (c.limOn), v (c.limGain), v (c.limCeiling), v (c.limRelease), b (c.limTP), i (c.limStyle) };
    s.dither = { i (c.ditherBits), b (c.ditherShape) };
    return s;
}

//==============================================================================
void MasteringAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    chain.prepare (sampleRate);
    chain.configure (makeSettings());
    reportedLatency = chain.getLatency();
    setLatencySamples (reportedLatency);
    for (auto& d : dryDelay) d.assign ((size_t) chain.getMaxLatency() + 1, 0.0f);
    dryPos = 0;
    bypassRamp.prepare (sampleRate, 30.0);
    bypassRamp.snap (apvts.getRawParameterValue (ID::bypass)->load() > 0.5f);
    monitorMode = monitorPrev = juce::roundToInt (apvts.getRawParameterValue (ID::monitor)->load());
    monitorFade = 1.0f;
    meters.inLoud.measureTruePeak = false;
    meters.inLoud.prepare (sampleRate);
    meters.outLoud.prepare (sampleRate);
    playBuf.setSize (2, std::max (samplesPerBlock, 4096));
    transport.prepareToPlay (samplesPerBlock, sampleRate);
    refTransport.prepareToPlay (samplesPerBlock, sampleRate);
    refRamp.prepare (sampleRate, 40.0);
    refRamp.snap (false);
    refBuf.setSize (2, std::max (samplesPerBlock, 4096));
}

void MasteringAudioProcessor::releaseResources()
{
    transport.releaseResources();
    refTransport.releaseResources();
}

bool MasteringAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void MasteringAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    // Standalone: a loaded file replaces the live input.
    if (fileLoaded.load())
    {
        juce::AudioSourceChannelInfo info (&buffer, 0, n);
        transport.getNextAudioBlock (info);
        if (fileChannels == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    }

    const auto s = makeSettings();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    // ---- Learn: capture the unprocessed input (skipping silence) ----
    if (capturing.load() && buffer.getMagnitude (0, n) > 1.0e-5f)
    {
        const int w = captureWritten.load();
        const int m = std::min (n, capture.getNumSamples() - w);
        if (m > 0)
        {
            capture.copyFrom (0, w, buffer, 0, 0, m);
            capture.copyFrom (1, w, buffer, 1, 0, m);
            captureWritten = w + m;
        }
        if (w + m >= capture.getNumSamples()) capturing = false;
    }

    // ---- Input metering ----
    msdsp::atomicMax (meters.inPeak[0], buffer.getMagnitude (0, 0, n));
    msdsp::atomicMax (meters.inPeak[1], buffer.getMagnitude (1, 0, n));
    meters.inLoud.process (L, R, n);
    preFifo.push (L, R, n);

    // ---- Latency-matched dry copy for Compare ----
    if (playBuf.getNumSamples() < n) playBuf.setSize (2, n, false, false, true);
    // (Read at the latency in force for this block; the chain switches latency only behind a silent gap.)
    const int dl = (int) dryDelay[0].size();
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& line = dryDelay[ch];
        auto* x = buffer.getReadPointer (ch);
        auto* d = playBuf.getWritePointer (ch);
        int p = dryPos;
        for (int i = 0; i < n; ++i)
        {
            line[(size_t) p] = x[i];
            int r = p - reportedLatency; if (r < 0) r += dl;
            d[i] = line[(size_t) r];
            if (++p == dl) p = 0;
        }
    }
    dryPos = (dryPos + n) % dl;

    // ---- The chain ----
    if (snapStructure.exchange (false)) chain.configure (s);
    chain.process (buffer, s);

    // Linear phase or the limiter style changed the latency: tell the host (it re-aligns its delay compensation).
    if (const int lat = chain.getLatency(); lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
    }

    // ---- Compare (bypass) with optional loudness matching ----
    const bool bypass = cp.bypass->load() > 0.5f;
    const bool match  = cp.matchBypass->load() > 0.5f;
    bypassRamp.target = bypass;
    const float mgT = match ? msdsp::dbToGain (meters.matchDb.load()) : 1.0f;
    if (! bypassRamp.silent())
    {
        auto* dL = playBuf.getReadPointer (0); auto* dR = playBuf.getReadPointer (1);
        const float mgStep = (mgT - matchGainCur) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float g = bypassRamp.next();
            matchGainCur += mgStep;
            L[i] += g * (dL[i] * matchGainCur - L[i]);
            R[i] += g * (dR[i] * matchGainCur - R[i]);
        }
    }
    matchGainCur = mgT;

    // ---- Reference listening (replaces the master at matched loudness) ----
    refRamp.target = listenRef.load() && refLoaded.load();
    const bool hearingRef = ! refRamp.silent();
    if (hearingRef)
    {
        if (refBuf.getNumSamples() < n) refBuf.setSize (2, n, false, false, true);
        juce::AudioSourceChannelInfo info (&refBuf, 0, n);
        refTransport.getNextAudioBlock (info);
        if (refChannels == 1) refBuf.copyFrom (1, 0, refBuf, 0, 0, n);
        const float g = msdsp::dbToGain (refGainDb.load());
        auto* rL = refBuf.getReadPointer (0); auto* rR = refBuf.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float k = refRamp.next();
            L[i] += k * (rL[i] * g - L[i]);
            R[i] += k * (rR[i] * g - R[i]);
        }
    }

    // ---- Output metering (the reference is not measured as the master) ----
    msdsp::atomicMax (meters.outPeak[0], buffer.getMagnitude (0, 0, n));
    msdsp::atomicMax (meters.outPeak[1], buffer.getMagnitude (1, 0, n));
    if (! hearingRef) meters.outLoud.process (L, R, n);
    postFifo.push (L, R, n);
    scopeFifo.push (L, R, n);

    if (! bypass && bypassRamp.silent() && ! hearingRef)
    {
        const float inS = meters.inLoud.shortTerm.load(), outS = meters.outLoud.shortTerm.load();
        if (inS > -60.0f && outS > -60.0f)
        {
            const float a = (float) std::min (1.0, n / (0.5 * currentSampleRate));
            meters.matchDb = meters.matchDb.load() + a * ((outS - inS) - meters.matchDb.load());
        }
    }

    {
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < n; ++i) { lr += L[i] * R[i]; ll += L[i] * L[i]; rr += R[i] * R[i]; }
        const double k = std::exp (-(double) n / (0.3 * currentSampleRate));
        corrLR = corrLR * k + lr; corrLL = corrLL * k + ll; corrRR = corrRR * k + rr;
        const double den = std::sqrt (corrLL * corrRR);
        meters.correlation = den > 1.0e-9 ? (float) (corrLR / den) : 1.0f;
    }

    // Switched-off modules keep running in the background; only show reduction for active ones.
    for (int b = 0; b < 3; ++b) { const float g = chain.mb.grMeter[b].exchange (0.0f); if (s.mb.on) msdsp::atomicMax (grMb[b], g); }
    { const float g = chain.bus.grMeter.exchange (0.0f); if (s.bus.on) msdsp::atomicMax (grBus, g); }
    msdsp::atomicMax (grLim, chain.lim.grMeter.exchange (0.0f));

    // ---- Monitoring (listening aid; affects what you hear). Mode changes crossfade over ~20 ms. ----
    const int mon = juce::roundToInt (cp.monitor->load());
    if (mon != monitorMode && monitorFade >= 1.0f) { monitorPrev = monitorMode; monitorMode = mon; monitorFade = 0.0f; }
    if (monitorMode != 0 || monitorFade < 1.0f)
    {
        auto apply = [] (int mode, float l, float r, float& ol, float& or_)
        {
            switch (mode)
            {
                case 1: ol = or_ = 0.5f * (l + r); break;
                case 2: ol = or_ = 0.5f * (l - r); break;
                case 3: ol = or_ = l; break;
                case 4: ol = or_ = r; break;
                default: ol = l; or_ = r; break;
            }
        };
        const float step = (float) (1.0 / (0.02 * currentSampleRate));
        for (int i = 0; i < n; ++i)
        {
            float nl, nr;
            apply (monitorMode, L[i], R[i], nl, nr);
            if (monitorFade < 1.0f)
            {
                float pl, pr;
                apply (monitorPrev, L[i], R[i], pl, pr);
                monitorFade = std::min (1.0f, monitorFade + step);
                nl = pl + monitorFade * (nl - pl);
                nr = pr + monitorFade * (nr - pr);
            }
            L[i] = nl; R[i] = nr;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* MasteringAudioProcessor::createEditor()
{
    return new MasteringAudioProcessorEditor (*this);
}

void MasteringAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPresetName, nullptr);
    juce::ValueTree as ("Assistant");
    as.setProperty ("curve", assistantOptions.curve, nullptr);
    as.setProperty ("style", assistantOptions.style, nullptr);
    as.setProperty ("amount", assistantOptions.amount, nullptr);
    as.setProperty ("refLoudness", assistantOptions.referenceLoudness, nullptr);
    for (auto& r : getReferences()) as.appendChild (r.toTree(), nullptr);
    state.appendChild (as, nullptr);
    state.setProperty ("matchDb", meters.matchDb.load(), nullptr); // Compare's level match, so it is right straight after reopening
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void MasteringAudioProcessor::replaceStateKeepingListening (const juce::ValueTree& tree)
{
    std::vector<std::pair<juce::String, float>> keep;
    for (auto* id : { ID::bypass, ID::matchBypass, ID::monitor })
        keep.push_back ({ id, apvts.getParameter (id)->getValue() });
    apvts.replaceState (tree);
    for (auto& [id, v] : keep) apvts.getParameter (id)->setValueNotifyingHost (v);
}

void MasteringAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentPresetName = tree.getProperty ("preset", "Default").toString();
            auto as = tree.getChildWithName ("Assistant");
            if (as.isValid())
            {
                assistantOptions.curve = as.getProperty ("curve", 1);
                assistantOptions.style = as.getProperty ("style", 1);
                assistantOptions.amount = as.getProperty ("amount", 1.0f);
                assistantOptions.referenceLoudness = as.getProperty ("refLoudness", true);
                std::vector<assistant::Profile> refs;
                for (const auto& c : as) { auto p = assistant::Profile::fromTree (c); if (p.isValid()) refs.push_back (p); }
                const juce::ScopedLock sl (assistantLock);
                references = std::move (refs);
                tree.removeChild (as, nullptr);
            }
            meters.matchDb = (float) tree.getProperty ("matchDb", 0.0f);
            replaceStateKeepingListening (tree);
            snapStructure = true;   // a session just loaded: apply linear phase / limiter style at once, no fade
            // Sessions saved before 1.5.2 have no program index: show it as Custom unless the name is a factory preset.
            const int fallback = factoryPresetNames().indexOf (currentPresetName);
            setProgramIndex ((int) tree.getProperty ("program", fallback >= 0 ? fallback : getNumFactoryPrograms()));
        }
}

//==============================================================================
juce::StringArray MasteringAudioProcessor::factoryPresetNames()
{
    juce::StringArray names;
    for (auto& p : factoryPresets()) names.add (p.name);
    return names;
}

// Host-visible programs: the factory presets, plus one "Custom" entry that stands for anything else
// (a user preset, an Assistant result, a recalled A/B slot), so the DAW never shows a stale name.
int MasteringAudioProcessor::getNumFactoryPrograms() { return (int) factoryPresets().size(); }
int MasteringAudioProcessor::getNumPrograms() { return getNumFactoryPrograms() + 1; }
const juce::String MasteringAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, getNumFactoryPrograms())) return factoryPresets()[(size_t) index].name;
    if (index == getNumFactoryPrograms()) return "Custom (user preset / Assistant)";
    return {};
}
void MasteringAudioProcessor::setCurrentProgram (int index)
{
    // Hosts often re-send the current program when a session reopens; re-applying it would wipe any
    // tweaks made after choosing it, so the current program (and the Custom entry) is left alone.
    if (index == currentProgram || ! juce::isPositiveAndBelow (index, getNumFactoryPrograms())) return;
    applyFactoryPreset (index);
}

void MasteringAudioProcessor::setProgramIndex (int index)
{
    currentProgram = juce::jlimit (0, getNumFactoryPrograms(), index);
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

void MasteringAudioProcessor::setParam (const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void MasteringAudioProcessor::resetAllToDefaults()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isPresetExempt (rp->getParameterID()))
                if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-7f) // touch only what changes
                    rp->setValueNotifyingHost (rp->getDefaultValue());
}

void MasteringAudioProcessor::applyFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumFactoryPrograms())) return;
    resetAllToDefaults();
    for (auto& [id, value] : factoryPresets()[(size_t) index].values) setParam (id, value);
    currentPresetName = factoryPresets()[(size_t) index].name;
    setProgramIndex (index);
}

juce::File MasteringAudioProcessor::getUserPresetFolder() const
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto folder = docs.getChildFile (productName()).getChildFile ("Presets");
    // 1.8.0 renamed the product: the first time, copy presets saved under the old name (the old folder is
    // left as it was, so nothing is lost and older versions still find their presets).
    const auto old = docs.getChildFile ("Mastering Studio").getChildFile ("Presets");
    if (! folder.isDirectory() && old.isDirectory() && old != folder)
        old.copyDirectoryTo (folder);
    return folder;
}

juce::Array<juce::File> MasteringAudioProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.mspreset");
    files.sort();
    return files;
}

bool MasteringAudioProcessor::saveUserPreset (const juce::String& name)
{
    auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;)
        if (isListeningControl (state.getChild (i).getProperty ("id").toString())) state.removeChild (i, nullptr);
    state.setProperty ("preset", clean, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".mspreset"))) return false;
    currentPresetName = clean;
    return true;
}

bool MasteringAudioProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    // Sound parameters missing from the file go to defaults; output trim, target and dither are
    // recalled when the preset stored them (presets saved by 1.0-1.3 did not).
    resetAllToDefaults();
    for (const auto& child : tree)
    {
        const auto id = child.getProperty ("id").toString();
        if (id.isNotEmpty() && ! isListeningControl (id) && child.hasProperty ("value"))
            setParam (id, (float) child.getProperty ("value"));
    }
    currentPresetName = file.getFileNameWithoutExtension();
    setProgramIndex (getNumFactoryPrograms()); // Custom
    return true;
}

//==============================================================================
void MasteringAudioProcessor::selectAB (int slot)
{
    if (slot == abSlot) return;
    abStates[abSlot] = apvts.copyState();
    abStates[abSlot].setProperty ("preset", currentPresetName, nullptr);
    abStates[abSlot].setProperty ("program", currentProgram, nullptr);
    if (abStates[slot].isValid())
    {
        currentPresetName = abStates[slot].getProperty ("preset", currentPresetName).toString();
        replaceStateKeepingListening (abStates[slot].createCopy());
        setProgramIndex ((int) abStates[slot].getProperty ("program", getNumFactoryPrograms()));
    }
    abSlot = slot;
}

void MasteringAudioProcessor::copyCurrentToOther()
{
    abStates[1 - abSlot] = apvts.copyState();
    abStates[1 - abSlot].setProperty ("preset", currentPresetName, nullptr);
    abStates[1 - abSlot].setProperty ("program", currentProgram, nullptr);
}

bool MasteringAudioProcessor::matchTarget (juce::String& message)
{
    const float target = targetLufs (juce::roundToInt (apvts.getRawParameterValue (ID::target)->load()));
    float measured = meters.outLoud.integrated.load();
    if (measured < -69.0f) measured = meters.outLoud.shortTerm.load();
    if (measured < -69.0f) { message = "Play some audio first, then try again."; return false; }

    const float diff = target - measured;
    const bool limOn = apvts.getRawParameterValue (ID::limOn)->load() > 0.5f;
    const juce::String id = limOn ? ID::limGain : ID::inGain;
    auto* p = apvts.getParameter (id);
    const auto range = p->getNormalisableRange();
    const float cur = range.convertFrom0to1 (p->getValue());
    const float next = range.snapToLegalValue (cur + diff);
    p->beginChangeGesture(); p->setValueNotifyingHost (range.convertTo0to1 (next)); p->endChangeGesture();
    meters.outLoud.resetRequest = true;
    message = juce::String (limOn ? "Limiter gain " : "Input gain ") + ParamText::db (next, 0)
            + " (" + ParamText::db (next - cur, 0) + "). Meters reset - play the loudest section to check.";
    if (std::abs ((cur + diff) - next) > 0.05f) message += " Reached the end of the range.";
    return true;
}

//==============================================================================
bool MasteringAudioProcessor::loadFile (const juce::File& file)
{
    auto* reader = formatManager.createReaderFor (file);
    if (reader == nullptr) return false;
    if (! readAhead.isThreadRunning()) readAhead.startThread();

    transport.stop();
    fileLoaded = false;
    transport.setSource (nullptr);
    auto src = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    src->setLooping (loopPlayback.load());
    fileChannels = (int) reader->numChannels;
    transport.setSource (src.get(), 32768, &readAhead, reader->sampleRate, 2);
    readerSource = std::move (src);
    loadedFile = file;
    fileLoaded = true;
    meters.inLoud.resetRequest = true;
    meters.outLoud.resetRequest = true;
    return true;
}

void MasteringAudioProcessor::ejectFile()
{
    transport.stop();
    fileLoaded = false;
    transport.setSource (nullptr);
    readerSource.reset();
    loadedFile = juce::File();
}

//==============================================================================
MasteringAudioProcessor::RenderResult MasteringAudioProcessor::renderFile (const juce::File& in, const juce::File& out, bool flac, int bits,
                                                                          const std::function<bool (double)>& progress)
{
    RenderResult res;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (in));
    if (reader == nullptr) { res.error = "Could not read " + in.getFileName(); return res; }
    const double fs = reader->sampleRate;
    const juce::int64 total = reader->lengthInSamples;

    auto settings = makeSettings();
    const bool shape = settings.dither.shape;
    settings.dither = { bits == 16 ? 2 : bits == 24 ? 1 : 0, shape && bits == 16 };

    MasterChain render;
    render.prepare (fs);
    render.configure (settings);
    const int latency = render.getLatency();
    msdsp::LoudnessMeter meter;
    meter.prepare (fs);

    out.deleteFile();
    auto stream = out.createOutputStream();
    if (stream == nullptr) { res.error = "Could not write to " + out.getFullPathName(); return res; }
    std::unique_ptr<juce::AudioFormatWriter> writer;
    {
        juce::WavAudioFormat wav; juce::FlacAudioFormat fl;
        juce::AudioFormat& af = flac ? static_cast<juce::AudioFormat&> (fl) : static_cast<juce::AudioFormat&> (wav);
        writer.reset (af.createWriterFor (stream.get(), fs, 2, bits, {}, 0));
    }
    if (writer == nullptr) { res.error = "This format / bit depth / sample rate combination is not supported."; return res; }
    stream.release(); // owned by the writer now

    constexpr int block = 4096;
    juce::AudioBuffer<float> buf (2, block);
    juce::int64 pos = 0;
    int skip = latency;
    const juce::int64 end = total + latency;   // flush the look-ahead at the end
    while (pos < end)
    {
        if (progress && ! progress ((double) pos / (double) std::max<juce::int64> (1, end)))
        {
            writer.reset(); out.deleteFile(); res.cancelled = true; return res;
        }
        const int n = (int) std::min<juce::int64> (block, end - pos);
        buf.setSize (2, n, false, false, true);
        buf.clear();
        if (pos < total)
        {
            const int nr = (int) std::min<juce::int64> (n, total - pos);
            reader->read (&buf, 0, nr, pos, true, true);
            if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, nr);
        }
        render.process (buf, settings);
        const int off = std::min (skip, n);
        skip -= off;
        if (n - off > 0)
        {
            writer->writeFromAudioSampleBuffer (buf, off, n - off);
            meter.process (buf.getReadPointer (0, off), buf.getReadPointer (1, off), n - off);
        }
        pos += n;
    }
    writer.reset();
    res.integrated = meter.integrated.load(); res.truePeak = meter.truePeak.load(); res.lra = meter.lra.load();
    return res;
}

//==============================================================================
// Master Assistant
void MasteringAudioProcessor::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (assistantLock);
    assistantStatus = s;
}

juce::String MasteringAudioProcessor::getAssistantStatus() const
{
    const juce::ScopedLock sl (assistantLock);
    return assistantStatus;
}

std::unique_ptr<assistant::Result> MasteringAudioProcessor::takeAssistantResult()
{
    const juce::ScopedLock sl (assistantLock);
    return std::move (pendingResult);
}

std::vector<assistant::Profile> MasteringAudioProcessor::getReferences() const
{
    const juce::ScopedLock sl (assistantLock);
    return references;
}

void MasteringAudioProcessor::removeReference (int index)
{
    const juce::ScopedLock sl (assistantLock);
    if (juce::isPositiveAndBelow (index, (int) references.size()))
    {
        if (references[(size_t) index].filePath == refLoadedPath) listenRef = false;
        references.erase (references.begin() + index);
    }
}

void MasteringAudioProcessor::startLearn()
{
    if (capturing.load()) return;
    const int cap = (int) std::min (15000000.0, currentSampleRate * 600.0); // up to ~5 min at 48 kHz
    capture.setSize (2, cap, false, false, true);
    captureWritten = 0;
    capturing = true;
    setStatus ("Learning - play the song, then press STOP LEARNING.");
}

void MasteringAudioProcessor::addReferenceFile (const juce::File& file)
{
    ++pendingJobs;
    cancelRequested = false;
    setStatus ("Analyzing reference " + file.getFileName() + "...");
    jobs.addJob ([this, file]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        juce::AudioBuffer<float> audio;
        double fs = 0.0;
        juce::String err;
        if (assistant::readAudio (fm, file, audio, fs, 900.0, err))
        {
            auto p = assistant::analyse (audio, fs, [this] (double f) { assistantProgress = (float) f; return ! cancelRequested.load(); });
            p.name = file.getFileNameWithoutExtension();
            p.filePath = file.getFullPathName();
            if (cancelRequested.load()) setStatus ("Canceled.");
            else if (! p.isValid()) setStatus (file.getFileName() + " is too quiet or too short to use as a reference.");
            else
            {
                {
                    const juce::ScopedLock sl (assistantLock);
                    references.erase (std::remove_if (references.begin(), references.end(),
                                                      [&p] (const assistant::Profile& r) { return r.filePath == p.filePath; }), references.end());
                    references.push_back (p);
                }
                setStatus ("Added reference: " + p.name + " (" + juce::String (p.integrated, 1) + " LUFS).");
            }
        }
        else setStatus (err);
        --pendingJobs;
    });
}

bool MasteringAudioProcessor::setListenReference (int index, bool on)
{
    if (! on) { listenRef = false; return true; }
    const auto refs = getReferences();
    if (! juce::isPositiveAndBelow (index, (int) refs.size())) return false;
    const auto& p = refs[(size_t) index];
    const juce::File f (p.filePath);
    if (! f.existsAsFile()) { setStatus ("Can't play " + p.name + ": the file has moved or been deleted."); return false; }

    if (p.filePath != refLoadedPath || ! refLoaded.load())
    {
        auto* reader = formatManager.createReaderFor (f);
        if (reader == nullptr) return false;
        if (! readAhead.isThreadRunning()) readAhead.startThread();
        listenRef = false;
        refLoaded = false;
        refTransport.stop();
        refTransport.setSource (nullptr);
        auto src = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        src->setLooping (true);
        refChannels = (int) reader->numChannels;
        refTransport.setSource (src.get(), 32768, &readAhead, reader->sampleRate, 2);
        refSource = std::move (src);
        refLoadedPath = p.filePath;
        refTransport.setPosition (std::min (60.0, refTransport.getLengthInSeconds() * 0.3)); // skip intros
        refTransport.start();
        refLoaded = true;
    }
    // Play the reference at the loudness of the current master so only tone and dynamics differ.
    float master = meters.outLoud.integrated.load();
    if (master < -69.0f) master = meters.outLoud.shortTerm.load();
    if (master < -69.0f) master = targetLufs (juce::roundToInt (apvts.getRawParameterValue (ID::target)->load()));
    refGainDb = juce::jlimit (-30.0f, 30.0f, master - p.integrated);
    listenRef = true;
    return true;
}

bool MasteringAudioProcessor::startAssistant (juce::String& error)
{
    if (isAssistantBusy()) { error = "The assistant is busy - wait for it to finish."; return false; }

    std::shared_ptr<juce::AudioBuffer<float>> captured;
    juce::File file;
    double fs = currentSampleRate;
    if (isFileLoaded()) file = loadedFile;
    else
    {
        if (capturing.load()) stopLearn();
        const int n = captureWritten.load();
        if (n < (int) (10.0 * currentSampleRate))
        {
            error = isStandalone() ? "Open a file first, or press LEARN and play at least 10 seconds of the song."
                                   : "Press LEARN, play the song (at least 10 seconds - the whole song is best), then press STOP LEARNING.";
            return false;
        }
        captured = std::make_shared<juce::AudioBuffer<float>> (2, n);
        for (int ch = 0; ch < 2; ++ch) captured->copyFrom (ch, 0, capture, ch, 0, n);
    }

    assistant::Request req;
    req.curve = (assistant::Curve) juce::jlimit (0, assistant::NumCurves - 1, assistantOptions.curve);
    req.style = (assistant::Style) juce::jlimit (0, 2, assistantOptions.style);
    req.amount = assistantOptions.amount;
    req.useReferenceLoudness = assistantOptions.referenceLoudness;
    req.references = getReferences();
    if (req.curve == assistant::Curve::Reference && req.references.empty())
    {
        error = "Add a reference track first (Add... or drop files on the Assistant), or choose a generic target.";
        return false;
    }
    const auto current = makeSettings();

    ++pendingJobs;
    cancelRequested = false;
    assistantProgress = 0.0f;
    setStatus ("Reading audio...");
    jobs.addJob ([this, file, captured, fs, req, current]
    {
        auto prog = [this] (double f) { assistantProgress = (float) f; return ! cancelRequested.load(); };
        juce::AudioBuffer<float> audio;
        double sr = fs;
        auto result = std::make_unique<assistant::Result>();
        bool ok = true;
        if (file != juce::File())
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            ok = assistant::readAudio (fm, file, audio, sr, 900.0, result->error);
        }
        else audio.makeCopyOf (*captured);

        if (ok)
        {
            setStatus ("Analyzing the mix...");
            const auto profile = assistant::analyse (audio, sr, [&prog] (double f) { return prog (0.2 * f); });
            if (cancelRequested.load()) result->cancelled = true;
            else
            {
                setStatus ("Trying settings and measuring the result...");
                *result = assistant::run (audio, sr, profile, req, current, [&prog] (double f) { return prog (0.2 + 0.8 * f); });
            }
        }
        setStatus (result->cancelled ? juce::String ("Canceled.") : result->error.isNotEmpty() ? result->error : juce::String ("Done - settings applied."));
        {
            const juce::ScopedLock sl (assistantLock);
            pendingResult = std::move (result);
        }
        --pendingJobs;
    });
    return true;
}

void MasteringAudioProcessor::applyAssistantResult (const assistant::Result& r)
{
    if (r.cancelled || r.error.isNotEmpty()) return;
    assistantUndo = apvts.copyState();
    undoPresetName = currentPresetName;
    undoProgram = currentProgram;
    for (auto& [id, value] : assistant::toParams (r.settings))
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    int best = 0;
    for (int i = 1; i < targetNames().size(); ++i)
        if (std::abs (targetLufs (i) - r.targetLufs) < std::abs (targetLufs (best) - r.targetLufs)) best = i;
    setParam (ID::target, (float) best);
    currentPresetName = "Assistant: " + juce::String (assistant::curveInfo ((assistant::Curve) assistantOptions.curve).name).upToFirstOccurrenceOf (" /", false, false)
                      + ", " + assistant::styleNames()[assistantOptions.style].upToFirstOccurrenceOf (" (", false, false);
    lastResult = std::make_unique<assistant::Result> (r);
    setProgramIndex (getNumFactoryPrograms()); // Custom
    meters.outLoud.resetRequest = true;
}

void MasteringAudioProcessor::revertAssistant()
{
    if (! assistantUndo.isValid()) return;
    replaceStateKeepingListening (assistantUndo);
    currentPresetName = undoPresetName;
    setProgramIndex (undoProgram);
    assistantUndo = {};
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasteringAudioProcessor();
}
