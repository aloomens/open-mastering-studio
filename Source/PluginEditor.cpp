#include "PluginEditor.h"
#include <BinaryData.h>

using namespace juce;


//==============================================================================
MasterLookAndFeel::MasterLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, Theme::bg);
    setColour (Slider::textBoxTextColourId, Theme::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, Theme::accent.withAlpha (0.4f));
    setColour (Label::textColourId, Theme::text);
    setColour (TextButton::buttonColourId, Theme::panel2);
    setColour (TextButton::buttonOnColourId, Theme::accent);
    setColour (TextButton::textColourOffId, Theme::text);
    setColour (TextButton::textColourOnId, Colour (0xff0d1117));
    setColour (ComboBox::backgroundColourId, Theme::panel2);
    setColour (ComboBox::outlineColourId, Theme::outline);
    setColour (ComboBox::textColourId, Theme::text);
    setColour (ComboBox::arrowColourId, Theme::dim);
    setColour (PopupMenu::backgroundColourId, Theme::panel2);
    setColour (PopupMenu::textColourId, Theme::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.3f));
    setColour (PopupMenu::highlightedTextColourId, Theme::text);
    setColour (PopupMenu::headerTextColourId, Theme::dim);
    setColour (TooltipWindow::backgroundColourId, Theme::panel2);
    setColour (TooltipWindow::textColourId, Theme::text);
    setColour (TooltipWindow::outlineColourId, Theme::outline);
    setColour (AlertWindow::backgroundColourId, Theme::panel);
    setColour (AlertWindow::textColourId, Theme::text);
    setColour (TextEditor::backgroundColourId, Theme::panel2);
    setColour (TextEditor::textColourId, Theme::text);
    setColour (TextEditor::outlineColourId, Theme::outline);
    setColour (ProgressBar::foregroundColourId, Theme::accent);
    setColour (ProgressBar::backgroundColourId, Theme::panel2);
}

void MasterLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = a0 + pos * (a1 - a0);
    const float track = jmax (2.5f, radius * 0.14f);
    const bool on = s.isEnabled();

    Path bgArc;
    bgArc.addCentredArc (c.x, c.y, radius - track, radius - track, 0.0f, a0, a1, true);
    g.setColour (Theme::outline);
    g.strokePath (bgArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    const bool bipolar = s.getProperties().getWithDefault ("bipolar", false);
    const float from = bipolar ? a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0) : a0;
    Path valArc;
    valArc.addCentredArc (c.x, c.y, radius - track, radius - track, 0.0f, jmin (from, angle), jmax (from, angle), true);
    g.setColour (on ? Theme::accent : Theme::dim);
    g.strokePath (valArc, PathStrokeType (track, PathStrokeType::curved, PathStrokeType::rounded));

    const float knobR = radius - track * 2.2f;
    g.setColour (Theme::panel2.brighter (0.15f));
    g.fillEllipse (c.x - knobR, c.y - knobR, knobR * 2, knobR * 2);
    g.setColour (Theme::outline.brighter (0.2f));
    g.drawEllipse (c.x - knobR, c.y - knobR, knobR * 2, knobR * 2, 1.0f);
    Path ptr;
    ptr.addRoundedRectangle (-1.5f, -knobR + 2.0f, 3.0f, knobR * 0.55f, 1.5f);
    g.setColour (Theme::text);
    g.fillPath (ptr, AffineTransform::rotation (angle).translated (c.x, c.y));
}

void MasterLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (b.getProperties().getWithDefault ("power", false))
    {
        const float d = jmin (r.getWidth(), r.getHeight()) - 4.0f;
        auto led = Rectangle<float> (d, d).withCentre (r.getCentre());
        const bool on = b.getToggleState();
        g.setColour (on ? Theme::green : Theme::outline);
        g.fillEllipse (led);
        if (on) { g.setColour (Theme::green.withAlpha (0.25f)); g.fillEllipse (led.expanded (3.0f)); }
        g.setColour (Colours::black.withAlpha (0.4f));
        g.drawEllipse (led, 1.0f);
        return;
    }
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour (TextButton::buttonOnColourId) : b.findColour (TextButton::buttonColourId);
    if (hi) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    if (! b.isEnabled()) fill = on ? fill.withMultipliedSaturation (0.3f).withMultipliedAlpha (0.35f) : fill.withMultipliedAlpha (0.6f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? fill.brighter (0.2f) : Theme::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

Font MasterLookAndFeel::getTextButtonFont (TextButton&, int h) { return uiFont (jmin (13.0f, h * 0.55f), true); }
Font MasterLookAndFeel::getComboBoxFont (ComboBox&)            { return uiFont (13.0f); }
Font MasterLookAndFeel::getPopupMenuFont()                      { return uiFont (14.0f); }
Font MasterLookAndFeel::getLabelFont (Label&)                   { return uiFont (12.0f); }

//==============================================================================
ParamControl::ParamControl (AudioProcessorValueTreeState& apvts, const String& id, const String& cap) : caption (cap)
{
    auto* p = apvts.getParameter (id);
    jassert (p != nullptr);
    if (dynamic_cast<AudioParameterBool*> (p) != nullptr)
    {
        kind = Toggle;
        button.setButtonText (caption);
        button.setClickingTogglesState (true);
        button.setTooltip (p->getName (64));
        bA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (apvts, id, button);
        addAndMakeVisible (button);
    }
    else if (auto* c = dynamic_cast<AudioParameterChoice*> (p))
    {
        kind = Choice;
        combo.addItemList (c->choices, 1);
        combo.setTooltip (p->getName (64));
        cA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, id, combo);
        addAndMakeVisible (combo);
    }
    else
    {
        kind = Knob;
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::TextBoxBelow, false, 74, 16);
        slider.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        slider.setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        slider.setTooltip (p->getName (64) + "  (double-click to reset, Ctrl-drag for fine)");
        sA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
        auto* rp = dynamic_cast<RangedAudioParameter*> (p);
        slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
        slider.setVelocityModeParameters (1.0, 1, 0.0, true, ModifierKeys::ctrlModifier);
        const auto range = rp->getNormalisableRange();
        slider.getProperties().set ("bipolar", range.start < 0.0f && range.end > 0.0f);
        addAndMakeVisible (slider);
    }
}

void ParamControl::paint (Graphics& g)
{
    if (kind == Toggle) return;
    g.setColour (Theme::dim);
    g.setFont (uiFont (11.5f));
    g.drawFittedText (caption, getLocalBounds().removeFromTop (15), Justification::centred, 1);
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    switch (kind)
    {
        case Knob:   r.removeFromTop (15); slider.setBounds (r); break;
        case Choice: r.removeFromTop (17); combo.setBounds (r.removeFromTop (24).reduced (2, 0)); break;
        case Toggle: button.setBounds (r.withSizeKeepingCentre (jmin (r.getWidth() - 6, 66), 24)); break;
    }
}

//==============================================================================
ModulePanel::ModulePanel (AudioProcessorValueTreeState& apvts, String t, std::vector<Group> gs, String h)
    : title (std::move (t)), help (std::move (h)), groups (std::move (gs))
{
    for (auto& grp : groups)
    {
        controls.emplace_back();
        for (auto& it : grp.items)
        {
            controls.back().push_back (std::make_unique<ParamControl> (apvts, it.id, it.caption));
            addAndMakeVisible (*controls.back().back());
        }
    }
}

void ModulePanel::paint (Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (Theme::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText (title.toUpperCase(), 14, 6, 300, 20, Justification::centredLeft);

    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto r = groupBounds[i].toFloat();
        g.setColour (Theme::panel2.withAlpha (0.6f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Theme::dim);
        g.setFont (uiFont (11.0f, true));
        g.drawText (groups[i].title.toUpperCase(), r.removeFromTop (18).reduced (6, 0), Justification::centredLeft);
    }
    if (! helpArea.isEmpty())
    {
        g.setColour (Theme::dim);
        g.setFont (uiFont (12.5f));
        g.drawFittedText (help, helpArea, Justification::topLeft, 12, 1.0f);
    }
}

void ModulePanel::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (22);
    static constexpr int rows = 2, gap = 10, groupTitle = 18;
    // Columns holding a drop-down are wider (1.5 cells), so choice names such as "Tape (Modeled)" fit.
    auto columnUnits = [this] (size_t gi, int c)
    {
        const int n = (int) groups[gi].items.size(), gr = jmin (rows, n);
        for (int k = c * gr; k < jmin (n, (c + 1) * gr); ++k)
            if (controls[gi][(size_t) k]->isChoice()) return 3;
        return 2;
    };
    int units = 0;   // half-cells
    for (size_t gi = 0; gi < groups.size(); ++gi)
        for (int c = 0; c < ((int) groups[gi].items.size() + rows - 1) / rows; ++c) units += columnUnits (gi, c);
    const int cellH = jmin (86, (area.getHeight() - groupTitle - 4) / rows);
    int cellW = 76;
    const int needed = units * cellW / 2 + gap * ((int) groups.size() - 1) + 8 * (int) groups.size();
    if (needed > area.getWidth()) cellW = jmax (52, 2 * (area.getWidth() - gap * ((int) groups.size() - 1) - 8 * (int) groups.size()) / jmax (1, units));

    groupBounds.clear();
    int x = area.getX();
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        const int n = (int) groups[gi].items.size();
        const int gc = (n + rows - 1) / rows;
        const int gr = jmin (rows, n);
        int w = 8;
        for (int c = 0; c < gc; ++c) w += columnUnits (gi, c) * cellW / 2;
        Rectangle<int> gb (x, area.getY(), w, groupTitle + gr * cellH + 4);
        groupBounds.push_back (gb);
        int cx = gb.getX() + 4;
        for (int c = 0; c < gc; ++c)
        {
            const int cw = columnUnits (gi, c) * cellW / 2;
            for (int r = 0; r < gr && c * gr + r < n; ++r)
                controls[gi][(size_t) (c * gr + r)]->setBounds (cx, gb.getY() + groupTitle + r * cellH, cw, cellH);
            cx += cw;
        }
        x = gb.getRight() + gap;
    }
    helpArea = Rectangle<int> (x + 6, area.getY() + 2, area.getRight() - x - 6, area.getHeight() - 4);
    if (helpArea.getWidth() < 170) helpArea = {};
}

//==============================================================================
ChainStrip::ChainStrip (AudioProcessorValueTreeState& s, std::vector<Entry> e) : apvts (s), entries (std::move (e))
{
    for (auto& en : entries)
    {
        auto b = std::make_unique<TextButton>();
        if (en.onParam.isNotEmpty())
        {
            b->setClickingTogglesState (true);
            b->getProperties().set ("power", true);
            b->setTooltip ("Switch " + en.name + " on/off");
            attachments.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (apvts, en.onParam, *b));
            addAndMakeVisible (*b);
        }
        else attachments.push_back (nullptr);
        power.push_back (std::move (b));
    }
}

void ChainStrip::setSelected (int i) { selected = i; repaint(); if (onSelect) onSelect (i); }

void ChainStrip::resized()
{
    boxes.clear();
    auto r = getLocalBounds().reduced (6, 6);
    const int n = (int) entries.size(), arrow = 14;
    const int w = (r.getWidth() - arrow * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
    {
        auto b = r.removeFromLeft (w);
        boxes.push_back (b);
        if (power[(size_t) i]->isVisible()) power[(size_t) i]->setBounds (b.getX() + 6, b.getCentreY() - 9, 18, 18);
        r.removeFromLeft (arrow);
    }
}

void ChainStrip::paint (Graphics& g)
{
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        auto b = boxes[i].toFloat();
        const bool sel = (int) i == selected;
        const bool on = entries[i].onParam.isEmpty() || apvts.getRawParameterValue (entries[i].onParam)->load() > 0.5f;
        g.setColour (sel ? Theme::panel2.brighter (0.12f) : Theme::panel);
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (sel ? Theme::accent : Theme::outline);
        g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, sel ? 1.6f : 1.0f);
        g.setColour (on ? Theme::text : Theme::dim);
        g.setFont (uiFont (13.0f, true));
        auto tr = b.toNearestInt();
        if (entries[i].onParam.isNotEmpty()) tr.removeFromLeft (26);
        g.drawFittedText (entries[i].name, tr.reduced (4, 0), entries[i].onParam.isNotEmpty() ? Justification::centredLeft : Justification::centred, 1);
        if (i + 1 < boxes.size())
        {
            const float ax = b.getRight() + 3.0f, ay = b.getCentreY();
            Path p; p.addTriangle (ax, ay - 4, ax, ay + 4, ax + 7, ay);
            g.setColour (Theme::dim);
            g.fillPath (p);
        }
    }
}

