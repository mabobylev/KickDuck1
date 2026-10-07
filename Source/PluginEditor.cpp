#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const juce::Colour bg            = juce::Colour::fromRGB(7, 9, 13);
    const juce::Colour panel         = juce::Colour::fromRGB(16, 19, 25);
    const juce::Colour panel2        = juce::Colour::fromRGB(11, 14, 19);
    const juce::Colour panel3        = juce::Colour::fromRGB(20, 24, 31);
    const juce::Colour grid          = juce::Colour::fromRGB(38, 43, 52);
    const juce::Colour gridBright    = juce::Colour::fromRGB(61, 68, 80);
    const juce::Colour text          = juce::Colour::fromRGB(236, 240, 247);
    const juce::Colour muted         = juce::Colour::fromRGB(133, 141, 154);
    const juce::Colour kickColour    = juce::Colour::fromRGB(255, 158, 68);
    const juce::Colour bassOutColour = juce::Colour::fromRGB(69, 222, 153);
    const juce::Colour curveColour   = juce::Colour::fromRGB(191, 118, 255);
    const juce::Colour grColour      = juce::Colour::fromRGB(255, 91, 116);

    float dbNorm(float db) noexcept
    {
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }

    std::vector<DuckingCurve::Point> defaultCurve()
    {
        // Stored DSP y: 1 = unity/no ducking, 0 = maximum ducking.
        // The editor draws this inverted, so both ends are visually at the bottom.
        return {
            { 0.00f, 1.00f },
            { 0.18f, 0.86f },
            { 0.72f, 0.22f },
            { 0.93f, 0.10f },
            { 1.00f, 1.00f }
        };
    }

    bool isUnityCurve(const std::vector<DuckingCurve::Point>& p)
    {
        if (p.size() < 2)
            return true;
        for (const auto& point : p)
            if (std::abs(point.y - 1.0f) > 0.0001f)
                return false;
        return true;
    }

    void cacheNumber(juce::String& dst, float value, const char* suffix)
    {
        char buf[48]{};
        if (!std::isfinite(value) || value <= -59.9f)
            std::snprintf(buf, sizeof(buf), "-inf%s", suffix);
        else
            std::snprintf(buf, sizeof(buf), "%.1f%s", value, suffix);
        dst = buf;
    }
}

juce::Font KickDuck1AudioProcessorEditor::font(float size, bool bold)
{
    return juce::Font(juce::FontOptions{}.withHeight(size)
                          .withStyle(bold ? "Bold" : "Regular"));
}

KickDuck1AudioProcessorEditor::KickDuck1AudioProcessorEditor(KickDuck1AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1260, 800);
    setResizable(true, true);
    setResizeLimits(1050, 680, 1800, 1100);

    auto curve = processor.getCurvePoints();
    if (isUnityCurve(curve))
    {
        curve = defaultCurve();
        processor.setCurvePoints(curve);
    }
    curveEditor.setPoints(curve);
    curveEditor.onPointsChanged = [this](const std::vector<DuckingCurve::Point>& points)
    {
        processor.setCurvePoints(points);
    };
    addAndMakeVisible(curveEditor);

    setupKnob(amountSlider, amountLabel, "AMOUNT");
    setupKnob(lengthSlider, lengthLabel, "LENGTH");
    setupKnob(attackSlider, attackLabel, "ATTACK");
    setupKnob(releaseSlider, releaseLabel, "RELEASE");
    setupKnob(mixSlider, mixLabel, "MIX");
    setupKnob(inputSlider, inputLabel, "INPUT");
    setupKnob(outputSlider, outputLabel, "OUTPUT");

    amountAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "amount", amountSlider);
    lengthAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "length", lengthSlider);
    attackAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "attack", attackSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "release", releaseSlider);
    mixAttachment     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "mix", mixSlider);
    inputAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "input", inputSlider);
    outputAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "output", outputSlider);

    for (auto* s : { &inputPeakText, &inputRmsText, &inputLufsText,
                     &outputPeakText, &outputRmsText, &outputLufsText,
                     &kickPeakText, &kickRmsText, &grDbText, &grPercentText,
                     &duckText, &sidechainText })
        s->preallocateBytes(64);

    startTimerHz(24);
}

KickDuck1AudioProcessorEditor::~KickDuck1AudioProcessorEditor()
{
    stopTimer();
}

