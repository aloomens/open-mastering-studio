#include "ModuleViews.h"
#include "PluginEditor.h"

using namespace juce;

namespace
{
constexpr double HistorySeconds = 4.0;

void drawPanel (Graphics& g, Rectangle<float> r)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (r, 6.0f);
}

void drawOffNote (Graphics& g, Rectangle<float> r, const String& text)
{
    g.setColour (Theme::bg.withAlpha (0.55f));
    g.fillRect (r);
    g.setColour (Theme::text);
    g.setFont (uiFont (15.0f, true));
    g.drawFittedText (text, r.toNearestInt(), Justification::centred, 2);
}

void dashedLine (Graphics& g, Line<float> l, float thickness = 1.2f)
{
    const float d[] { 5.0f, 4.0f };
    g.drawDashedLine (l, d, 2, thickness);
}

int framesFor (double seconds, double fs) { return jmax (1, (int) (seconds * fs / MasterChain::VizChunk)); }
} // namespace

//==============================================================================
GlueView::GlueView (MasteringAudioProcessor& p, const VizHistory& h) : proc (p), hist (h) {}

float GlueView::pv (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

void GlueView::resized()
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
    r.removeFromTop (22.0f);
    const float side = jmin (r.getHeight() - 26.0f, r.getWidth() * 0.42f);
    curveArea = r.removeFromLeft (side).withHeight (side).translated (24.0f, 0.0f).withTrimmedRight (24.0f);
    r.removeFromLeft (40.0f);
    historyArea = r.withTrimmedBottom (18.0f).withTrimmedRight (34.0f);
}