void ChainStrip::mouseUp (const MouseEvent& e)
{
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i].contains (e.getPosition())) { setSelected ((int) i); return; }
}

//==============================================================================
SpectrumView::SpectrumView (MasteringAudioProcessor& p) : proc (p)
{
    preRing.assign (N, 0.0f); postRing.assign (N, 0.0f); fftData.assign (2 * N, 0.0f); scratch.assign (8192, 0.0f);
    nodes = {
        { "Low Cut",    ID::hpFreq, "", "", ID::hpOn, Colour (0xff9575cd) },
        { "Low Shelf",  ID::lsFreq, ID::lsGain, "", "", Colour (0xffef5350) },
        { "Bell 1",     ID::p1Freq, ID::p1Gain, ID::p1Q, "", Colour (0xffffb74d) },
        { "Bell 2",     ID::p2Freq, ID::p2Gain, ID::p2Q, "", Colour (0xffffee58) },
        { "Bell 3",     ID::p3Freq, ID::p3Gain, ID::p3Q, "", Colour (0xff66bb6a) },
        { "High Shelf", ID::hsFreq, ID::hsGain, "", "", Colour (0xff4fc3f7) },
        { "High Cut",   ID::lpFreq, "", "", ID::lpOn, Colour (0xff7986cb) },
    };
}