void KickDuck1AudioProcessorEditor::setupKnob(juce::Slider& slider, juce::Label& label,
                                               const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, curveColour);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, gridBright);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, panel2);
    slider.setColour(juce::Slider::textBoxOutlineColourId, grid);
    addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setFont(font(10.0f, true));
    label.setColour(juce::Label::textColourId, muted);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

juce::String KickDuck1AudioProcessorEditor::formatDb(float value)
{
    if (!std::isfinite(value) || value <= -59.9f)
        return "-∞ dB";
    return juce::String(value, 1) + " dB";
}

juce::String KickDuck1AudioProcessorEditor::formatMs(float value)
{
    if (value >= 1000.0f)
        return juce::String(value / 1000.0f, 2) + " s";
    return juce::String(value, value < 100.0f ? 1 : 0) + " ms";
}

juce::String KickDuck1AudioProcessorEditor::formatPercent(float value)
{
    return juce::String(juce::jlimit(0.0f, 100.0f, value), 1) + " %";
}

juce::String KickDuck1AudioProcessorEditor::formatLength(float value)
{
    if (std::abs(value - 0.125f) < 0.035f) return "1/8";
    if (std::abs(value - 0.25f)  < 0.05f)  return "1/4";
    if (std::abs(value - 0.5f)   < 0.08f)  return "1/2";
    if (std::abs(value - 1.0f)   < 0.12f)  return "1";
    if (std::abs(value - 2.0f)   < 0.20f)  return "2";
    return juce::String(value, 2);
}