void GlueView::paint (Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const bool on = pv (ID::busOn) > 0.5f;
    const float thr = pv (ID::busThr), ratio = pv (ID::busRatio), makeup = pv (ID::busMakeup);
    static constexpr float lo = -48.0f, hi = 0.0f, knee = 8.0f;
    const double fs = proc.getSampleRateNow() > 0 ? proc.getSampleRateNow() : 48000.0;

    g.setColour (Theme::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText ("GLUE COMP  -  input vs output", 14, 8, 400, 18, Justification::centredLeft);
    g.drawText ("LEVEL AND GAIN REDUCTION  (last 4 s)", (int) historyArea.getX(), 8, 400, 18, Justification::centredLeft);

    // ---- Transfer curve ----
    const auto c = curveArea;
    auto xOf = [&c] (float db) { return jmap (jlimit (lo, hi, db), lo, hi, c.getX(), c.getRight()); };
    auto yOf = [&c] (float db) { return jmap (jlimit (lo, hi, db), lo, hi, c.getBottom(), c.getY()); };
    g.setColour (Theme::bg);
    g.fillRect (c);
    g.setFont (uiFont (10.5f));
    for (float db = lo; db <= hi; db += 12.0f)
    {
        g.setColour (Theme::outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) yOf (db), c.getX(), c.getRight());
        g.drawVerticalLine ((int) xOf (db), c.getY(), c.getBottom());
        g.setColour (Theme::dim);
        g.drawText (String ((int) db), (int) c.getX() - 30, (int) yOf (db) - 7, 26, 14, Justification::centredRight);
        g.drawText (String ((int) db), (int) xOf (db) - 15, (int) c.getBottom() + 2, 30, 14, Justification::centred);
    }
    g.setColour (Theme::dim.withAlpha (0.6f));
    dashedLine (g, { xOf (lo), yOf (lo), xOf (hi), yOf (hi) });

    Path curve;
    for (float in = lo; in <= hi + 0.01f; in += 0.25f)
    {
        const float out = in + msdsp::CompCore::curve (in, thr, ratio, knee);
        if (in == lo) curve.startNewSubPath (xOf (in), yOf (out)); else curve.lineTo (xOf (in), yOf (out));
    }
    g.setColour (on ? Theme::warm : Theme::dim);
    g.strokePath (curve, PathStrokeType (2.2f));

    g.setColour (Theme::accent.withAlpha (0.8f));
    dashedLine (g, { xOf (thr), c.getY(), xOf (thr), c.getBottom() }, 1.4f);
    g.fillRoundedRectangle (Rectangle<float> (10.0f, 18.0f).withCentre ({ xOf (thr), c.getY() + 12.0f }), 3.0f);

    const auto now = hist.recentMax (framesFor (0.05, fs));
    if (on && now.busLevel > lo)
    {
        const Point<float> dot { xOf (now.busLevel), yOf (now.busLevel - now.busGr) };
        g.setColour (Theme::text);
        g.fillEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (dot));
        g.setColour (Theme::accent);
        g.drawEllipse (Rectangle<float> (15.0f, 15.0f).withCentre (dot), 1.5f);
    }
    g.setColour (Theme::dim);
    g.setFont (uiFont (11.5f));
    g.drawText ("Threshold " + ParamText::db (thr, 0) + "  |  " + ParamText::ratio (ratio, 0) + "  |  makeup " + ParamText::db (makeup, 0)
                + "   (drag to set the threshold)", c.withY (c.getBottom() + 16).withHeight (16).withWidth (c.getWidth() + 80).toNearestInt(), Justification::centredLeft);

    // ---- History: level lane (input and after glue) and reduction lane ----
    const auto hA = historyArea;
    auto levelLane = hA.withHeight (hA.getHeight() * 0.6f);
    auto grLane = hA.withTrimmedTop (hA.getHeight() * 0.6f + 10.0f);
    g.setColour (Theme::bg);
    g.fillRect (levelLane); g.fillRect (grLane);
    auto yL = [&levelLane] (float db) { return jmap (jlimit (lo, hi, db), lo, hi, levelLane.getBottom(), levelLane.getY()); };
    static constexpr float grMax = 12.0f;
    auto yG = [&grLane] (float gr) { return grLane.getY() + grLane.getHeight() * jlimit (0.0f, 1.0f, gr / grMax); };
    g.setFont (uiFont (10.5f));
    for (float db : { -12.0f, -24.0f, -36.0f })
    {
        g.setColour (Theme::outline.withAlpha (0.45f)); g.drawHorizontalLine ((int) yL (db), levelLane.getX(), levelLane.getRight());
        g.setColour (Theme::dim); g.drawText (String ((int) db), (int) levelLane.getRight() + 4, (int) yL (db) - 7, 30, 14, Justification::centredLeft);
    }
    for (float gr : { 3.0f, 6.0f, 9.0f })
    {
        g.setColour (Theme::outline.withAlpha (0.45f)); g.drawHorizontalLine ((int) yG (gr), grLane.getX(), grLane.getRight());
        g.setColour (Theme::dim); g.drawText ("-" + String ((int) gr), (int) grLane.getRight() + 4, (int) yG (gr) - 7, 30, 14, Justification::centredLeft);
    }
    g.setColour (Theme::accent.withAlpha (0.5f));
    dashedLine (g, { levelLane.getX(), yL (thr), levelLane.getRight(), yL (thr) });

    const int width = jmax (1, (int) hA.getWidth());
    const int total = jmin (hist.count(), framesFor (HistorySeconds, fs));
    const float perPx = (float) framesFor (HistorySeconds, fs) / (float) width;
    Path inPath, outPath;
    bool started = false;
    float curGr = 0.0f;
    for (int px = width - 1; px >= 0; --px)
    {
        const int a0 = (int) ((width - 1 - px) * perPx), a1 = jmax (a0 + 1, (int) ((width - px) * perPx));
        if (a0 >= total) break;
        float lvl = -100.0f, gr = 0.0f;
        for (int a = a0; a < jmin (a1, total); ++a) { lvl = jmax (lvl, hist.at (a).busLevel); gr = jmax (gr, hist.at (a).busGr); }
        if (! on) gr = 0.0f;
        if (px == width - 1) curGr = gr;
        const float x = hA.getX() + (float) px;
        if (gr > 0.01f) { g.setColour (Theme::warm.withAlpha (0.85f)); g.drawVerticalLine ((int) x, grLane.getY(), yG (gr)); }
        if (! started) { inPath.startNewSubPath (x, yL (lvl)); outPath.startNewSubPath (x, yL (lvl - gr)); started = true; }
        else { inPath.lineTo (x, yL (lvl)); outPath.lineTo (x, yL (lvl - gr)); }
    }
    g.setColour (Theme::dim);
    g.strokePath (inPath, PathStrokeType (1.3f));
    g.setColour (Theme::accent);
    g.strokePath (outPath, PathStrokeType (1.6f));

    g.setFont (uiFont (11.5f));
    g.setColour (Theme::dim);
    g.drawText ("gray: level into the glue   blue: after reduction   dashed: threshold", levelLane.reduced (6, 3).toNearestInt(), Justification::topLeft);
    g.drawText ("gain reduction " + String (-curGr, 1) + " dB", grLane.reduced (6, 3).toNearestInt(), Justification::bottomLeft);

    if (! on) drawOffNote (g, getLocalBounds().toFloat().reduced (8), "Glue Comp is off - switch it on in the chain strip below");
}