float SpectrumView::pv (const String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

// Labels on the chart points match the EQ panel: bells are numbered 1-3 like "Bell 1..3";
// the shelves and cut filters get letters.
String SpectrumView::nodeLabel (int index)
{
    static const char* labels[] { "LC", "LS", "1", "2", "3", "HS", "HC" };
    return isPositiveAndBelow (index, 7) ? labels[index] : "";
}

String SpectrumView::nodeName (int index)
{
    static const char* names[] { "Low Cut", "Low Shelf", "Bell 1", "Bell 2", "Bell 3", "High Shelf", "High Cut" };
    return isPositiveAndBelow (index, 7) ? names[index] : "";
}

void SpectrumView::setPv (const String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

static constexpr float fMin = 20.0f, fMax = 20000.0f, specTop = 6.0f, specBottom = -84.0f, eqRange = 15.0f;

float SpectrumView::xForFreq (float f) const { return plot.getX() + plot.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); }
float SpectrumView::freqForX (float x) const { return fMin * std::pow (fMax / fMin, (x - plot.getX()) / plot.getWidth()); }
float SpectrumView::yForSpec (float db) const { return jmap (db, specTop, specBottom, plot.getY(), plot.getBottom()); }
float SpectrumView::yForEq (float db) const   { return jmap (db, eqRange, -eqRange, plot.getY(), plot.getBottom()); }
float SpectrumView::eqForY (float y) const    { return jmap (y, plot.getY(), plot.getBottom(), eqRange, -eqRange); }

void SpectrumView::resized()
{
    plot = getLocalBounds().toFloat().reduced (34.0f, 14.0f).withTrimmedBottom (8.0f);
    const auto w = (size_t) jmax (1, (int) plot.getWidth());
    preDisp.assign (w, -120.0f); postDisp.assign (w, -120.0f); eqCurve.assign (w, 0.0f);
}

juce::Point<float> SpectrumView::nodePos (int i) const
{
    const auto& n = nodes[(size_t) i];
    const float g = n.gainId.isNotEmpty() ? pv (n.gainId) : 0.0f;
    return { xForFreq (pv (n.freqId)), yForEq (g) };
}

int SpectrumView::hitNode (juce::Point<float> p) const
{
    int best = -1; float bestD = 12.0f;
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const float d = nodePos (i).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void SpectrumView::computeSpectrum (std::vector<float>& ring, int ringPos, std::vector<float>& display)
{
    for (int i = 0; i < N; ++i) fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % N)];
    std::fill (fftData.begin() + N, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double fs = proc.getSampleRateNow() > 0 ? proc.getSampleRateNow() : 48000.0;
    const float norm = 4.0f / (float) N;
    for (size_t x = 0; x < display.size(); ++x)
    {
        const float f0 = freqForX (plot.getX() + (float) x), f1 = freqForX (plot.getX() + (float) x + 1.0f);
        const float b0 = (float) (f0 * N / fs), b1 = (float) (f1 * N / fs);
        float mag;
        if (b1 - b0 < 1.0f)
        {
            const int i0 = jlimit (0, N / 2 - 1, (int) b0);
            const float frac = b0 - (float) i0;
            mag = fftData[(size_t) i0] * (1.0f - frac) + fftData[(size_t) jmin (N / 2, i0 + 1)] * frac;
        }
        else
        {
            mag = 0.0f;
            for (int k = jlimit (0, N / 2, (int) b0); k <= jlimit (0, N / 2, (int) std::ceil (b1)); ++k) mag = jmax (mag, fftData[(size_t) k]);
        }
        const float tilt = 4.5f * std::log2 (jmax (f0, 1.0f) / 1000.0f);
        const float db = msdsp::gainToDb (mag * norm) + tilt;
        float& d = display[x];
        d = db > d ? d + (db - d) * 0.7f : d + (db - d) * 0.18f;
    }
}

void SpectrumView::computeEqCurve()
{
    using msdsp::FilterType;
    const double fs = proc.getSampleRateNow() > 0 ? proc.getSampleRateNow() : 48000.0;
    const bool hpOn = pv (ID::hpOn) > 0.5f, lpOn = pv (ID::lpOn) > 0.5f;
    const int slope = roundToInt (pv (ID::hpSlope));
    const auto& hq = msdsp::butterworthQs (slope == 0 ? 2 : slope == 1 ? 4 : 8);
    const auto& lq = msdsp::butterworthQs (4);
    const float hpF = pv (ID::hpFreq), lpF = pv (ID::lpFreq);
    const float lsF = pv (ID::lsFreq), lsG = pv (ID::lsGain), hsF = pv (ID::hsFreq), hsG = pv (ID::hsGain);
    const float bf[3] { pv (ID::p1Freq), pv (ID::p2Freq), pv (ID::p3Freq) };
    const float bg[3] { pv (ID::p1Gain), pv (ID::p2Gain), pv (ID::p3Gain) };
    const float bq[3] { pv (ID::p1Q), pv (ID::p2Q), pv (ID::p3Q) };

    for (size_t x = 0; x < eqCurve.size(); ++x)
    {
        const double f = freqForX (plot.getX() + (float) x);
        double mag = 1.0;
        if (hpOn) for (double q : hq) mag *= msdsp::svfMagnitude (FilterType::HighPass, fs, hpF, q, 0, f);
        mag *= msdsp::svfMagnitude (FilterType::LowShelf, fs, lsF, 0.7071, lsG, f);
        for (int b = 0; b < 3; ++b) mag *= msdsp::svfMagnitude (FilterType::Bell, fs, bf[b], bq[b], bg[b], f);
        mag *= msdsp::svfMagnitude (FilterType::HighShelf, fs, hsF, 0.7071, hsG, f);
        if (lpOn) for (double q : lq) mag *= msdsp::svfMagnitude (FilterType::LowPass, fs, lpF, q, 0, f);
        eqCurve[x] = msdsp::gainToDb ((float) mag);
    }
}

void SpectrumView::update()
{
    auto pullInto = [this] (AnalyzerFifo& f, std::vector<float>& ring, int& pos)
    {
        int n;
        while ((n = f.pull (scratch.data(), (int) scratch.size())) > 0)
            for (int i = 0; i < n; ++i) { ring[(size_t) pos] = scratch[(size_t) i]; pos = (pos + 1) % N; }
    };
    pullInto (proc.preFifo, preRing, prePos);
    pullInto (proc.postFifo, postRing, postPos);
    if (getWidth() <= 0) return;
    computeSpectrum (preRing, prePos, preDisp);
    computeSpectrum (postRing, postPos, postDisp);
    computeEqCurve();
    repaint();
}

void SpectrumView::paint (Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // Grid
    g.setFont (uiFont (10.5f));
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float x = xForFreq (f);
        g.setColour (Theme::outline.withAlpha (0.55f));
        g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (Theme::dim);
        g.drawText (f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f), (int) x - 20, (int) plot.getBottom() + 1, 40, 14, Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yForEq (db);
        g.setColour (Theme::outline.withAlpha (db == 0.0f ? 0.9f : 0.45f));
        g.drawHorizontalLine (roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Theme::dim);
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), (int) plot.getRight() + 12, (int) y - 7, 22, 14, Justification::centredLeft);
    }
    for (float db : { -72.0f, -48.0f, -24.0f, 0.0f })
    {
        g.setColour (Theme::dim.withAlpha (0.7f));
        g.drawText (String ((int) db), 2, (int) yForSpec (db) - 7, 30, 14, Justification::centredRight);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    auto specPath = [this] (const std::vector<float>& d, bool closed)
    {
        Path p;
        for (size_t x = 0; x < d.size(); ++x)
        {
            const float y = jlimit (plot.getY() - 2, plot.getBottom() + 2, yForSpec (d[x]));
            if (x == 0) p.startNewSubPath (plot.getX(), closed ? plot.getBottom() : y);
            p.lineTo (plot.getX() + (float) x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
        return p;
    };
    if (! preDisp.empty())
    {
        g.setColour (Theme::dim.withAlpha (0.22f));
        g.fillPath (specPath (preDisp, true));
        auto post = specPath (postDisp, true);
        g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.28f), 0, plot.getY(), Theme::accent.withAlpha (0.02f), 0, plot.getBottom(), false));
        g.fillPath (post);
        g.setColour (Theme::accent.withAlpha (0.9f));
        g.strokePath (specPath (postDisp, false), PathStrokeType (1.3f));
    }

    if (mode != Mode::EQ)
    {
        g.restoreState();
        paintBands (g);
        return;
    }

    // EQ curve
    const bool eqOn = pv (ID::eqOn) > 0.5f;
    if (! eqCurve.empty())
    {
        Path curve, fill;
        const float y0 = yForEq (0.0f);
        fill.startNewSubPath (plot.getX(), y0);
        for (size_t x = 0; x < eqCurve.size(); ++x)
        {
            const float y = jlimit (plot.getY() - 4, plot.getBottom() + 4, yForEq (eqCurve[x]));
            if (x == 0) curve.startNewSubPath (plot.getX(), y); else curve.lineTo (plot.getX() + (float) x, y);
            fill.lineTo (plot.getX() + (float) x, y);
        }
        fill.lineTo (plot.getRight(), y0); fill.closeSubPath();
        g.setColour (Theme::warm.withAlpha (eqOn ? 0.12f : 0.04f));
        g.fillPath (fill);
        g.setColour (eqOn ? Theme::warm : Theme::dim);
        g.strokePath (curve, PathStrokeType (2.0f));
    }

    g.restoreState(); // points are drawn unclipped, so one at 20 Hz or 20 kHz stays whole and easy to grab

    // Nodes
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto& n = nodes[(size_t) i];
        const auto p = nodePos (i);
        const bool active = n.onId.isEmpty() || pv (n.onId) > 0.5f;
        const float r = (i == hover || i == dragging) ? 10.5f : 9.0f;
        auto col = eqOn ? n.colour : Theme::dim;
        g.setColour (col.withAlpha (active ? 0.95f : 0.35f));
        if (active) g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
        g.drawEllipse (p.x - r, p.y - r, r * 2, r * 2, 1.5f);
        g.setColour (active ? Colour (0xff111111) : col);
        const String label = nodeLabel (i);
        g.setFont (uiFont (label.length() > 1 ? 8.5f : 10.5f, true));
        g.drawText (label, Rectangle<float> (r * 2 + 4, r * 2).withCentre (p), Justification::centred);
    }

    // Hover read-out
    const int info = dragging >= 0 ? dragging : hover;
    if (info >= 0)
    {
        const auto& n = nodes[(size_t) info];
        String s = n.name + "   " + ParamText::hz (pv (n.freqId), 0);
        if (n.gainId.isNotEmpty()) s += "   " + ParamText::db (pv (n.gainId), 0);
        if (n.qId.isNotEmpty()) s += "   Q " + String (pv (n.qId), 2);
        if (n.onId.isNotEmpty() && pv (n.onId) < 0.5f) s += "   (off - double-click to enable)";
        const auto p = nodePos (info);
        Rectangle<float> box (0, 0, (float) GlyphArrangement::getStringWidthInt (uiFont (12.0f), s) + 16.0f, 22.0f);
        box.setCentre (jlimit (plot.getX() + box.getWidth() / 2, plot.getRight() - box.getWidth() / 2, p.x), p.y - 24.0f > plot.getY() + 12 ? p.y - 24.0f : p.y + 24.0f);
        g.setColour (Theme::panel2.withAlpha (0.95f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (Theme::text);
        g.setFont (uiFont (12.0f));
        g.drawText (s, box, Justification::centred);
    }

    g.setColour (Theme::dim);
    g.setFont (uiFont (11.0f));
    const int eqMode = roundToInt (pv (ID::eqMode));
    g.drawText (String ("EQ: ") + (eqMode == 0 ? "Stereo" : eqMode == 1 ? "Mid" : "Side") + (eqOn ? "" : " (off)")
                + "     Analyzer: gray = input, blue = output, 4.5 dB/oct tilt", plot.toNearestInt().removeFromTop (16).translated (6, 2), Justification::centredLeft);
}

// ---- Multiband / Stereo: three bands over the spectrum ----
String SpectrumView::crossoverId (int which) const
{
    if (mode == Mode::Multiband) return which == 0 ? ID::mbXLow : ID::mbXHigh;
    return which == 0 ? ID::imgXLow : ID::imgXHigh;
}

int SpectrumView::hitCrossover (float x) const
{
    for (int k = 0; k < 2; ++k)
        if (std::abs (x - xForFreq (pv (crossoverId (k)))) < 7.0f) return k;
    return -1;
}

void SpectrumView::paintBands (Graphics& g)
{
    const bool mb = mode == Mode::Multiband;
    const bool on = pv (mb ? ID::mbOn : ID::imgOn) > 0.5f;
    const float f1 = pv (crossoverId (0)), f2 = jmax (pv (crossoverId (1)), f1 * 1.5f);
    const float x1 = xForFreq (f1), x2 = xForFreq (f2);
    const Colour bandCol[3] { Colour (0xffef5350), Colour (0xffffb74d), Colour (0xff4fc3f7) };
    const Rectangle<float> bands[3] { plot.withRight (x1), plot.withLeft (x1).withRight (x2), plot.withLeft (x2) };
    static const char* names[3] { "LOW", "MID", "HIGH" };

    for (int b = 0; b < 3; ++b)
    {
        g.setColour (bandCol[b].withAlpha (on ? 0.10f : 0.04f));
        g.fillRect (bands[b]);
        g.setColour (bandCol[b].withAlpha (on ? 1.0f : 0.5f));
        g.setFont (uiFont (12.0f, true));
        g.drawText (names[b], bands[b].withHeight (40).withTrimmedTop (22).toNearestInt(), Justification::centred);

        if (mb)
        {
            // Live gain reduction hanging from the top of each band
            const float gr = on ? proc.getMbGrLatest (b) : 0.0f;
            grShown[b] = gr > grShown[b] ? gr : grShown[b] * 0.88f;
            const float h = plot.getHeight() * 0.45f * jlimit (0.0f, 1.0f, grShown[b] / 12.0f);
            auto bar = bands[b].reduced (jmin (24.0f, bands[b].getWidth() * 0.25f), 0).withTop (plot.getY() + 44).withHeight (h);
            g.setColour (Theme::warm.withAlpha (0.8f));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (Theme::text);
            g.setFont (uiFont (12.0f));
            g.drawText (on ? "-" + String (grShown[b], 1) + " dB" : String ("off"), bands[b].withTop (bar.getBottom() + 2).withHeight (16).toNearestInt(), Justification::centred);
            const char* thr[] { "mb0Thr", "mb1Thr", "mb2Thr" };
            const char* ratio[] { "mb0Ratio", "mb1Ratio", "mb2Ratio" };
            g.setColour (Theme::dim);
            g.setFont (uiFont (11.0f));
            g.drawText (ParamText::db (pv (thr[b]), 0) + "  " + ParamText::ratio (pv (ratio[b]), 0),
                        bands[b].withTop (plot.getBottom() - 20).withHeight (16).toNearestInt(), Justification::centred);
        }
        else
        {
            // Width per band: 0 % mono ... 100 % unchanged ... 200 % twice as wide
            const char* wid[] { ID::imgLow, ID::imgMid, ID::imgHigh };
            const float w = pv (wid[b]);
            auto track = bands[b].reduced (jmin (20.0f, bands[b].getWidth() * 0.2f), 0).withTop (plot.getY() + 48).withHeight (8);
            g.setColour (Theme::bg);
            g.fillRoundedRectangle (track, 4.0f);
            const float cx = track.getCentreX(), half = track.getWidth() * 0.5f * jlimit (0.0f, 2.0f, w / 100.0f) * 0.5f;
            g.setColour (bandCol[b].withAlpha (on ? 0.9f : 0.4f));
            g.fillRoundedRectangle (Rectangle<float>::leftTopRightBottom (cx - half, track.getY(), cx + half, track.getBottom()), 4.0f);
            g.setColour (Theme::dim);
            g.drawVerticalLine ((int) (cx - track.getWidth() * 0.25f), track.getY() - 3, track.getBottom() + 3);
            g.drawVerticalLine ((int) (cx + track.getWidth() * 0.25f), track.getY() - 3, track.getBottom() + 3);
            g.setColour (Theme::text);
            g.setFont (uiFont (12.0f));
            g.drawText ("width " + String (roundToInt (w)) + " %" + (w < 1 ? " (mono)" : ""), bands[b].withTop (track.getBottom() + 4).withHeight (16).toNearestInt(), Justification::centred);
        }
    }

    // Crossovers (draggable)
    for (int k = 0; k < 2; ++k)
    {
        const float x = k == 0 ? x1 : x2, f = k == 0 ? f1 : f2;
        const bool hot = k == draggingX || k == hoverX;
        g.setColour (Theme::text.withAlpha (hot ? 1.0f : 0.7f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        auto handle = Rectangle<float> (hot ? 14.0f : 12.0f, 26.0f).withCentre ({ x, plot.getCentreY() });
        g.setColour (Theme::panel2);
        g.fillRoundedRectangle (handle, 4.0f);
        g.setColour (Theme::text);
        g.drawRoundedRectangle (handle, 4.0f, 1.2f);
        g.setFont (uiFont (11.5f, true));
        g.drawText (ParamText::hz (f, 0), Rectangle<float> (70, 16).withCentre ({ x, handle.getBottom() + 12 }).toNearestInt(), Justification::centred);
    }

    if (! mb) paintScope (g, Rectangle<float> (170.0f, 170.0f).withPosition (plot.getRight() - 180.0f, plot.getBottom() - 200.0f));

    g.setColour (Theme::dim);
    g.setFont (uiFont (11.0f));
    g.drawText (String (mb ? "Multiband" : "Stereo") + (on ? "" : " (off)") + "  -  drag the crossover handles   |   gray = input, blue = output"
                + (mb ? "   |   orange = gain reduction per band" : "   |   vectorscope: vertical = mono, wide = stereo"),
                plot.toNearestInt().removeFromTop (16).translated (6, 2), Justification::centredLeft);
}

void SpectrumView::paintScope (Graphics& g, Rectangle<float> r)
{
    g.setColour (Theme::bg.withAlpha (0.92f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.45f;
    g.setColour (Theme::outline.withAlpha (0.6f));
    g.drawLine (c.x, r.getY() + 6, c.x, r.getBottom() - 6, 1.0f);       // mono axis
    g.drawLine (r.getX() + 6, c.y, r.getRight() - 6, c.y, 1.0f);        // side axis
    g.setFont (uiFont (10.0f));
    g.setColour (Theme::dim);
    g.drawText ("M", (int) c.x + 3, (int) r.getY() + 3, 14, 12, Justification::centredLeft);
    g.drawText ("L", (int) r.getX() + 6, (int) r.getY() + 3, 14, 12, Justification::centredLeft);
    g.drawText ("R", (int) r.getRight() - 18, (int) r.getY() + 3, 14, 12, Justification::centredRight);
    g.drawText ("correlation " + String (proc.meters.correlation.load(), 2), r.withTop (r.getBottom() - 16).toNearestInt(), Justification::centred);
    if (scope == nullptr || scope->empty()) return;

    // Auto-scaled so quiet and loud material both fill the scope
    float pk = 1.0e-4f;
    for (size_t i = 0; i < scope->size(); ++i) pk = jmax (pk, std::abs ((*scope)[i]));
    scopeScale = pk > scopeScale ? pk : scopeScale * 0.97f + pk * 0.03f;
    const float k = rad / (scopeScale * 1.4142f);
    g.setColour (Theme::accent.withAlpha (0.55f));
    for (size_t i = 0; i + 1 < scope->size(); i += 2)
    {
        const float l = (*scope)[i], rr = (*scope)[i + 1];
        const float sx = (l - rr) * 0.7071f * k, my = (l + rr) * 0.7071f * k;
        g.fillRect (c.x - sx, c.y - my, 1.6f, 1.6f);   // left channel to the left, like a classic goniometer
    }
}

void SpectrumView::mouseMove (const MouseEvent& e)
{
    if (mode != Mode::EQ)
    {
        const int h = hitCrossover (e.position.x);
        if (h != hoverX) { hoverX = h; repaint(); }
        setMouseCursor (h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
        return;
    }
    const int h = hitNode (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}
void SpectrumView::mouseExit (const MouseEvent&) { hover = -1; hoverX = -1; repaint(); }

void SpectrumView::mouseDown (const MouseEvent& e)
{
    if (mode != Mode::EQ)
    {
        draggingX = hitCrossover (e.position.x);
        if (draggingX >= 0) proc.apvts.getParameter (crossoverId (draggingX))->beginChangeGesture();
        return;
    }
    dragging = hitNode (e.position);
    if (dragging < 0) return;
    const auto& n = nodes[(size_t) dragging];
    for (auto& id : { n.freqId, n.gainId })
        if (id.isNotEmpty()) proc.apvts.getParameter (id)->beginChangeGesture();
    if (n.onId.isNotEmpty() && pv (n.onId) < 0.5f) setPv (n.onId, 1.0f);
    if (onNodeTouched) onNodeTouched();
}

void SpectrumView::mouseDrag (const MouseEvent& e)
{
    if (mode != Mode::EQ)
    {
        if (draggingX < 0) return;
        float f = freqForX (jlimit (plot.getX(), plot.getRight(), e.position.x));
        // keep the bands in order: low crossover at most 2/3 of the high one, and vice versa
        if (draggingX == 0) f = jmin (f, pv (crossoverId (1)) / 1.5f);
        else f = jmax (f, pv (crossoverId (0)) * 1.5f);
        setPv (crossoverId (draggingX), f);
        return;
    }
    if (dragging < 0) return;
    const auto& n = nodes[(size_t) dragging];
    setPv (n.freqId, freqForX (jlimit (plot.getX(), plot.getRight(), e.position.x)));
    if (n.gainId.isNotEmpty()) setPv (n.gainId, jlimit (-12.0f, 12.0f, eqForY (e.position.y)));
}

void SpectrumView::mouseUp (const MouseEvent&)
{
    if (draggingX >= 0) { proc.apvts.getParameter (crossoverId (draggingX))->endChangeGesture(); draggingX = -1; repaint(); }
    if (dragging < 0) return;
    const auto& n = nodes[(size_t) dragging];
    for (auto& id : { n.freqId, n.gainId })
        if (id.isNotEmpty()) proc.apvts.getParameter (id)->endChangeGesture();
    dragging = -1;
}

void SpectrumView::mouseDoubleClick (const MouseEvent& e)
{
    if (mode != Mode::EQ) return;
    const int h = hitNode (e.position);
    if (h < 0) return;
    const auto& n = nodes[(size_t) h];
    if (n.onId.isNotEmpty()) setPv (n.onId, pv (n.onId) > 0.5f ? 0.0f : 1.0f);
    else if (n.gainId.isNotEmpty()) setPv (n.gainId, 0.0f);
}

void SpectrumView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (mode != Mode::EQ) return;
    const int h = hitNode (e.position);
    if (h < 0 || nodes[(size_t) h].qId.isEmpty()) return;
    const auto& id = nodes[(size_t) h].qId;
    auto* p = proc.apvts.getParameter (id);
    p->beginChangeGesture();
    setPv (id, jlimit (0.1f, 10.0f, pv (id) * std::exp (w.deltaY * 1.5f)));
    p->endChangeGesture();
}

//==============================================================================
MeterPanel::MeterPanel (MasteringAudioProcessor& p) : proc (p)
{
    target.addItemList (targetNames(), 1);
    targetA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ID::target, target);
    addAndMakeVisible (target);
    matchBtn.setTooltip ("Moves the limiter gain (or input gain if the limiter is off) so the integrated loudness hits the target");
    resetBtn.setTooltip ("Reset loudness and peak measurements");
    addAndMakeVisible (matchBtn);
    addAndMakeVisible (resetBtn);
    status.setFont (uiFont (11.5f));
    status.setColour (Label::textColourId, Theme::dim);
    status.setJustificationType (Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);
    matchBtn.onClick = [this]
    {
        String msg;
        proc.matchTarget (msg);
        status.setText (msg, dontSendNotification);
    };
    resetBtn.onClick = [this]
    {
        proc.meters.inLoud.resetRequest = true;
        proc.meters.outLoud.resetRequest = true;
        for (auto& h : inHold) h = -100.0f;
        for (auto& h : outHold) h = -100.0f;
        status.setText ({}, dontSendNotification);
    };
    for (auto* a : { in, out, inHold, outHold }) for (int i = 0; i < 2; ++i) a[i] = -100.0f;
}

void MeterPanel::update()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float i = msdsp::gainToDb (proc.meters.inPeak[ch].exchange (0.0f));
        const float o = msdsp::gainToDb (proc.meters.outPeak[ch].exchange (0.0f));
        in[ch] = jmax (i, in[ch] - 0.8f);
        out[ch] = jmax (o, out[ch] - 0.8f);
        if (i >= inHold[ch]) { inHold[ch] = i; holdTimer[ch] = 45; } else if (--holdTimer[ch] < 0) inHold[ch] -= 0.5f;
        if (o >= outHold[ch]) { outHold[ch] = o; holdTimer[2 + ch] = 45; } else if (--holdTimer[2 + ch] < 0) outHold[ch] -= 0.5f;
        in[ch] = jmax (in[ch], -100.0f); out[ch] = jmax (out[ch], -100.0f);
        inHold[ch] = jmax (inHold[ch], -100.0f); outHold[ch] = jmax (outHold[ch], -100.0f);
    }
    const float g[5] { proc.grMb[0].exchange (0.0f), proc.grMb[1].exchange (0.0f), proc.grMb[2].exchange (0.0f),
                       proc.grBus.exchange (0.0f), proc.grLim.exchange (0.0f) };
    for (int k = 0; k < 5; ++k) gr[k] = jmax (g[k], gr[k] * 0.86f);
    repaint();
}

void MeterPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (22);
    levelArea = r.removeFromTop (190);
    r.removeFromTop (10);
    grArea = r.removeFromTop (22 + 5 * 17);
    r.removeFromTop (10);
    loudArea = r.removeFromTop (172);
    r.removeFromTop (6);
    auto row = r.removeFromTop (26);
    target.setBounds (row);
    r.removeFromTop (6);
    row = r.removeFromTop (26);
    matchBtn.setBounds (row.removeFromLeft (row.getWidth() * 2 / 3 - 3));
    row.removeFromLeft (6);
    resetBtn.setBounds (row);
    r.removeFromTop (4);
    corrArea = r.removeFromBottom (36);
    status.setBounds (r);
}

void MeterPanel::paint (Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (Theme::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText ("METERS", 12, 8, 200, 18, Justification::centredLeft);

    const float ceilDb = proc.apvts.getRawParameterValue (ID::limCeiling)->load();

    // ---- Peak bars ----
    {
        auto a = levelArea;
        auto scale = a.removeFromLeft (28);
        a.removeFromTop (16);
        const auto bars = a.withTrimmedBottom (2);
        constexpr float lo = -60.0f, hi = 3.0f;
        auto yOf = [&] (float db) { return jmap (jlimit (lo, hi, db), hi, lo, (float) bars.getY(), (float) bars.getBottom()); };
        g.setFont (uiFont (10.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            g.setColour (Theme::dim);
            g.drawText (String ((int) db), scale.getX(), (int) yOf (db) - 6, 24, 12, Justification::centredRight);
            g.setColour (Theme::outline.withAlpha (0.5f));
            g.drawHorizontalLine ((int) yOf (db), (float) bars.getX(), (float) bars.getRight());
        }
        const int bw = (bars.getWidth() - 30) / 4;
        const float vals[4] { in[0], in[1], out[0], out[1] }, holds[4] { inHold[0], inHold[1], outHold[0], outHold[1] };
        for (int k = 0; k < 4; ++k)
        {
            const int x = bars.getX() + k * bw + (k >= 2 ? 30 : 0);
            Rectangle<float> slot ((float) x + 2, (float) bars.getY(), (float) bw - 4, (float) bars.getHeight());
            g.setColour (Theme::bg);
            g.fillRect (slot);
            const float top = yOf (vals[k]);
            g.setGradientFill (ColourGradient (Theme::red, 0, yOf (0.0f), Theme::green, 0, yOf (-24.0f), false));
            g.fillRect (slot.withTop (top));
            g.setColour (holds[k] > -0.1f ? Theme::red : Theme::text);
            g.fillRect (slot.getX(), yOf (holds[k]), slot.getWidth(), 2.0f);
        }
        g.setColour (Theme::dim);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("IN", bars.getX(), levelArea.getY(), bw * 2, 14, Justification::centred);
        g.drawText ("OUT", bars.getX() + 2 * bw + 30, levelArea.getY(), bw * 2, 14, Justification::centred);
        g.setFont (uiFont (10.5f));
        auto pk = [] (float db) { return db <= -99.0f ? String ("-inf") : String (db, 1); };
        const float inPk = jmax (inHold[0], inHold[1]), outPk = jmax (outHold[0], outHold[1]);
        g.setColour (Theme::dim);
        g.drawText ("peak", bars.getX() + bw * 2, bars.getBottom() - 46, 30, 12, Justification::centred);
        g.setColour (Theme::text);
        g.drawText (pk (inPk), bars.getX() + bw * 2, bars.getBottom() - 30, 30, 12, Justification::centred);
        g.setColour (outPk > ceilDb + 0.05f ? Theme::red : Theme::text);
        g.drawText (pk (outPk), bars.getX() + bw * 2, bars.getBottom() - 14, 30, 12, Justification::centred);
    }

    // ---- Gain reduction ----
    {
        auto a = grArea;
        g.setColour (Theme::dim);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("GAIN REDUCTION (0-12 dB)", a.removeFromTop (20), Justification::centredLeft);
        const char* names[] { "MB Low", "MB Mid", "MB High", "Glue", "Limiter" };
        for (int k = 0; k < 5; ++k)
        {
            auto row = a.removeFromTop (17);
            g.setColour (Theme::dim);
            g.setFont (uiFont (11.0f));
            g.drawText (names[k], row.removeFromLeft (56), Justification::centredLeft);
            auto valBox = row.removeFromRight (44);
            auto bar = row.reduced (0, 4).toFloat();
            g.setColour (Theme::bg);
            g.fillRect (bar);
            g.setColour (Theme::warm);
            const float w = bar.getWidth() * jlimit (0.0f, 1.0f, gr[k] / 12.0f);
            g.fillRect (bar.withWidth (w).withX (bar.getRight() - w));
            g.setColour (Theme::text);
            g.drawText (String (-gr[k], 1), valBox, Justification::centredRight);
        }
    }

    // ---- Loudness ----
    {
        auto a = loudArea;
        auto& m = proc.meters.outLoud;
        const float I = m.integrated.load(), tp = m.truePeak.load();
        const float tgt = targetLufs (roundToInt (proc.apvts.getRawParameterValue (ID::target)->load()));
        auto fmt = [] (float v) { return v < -69.0f ? String ("--") : String (v, 1); };

        g.setColour (Theme::dim);
        g.setFont (uiFont (11.0f, true));
        g.drawText ("INTEGRATED LUFS", a.removeFromTop (16), Justification::centredLeft);
        auto big = a.removeFromTop (40);
        g.setColour (I < -69.0f ? Theme::dim : std::abs (I - tgt) <= 1.0f ? Theme::green : Theme::text);
        g.setFont (uiFont (34.0f, true));
        g.drawText (fmt (I), big.removeFromLeft (big.getWidth() / 2 + 20), Justification::centredLeft);
        g.setFont (uiFont (12.0f));
        g.setColour (Theme::dim);
        g.drawFittedText (I < -69.0f ? String ("target ") + String (tgt, 0)
                                     : "target " + String (tgt, 0) + "\n" + (I - tgt > 0 ? "+" : "") + String (I - tgt, 1) + " LU",
                          big, Justification::centredRight, 2);

        auto row = [&] (const String& name, const String& value, Colour c)
        {
            auto r = a.removeFromTop (18);
            g.setColour (Theme::dim); g.setFont (uiFont (12.0f));
            g.drawText (name, r, Justification::centredLeft);
            g.setColour (c); g.setFont (uiFont (12.5f, true));
            g.drawText (value, r, Justification::centredRight);
        };
        row ("Momentary", fmt (m.momentary.load()) + " LUFS", Theme::text);
        row ("Short-term", fmt (m.shortTerm.load()) + " LUFS", Theme::text);
        row ("Loudness range", String (m.lra.load(), 1) + " LU", Theme::text);
        row ("True peak (max)", fmt (tp) + " dBTP", tp > ceilDb + 0.1f || tp > -0.1f ? Theme::red : Theme::green);
        row ("Input short-term", fmt (proc.meters.inLoud.shortTerm.load()) + " LUFS", Theme::dim);
        const bool matching = proc.apvts.getRawParameterValue (ID::matchBypass)->load() > 0.5f;
        row ("Compare offset", matching ? ParamText::db (-proc.meters.matchDb.load(), 0) : String ("off"), Theme::dim);
    }

    // ---- Correlation ----
    {
        auto a = corrArea;
        g.setColour (Theme::dim);
        g.setFont (uiFont (11.0f, true));
        auto head = a.removeFromTop (16);
        g.drawText ("CORRELATION", head, Justification::centredLeft);
        const float c = proc.meters.correlation.load();
        g.setColour (Theme::text);
        g.drawText (String (c, 2), head, Justification::centredRight);
        auto bar = a.reduced (0, 4).toFloat();
        g.setColour (Theme::bg);
        g.fillRect (bar);
        const float cx = bar.getCentreX(), x = jmap (jlimit (-1.0f, 1.0f, c), -1.0f, 1.0f, bar.getX(), bar.getRight());
        g.setColour (c < 0.0f ? Theme::red : Theme::green);
        g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (cx, x), bar.getY(), jmax (cx, x), bar.getBottom()));
        g.setColour (Theme::dim);
        g.drawVerticalLine ((int) cx, bar.getY(), bar.getBottom());
    }
}

//==============================================================================
namespace
{
struct ExportFormat { const char* name; bool flac; int bits; };
const ExportFormat exportFormats[] { { "WAV 24-bit", false, 24 }, { "WAV 16-bit", false, 16 }, { "WAV 32-bit float", false, 32 },
                                     { "FLAC 24-bit", true, 24 }, { "FLAC 16-bit", true, 16 } };

// Renders a file through a private copy of the chain and writes the master.
class ExportJob : public ThreadWithProgressWindow
{
public:
    ExportJob (MasteringAudioProcessor& p, File in, File out, int fmt)
        : ThreadWithProgressWindow ("Exporting master...", true, true), proc (p), src (std::move (in)), dst (std::move (out)), format (exportFormats[fmt]) {}

    void run() override
    {
        res = proc.renderFile (src, dst, format.flac, format.bits, [this] (double prog)
        {
            setProgress (prog);
            return ! threadShouldExit();
        });
    }

    void threadComplete (bool userPressedCancel) override
    {
        const bool shaping = proc.apvts.getRawParameterValue (ID::ditherShape)->load() > 0.5f && format.bits == 16;
        if (userPressedCancel || res.cancelled)
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Export canceled", "Nothing was written.");
        else if (res.error.isNotEmpty())
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Export failed", res.error);
        else
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Export finished",
                dst.getFullPathName() + "\n\n"
                + "Integrated: " + (res.integrated < -69.0f ? String ("--") : String (res.integrated, 1) + " LUFS") + "\n"
                + "True peak: " + String (res.truePeak, 2) + " dBTP\n"
                + "Loudness range: " + String (res.lra, 1) + " LU\n\n"
                + format.name + (format.bits < 32 ? ", TPDF dither" : "") + (shaping ? " + noise shaping" : ""));
        delete this;
    }

private:
    MasteringAudioProcessor& proc;
    File src, dst;
    ExportFormat format;
    MasteringAudioProcessor::RenderResult res;
};
} // namespace

PlayerBar::PlayerBar (MasteringAudioProcessor& p) : proc (p), thumb (512, p.formatManager, cache)
{
    thumb.addChangeListener (this);
    for (auto* b : { &openBtn, &playBtn, &stopBtn, &loopBtn, &ejectBtn, &exportBtn }) addAndMakeVisible (*b);
    loopBtn.setClickingTogglesState (true);
    loopBtn.setToggleState (proc.loopPlayback.load(), dontSendNotification);
    loopBtn.onClick = [this] { proc.setLooping (loopBtn.getToggleState()); };
    openBtn.onClick = [this] { openFile(); };
    playBtn.onClick = [this]
    {
        if (! proc.isFileLoaded()) { openFile(); return; }
        if (proc.transport.isPlaying()) proc.transport.stop();
        else
        {
            if (proc.transport.getCurrentPosition() >= proc.transport.getLengthInSeconds() - 0.01) proc.transport.setPosition (0.0);
            proc.transport.start();
        }
    };
    stopBtn.onClick = [this] { proc.transport.stop(); proc.transport.setPosition (0.0); };
    ejectBtn.onClick = [this] { proc.ejectFile(); thumb.clear(); repaint(); };
    exportBtn.onClick = [this] { exportFile(); };
    exportBtn.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.25f));
    for (int i = 0; i < (int) std::size (exportFormats); ++i) format.addItem (exportFormats[i].name, i + 1);
    format.setSelectedId (1, dontSendNotification);
    format.setTooltip ("Export format. 16/24-bit exports are TPDF-dithered automatically.");
    addAndMakeVisible (format);
    if (proc.getLoadedFile().existsAsFile()) thumb.setSource (new FileInputSource (proc.getLoadedFile()));
}