void KickDuck1AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto outer = getLocalBounds().toFloat().reduced(10.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(outer, 16.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(outer.reduced(0.5f), 16.0f, 1.0f);

    g.setColour(text);
    g.setFont(font(24.0f, true));
    g.drawText("KICKDUCK 1", 28, 18, 250, 30, juce::Justification::left);
    g.setColour(muted);
    g.setFont(font(10.0f, true));
    g.drawText("SIDECHAIN DUCKING / PUMPING", 30, 46, 300, 18, juce::Justification::left);

    const bool connected = processor.isSidechainConnected();
    const float sx = (float)getWidth() - 292.0f;
    g.setColour(connected ? bassOutColour : muted);
    g.fillEllipse(sx, 29.0f, 8.0f, 8.0f);
    g.setColour(connected ? text : muted);
    g.setFont(font(10.5f, true));
    g.drawText(connected ? "KICK AUX  •  ONLINE" : "KICK AUX  •  WAITING",
               sx + 15.0f, 23.0f, 170.0f, 20.0f, juce::Justification::left);

    g.setColour(grid);
    g.drawHorizontalLine(70, 26.0f, (float)getWidth() - 26.0f);

    auto content = getLocalBounds().withTrimmedTop(82).withTrimmedBottom(154).reduced(18, 0).toFloat();
    auto meters = content.removeFromRight(290.0f);
    auto graph = content;

    g.setColour(panel2);
    g.fillRoundedRectangle(graph, 12.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(graph, 12.0f, 1.0f);

    g.setColour(panel2);
    g.fillRoundedRectangle(meters, 12.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(meters, 12.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(9.5f, true));
    g.drawText("KICK + BASS OUT  /  SNAPSHOT HISTORY", graph.getX() + 15, graph.getY() + 10, 300, 18, juce::Justification::left);
    g.setColour(curveColour);
    g.drawText("DUCK CURVE", graph.getRight() - 110, graph.getY() + 10, 94, 18, juce::Justification::right);

    g.setColour(muted);
    g.drawText("LEVELS", meters.getX() + 14, meters.getY() + 10, 100, 18, juce::Justification::left);

    auto m = meters.reduced(12.0f, 35.0f);
    const float gap = 8.0f;
    const float w = (m.getWidth() - gap * 2.0f) / 3.0f;
    auto inputMeter = m.removeFromLeft(w);
    m.removeFromLeft(gap);
    auto outputMeter = m.removeFromLeft(w);
    m.removeFromLeft(gap);
    auto grMeter = m;

    drawMeter(g, inputMeter, processor.getInputPeak(), "INPUT", inputPeakText, false);
    drawMeter(g, outputMeter, processor.getOutputPeak(), "OUTPUT", outputPeakText, false);
    drawMeter(g, grMeter, processor.getGainReductionDb(), "GR", grDbText, true);

    auto readouts = meters.reduced(12.0f, 0.0f);
    readouts = readouts.withTop(readouts.getBottom() - 118.0f);
    const float rw = (readouts.getWidth() - 8.0f) * 0.5f;
    auto inRead = readouts.removeFromLeft(rw);
    readouts.removeFromLeft(8.0f);
    auto outRead = readouts;
    drawReadout(g, inRead, "INPUT", inputRmsText, inputLufsText, kickColour);
    drawReadout(g, outRead, "OUTPUT", outputRmsText, outputLufsText, bassOutColour);

    g.setColour(kickColour);
    g.setFont(font(9.5f, true));
    g.drawText("KICK  " + kickPeakText + "  /  " + kickRmsText,
               meters.getX() + 12, meters.getBottom() - 88,
               meters.getWidth() - 24, 18, juce::Justification::centred);

    g.setColour(curveColour);
    g.drawText(duckText,
               meters.getX() + 12, meters.getBottom() - 64,
               meters.getWidth() - 24, 18, juce::Justification::centred);

    g.setColour(connected ? bassOutColour : muted);
    g.drawText(sidechainText,
               meters.getX() + 12, meters.getBottom() - 39,
               meters.getWidth() - 24, 18, juce::Justification::centred);

    auto controls = getLocalBounds().removeFromBottom(140).reduced(22, 7).toFloat();
    g.setColour(panel2);
    g.fillRoundedRectangle(controls, 12.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(controls, 12.0f, 1.0f);
    g.setColour(muted);
    g.setFont(font(9.5f, true));
    g.drawText("DUCKING CONTROLS", controls.getX() + 14, controls.getY() + 8, 150, 16, juce::Justification::left);
}

void KickDuck1AudioProcessorEditor::resized()
{
    auto content = getLocalBounds().withTrimmedTop(82).withTrimmedBottom(154).reduced(18, 0);
    auto meters = content.removeFromRight(290);
    curveEditor.setBounds(content);

    auto controls = getLocalBounds().removeFromBottom(140).reduced(22, 7);
    controls.removeFromTop(25);
    const int gap = 8;
    const int cellW = (controls.getWidth() - gap * 6) / 7;

    auto place = [&controls, cellW, gap](juce::Slider& slider, juce::Label& label)
    {
        auto cell = controls.removeFromLeft(cellW);
        controls.removeFromLeft(gap);
        label.setBounds(cell.removeFromTop(18));
        slider.setBounds(cell.reduced(4, 0));
    };

    place(amountSlider, amountLabel);
    place(lengthSlider, lengthLabel);
    place(attackSlider, attackLabel);
    place(releaseSlider, releaseLabel);
    place(mixSlider, mixLabel);
    place(inputSlider, inputLabel);
    place(outputSlider, outputLabel);

    juce::ignoreUnused(meters);
}

void KickDuck1AudioProcessorEditor::drawMeter(juce::Graphics& g, juce::Rectangle<float> area,
                                               float levelDb, const juce::String& title,
                                               const juce::String& value, bool gainReduction) const
{
    g.setColour(panel3);
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(area.reduced(0.5f), 8.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(8.5f, true));
    g.drawText(title, area.getX(), area.getY() + 7, area.getWidth(), 15, juce::Justification::centred);

    auto bar = area.reduced(8.0f, 29.0f);
    bar.removeFromBottom(30.0f);
    g.setColour(juce::Colour::fromRGB(8, 11, 15));
    g.fillRoundedRectangle(bar, 4.0f);

    const float norm = gainReduction
        ? juce::jlimit(0.0f, 1.0f, -levelDb / 24.0f)
        : dbNorm(levelDb);

    auto fill = bar.withTop(bar.getBottom() - bar.getHeight() * norm);
    g.setColour(gainReduction ? grColour : (levelDb > -6.0f ? kickColour : bassOutColour));
    g.fillRoundedRectangle(fill, 4.0f);
    g.setColour(gridBright);
    g.drawRoundedRectangle(bar, 4.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(7.0f));
    for (int db : { 0, -12, -24, -36, -48, -60 })
    {
        const float y = bar.getBottom() - ((float)(db + 60) / 60.0f) * bar.getHeight();
        g.drawText(juce::String(db), bar.getRight() + 3, y - 5, 22, 11, juce::Justification::left);
    }

    g.setColour(text);
    g.setFont(font(10.0f, true));
    g.drawText(value, area.getX(), area.getBottom() - 27, area.getWidth(), 18, juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::drawReadout(juce::Graphics& g, juce::Rectangle<float> area,
                                                 const juce::String& title, const juce::String& main,
                                                 const juce::String& sub, juce::Colour accent) const
{
    g.setColour(muted);
    g.setFont(font(8.0f, true));
    g.drawText(title, area.getX(), area.getY(), area.getWidth(), 14, juce::Justification::centred);
    g.setColour(accent);
    g.setFont(font(13.0f, true));
    g.drawText(main, area.getX(), area.getY() + 15, area.getWidth(), 20, juce::Justification::centred);
    g.setColour(muted);
    g.setFont(font(8.0f));
    g.drawText(sub, area.getX(), area.getY() + 37, area.getWidth(), 14, juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::timerCallback()
{
    updateMeters();
    updateWaveform();
    repaint();
}

void KickDuck1AudioProcessorEditor::updateMeters()
{
    const auto* amountParam = processor.apvts.getRawParameterValue("amount");
    const float amount = amountParam != nullptr
        ? amountParam->load(std::memory_order_relaxed) * 0.01f
        : 0.75f;

    curveEditor.setAmount(amount);
    curveEditor.setPhase(processor.getCurrentDuck());

    cacheNumber(inputPeakText, processor.getInputPeak(), " dB");
    cacheNumber(inputRmsText, processor.getInputRms(), " dB");
    cacheNumber(inputLufsText, processor.getInputLufs(), " LUFS");
    cacheNumber(outputPeakText, processor.getOutputPeak(), " dB");
    cacheNumber(outputRmsText, processor.getOutputRms(), " dB");
    cacheNumber(outputLufsText, processor.getOutputLufs(), " LUFS");
    cacheNumber(kickPeakText, processor.getKickActivity(), " dB");
    cacheNumber(kickRmsText, processor.getKickRms(), " dB");
    cacheNumber(grDbText, processor.getGainReductionDb(), " dB");
    cacheNumber(grPercentText, processor.getGainReductionPercent(), " %");

    duckText = "DUCK  " + formatPercent(processor.getCurrentDuck() * 100.0f)
             + "   •   AMOUNT  " + formatPercent(amount * 100.0f);

    sidechainText = processor.isSidechainConnected()
        ? "●  SIDECHAIN ACTIVE"
        : "○  SIDECHAIN NOT CONNECTED";
}

void KickDuck1AudioProcessorEditor::updateWaveform()
{
    // Deliberately do NOT refresh every timer tick. The old implementation
    // made the waveform look like a continuously scrolling oscilloscope.
    // A new 1.5 s frame is captured every ~0.5 s and then held still.
    ++waveformFrameCounter;
    if (waveformFrameCounter < 12)
        return;
    waveformFrameCounter = 0;

    const double sr = processor.getSampleRate();
    if (!std::isfinite(sr) || sr <= 0.0)
        return;

    const int samples = juce::jlimit(1, 96000, static_cast<int>(sr * 1.5));
    curveEditor.setWaveformsFromProcessor(processor, samples);
}

KickDuck1AudioProcessorEditor::CurveEditor::CurveEditor()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> KickDuck1AudioProcessorEditor::CurveEditor::graphBounds() const
{
    return getLocalBounds().toFloat().reduced(18.0f, 34.0f);
}

void KickDuck1AudioProcessorEditor::CurveEditor::setPoints(const std::vector<DuckingCurve::Point>& p)
{
    points = p;
    if (points.size() < 2)
        points = defaultCurve();
    if (points.size() > maxCurvePoints)
        points.resize(maxCurvePoints);
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::normalizeWaveform(
    std::array<float, maxWaveformPoints>& waveform)
{
    float peak = 0.0f;
    for (float v : waveform)
        peak = std::max(peak, std::abs(v));

    if (!std::isfinite(peak) || peak < 0.00001f)
    {
        waveform.fill(0.0f);
        return;
    }

    const float gain = 0.92f / peak;
    for (float& v : waveform)
        v = juce::jlimit(-1.0f, 1.0f, v * gain);
}

void KickDuck1AudioProcessorEditor::CurveEditor::setWaveformsFromProcessor(
    KickDuck1AudioProcessor& p, int samplesToCopy)
{
    p.copyWaveformSnapshot(bassInWaveform.data(), bassOutWaveform.data(),
                           kickWaveform.data(), maxWaveformPoints, samplesToCopy);

    // Visual-only normalization. DSP values are untouched.
    normalizeWaveform(bassOutWaveform);
    normalizeWaveform(kickWaveform);
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setAmount(float v)
{
    amount = juce::jlimit(0.0f, 1.0f, v);
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setPhase(float v)
{
    phase = juce::jlimit(0.0f, 1.0f, v);
    repaint();
}

float KickDuck1AudioProcessorEditor::CurveEditor::xToNorm(float x) const
{
    auto b = graphBounds();
    return juce::jlimit(0.0f, 1.0f, (x - b.getX()) / b.getWidth());
}

float KickDuck1AudioProcessorEditor::CurveEditor::screenYToVisualNorm(float y) const
{
    auto b = graphBounds();
    return juce::jlimit(0.0f, 1.0f, (b.getBottom() - y) / b.getHeight());
}

juce::Point<float> KickDuck1AudioProcessorEditor::CurveEditor::pointToScreen(
    const DuckingCurve::Point& point) const
{
    auto b = graphBounds();

    // DSP y=1 is unity/no ducking and is therefore drawn at the bottom.
    // DSP y=0 is maximum ducking and is drawn at the top.
    const float visual = (1.0f - point.y) * amount;

    return {
        b.getX() + point.x * b.getWidth(),
        b.getBottom() - visual * b.getHeight()
    };
}

int KickDuck1AudioProcessorEditor::CurveEditor::findPoint(juce::Point<float> pos) const
{
    int found = -1;
    float best = 18.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const float d = pointToScreen(points[(size_t)i]).getDistanceFrom(pos);
        if (d < best)
        {
            best = d;
            found = i;
        }
    }
    return found;
}

void KickDuck1AudioProcessorEditor::CurveEditor::notifyPointsChanged()
{
    if (onPointsChanged)
        onPointsChanged(points);
}

void KickDuck1AudioProcessorEditor::CurveEditor::drawWaveform(
    juce::Graphics& g, const std::array<float, maxWaveformPoints>& waveform,
    juce::Rectangle<float> area, juce::Colour colour, float thickness) const
{
    const float mid = area.getCentreY();
    const float scale = area.getHeight() * 0.47f;

    g.setColour(colour.withAlpha(0.92f));
    for (int i = 1; i < maxWaveformPoints; ++i)
    {
        const float x0 = area.getX() + area.getWidth() * (float)(i - 1) / (float)(maxWaveformPoints - 1);
        const float x1 = area.getX() + area.getWidth() * (float)i / (float)(maxWaveformPoints - 1);
        const float y0 = mid - juce::jlimit(-1.0f, 1.0f, waveform[(size_t)i - 1]) * scale;
        const float y1 = mid - juce::jlimit(-1.0f, 1.0f, waveform[(size_t)i]) * scale;
        g.drawLine(x0, y0, x1, y1, thickness);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::drawCurve(juce::Graphics& g) const
{
    if (points.size() < 2)
        return;

    g.setColour(curveColour);
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto a = pointToScreen(points[i - 1]);
        const auto b = pointToScreen(points[i]);
        g.drawLine(a.x, a.y, b.x, b.y, 3.0f);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::paint(juce::Graphics& g)
{
    auto b = graphBounds();

    // Grid.
    g.setColour(grid);
    for (int i = 0; i <= 8; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float)i / 8.0f;
        g.drawVerticalLine((int)x, b.getY(), b.getBottom());
    }
    for (int i = 0; i <= 6; ++i)
    {
        const float y = b.getY() + b.getHeight() * (float)i / 6.0f;
        g.drawHorizontalLine((int)y, b.getX(), b.getRight());
    }

    // Single shared zero line for Kick and Bass Out.
    const float mid = b.getCentreY();
    g.setColour(gridBright);
    g.drawHorizontalLine((int)mid, b.getX(), b.getRight());

    // Both signals use the same center line and the same vertical scale.
    // Kick is drawn thicker so short transients remain visible.
    drawWaveform(g, bassOutWaveform, b, bassOutColour, 1.15f);
    drawWaveform(g, kickWaveform, b, kickColour, 1.9f);

    // Duck curve overlays the same graph.
    drawCurve(g);

    // Frame playhead is intentionally subtle; it does not make the waveform scroll.
    const float px = b.getX() + phase * b.getWidth();
    g.setColour(juce::Colours::white.withAlpha(0.20f));
    g.drawVerticalLine((int)px, b.getY(), b.getBottom());

    // Legend.
    g.setFont(font(10.0f, true));
    g.setColour(kickColour);
    g.fillEllipse(b.getX() + 5.0f, b.getY() + 7.0f, 7.0f, 7.0f);
    g.setColour(text);
    g.drawText("KICK", b.getX() + 17.0f, b.getY(), 45, 20, juce::Justification::left);

    g.setColour(bassOutColour);
    g.fillEllipse(b.getX() + 72.0f, b.getY() + 7.0f, 7.0f, 7.0f);
    g.setColour(text);
    g.drawText("BASS OUT", b.getX() + 84.0f, b.getY(), 70, 20, juce::Justification::left);

    g.setColour(curveColour);
    g.fillEllipse(b.getX() + 174.0f, b.getY() + 7.0f, 7.0f, 7.0f);
    g.setColour(text);
    g.drawText("DUCK", b.getX() + 186.0f, b.getY(), 45, 20, juce::Justification::left);

    g.setColour(muted);
    g.setFont(font(8.5f, true));
    g.drawText("1.5 s SNAPSHOT  •  DRAG POINTS  •  DOUBLE-CLICK ADD  •  RIGHT-CLICK DELETE",
               b.getRight() - 440.0f, b.getBottom() - 19.0f, 430.0f, 15,
               juce::Justification::right);

    g.setColour(curveColour);
    g.setFont(font(9.0f, true));
    g.drawText("AMOUNT  " + juce::String(amount * 100.0f, 0) + "%",
               b.getRight() - 95.0f, b.getY(), 85, 18, juce::Justification::right);

    // Curve points.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto p = pointToScreen(points[i]);
        const bool selected = static_cast<int>(i) == selectedPoint;
        const float r = selected ? 7.0f : 6.0f;
        g.setColour(selected ? juce::Colours::white : curveColour);
        g.fillEllipse(p.x - r, p.y - r, 2.0f * r, 2.0f * r);
        g.setColour(bg);
        g.fillEllipse(p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDown(const juce::MouseEvent& e)
{
    selectedPoint = findPoint(e.position);

    if (e.mods.isRightButtonDown())
    {
        if (selectedPoint > 0 && selectedPoint < static_cast<int>(points.size()) - 1)
        {
            points.erase(points.begin() + selectedPoint);
            selectedPoint = -1;
            notifyPointsChanged();
            repaint();
        }
        return;
    }

    if (selectedPoint >= 0)
        dragging = true;
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging || selectedPoint < 0)
        return;

    float x = xToNorm(e.position.x);
    const float visual = screenYToVisualNorm(e.position.y);

    if (selectedPoint == 0)
        x = 0.0f;
    else if (selectedPoint == static_cast<int>(points.size()) - 1)
        x = 1.0f;
    else
    {
        x = std::max(x, points[(size_t)selectedPoint - 1].x + 0.002f);
        if (selectedPoint + 1 < static_cast<int>(points.size()))
            x = std::min(x, points[(size_t)selectedPoint + 1].x - 0.002f);
    }

    float y = 1.0f;
    if (amount > 0.001f)
        y = 1.0f - visual / amount;

    points[(size_t)selectedPoint].x = juce::jlimit(0.0f, 1.0f, x);
    points[(size_t)selectedPoint].y = juce::jlimit(0.0f, 1.0f, y);

    notifyPointsChanged();
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (findPoint(e.position) >= 0 || points.size() >= maxCurvePoints)
        return;

    const float x = xToNorm(e.position.x);
    const float visual = screenYToVisualNorm(e.position.y);
    const float y = amount > 0.001f ? 1.0f - visual / amount : 1.0f;

    points.push_back({ x, juce::jlimit(0.0f, 1.0f, y) });
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    notifyPointsChanged();
    repaint();
}
