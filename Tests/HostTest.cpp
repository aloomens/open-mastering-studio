// Loads the built Open Mastering Studio plugin the way a DAW does (through JUCE's plugin hosting) and checks:
//  - the plugin is found and loads, and accepts stereo in / stereo out
//  - audio passes, the output is finite and under the limiter ceiling
//  - the reported latency matches where the audio actually comes out
//  - the state round-trips through the plugin interface, and the factory presets are visible
//  - the editor opens
// Usage: PluginHostTest "path/to/Open Mastering Studio.vst3"   (or the .lv2 bundle on Linux)
// LV2: test with no other copy of the same version installed (e.g. in ~/.lv2). With a duplicate URI the
// checks still run (on the installed copy) but JUCE's LV2 host crashes on exit; Ardour handles duplicates fine.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <cmath>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: PluginHostTest <plugin.vst3 | plugin.lv2>\n"); return 2; }
    int fails = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++fails; };

    const juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1])));
    std::unique_ptr<juce::AudioPluginFormat> fmt;
    juce::String identifier = file.getFullPathName();

    if (file.hasFileExtension ("vst3"))
    {
        fmt = std::make_unique<juce::VST3PluginFormat>();
    }
   #if JUCE_PLUGINHOST_LV2
    else if (file.hasFileExtension ("lv2"))
    {
        // LV2 plugins are found by URI after the host scans the folder that holds the bundle
        fmt = std::make_unique<juce::LV2PluginFormat>();
        const auto uris = fmt->searchPathsForPlugins (juce::FileSearchPath (file.getParentDirectory().getFullPathName()), false, false);
        for (auto& u : uris)
            if (u.containsIgnoreCase ("open-mastering-studio")) identifier = u;
        std::printf ("      LV2 URI %s\n", identifier.toRawUTF8());
    }
   #endif
    else
    {
        std::printf ("unknown plugin type: %s\n", file.getFullPathName().toRawUTF8());
        return 2;
    }

    std::printf ("      format %s\n", fmt->getName().toRawUTF8());
    juce::OwnedArray<juce::PluginDescription> found;
    fmt->findAllTypesForFile (found, identifier);
    check (found.size() == 1, "plugin found");
    if (found.isEmpty()) return 1;
    std::printf ("      %s by %s, version %s\n", found[0]->name.toRawUTF8(), found[0]->manufacturerName.toRawUTF8(), found[0]->version.toRawUTF8());

    juce::String err;
    const double fs = 48000.0;
    const int block = 512;
    auto inst = fmt->createInstanceFromDescription (*found[0], fs, block, err);
    check (inst != nullptr, "instance created " + err);
    if (inst == nullptr) return 1;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo()); stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    check (inst->checkBusesLayoutSupported (stereo), "stereo in / stereo out supported");
    inst->setBusesLayout (stereo);
    inst->setNonRealtime (true);
    inst->prepareToPlay (fs, block);
    juce::AudioBuffer<float> io (2, block);
    juce::MidiBuffer midi;

    // LV2 reports latency through an output port that is only written while processing, so hosts read it
    // after the first block (VST3 reports it at once). Run one silent block, then reset for the real test.
    io.clear();
    inst->processBlock (io, midi);
    const int L = inst->getLatencySamples();
    inst->releaseResources();
    inst->prepareToPlay (fs, block);
    std::printf ("      reported latency %d samples (%.1f ms)\n", L, 1000.0 * L / fs);
    check (L > 0 && L < (int) (0.05 * fs), "latency reported (oversampling + limiter look-ahead)");

    // silence, then a 1 kHz tone at -12 dBFS from 0.5 s: find where it comes out
    const int start = (int) (fs * 0.5);
    const int total = (int) (fs * 3.0) + L;
    std::vector<float> outL ((size_t) total);
    for (int s = 0; s < total; s += block)
    {
        const int n = std::min (block, total - s);
        io.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const int t = s + i;
            const float v = t >= start ? 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (t - start) / fs) : 0.0f;
            io.setSample (0, i, v); io.setSample (1, i, v);
        }
        inst->processBlock (io, midi);
        for (int i = 0; i < n; ++i) outL[(size_t) (s + i)] = io.getSample (0, i);
    }

    bool finite = true; double pk = 0, e = 0;
    for (float v : outL) { finite &= std::isfinite (v); pk = std::max (pk, (double) std::abs (v)); }
    for (int i = start + L + (int) fs; i < start + L + (int) (2.0 * fs); ++i) e += (double) outL[(size_t) i] * outL[(size_t) i];
    const double rmsDb = 10 * std::log10 (e / fs + 1e-20);
    std::printf ("      output peak %.2f dBFS, steady RMS %.1f dBFS\n", 20 * std::log10 (pk + 1e-12), rmsDb);
    check (finite, "output finite");
    check (rmsDb > -30 && pk <= 1.0, "audio passes at a sensible level, never over full scale");

    int onset = -1;
    for (int i = 0; i < total; ++i) if (std::abs (outL[(size_t) i]) > 0.01f) { onset = i; break; }
    std::printf ("      tone comes out at sample %d (expected %d)\n", onset, start + L);
    check (onset >= 0 && std::abs (onset - (start + L)) <= 32, "reported latency matches the audio");

    juce::MemoryBlock state;
    inst->getStateInformation (state);
    auto inst2 = fmt->createInstanceFromDescription (*found[0], fs, block, err);
    check (inst2 != nullptr, "second instance created " + err);
    if (inst2 != nullptr)
    {
        inst2->setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        auto& p1 = inst->getParameters(); auto& p2 = inst2->getParameters();
        for (int i = 0; i < p1.size() && i < p2.size(); ++i) same &= std::abs (p1[i]->getValue() - p2[i]->getValue()) < 1e-6f;
        check (same && p1.size() == p2.size() && p1.size() > 40, "state round-trip (" + juce::String (p1.size()) + " parameters)");
    }
    check (inst->getNumPrograms() >= 8, "factory presets visible to the host (" + juce::String (inst->getNumPrograms()) + ")");

    if (inst->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr && ed->getWidth() > 600 && ed->getHeight() > 400,
               "editor opens (" + (ed ? juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) : juce::String ("none")) + ")");
        ed = nullptr;
    }
    else check (false, "plugin has an editor");

    inst->releaseResources();
    inst = nullptr; inst2 = nullptr;
    std::printf ("%d failure(s)\n", fails);
    return fails;
}