PlayerBar::~PlayerBar() { thumb.removeChangeListener (this); }

void PlayerBar::changeListenerCallback (ChangeBroadcaster*) { repaint(); }

bool PlayerBar::load (const File& f)
{
    if (! proc.loadFile (f))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Can't open file", f.getFileName() + " is not a supported audio file.");
        return false;
    }
    thumb.setSource (new FileInputSource (f));
    repaint();
    return true;
}

void PlayerBar::openFile()
{
    chooser = std::make_unique<FileChooser> ("Open a mix to master", File::getSpecialLocation (File::userMusicDirectory),
                                             proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() && load (f)) proc.transport.start();
    });
}

void PlayerBar::exportFile()
{
    const auto in = proc.getLoadedFile();
    if (! in.existsAsFile())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Export", "Open (or drop) an audio file first. The export renders that file through the current settings.");
        return;
    }
    const int fmt = jmax (0, format.getSelectedId() - 1);
    const String ext = exportFormats[fmt].flac ? ".flac" : ".wav";
    chooser = std::make_unique<FileChooser> ("Export master", in.getSiblingFile (in.getFileNameWithoutExtension() + " (Master)" + ext), "*" + ext);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this, in, fmt, ext] (const FileChooser& fc)
    {
        auto out = fc.getResult();
        if (out == File()) return;
        if (! out.hasFileExtension (ext)) out = out.withFileExtension (ext);
        if (out == in)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Export", "Choose a different file name - the source can't be overwritten.");
            return;
        }
        proc.transport.stop();
        (new ExportJob (proc, in, out, fmt))->launchThread();
    });
}

