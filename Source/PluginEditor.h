#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ModuleViews.h"

namespace Theme
{
    const juce::Colour bg       { 0xff14161b };
    const juce::Colour panel    { 0xff1c1f26 };
    const juce::Colour panel2   { 0xff242832 };
    const juce::Colour outline  { 0xff343a47 };
    const juce::Colour text     { 0xffe4e7ee };
    const juce::Colour dim      { 0xff8a91a3 };
    const juce::Colour accent   { 0xff4fc3f7 };
    const juce::Colour warm     { 0xffffb74d };
    const juce::Colour green    { 0xff66bb6a };
    const juce::Colour yellow   { 0xffffe066 };
    const juce::Colour red      { 0xffef5350 };
}

// "v1.6.2" - the CMake project version, compiled in (MS_VERSION).
inline juce::String versionText()
{
   #ifdef MS_VERSION
    return juce::String ("v") + MS_VERSION;
   #else
    return "v?";
   #endif
}

inline juce::Font uiFont (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

class MasterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MasterLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
};

//==============================================================================
// One parameter as a knob, toggle or drop-down, with a caption.
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& caption);
    void resized() override;
    void paint (juce::Graphics&) override;
    bool isChoice() const { return kind == Choice; }
private:
    juce::String caption;
    enum Kind { Knob, Toggle, Choice } kind = Knob;
    juce::Slider slider;
    juce::TextButton button;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cA;
};

class ModulePanel : public juce::Component
{
public:
    struct Item  { juce::String id, caption; };
    struct Group { juce::String title; std::vector<Item> items; };
    ModulePanel (juce::AudioProcessorValueTreeState&, juce::String title, std::vector<Group>, juce::String help);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::String title, help;
    std::vector<Group> groups;
    std::vector<std::vector<std::unique_ptr<ParamControl>>> controls;
    std::vector<juce::Rectangle<int>> groupBounds;
    juce::Rectangle<int> helpArea;
};