void GlueView::mouseMove (const MouseEvent& e)
{
    setMouseCursor (curveArea.expanded (6).contains (e.position) ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void GlueView::mouseDown (const MouseEvent& e)
{
    if (! curveArea.expanded (6).contains (e.position)) return;
    dragging = true;
    proc.apvts.getParameter (ID::busThr)->beginChangeGesture();
    mouseDrag (e);
}

void GlueView::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    auto* p = proc.apvts.getParameter (ID::busThr);
    const float db = jmap (jlimit (curveArea.getX(), curveArea.getRight(), e.position.x), curveArea.getX(), curveArea.getRight(), -48.0f, 0.0f);
    p->setValueNotifyingHost (p->convertTo0to1 (jlimit (-40.0f, 0.0f, db)));
}

void GlueView::mouseUp (const MouseEvent&)
{
    if (dragging) proc.apvts.getParameter (ID::busThr)->endChangeGesture();
    dragging = false;
}

//==============================================================================
SaturationView::SaturationView (MasteringAudioProcessor& p, const VizHistory& h) : proc (p), hist (h) {}

float SaturationView::pv (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

void SaturationView::resized()
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
    r.removeFromTop (22.0f);
    const float side = jmin (r.getHeight() - 22.0f, r.getWidth() * 0.42f);
    curveArea = r.removeFromLeft (side).withHeight (side).translated (24.0f, 0.0f).withTrimmedRight (24.0f);
    r.removeFromLeft (40.0f);
    harmArea = r.withTrimmedBottom (40.0f).withTrimmedRight (10.0f);
}

void SaturationView::paint (Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const bool on = pv (ID::satOn) > 0.5f;
    const int type = roundToInt (pv (ID::satType));
    const float drive = msdsp::dbToGain (pv (ID::satDrive)), mix = pv (ID::satMix) * 0.01f;
    const double fs = proc.getSampleRateNow() > 0 ? proc.getSampleRateNow() : 48000.0;
    auto transfer = [type, drive, mix] (float x) { return x + mix * (msdsp::Saturator::shape (type, x, drive) - x); };

    static const char* typeNames[] { "Tape", "Tube", "Soft Clip", "Tape (Modeled) resting", "Tube (Modeled) resting" };
    static const char* typeNotes[] { "symmetric: adds odd harmonics (3rd, 5th) - edge and density",
                                     "asymmetric: adds even harmonics (2nd) as well - warmth",
                                     "symmetric, rounds peaks off firmly: odd harmonics, more bite",
                                     "hysteresis model: odd harmonics; highs saturate first and it follows the music, so this is a guide",
                                     "bias follows the level: even harmonics grow on loud passages, so this is the quiet-passage curve" };
    g.setColour (Theme::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText ("SATURATION  -  " + String (typeNames[jlimit (0, 4, type)]) + " curve", 14, 8, 500, 18, Justification::centredLeft);
    g.drawText ("HARMONICS ADDED AT THE CURRENT LEVEL", (int) harmArea.getX(), 8, 500, 18, Justification::centredLeft);

    // ---- Curve ----
    static constexpr float range = 1.5f;
    const auto c = curveArea;
    auto xOf = [&c] (float v) { return jmap (v, -range, range, c.getX(), c.getRight()); };
    auto yOf = [&c] (float v) { return jmap (jlimit (-range, range, v), -range, range, c.getBottom(), c.getY()); };
    g.setColour (Theme::bg);
    g.fillRect (c);

    const float peak = hist.recentMax (framesFor (0.15, fs)).satIn;
    shownPeak = peak > shownPeak ? peak : shownPeak * 0.92f + peak * 0.08f;
    if (on && shownPeak > 1.0e-4f)
    {
        g.setColour (Theme::accent.withAlpha (0.12f));
        g.fillRect (Rectangle<float>::leftTopRightBottom (xOf (-jmin (shownPeak, range)), c.getY(), xOf (jmin (shownPeak, range)), c.getBottom()));
    }
    g.setFont (uiFont (10.5f));
    for (float v : { -1.0f, 0.0f, 1.0f })
    {
        g.setColour (Theme::outline.withAlpha (0.55f));
        g.drawHorizontalLine ((int) yOf (v), c.getX(), c.getRight());
        g.drawVerticalLine ((int) xOf (v), c.getY(), c.getBottom());
    }
    g.setColour (Theme::dim);
    g.drawText ("+full scale", (int) xOf (1.0f) - 34, (int) c.getBottom() + 2, 68, 14, Justification::centred);
    g.drawText ("-full scale", (int) xOf (-1.0f) - 34, (int) c.getBottom() + 2, 68, 14, Justification::centred);
    g.drawText ("in", (int) c.getRight() - 20, (int) yOf (0.0f) - 15, 18, 14, Justification::centredRight);
    g.drawText ("out", (int) xOf (0.0f) + 3, (int) c.getY() + 2, 30, 14, Justification::centredLeft);
    g.setColour (Theme::dim.withAlpha (0.6f));
    dashedLine (g, { xOf (-range), yOf (-range), xOf (range), yOf (range) });

    Path curve;
    for (int i = 0; i <= 300; ++i)
    {
        const float x = -range + 2.0f * range * (float) i / 300.0f;
        if (i == 0) curve.startNewSubPath (xOf (x), yOf (transfer (x))); else curve.lineTo (xOf (x), yOf (transfer (x)));
    }
    g.setColour (on ? Theme::warm : Theme::dim);
    g.strokePath (curve, PathStrokeType (2.2f));
    g.setColour (Theme::dim);
    g.setFont (uiFont (11.5f));
    g.drawText ("Drive " + ParamText::db (pv (ID::satDrive), 0) + "  |  Mix " + String (roundToInt (mix * 100)) + " %  |  shaded: where your signal is",
                c.withY (c.getBottom() + 18).withHeight (16).withWidth (c.getWidth() + 120).toNearestInt(), Justification::centredLeft);

    // ---- Harmonics of a sine at the current peak level ----
    const bool silent = shownPeak < 1.0e-3f;
    const float a = silent ? 0.5f : jmin (shownPeak, range);
    constexpr int N = 512, H = 9;
    double re[H + 1] {}, im[H + 1] {};
    for (int k = 0; k < N; ++k)
    {
        const double ph = 2.0 * MathConstants<double>::pi * k / N;
        const double y = transfer (a * (float) std::sin (ph));
        for (int h = 1; h <= H; ++h) { re[h] += y * std::cos (h * ph); im[h] += y * std::sin (h * ph); }
    }
    double mag[H + 1] {};
    for (int h = 1; h <= H; ++h) mag[h] = std::sqrt (re[h] * re[h] + im[h] * im[h]);
    const auto hA = harmArea;
    static constexpr float top = 0.0f, bottom = -100.0f;
    auto yH = [&hA] (float db) { return jmap (jlimit (bottom, top, db), top, bottom, hA.getY(), hA.getBottom()); };
    g.setColour (Theme::bg);
    g.fillRect (hA);
    g.setFont (uiFont (10.5f));
    for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
    {
        g.setColour (Theme::outline.withAlpha (0.45f)); g.drawHorizontalLine ((int) yH (db), hA.getX(), hA.getRight());
        g.setColour (Theme::dim); g.drawText (String ((int) db), (int) hA.getRight() + 2, (int) yH (db) - 7, 30, 14, Justification::centredLeft);
    }
    const float bw = hA.getWidth() / (float) (H - 1);
    for (int h = 2; h <= H; ++h)
    {
        const float db = (float) (20.0 * std::log10 (mag[h] / jmax (1e-12, mag[1]) + 1e-12));
        auto bar = Rectangle<float>::leftTopRightBottom (hA.getX() + (h - 2) * bw + bw * 0.2f, yH (db), hA.getX() + (h - 1) * bw - bw * 0.2f, hA.getBottom());
        g.setColour (((h % 2 == 0) ? Theme::warm : Theme::accent).withAlpha (on ? 0.9f : 0.3f));
        if (db > bottom) g.fillRect (bar);
        g.setColour (Theme::dim);
        g.drawText ("H" + String (h), (int) bar.getX() - 4, (int) hA.getBottom() + 2, (int) bar.getWidth() + 8, 14, Justification::centred);
        if (db > bottom + 4) { g.setColour (Theme::text); g.drawText (String (roundToInt (db)), (int) bar.getX() - 6, (int) bar.getY() - 15, (int) bar.getWidth() + 12, 14, Justification::centred); }
    }
    g.setColour (Theme::dim);
    g.setFont (uiFont (11.5f));
    g.drawFittedText (String (typeNotes[jlimit (0, 4, type)]) + "\norange = even harmonics, blue = odd; dB relative to the note itself"
                      + (silent ? "  (no signal: shown for a -6 dBFS tone)" : ""),
                      hA.withY (hA.getBottom() + 16).withHeight (32).toNearestInt(), Justification::topLeft, 2);

    if (! on) drawOffNote (g, getLocalBounds().toFloat().reduced (8), "Saturation is off - switch it on in the chain strip below");
}

//==============================================================================
LimiterView::LimiterView (MasteringAudioProcessor& p, const VizHistory& h) : proc (p), hist (h) {}

float LimiterView::pv (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

void LimiterView::resized()
{
    auto r = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
    r.removeFromTop (22.0f);
    r.removeFromRight (40.0f);
    waveArea = r.withHeight (r.getHeight() * 0.66f);
    grArea = r.withTrimmedTop (r.getHeight() * 0.66f + 12.0f).withTrimmedBottom (16.0f);
}

void LimiterView::paint (Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const bool on = pv (ID::limOn) > 0.5f;
    const float ceilDb = pv (ID::limCeiling), ceil = msdsp::dbToGain (ceilDb);
    const double fs = proc.getSampleRateNow() > 0 ? proc.getSampleRateNow() : 48000.0;

    g.setColour (Theme::text);
    g.setFont (uiFont (13.0f, true));
    g.drawText ("LIMITER  -  output waveform and gain reduction (last 4 s)", 14, 8, 600, 18, Justification::centredLeft);

    const auto w = waveArea;
    const float mid = w.getCentreY(), half = w.getHeight() * 0.5f;
    auto yW = [mid, half] (float a) { return mid - jlimit (-1.05f, 1.05f, a) / 1.05f * half; };
    static constexpr float grMax = 12.0f;
    auto yG = [this] (float gr) { return grArea.getY() + grArea.getHeight() * jlimit (0.0f, 1.0f, gr / grMax); };
    g.setColour (Theme::bg);
    g.fillRect (w); g.fillRect (grArea);
    g.setFont (uiFont (10.5f));
    for (float gr : { 3.0f, 6.0f, 9.0f })
    {
        g.setColour (Theme::outline.withAlpha (0.45f)); g.drawHorizontalLine ((int) yG (gr), grArea.getX(), grArea.getRight());
        g.setColour (Theme::dim); g.drawText ("-" + String ((int) gr), (int) grArea.getRight() + 4, (int) yG (gr) - 7, 30, 14, Justification::centredLeft);
    }
    g.setColour (Theme::outline.withAlpha (0.5f));
    g.drawHorizontalLine ((int) mid, w.getX(), w.getRight());

    const int width = jmax (1, (int) w.getWidth());
    const int span = framesFor (HistorySeconds, fs);
    const int total = jmin (hist.count(), span);
    const float perPx = (float) span / (float) width;
    float maxGr = 0.0f, curGr = 0.0f;
    for (int px = width - 1; px >= 0; --px)
    {
        const int a0 = (int) ((width - 1 - px) * perPx), a1 = jmax (a0 + 1, (int) ((width - px) * perPx));
        if (a0 >= total) break;
        float pk = 0.0f, gr = 0.0f;
        for (int a = a0; a < jmin (a1, total); ++a) { pk = jmax (pk, hist.at (a).outPeak); gr = jmax (gr, hist.at (a).limGr); }
        if (! on) gr = 0.0f;
        if (px == width - 1) curGr = gr;
        maxGr = jmax (maxGr, gr);
        const float x = w.getX() + (float) px;
        g.setColour ((gr > 0.05f ? Theme::accent.interpolatedWith (Theme::warm, jmin (1.0f, gr / 6.0f)) : Theme::accent).withAlpha (0.85f));
        g.drawVerticalLine ((int) x, yW (pk), yW (-pk));
        if (gr > 0.01f) { g.setColour (Theme::warm.withAlpha (0.85f)); g.drawVerticalLine ((int) x, grArea.getY(), yG (gr)); }
    }

    // Ceiling
    g.setColour (Theme::red.withAlpha (0.9f));
    dashedLine (g, { w.getX(), yW (ceil), w.getRight(), yW (ceil) }, 1.4f);
    dashedLine (g, { w.getX(), yW (-ceil), w.getRight(), yW (-ceil) }, 1.4f);
    g.setFont (uiFont (11.0f));
    g.drawText ("ceiling " + String (ceilDb, 1) + (pv (ID::limTP) > 0.5f ? " dBTP" : " dB"), (int) w.getRight() + 2, (int) yW (ceil) - 7, 60, 14, Justification::centredLeft);

    g.setColour (Theme::dim);
    g.setFont (uiFont (11.5f));
    g.drawText ("gain into limiter " + ParamText::db (pv (ID::limGain), 0) + "  |  release " + ParamText::ms (pv (ID::limRelease), 0)
                + "  |  orange = being limited", w.reduced (6, 4).toNearestInt(), Justification::topLeft);
    g.drawText ("gain reduction now " + String (-curGr, 1) + " dB   max " + String (-maxGr, 1) + " dB",
                grArea.reduced (6, 3).toNearestInt(), Justification::bottomLeft);

    if (! on) drawOffNote (g, getLocalBounds().toFloat().reduced (8), "Limiter is off - switch it on in the chain strip below");
}