void PlayerBar::update()
{
    playBtn.setButtonText (proc.transport.isPlaying() ? "Pause" : "Play");
    repaint (wave);
}

void PlayerBar::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto row = r.withSizeKeepingCentre (r.getWidth(), 28);
    for (auto* b : { &openBtn, &playBtn, &stopBtn, &loopBtn, &ejectBtn })
    {
        b->setBounds (row.removeFromLeft (b == &openBtn ? 72 : 58).withY (row.getY()));
        row.removeFromLeft (4);
    }
    exportBtn.setBounds (row.removeFromRight (130));
    row.removeFromRight (6);
    format.setBounds (row.removeFromRight (140));
    row.removeFromRight (10);
    row.removeFromLeft (8);
    wave = Rectangle<int> (row.getX(), r.getY(), row.getWidth(), r.getHeight());
}

void PlayerBar::paint (Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (Theme::bg);
    g.fillRoundedRectangle (wave.toFloat(), 4.0f);
    const double len = proc.transport.getLengthInSeconds();
    auto time = [] (double s) { const int t = (int) s; return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2); };

    if (! proc.isFileLoaded() || len <= 0.0)
    {
        g.setColour (Theme::dim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Open or drop a WAV / AIFF / FLAC / MP3 / OGG file to master it. Live input is used when no file is loaded.", wave.reduced (10, 0), Justification::centred);
        return;
    }
    auto w = wave.reduced (2, 14);
    g.setColour (Theme::accent.withAlpha (0.55f));
    thumb.drawChannels (g, w, 0.0, thumb.getTotalLength(), 1.0f);
    const float x = (float) w.getX() + (float) (proc.transport.getCurrentPosition() / len) * (float) w.getWidth();
    g.setColour (Theme::text);
    g.drawVerticalLine ((int) x, (float) wave.getY(), (float) wave.getBottom());
    g.setFont (uiFont (11.5f));
    g.drawText (proc.getLoadedFile().getFileName(), wave.reduced (6, 1).removeFromTop (13), Justification::centredLeft);
    g.drawText (time (proc.transport.getCurrentPosition()) + " / " + time (len), wave.reduced (6, 1).removeFromTop (13), Justification::centredRight);
}

void PlayerBar::mouseDown (const MouseEvent& e) { mouseDrag (e); }
void PlayerBar::mouseDrag (const MouseEvent& e)
{
    if (! wave.contains (e.getMouseDownPosition()) || ! proc.isFileLoaded()) return;
    const double frac = jlimit (0.0, 1.0, (double) (e.x - wave.getX()) / (double) wave.getWidth());
    proc.transport.setPosition (frac * proc.transport.getLengthInSeconds());
}

//==============================================================================
AssistantPanel::AssistantPanel (MasteringAudioProcessor& p) : proc (p)
{
    curveBox.addSectionHeading ("Generic target curves");
    for (auto c : assistant::curveMenuOrder()) curveBox.addItem (assistant::curveInfo (c).name, (int) c + 1);
    curveBox.addSeparator();
    curveBox.addItem (assistant::curveInfo (assistant::Curve::Reference).name, (int) assistant::Curve::Reference + 1);
    styleBox.addItemList (assistant::styleNames(), 1);
    curveBox.setSelectedId (proc.assistantOptions.curve + 1, dontSendNotification);
    styleBox.setSelectedId (proc.assistantOptions.style + 1, dontSendNotification);
    curveBox.setTooltip (assistant::curveInfo ((assistant::Curve) proc.assistantOptions.curve).description);
    styleBox.setTooltip ("Dynamic keeps the most punch; Loud trades some dynamics for level. Loudness is capped if it would over-compress.");
    amount.setSliderStyle (Slider::LinearHorizontal);
    amount.setRange (0.0, 100.0, 1.0);
    amount.setTextValueSuffix (" %");
    amount.setTextBoxStyle (Slider::TextBoxRight, false, 54, 20);
    amount.setColour (Slider::trackColourId, Theme::accent);
    amount.setColour (Slider::backgroundColourId, Theme::outline);
    amount.setColour (Slider::thumbColourId, Theme::text);
    amount.setValue (proc.assistantOptions.amount * 100.0, dontSendNotification);
    amount.setTooltip ("How far EQ and stereo width move toward the target (loudness is always matched)");
    refLoudBtn.setClickingTogglesState (true);
    refLoudBtn.setToggleState (proc.assistantOptions.referenceLoudness, dontSendNotification);
    refLoudBtn.setTooltip ("Aim for the references' loudness instead of the style's (still capped to protect dynamics)");
    learnBtn.setClickingTogglesState (true);
    learnBtn.setColour (TextButton::buttonOnColourId, Theme::red);
    learnBtn.setTooltip ("Capture what is playing (DAW track or live input) for analysis");
    listenBtn.setClickingTogglesState (true);
    listenBtn.setColour (TextButton::buttonOnColourId, Theme::warm);
    listenBtn.setTooltip ("Hear the selected reference instead of your master, at the same loudness");
    runBtn.setColour (TextButton::buttonColourId, Theme::accent.withAlpha (0.35f));
    runBtn.setTooltip ("Analyze the mix, choose settings, render and refine them, then apply. Revert undoes it.");
    revertBtn.setTooltip ("Go back to the settings you had before the assistant ran");

    notes.setMultiLine (true);
    notes.setReadOnly (true);
    notes.setCaretVisible (false);
    notes.setScrollbarsShown (true);
    notes.setFont (uiFont (13.0f));
    notes.setColour (TextEditor::backgroundColourId, Theme::bg);
    notes.setColour (TextEditor::outlineColourId, Theme::outline);
    notes.setText ("Choose a target and style, then click Analyze & Apply.\n\n"
                   "Generic curves (Pop, Rock, Hip-Hop, R&B, Electronic, Singer-Songwriter, Acoustic, Jazz, Classical) are built from published "
                   "measurements of thousands of commercial recordings: the average spectrum of popular music, adjusted for how "
                   "percussive each style is and for measured genre differences. Classical is extrapolated.\n\n"
                   "Reference tracks: matches the average tonal balance, width and loudness of the masters you add.");

    curveBox.onChange = [this] { syncOptions(); };
    styleBox.onChange = [this] { syncOptions(); };
    amount.onValueChange = [this] { syncOptions(); };
    refLoudBtn.onClick = [this] { syncOptions(); };
    learnBtn.onClick = [this] { if (learnBtn.getToggleState()) proc.startLearn(); else proc.stopLearn(); };
    addRefBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Add reference tracks", File::getSpecialLocation (File::userMusicDirectory),
                                                 proc.formatManager.getWildcardForAllFormats());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc) { addReferences (fc.getResults()); });
    };
    removeRefBtn.onClick = [this] { proc.removeReference (selectedRef); selectedRef = -1; };
    listenBtn.onClick = [this]
    {
        if (! proc.setListenReference (selectedRef, listenBtn.getToggleState())) listenBtn.setToggleState (false, dontSendNotification);
    };
    runBtn.onClick = [this]
    {
        String err;
        if (! proc.startAssistant (err)) AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Master Assistant", err);
    };
    cancelBtn.onClick = [this] { proc.cancelAssistant(); };
    revertBtn.onClick = [this] { proc.revertAssistant(); notes.setText ("Reverted to the previous settings."); };

    for (Component* c : { (Component*) &curveBox, (Component*) &styleBox, (Component*) &amount, (Component*) &refLoudBtn, (Component*) &learnBtn,
                          (Component*) &addRefBtn, (Component*) &removeRefBtn, (Component*) &listenBtn, (Component*) &runBtn,
                          (Component*) &cancelBtn, (Component*) &revertBtn, (Component*) &notes })
        addAndMakeVisible (c);
    if (auto* r = proc.getLastAssistantResult()) showResult (*r);
    update();
}

void AssistantPanel::syncOptions()
{
    proc.assistantOptions.curve = jmax (0, curveBox.getSelectedId() - 1);
    proc.assistantOptions.style = jmax (0, styleBox.getSelectedId() - 1);
    proc.assistantOptions.amount = (float) amount.getValue() / 100.0f;
    proc.assistantOptions.referenceLoudness = refLoudBtn.getToggleState();
    curveBox.setTooltip (assistant::curveInfo ((assistant::Curve) proc.assistantOptions.curve).description);
    repaint();
}

void AssistantPanel::addReferences (const Array<File>& files)
{
    for (auto& f : files)
        if (f.existsAsFile()) proc.addReferenceFile (f);
    if (! files.isEmpty() && curveBox.getSelectedId() != 3) { curveBox.setSelectedId (3, dontSendNotification); syncOptions(); }
}

void AssistantPanel::showResult (const assistant::Result& r)
{
    String t = r.summary + "\n";
    for (auto& n : r.notes) t << "\n- " << n;
    t << "\n\nEverything above is now on the normal controls - adjust to taste, or click Revert.";
    notes.setText (t);
}

assistant::Spectrum AssistantPanel::currentTarget (bool& valid) const
{
    valid = true;
    const int c = jmax (0, curveBox.getSelectedId() - 1);
    if (c != (int) assistant::Curve::Reference) return assistant::targetCurve ((assistant::Curve) c);
    assistant::Spectrum s {};
    valid = ! refs.empty();
    for (auto& r : refs)
        for (int b = 0; b < assistant::NumBands; ++b) s[(size_t) b] += r.spectrum[(size_t) b] / (float) refs.size();
    return s;
}