//==============================================================================
class ChainStrip : public juce::Component
{
public:
    struct Entry { juce::String name, onParam; };
    ChainStrip (juce::AudioProcessorValueTreeState&, std::vector<Entry>);
    std::function<void (int)> onSelect;
    void setSelected (int);
    int getSelected() const { return selected; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh() { repaint(); }
private:
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<juce::TextButton>> power;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
    std::vector<juce::Rectangle<int>> boxes;
    int selected = 1;
};

//==============================================================================
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (MasteringAudioProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void()> onNodeTouched;

    // EQ: curve + points. Multiband / Stereo: the spectrum split into the module's three bands with
    // draggable crossovers (plus live gain reduction, or widths and a vectorscope).
    enum class Mode { EQ, Multiband, Stereo };
    void setMode (Mode m) { mode = m; repaint(); }
    Mode getMode() const { return mode; }
    void setScope (const std::vector<float>* interleavedLR) { scope = interleavedLR; }

    static juce::String nodeLabel (int index);   // label drawn on chart point `index`
    static juce::String nodeName (int index);    // section the point controls (matches the EQ panel)
    int getNumNodes() const { return (int) nodes.size(); }
    juce::String getNodeName (int index) const { return nodes[(size_t) index].name; }

private:
    static constexpr int Order = 12, N = 1 << Order;
    struct Node { juce::String name, freqId, gainId, qId, onId; juce::Colour colour; };

    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForSpec (float db) const;
    float yForEq (float db) const;
    float eqForY (float y) const;
    juce::Point<float> nodePos (int i) const;
    int hitNode (juce::Point<float>) const;
    float pv (const juce::String& id) const;
    void setPv (const juce::String& id, float v);
    void computeSpectrum (std::vector<float>& ring, int ringPos, std::vector<float>& display);
    void computeEqCurve();

    MasteringAudioProcessor& proc;
    juce::dsp::FFT fft { Order };
    juce::dsp::WindowingFunction<float> window { (size_t) N, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> preRing, postRing, fftData, preDisp, postDisp, eqCurve, scratch;
    int prePos = 0, postPos = 0;
    std::vector<Node> nodes;
    int hover = -1, dragging = -1;
    juce::Rectangle<float> plot;

    void paintBands (juce::Graphics&);
    void paintScope (juce::Graphics&, juce::Rectangle<float>);
    int hitCrossover (float x) const;
    juce::String crossoverId (int which) const;
    Mode mode = Mode::EQ;
    const std::vector<float>* scope = nullptr;
    float grShown[3] {}, scopeScale = 1.0f;
    int draggingX = -1, hoverX = -1;
};

//==============================================================================
class MeterPanel : public juce::Component
{
public:
    explicit MeterPanel (MasteringAudioProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    MasteringAudioProcessor& proc;
    juce::ComboBox target;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetA;
    juce::TextButton matchBtn { "Match Target" }, resetBtn { "Reset" };
    juce::Label status;
    float in[2] {}, out[2] {}, inHold[2] {}, outHold[2] {}, gr[5] {};
    int holdTimer[4] {};
    juce::Rectangle<int> levelArea, grArea, loudArea, corrArea;
};

//==============================================================================
class PlayerBar : public juce::Component, private juce::ChangeListener
{
public:
    explicit PlayerBar (MasteringAudioProcessor&);
    ~PlayerBar() override;
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    bool load (const juce::File&);
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void openFile();
    void exportFile();
    MasteringAudioProcessor& proc;
    juce::TextButton openBtn { "Open..." }, playBtn { "Play" }, stopBtn { "Stop" }, loopBtn { "Loop" }, ejectBtn { "Eject" }, exportBtn { "Export Master..." };
    juce::ComboBox format;
    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumb;
    juce::Rectangle<int> wave;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// Master Assistant: target choice, reference tracks, learn, run/revert, and a tonal-balance chart.
class AssistantPanel : public juce::Component
{
public:
    explicit AssistantPanel (MasteringAudioProcessor&);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void addReferences (const juce::Array<juce::File>&);

private:
    void syncOptions();
    void showResult (const assistant::Result&);
    assistant::Spectrum currentTarget (bool& valid) const;

    MasteringAudioProcessor& proc;
    juce::ComboBox curveBox, styleBox;
    juce::Slider amount;
    juce::TextButton refLoudBtn { "Use their loudness" };
    juce::TextButton learnBtn { "Learn" }, addRefBtn { "Add..." }, removeRefBtn { "Remove" }, listenBtn { "Listen" };
    juce::TextButton runBtn { "Analyze & Apply" }, cancelBtn { "Cancel" }, revertBtn { "Revert" };
    juce::TextEditor notes;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<assistant::Profile> refs;
    int selectedRef = -1;
    juce::Rectangle<int> sourceArea, listArea, statusArea, chartArea;
    juce::String sourceText, statusText;
    float progress = 0.0f;
    bool busy = false;
};

//==============================================================================
class MasteringAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      public juce::FileDragAndDropTarget,
                                      private juce::Timer
{
public:
    explicit MasteringAudioProcessorEditor (MasteringAudioProcessor&);
    ~MasteringAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void selectModule (int i) { strip.setSelected (i); }
    void parentHierarchyChanged() override;
    void showAssistant (bool show);
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void mouseUp (const juce::MouseEvent&) override;     // click the title/version: About box
    void mouseMove (const juce::MouseEvent&) override;
    static juce::String aboutText();                      // copyright, license and no-warranty notice
    static juce::Rectangle<int> titleArea();             // the title + version (clickable), measured from the fonts
    static constexpr float TitleFontSize = 17.0f;

private:
    void timerCallback() override;
    void showModule (int);
    void rebuildPresetMenu();
    void presetChosen();
    void savePresetDialog();
    void stepPreset (int delta);

    MasteringAudioProcessor& proc;
    MasterLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, assistantBtn { "Assistant" };
    juce::TextButton aBtn { "A" }, bBtn { "B" }, copyBtn { "Copy A>B" };
    juce::TextButton compareBtn { "Compare" }, matchBtn { "Level Match" };
    juce::ComboBox monitorBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compareA, matchA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> monitorA;

    std::unique_ptr<PlayerBar> player;
    SpectrumView spectrum;
    VizHistory history;
    std::vector<float> scopeData;
    GlueView glueView;
    SaturationView satView;
    LimiterView limView;
    juce::TextButton pinEqBtn { "Always show EQ" };
    AssistantPanel assistantPanel;
    juce::Rectangle<int> chartArea;
    int blink = 0;
    bool wasBusy = false, badgeShown = false;
    juce::String noticeText;
    juce::Colour noticeColour;
    double noticeUntil = 0.0;
    void updateChartView();
    void paintOverChildren (juce::Graphics&) override;
    ChainStrip strip;
    std::vector<std::unique_ptr<ModulePanel>> panels;
    MeterPanel meters;
    juce::Array<juce::File> userPresets;
    juce::String shownPreset;
    juce::Component::SafePointer<juce::DocumentWindow> setUpWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasteringAudioProcessorEditor)
};