void AssistantPanel::update()
{
    refs = proc.getReferences();
    if (selectedRef >= (int) refs.size()) selectedRef = -1;
    busy = proc.isAssistantBusy();
    progress = proc.getAssistantProgress();
    statusText = proc.getAssistantStatus();

    runBtn.setEnabled (! busy);
    const String runText = busy ? "Analyzing... " + String (roundToInt (progress * 100.0f)) + "%" : String ("Analyze & Apply");
    if (runBtn.getButtonText() != runText) runBtn.setButtonText (runText);
    cancelBtn.setVisible (busy);
    revertBtn.setVisible (! busy);
    revertBtn.setEnabled (proc.canRevertAssistant());
    removeRefBtn.setEnabled (selectedRef >= 0);
    listenBtn.setEnabled (selectedRef >= 0 || proc.isListeningToReference());
    listenBtn.setToggleState (proc.isListeningToReference(), dontSendNotification);
    refLoudBtn.setEnabled (curveBox.getSelectedId() == 3);

    const bool learning = proc.isLearning();
    learnBtn.setToggleState (learning, dontSendNotification);
    const double secs = proc.getLearnedSeconds();
    const String mmss = String ((int) secs / 60) + ":" + String ((int) secs % 60).paddedLeft ('0', 2);
    learnBtn.setButtonText (learning ? "Stop learning (" + mmss + ")" : secs > 0 ? "Learn again" : "Learn");
    if (proc.isFileLoaded())
        sourceText = "Analyzes the whole loaded file:\n" + proc.getLoadedFile().getFileName();
    else if (secs > 0)
        sourceText = "Captured " + mmss + " of audio" + (learning ? " - keep playing, the whole song is best." : ".");
    else
        sourceText = proc.isStandalone() ? "Open a file in the player above, or press Learn and play live input."
                                         : "Press Learn, play the song in your DAW (the whole song is best), then stop.";

    if (auto r = proc.takeAssistantResult())
        if (! r->cancelled && r->error.isEmpty()) { proc.applyAssistantResult (*r); showResult (*r); }
    repaint();
}

void AssistantPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    r.removeFromTop (26);
    auto left = r.removeFromLeft (340);
    r.removeFromLeft (16);

    // Top to bottom in order of use, with the main action right after the choices, so it can never
    // be squeezed out: the reference list (last) is the part that shrinks when the window is short.
    auto row = [&left] (int h) { auto x = left.removeFromTop (h); left.removeFromTop (6); return x; };
    left.removeFromTop (16);
    sourceArea = row (32);
    learnBtn.setBounds (row (26));
    auto inl = [&row] (Component& c) { auto x = row (26); x.removeFromLeft (96); c.setBounds (x); };
    inl (curveBox);
    inl (styleBox);
    inl (amount);

    auto run = row (32);
    cancelBtn.setBounds (run.removeFromRight (90));
    revertBtn.setBounds (cancelBtn.getBounds());
    run.removeFromRight (6);
    runBtn.setBounds (run);
    statusArea = row (30);

    // References: heading row (with the loudness toggle on the right), list, buttons.
    auto head = left.removeFromTop (22);
    refLoudBtn.setBounds (head.removeFromRight (160));
    left.removeFromTop (4);
    auto btns = left.removeFromBottom (26);
    left.removeFromBottom (6);
    listArea = left.withHeight (juce::jlimit (20, 4 * 20, left.getHeight() / 20 * 20));
    const int bw = (btns.getWidth() - 12) / 3;
    addRefBtn.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (6);
    removeRefBtn.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (6);
    listenBtn.setBounds (btns);

    chartArea = r.removeFromTop (juce::jmax (150, r.getHeight() * 55 / 100));
    r.removeFromTop (8);
    notes.setBounds (r);
}

void AssistantPanel::mouseDown (const MouseEvent& e)
{
    if (! listArea.contains (e.getPosition())) return;
    const int i = (e.y - listArea.getY()) / 20;
    if (i >= jmax (1, listArea.getHeight() / 20)) return;
    selectedRef = i < (int) refs.size() ? i : -1;
    update();
}

void AssistantPanel::paint (Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (Theme::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText ("MASTER ASSISTANT", 14, 8, 300, 20, Justification::centredLeft);

    auto heading = [&g] (const String& t, int x, int y) { g.setColour (Theme::dim); g.setFont (uiFont (11.0f, true)); g.drawText (t, x, y, 300, 14, Justification::centredLeft); };
    heading ("SOURCE", sourceArea.getX(), sourceArea.getY() - 16);
    g.setColour (Theme::text);
    g.setFont (uiFont (12.5f));
    g.drawFittedText (sourceText, sourceArea, Justification::topLeft, 2);
    for (auto* c : { (Component*) &curveBox, (Component*) &styleBox, (Component*) &amount })
    {
        const String t = c == &curveBox ? "TARGET" : c == &styleBox ? "STYLE" : "MATCH AMOUNT";
        g.setColour (Theme::dim); g.setFont (uiFont (11.0f, true));
        g.drawText (t, sourceArea.getX(), c->getY(), 92, c->getHeight(), Justification::centredLeft);
    }
    heading ("REFERENCE TRACKS", listArea.getX(), listArea.getY() - 16);

    // reference list
    g.setColour (Theme::bg);
    g.fillRect (listArea);
    if (refs.empty())
    {
        g.setColour (Theme::dim); g.setFont (uiFont (12.0f));
        g.drawFittedText (listArea.getHeight() >= 40 ? "Drop finished masters you like here, or click Add...\nThey are analyzed once and saved with the session." : "Drop finished masters here, or click Add...", listArea.reduced (8, 2), Justification::centred, 3);
    }
    const int visibleRows = jmax (1, listArea.getHeight() / 20);
    for (int i = 0; i < (int) refs.size() && i < visibleRows; ++i)
    {
        auto row = Rectangle<int> (listArea.getX(), listArea.getY() + i * 20, listArea.getWidth(), 20);
        if (i == selectedRef) { g.setColour (Theme::accent.withAlpha (0.25f)); g.fillRect (row); }
        g.setColour (File (refs[(size_t) i].filePath).existsAsFile() ? Theme::text : Theme::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (refs[(size_t) i].name, row.reduced (6, 0).withTrimmedRight (110), Justification::centredLeft);
        g.setColour (Theme::dim);
        g.drawText (String (refs[(size_t) i].integrated, 1) + " LUFS  PLR " + String (refs[(size_t) i].plr, 1), row.reduced (6, 0), Justification::centredRight);
    }
    if ((int) refs.size() > visibleRows) { g.setColour (Theme::dim); g.drawText ("+" + String ((int) refs.size() - visibleRows) + " more", listArea.getRight() - 70, listArea.getBottom() - 14, 64, 12, Justification::centredRight); }

    // status + progress
    if (busy)
    {
        auto bar = statusArea.removeFromBottom (6).toFloat();
        g.setColour (Theme::bg); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (Theme::accent); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, progress)), 3.0f);
    }
    g.setColour (Theme::dim); g.setFont (uiFont (12.0f));
    g.drawFittedText (statusText, statusArea, Justification::centredLeft, 2);

    // ---- chart ----
    auto c = chartArea.toFloat();
    g.setColour (Theme::bg);
    g.fillRoundedRectangle (c, 4.0f);
    auto plot = c.reduced (34.0f, 22.0f).withTrimmedBottom (2.0f);
    static constexpr float top = 12.0f, bottom = -30.0f;
    auto xOf = [&plot] (int b) { return plot.getX() + plot.getWidth() * (float) b / (float) (assistant::NumBands - 1); };
    auto yOf = [&plot] (float db) { return jmap (jlimit (bottom, top, db), top, bottom, plot.getY(), plot.getBottom()); };
    g.setFont (uiFont (10.5f));
    for (int b = 2; b < assistant::NumBands; b += 3)
    {
        g.setColour (Theme::outline.withAlpha (0.5f));
        g.drawVerticalLine ((int) xOf (b), plot.getY(), plot.getBottom());
        g.setColour (Theme::dim);
        g.drawText (assistant::bandLabel (b), (int) xOf (b) - 20, (int) plot.getBottom() + 2, 40, 14, Justification::centred);
    }
    for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f })
    {
        g.setColour (Theme::outline.withAlpha (db == 0.0f ? 0.9f : 0.4f));
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (Theme::dim);
        g.drawText (String ((int) db), (int) c.getX() + 2, (int) yOf (db) - 7, 28, 14, Justification::centredRight);
    }
    auto curve = [&] (const assistant::Spectrum& s, Colour col, float thick, bool dashed)
    {
        Path p;
        for (int b = 0; b < assistant::NumBands; ++b)
            if (b == 0) p.startNewSubPath (xOf (b), yOf (s[(size_t) b])); else p.lineTo (xOf (b), yOf (s[(size_t) b]));
        g.setColour (col);
        if (dashed)
        {
            Path d; const float dl[] { 6.0f, 4.0f };
            PathStrokeType (thick).createDashedStroke (d, p, dl, 2);
            g.fillPath (d);
        }
        else g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved));
    };
    bool targetValid = false;
    const auto tgt = currentTarget (targetValid);
    const auto* last = proc.getLastAssistantResult();
    if (last != nullptr)
    {
        curve (last->mixSpectrum, Theme::dim, 1.6f, false);
        curve (last->resultSpectrum, Theme::accent, 2.2f, false);
        curve (last->targetSpectrum, Theme::warm, 2.0f, true);
    }
    if (targetValid && (last == nullptr || tgt != last->targetSpectrum)) curve (tgt, Theme::warm.withAlpha (last ? 0.45f : 1.0f), 2.0f, last != nullptr);

    g.setFont (uiFont (11.5f));
    float lx = plot.getX() + 6;
    auto legend = [&] (const String& t, Colour col)
    {
        g.setColour (col); g.fillRect (lx, c.getY() + 9, 14.0f, 3.0f);
        g.setColour (Theme::text); g.drawText (t, (int) lx + 18, (int) c.getY() + 3, 140, 14, Justification::centredLeft);
        lx += 24.0f + (float) GlyphArrangement::getStringWidthInt (uiFont (11.5f), t);
    };
    legend ("Target", Theme::warm);
    if (last != nullptr) { legend ("Your mix", Theme::dim); legend ("After assistant", Theme::accent); }
    g.setColour (Theme::dim);
    const auto& info = assistant::curveInfo ((assistant::Curve) jmax (0, curveBox.getSelectedId() - 1));
    g.drawText (String (info.name) + ": " + info.description, chartArea.reduced (8, 3).withTrimmedLeft ((int) (lx - c.getX())),
                Justification::topRight);
    g.drawText ("1/3-octave long-term balance, 0 dB = 200 Hz-4 kHz average", chartArea.reduced (8, 3), Justification::bottomRight);
}

//==============================================================================
MasteringAudioProcessorEditor::MasteringAudioProcessorEditor (MasteringAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p), glueView (p, history), satView (p, history), limView (p, history), assistantPanel (p),
      strip (p.apvts, { { "Input", "" }, { "EQ", ID::eqOn }, { "Multiband", ID::mbOn }, { "Glue Comp", ID::busOn },
                        { "Saturation", ID::satOn }, { "Stereo", ID::imgOn }, { "Limiter", ID::limOn }, { "Output", "" } }),
      meters (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;
    using G = ModulePanel::Group;

    panels.push_back (std::make_unique<ModulePanel> (s, "Input", std::vector<G> {
        { "Input", { { ID::inGain, "Gain" } } } },
        "Trims the level going into the chain. Aim for mix peaks around -6 dBFS so the compressors and saturation respond as designed. "
        "Drop a file on the window (standalone) or play your session (plugin) and watch the IN meters."));

    panels.push_back (std::make_unique<ModulePanel> (s, "Equalizer", std::vector<G> {
        { "Low Cut",    { { ID::hpOn, "On" }, { ID::hpFreq, "Freq" }, { ID::hpSlope, "Slope" } } },
        { "Low Shelf",  { { ID::lsGain, "Gain" }, { ID::lsFreq, "Freq" } } },
        { "Bell 1",     { { ID::p1Gain, "Gain" }, { ID::p1Freq, "Freq" }, { ID::p1Q, "Q" } } },
        { "Bell 2",     { { ID::p2Gain, "Gain" }, { ID::p2Freq, "Freq" }, { ID::p2Q, "Q" } } },
        { "Bell 3",     { { ID::p3Gain, "Gain" }, { ID::p3Freq, "Freq" }, { ID::p3Q, "Q" } } },
        { "High Shelf", { { ID::hsGain, "Gain" }, { ID::hsFreq, "Freq" } } },
        { "High Cut",   { { ID::lpOn, "On" }, { ID::lpFreq, "Freq" } } },
        { "Channels",   { { ID::eqMode, "Apply to" } } } },
        "Drag the points on the analyzer (labeled like this panel); scroll on a bell to change Q, double-click to reset."));

    std::vector<G> mbGroups { { "Crossover", { { ID::mbXLow, "Low/Mid" }, { ID::mbXHigh, "Mid/High" },
                                               { ID::mbLinear, "Lin. Phase" }, { ID::mbAutoRel, "Auto Rel." } } } };
    const char* bandNames[] { "Low Band", "Mid Band", "High Band" };
    for (int b = 0; b < 3; ++b)
        mbGroups.push_back ({ bandNames[b], { { ID::mb (b, "Thr"), "Threshold" }, { ID::mb (b, "Ratio"), "Ratio" }, { ID::mb (b, "Att"), "Attack" },
                                              { ID::mb (b, "Rel"), "Release" }, { ID::mb (b, "Gain"), "Gain" }, { ID::mb (b, "Solo"), "Solo" }, { ID::mb (b, "Byp"), "Bypass" } } });
    panels.push_back (std::make_unique<ModulePanel> (s, "Multiband Compressor", mbGroups,
        "Three phase-aligned Linkwitz-Riley bands, each with its own compressor. Solo a band to hear it. "
        "Lin. Phase uses linear-phase crossovers (no phase shift at the band edges; adds about 90 ms of latency). "
        "Auto Rel. makes release program-dependent: fast after short peaks, slow on sustained material."));

    panels.push_back (std::make_unique<ModulePanel> (s, "Glue Compressor", std::vector<G> {
        { "Dynamics", { { ID::busThr, "Threshold" }, { ID::busRatio, "Ratio" }, { ID::busAtt, "Attack" }, { ID::busRel, "Release" } } },
        { "Output",   { { ID::busMakeup, "Makeup" }, { ID::busMix, "Mix" }, { ID::busHpf, "SC Low Cut" } } } },
        "Stereo-linked bus compressor with a soft knee. Slow attack (10-30 ms) and 1.5-2:1 for 1-3 dB of reduction glues a mix together. "
        "SC Low Cut stops the kick and bass from pumping the whole mix. Mix blends in the uncompressed signal (parallel compression)."));

    panels.push_back (std::make_unique<ModulePanel> (s, "Saturation", std::vector<G> {
        { "Character", { { ID::satType, "Type" }, { ID::satDrive, "Drive" }, { ID::satMix, "Mix" } } } },
        "8x oversampled harmonic saturation. Tape is smooth and symmetric, Tube adds even harmonics, Soft Clip rounds off peaks. "
        "Tape (Modeled) is a magnetic hysteresis model with record/playback EQ: highs saturate first and the color follows the music. "
        "Tube (Modeled) shifts its bias with the level, so loud passages bloom with even harmonics. "
        "Quiet passages stay at the same level, so Drive changes the color rather than the volume."));

    panels.push_back (std::make_unique<ModulePanel> (s, "Stereo Imager", std::vector<G> {
        { "Crossover", { { ID::imgXLow, "Low/Mid" }, { ID::imgXHigh, "Mid/High" } } },
        { "Width",     { { ID::imgLow, "Low" }, { ID::imgMid, "Mid" }, { ID::imgHigh, "High" } } },
        { "Mono Bass", { { ID::imgMono, "On" }, { ID::imgMonoFreq, "Below" } } },
        { "Options",   { { ID::imgSafe, "Mono-Safe" }, { ID::imgLinear, "Lin. Phase" } } } },
        "Mid/side width per band: 0% is mono, 100% unchanged, 200% twice as wide. Mono Bass removes stereo below its frequency "
        "(tighter low end, safe for vinyl and clubs). Mono-Safe stops widening from pushing the correlation below zero. "
        "Lin. Phase: no phase shift at the crossovers (adds about 90 ms of latency)."));

    panels.push_back (std::make_unique<ModulePanel> (s, "Limiter", std::vector<G> {
        { "Limiter", { { ID::limGain, "Gain" }, { ID::limCeiling, "Ceiling" }, { ID::limRelease, "Release" }, { ID::limTP, "True Peak" },
                       { ID::limStyle, "Style" } } } },
        "Look-ahead brickwall limiter. Gain pushes the level into the limiter; Ceiling is the maximum output. True Peak detects "
        "inter-sample peaks (8x) so the master stays under the ceiling after MP3/AAC conversion. -1 dBTP is the usual streaming ceiling. "
        "Transparent style: longer look-ahead, program-dependent release and smoother gain changes - cleaner when pushed hard. "
        "Use Match Target in the meter panel to set Gain from the measured loudness."));

    panels.push_back (std::make_unique<ModulePanel> (s, "Output", std::vector<G> {
        { "Output", { { ID::outGain, "Trim" } } },
        { "Dither", { { ID::ditherBits, "Bit depth" }, { ID::ditherShape, "Shaping" } } } },
        "Trim lowers the final level (it can't raise it above the limiter ceiling). Turn Dither on only when your DAW renders to 16 or 24-bit; "
        "use it once, as the very last step. Shaping moves the dither noise to where hearing is least sensitive (psychoacoustic 9th-order "
        "at 44.1/48 kHz). The standalone exporter dithers automatically."));

    for (auto& panel : panels) addChildComponent (*panel);
    strip.onSelect = [this] (int i) { showModule (i); };
    spectrum.onNodeTouched = [this] { if (strip.getSelected() != 1) strip.setSelected (1); };

    addAndMakeVisible (spectrum);
    addChildComponent (glueView);
    addChildComponent (satView);
    addChildComponent (limView);
    spectrum.setScope (&scopeData);
    pinEqBtn.setClickingTogglesState (true);
    pinEqBtn.setTooltip ("Keep the EQ curve on the chart instead of showing the selected module's view");
    pinEqBtn.onClick = [this] { updateChartView(); };
    addAndMakeVisible (pinEqBtn);
    addChildComponent (assistantPanel);
    addAndMakeVisible (strip);
    addAndMakeVisible (meters);
    strip.setSelected (1);

    if (proc.isStandalone())
    {
        player = std::make_unique<PlayerBar> (proc);
        addAndMakeVisible (*player);
    }

    // ---- Top bar ----
    presetBox.setTooltip ("Presets");
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);
    rebuildPresetMenu();
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (1); };
    saveBtn.onClick = [this] { savePresetDialog(); };
    assistantBtn.setClickingTogglesState (true);
    assistantBtn.setTooltip ("Master Assistant: analyze the track and set up the chain for a target curve or reference tracks");
    assistantBtn.onClick = [this] { showAssistant (assistantBtn.getToggleState()); };
    addAndMakeVisible (assistantBtn);
    aBtn.onClick = [this] { proc.selectAB (0); };
    bBtn.onClick = [this] { proc.selectAB (1); };
    copyBtn.onClick = [this] { proc.copyCurrentToOther(); copyBtn.setButtonText (proc.getABSlot() == 0 ? "Copy A>B" : "Copy B>A"); };
    aBtn.setTooltip ("Settings slot A");
    bBtn.setTooltip ("Settings slot B");
    copyBtn.setTooltip ("Copy the current slot's settings into the other slot");
    compareBtn.setClickingTogglesState (true);
    compareBtn.setTooltip ("Hear the unprocessed input (time-aligned). With Level Match on it plays at the same loudness as the master, so you judge tone, not volume.");
    compareBtn.setColour (TextButton::buttonOnColourId, Theme::warm);
    matchBtn.setClickingTogglesState (true);
    matchBtn.setTooltip ("Level-match the Compare signal to the processed output");
    compareA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, ID::bypass, compareBtn);
    matchA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (s, ID::matchBypass, matchBtn);
    monitorBox.addItemList ({ "Stereo", "Mono", "Side", "Left", "Right" }, 1);
    monitorBox.setTooltip ("Monitoring: listen in mono, to the side signal only, or to one channel. Affects what you hear (and the plugin output), not the meters.");
    monitorA = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (s, ID::monitor, monitorBox);
    for (Component* c : { (Component*) &prevBtn, (Component*) &nextBtn, (Component*) &saveBtn, (Component*) &aBtn, (Component*) &bBtn,
                          (Component*) &copyBtn, (Component*) &compareBtn, (Component*) &matchBtn, (Component*) &monitorBox })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (1200, player ? 800 : 720, 8192, 8192); // large maximum so a maximised window fills any screen
    setSize (1320, player ? 880 : 800);
    startTimerHz (30);
}

MasteringAudioProcessorEditor::~MasteringAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MasteringAudioProcessorEditor::showModule (int i)
{
    for (int k = 0; k < (int) panels.size(); ++k) panels[(size_t) k]->setVisible (k == i);
    updateChartView();
}

// The chart shows the view for the selected module: EQ curve (Input/EQ/Output), bands with gain
// reduction (Multiband), transfer curve and history (Glue), curve and harmonics (Saturation),
// bands, widths and vectorscope (Stereo), waveform and reduction (Limiter).
void MasteringAudioProcessorEditor::updateChartView()
{
    const bool assistant = assistantPanel.isVisible();
    const int m = pinEqBtn.getToggleState() ? 1 : strip.getSelected();
    spectrum.setMode (m == 2 ? SpectrumView::Mode::Multiband : m == 5 ? SpectrumView::Mode::Stereo : SpectrumView::Mode::EQ);
    spectrum.setVisible (! assistant && m != 3 && m != 4 && m != 6);
    glueView.setVisible (! assistant && m == 3);
    satView.setVisible (! assistant && m == 4);
    limView.setVisible (! assistant && m == 6);
    pinEqBtn.setVisible (! assistant);
}

// Learning indicator: a blinking badge on the chart, visible whatever view or panel is open.
void MasteringAudioProcessorEditor::paintOverChildren (Graphics& g)
{
    if (chartArea.isEmpty()) return;
    const bool busy = proc.isAssistantBusy(), learning = proc.isLearning();
    const bool notice = Time::getMillisecondCounterHiRes() < noticeUntil;
    if (! busy && ! learning && ! notice) return;

    // One badge at the top of the chart; working beats learning beats the finished notice.
    String text, sub;
    Colour accent, fill;
    float progress = -1.0f;
    if (busy)
    {
        text = "ANALYZING & APPLYING  " + String (roundToInt (proc.getAssistantProgress() * 100.0f)) + "%";
        sub = proc.getAssistantStatus();
        accent = Theme::warm; fill = Colour (0xff3a2a12);
        progress = jlimit (0.0f, 1.0f, proc.getAssistantProgress());
    }
    else if (learning)
    {
        auto mmss = [] (double s) { return String ((int) s / 60) + ":" + String ((int) s % 60).paddedLeft ('0', 2); };
        text = "LEARNING  " + mmss (proc.getLearnedSeconds()) + " captured  (up to " + mmss (proc.getLearnCapacitySeconds()) + ")";
        accent = Theme::red; fill = Colour (0xff3a1416);
    }
    else
    {
        text = noticeText;
        accent = noticeColour; fill = noticeColour.darker (2.2f);
    }

    const float w = (float) jmax (GlyphArrangement::getStringWidthInt (uiFont (13.0f, true), text),
                                  GlyphArrangement::getStringWidthInt (uiFont (11.5f), sub)) + 50.0f;
    const float h = sub.isNotEmpty() || progress >= 0.0f ? 48.0f : 28.0f;
    auto badge = Rectangle<float> (jmin (w, (float) chartArea.getWidth() - 40.0f), h)
                     .withCentre ({ (float) chartArea.getCentreX(), (float) chartArea.getY() + 12.0f + h * 0.5f });
    g.setColour (fill.withAlpha (0.96f));
    g.fillRoundedRectangle (badge, 14.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (badge, 14.0f, 1.5f);

    auto line = badge.withHeight (28.0f);
    const auto dotCentre = Point<float> (line.getX() + 18.0f, line.getCentreY());
    if (busy)
    {
        // spinner
        const float a = (float) blink * 0.35f;
        Path arc;
        arc.addCentredArc (dotCentre.x, dotCentre.y, 6.0f, 6.0f, 0.0f, a, a + 4.2f, true);
        g.setColour (accent);
        g.strokePath (arc, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    else if (learning)
    {
        g.setColour (accent.withAlpha ((blink / 15) % 2 == 0 ? 1.0f : 0.25f));   // ~1 Hz blink
        g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre (dotCentre));
    }
    g.setColour (Theme::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText (text, line.withTrimmedLeft (busy || learning ? 32.0f : 14.0f).withTrimmedRight (10.0f).toNearestInt(), Justification::centredLeft);
    if (sub.isNotEmpty())
    {
        g.setColour (Theme::dim);
        g.setFont (uiFont (11.5f));
        g.drawText (sub, badge.withTrimmedTop (24.0f).withHeight (14.0f).withTrimmedLeft (32.0f).toNearestInt(), Justification::centredLeft);
    }
    if (progress >= 0.0f)
    {
        auto bar = Rectangle<float> (badge.getX() + 32.0f, badge.getBottom() - 9.0f, badge.getWidth() - 46.0f, 4.0f);
        g.setColour (Theme::bg);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 2.0f);
    }
}

void MasteringAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Theme::bg);
    // "OPEN MASTERING" + "STUDIO" (accent) + version, straight from the build settings
    const auto titleFont = uiFont (TitleFontSize, true);
    const String first = "OPEN MASTERING ";
    const int firstW = GlyphArrangement::getStringWidthInt (titleFont, first);
    const int studioW = GlyphArrangement::getStringWidthInt (titleFont, "STUDIO");
    g.setFont (titleFont);
    g.setColour (Theme::text);
    g.drawText (first, 16, 10, firstW + 4, 28, Justification::centredLeft);
    g.setColour (Theme::accent);
    g.drawText ("STUDIO", 16 + firstW, 10, studioW + 4, 28, Justification::centredLeft);
    g.setColour (Theme::dim);
    g.setFont (uiFont (11.5f));
    g.drawText (versionText(), 16 + firstW + studioW + 7, 17, 60, 18, Justification::centredLeft);
}

Rectangle<int> MasteringAudioProcessorEditor::titleArea()
{
    const auto titleFont = uiFont (TitleFontSize, true);
    const int w = GlyphArrangement::getStringWidthInt (titleFont, "OPEN MASTERING STUDIO")
                + 7 + GlyphArrangement::getStringWidthInt (uiFont (11.5f), versionText());
    return { 10, 6, 6 + w + 6, 36 };
}

// About box (the "Appropriate Legal Notices" of the GNU AGPL): copyright, no warranty, license.
String MasteringAudioProcessorEditor::aboutText()
{
    return productName() + " " + versionText() + "\n"
           "Copyright (C) 2026 Al Oomens\n\n"
           "This program is free software: you can redistribute it and/or modify it under the terms of the "
           "GNU Affero General Public License as published by the Free Software Foundation, either version 3 "
           "of the License, or (at your option) any later version.\n\n"
           "This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without "
           "even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU "
           "Affero General Public License (LICENSE.txt, included with the program) for more details.\n\n"
           "The source code is distributed alongside the program (MasteringStudio-<version>-source.zip).\n\n"
           "Built with JUCE (AGPLv3) and the Steinberg VST3 and ASIO SDKs (GPLv3). "
           "VST and ASIO are trademarks of Steinberg Media Technologies GmbH. "
           "See THIRD-PARTY-NOTICES.txt.";
}

void MasteringAudioProcessorEditor::mouseUp (const MouseEvent& e)
{
    if (titleArea().contains (e.getPosition()))
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "About " + productName(), aboutText(), "OK", this);
}

void MasteringAudioProcessorEditor::mouseMove (const MouseEvent& e)
{
    setMouseCursor (titleArea().contains (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void MasteringAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (48).reduced (10, 9);
    top.removeFromLeft (titleArea().getRight());   // title and version
    monitorBox.setBounds (top.removeFromRight (100)); top.removeFromRight (10);
    matchBtn.setBounds (top.removeFromRight (100)); top.removeFromRight (4);
    compareBtn.setBounds (top.removeFromRight (86)); top.removeFromRight (16);
    copyBtn.setBounds (top.removeFromRight (82)); top.removeFromRight (4);
    bBtn.setBounds (top.removeFromRight (32)); top.removeFromRight (2);
    aBtn.setBounds (top.removeFromRight (32)); top.removeFromRight (8);
    // The preset box takes what is left (up to 230 px), so the longer title fits at the smallest window size.
    constexpr int leftFixed = 4 + 28 + 2 + 28 + 4 + 56 + 10 + 96;
    presetBox.setBounds (top.removeFromLeft (jlimit (140, 230, top.getWidth() - leftFixed))); top.removeFromLeft (4);
    prevBtn.setBounds (top.removeFromLeft (28)); top.removeFromLeft (2);
    nextBtn.setBounds (top.removeFromLeft (28)); top.removeFromLeft (4);
    saveBtn.setBounds (top.removeFromLeft (56)); top.removeFromLeft (10);
    assistantBtn.setBounds (top.removeFromLeft (96));

    area.reduce (10, 0);
    area.removeFromBottom (10);
    if (player) { player->setBounds (area.removeFromTop (64)); area.removeFromTop (8); }
    meters.setBounds (area.removeFromRight (262));
    area.removeFromRight (8);
    const auto panelArea = area.removeFromBottom (226);
    for (auto& pnl : panels) pnl->setBounds (panelArea);
    area.removeFromBottom (4);
    strip.setBounds (area.removeFromBottom (44));
    area.removeFromBottom (4);
    spectrum.setBounds (area);
    glueView.setBounds (area);
    satView.setBounds (area);
    limView.setBounds (area);
    assistantPanel.setBounds (area);
    chartArea = area;
    pinEqBtn.setBounds (area.getRight() - 132, area.getY() + 6, 124, 22);
}

// Standalone only: JUCE's standalone window offers just minimise/close. Once the editor is inside
// that window, turn on the maximise button (double-clicking the title bar works too) and set the icon.
void MasteringAudioProcessorEditor::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();
    if (! proc.isStandalone()) return;
    auto* window = findParentComponentOfClass<DocumentWindow>();
    if (window == nullptr || window == setUpWindow.getComponent()) return;
    setUpWindow = window;
    window->setTitleBarButtonsRequired (DocumentWindow::allButtons, false);
    window->setIcon (ImageCache::getFromMemory (BinaryData::icon_big_png, BinaryData::icon_big_pngSize));
    window->setName (productName() + " " + versionText().fromFirstOccurrenceOf ("v", false, false));
}

void MasteringAudioProcessorEditor::showAssistant (bool show)
{
    assistantBtn.setToggleState (show, dontSendNotification);
    assistantPanel.setVisible (show);
    updateChartView();
}

void MasteringAudioProcessorEditor::timerCallback()
{
    history.pullFrom (proc.vizFifo);
    {
        // newest ~1500 stereo pairs for the vectorscope
        static constexpr int Pairs = 1500;
        scopeData.resize ((size_t) Pairs * 2);
        std::vector<float> fresh ((size_t) 8192 * 2);
        int got, total = 0;
        while ((got = proc.scopeFifo.pull (fresh.data() + 2 * (total % 8192), 8192 - total % 8192)) > 0) total += got;
        const int keep = jmin (Pairs, jmin (total, 8192));
        if (keep > 0)
        {
            std::move (scopeData.begin() + 2 * keep, scopeData.end(), scopeData.begin());
            for (int i = 0; i < keep; ++i)
            {
                const int src = (total - keep + i) % 8192;
                scopeData[(size_t) (2 * (Pairs - keep + i))] = fresh[(size_t) (2 * src)];
                scopeData[(size_t) (2 * (Pairs - keep + i) + 1)] = fresh[(size_t) (2 * src + 1)];
            }
        }
    }
    if (glueView.isVisible()) glueView.repaint();
    if (satView.isVisible()) satView.repaint();
    if (limView.isVisible()) limView.repaint();
    ++blink;
    // Apply a finished Assistant result first, so the "applied" notice below names the new settings.
    assistantPanel.update();

    // Top-bar Assistant button and chart badges: learning, working (with progress), and a short
    // "applied" / "canceled" notice when a run ends.
    {
        const bool busy = proc.isAssistantBusy(), learning = proc.isLearning();
        if (wasBusy && ! busy)
        {
            const String status = proc.getAssistantStatus();
            const bool ok = status.startsWith ("Done");
            noticeText = ok ? String::fromUTF8 ("\xe2\x9c\x93 Settings applied  -  ") + proc.currentPresetName
                            : status.isNotEmpty() ? status : String ("Assistant stopped");
            noticeColour = ok ? Theme::green : status.startsWith ("Canceled") ? Theme::dim : Theme::red;
            noticeUntil = Time::getMillisecondCounterHiRes() + 4000.0;
        }
        wasBusy = busy;
        const bool notice = Time::getMillisecondCounterHiRes() < noticeUntil;
        const String btnText = busy ? "Analyzing " + String (roundToInt (proc.getAssistantProgress() * 100.0f)) + "%"
                             : learning ? String::fromUTF8 ("\xe2\x97\x8f Learning") : String ("Assistant");
        const Colour btnColour = busy ? Theme::warm.withAlpha (0.55f)
                               : learning ? Theme::red.withAlpha ((blink / 15) % 2 == 0 ? 0.7f : 0.35f) : Theme::panel2;
        if (assistantBtn.getButtonText() != btnText) assistantBtn.setButtonText (btnText);
        assistantBtn.setColour (TextButton::buttonColourId, btnColour);
        if (busy || learning || notice || badgeShown)
            repaint (chartArea.getX(), chartArea.getY(), chartArea.getWidth(), 70);
        badgeShown = busy || learning || notice;
    }
    spectrum.update();
    meters.update();
    strip.refresh();
    if (player) player->update();
    aBtn.setToggleState (proc.getABSlot() == 0, dontSendNotification);
    bBtn.setToggleState (proc.getABSlot() == 1, dontSendNotification);
    if (proc.currentPresetName != shownPreset)
    {
        shownPreset = proc.currentPresetName;
        presetBox.setText (shownPreset, dontSendNotification);
    }
}

void MasteringAudioProcessorEditor::rebuildPresetMenu()
{
    presetBox.clear (dontSendNotification);
    auto names = MasteringAudioProcessor::factoryPresetNames();
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < names.size(); ++i) presetBox.addItem (names[i], i + 1);
    userPresets = proc.getUserPresets();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < userPresets.size(); ++i) presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 100 + i);
    }
    presetBox.addSeparator();
    presetBox.addItem ("Save preset...", 1000);
    presetBox.addItem ("Open presets folder", 1001);
    shownPreset = proc.currentPresetName;
    presetBox.setText (shownPreset, dontSendNotification);
}

void MasteringAudioProcessorEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id <= 0) return;
    if (id < 100) proc.applyFactoryPreset (id - 1);
    else if (id < 1000) proc.loadUserPreset (userPresets[id - 100]);
    else if (id == 1000) savePresetDialog();
    else if (id == 1001)
    {
        auto folder = proc.getUserPresetFolder();
        folder.createDirectory();
        folder.startAsProcess();
    }
    shownPreset = proc.currentPresetName;
    presetBox.setText (shownPreset, dontSendNotification);
}

void MasteringAudioProcessorEditor::stepPreset (int delta)
{
    const int n = MasteringAudioProcessor::getNumFactoryPrograms(); // the < > buttons step through factory presets only
    proc.applyFactoryPreset ((juce::jmin (proc.getCurrentProgram(), n) + delta + n) % n);
}

void MasteringAudioProcessorEditor::savePresetDialog()
{
    auto* w = new AlertWindow ("Save preset", "Name for this preset (saved in Documents\\" + productName() + "\\Presets):", MessageBoxIconType::NoIcon);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", proc.currentPresetName);
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    SafePointer<MasteringAudioProcessorEditor> safe (this);
    w->enterModalState (true, ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = w->getTextEditorContents ("name");
        if (! safe->proc.saveUserPreset (name))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Save preset", "Couldn't save \"" + name + "\".");
        safe->rebuildPresetMenu();
    }), true);
}

bool MasteringAudioProcessorEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (File (f).getFileExtension()) == nullptr) return false;
    if (assistantPanel.isVisible()) return ! files.isEmpty();   // drop = add reference tracks
    return player != nullptr && files.size() == 1;
}

void MasteringAudioProcessorEditor::filesDropped (const StringArray& files, int, int)
{
    if (assistantPanel.isVisible())
    {
        Array<File> list;
        for (auto& f : files) list.add (File (f));
        assistantPanel.addReferences (list);
        return;
    }
    if (player != nullptr && player->load (File (files[0]))) proc.transport.start();
}
