#include "PluginEditor.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace
{
    const juce::Colour bg = juce::Colour::fromRGB(7, 9, 13);
    const juce::Colour panel = juce::Colour::fromRGB(16, 19, 25);
    const juce::Colour panel2 = juce::Colour::fromRGB(11, 14, 19);
    const juce::Colour grid = juce::Colour::fromRGB(38, 43, 52);
    const juce::Colour text = juce::Colour::fromRGB(236, 240, 247);
    const juce::Colour muted = juce::Colour::fromRGB(133, 141, 154);
    const juce::Colour kickColour = juce::Colour::fromRGB(255, 158, 68);
    const juce::Colour bassInColour = juce::Colour::fromRGB(77, 153, 255);
    const juce::Colour bassOutColour = juce::Colour::fromRGB(69, 222, 153);
    const juce::Colour curveColour = juce::Colour::fromRGB(191, 118, 255);
    const juce::Colour grColour = juce::Colour::fromRGB(255, 91, 116);

    juce::Font font(float size, bool bold = false)
    {
        return juce::Font(juce::FontOptions{}.withHeight(size)
                              .withStyle(bold ? "Bold" : "Regular"));
    }

    float dbNorm(float db)
    {
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }

    void setCachedNumber(juce::String& out, const char* prefix, float value,
                         const char* suffix = "", int decimals = 1)
    {
        out.clear();
        out.append(prefix, 64);
        if (!std::isfinite(value))
        {
            out.append("-inf", 64);
        }
        else
        {
            char number[32]{};
            std::snprintf(number, sizeof(number), "%.*f", decimals, value);
            out.append(juce::String(number), 64);
        }
        out.append(suffix, 64);
    }
}

KickDuck1AudioProcessorEditor::KickDuck1AudioProcessorEditor(KickDuck1AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1260, 800);
    setResizable(true, true);
    setResizeLimits(1050, 680, 1800, 1100);

    curveEditor.setPoints(processor.getCurvePoints());
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

    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "amount", amountSlider);
    lengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "length", lengthSlider);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "attack", attackSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "release", releaseSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "mix", mixSlider);
    inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "input", inputSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "output", outputSlider);

    for (auto* label : { &inputPeakValue, &inputRmsValue, &inputLufsValue,
                         &outputPeakValue, &outputRmsValue, &outputLufsValue,
                         &kickPeakValue, &kickRmsValue, &grDbValue,
                         &grPercentValue, &duckValue, &sidechainStatus })
        setupValueLabel(*label);

    sidechainStatus.setFont(font(11.0f, true));
    sidechainStatus.setJustificationType(juce::Justification::centred);

    for (auto* s : { &inputPeakText, &inputRmsText, &inputLufsText, &outputPeakText, &outputRmsText, &outputLufsText, &kickPeakText, &kickRmsText, &grDbText, &grPercentText, &duckText, &sidechainText })
        s->preallocateBytes(64);

    startTimerHz(24);
}

KickDuck1AudioProcessorEditor::~KickDuck1AudioProcessorEditor()
{
    stopTimer();
}

void KickDuck1AudioProcessorEditor::setupValueLabel(juce::Label& label)
{
    label.setColour(juce::Label::textColourId, text);
    label.setFont(font(12.0f, false));
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void KickDuck1AudioProcessorEditor::setupKnob(juce::Slider& slider, juce::Label& label,
                                               const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 20);
    slider.setColour(juce::Slider::rotarySliderFillColourId, curveColour);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId, grid);
    slider.setColour(juce::Slider::textBoxTextColourId, text);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, panel2);
    slider.setColour(juce::Slider::textBoxOutlineColourId, grid);
    addAndMakeVisible(slider);
    label.setText(name, juce::dontSendNotification);
    label.setFont(font(10.5f, true));
    label.setColour(juce::Label::textColourId, muted);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

juce::String KickDuck1AudioProcessorEditor::formatDb(float value)
{
    if (!std::isfinite(value) || value <= -59.9f) return "-∞ dB";
    return juce::String(value, 1) + " dB";
}

juce::String KickDuck1AudioProcessorEditor::formatMs(float value)
{
    if (value >= 1000.0f) return juce::String(value / 1000.0f, 2) + " s";
    return juce::String(value, value < 100.0f ? 1 : 0) + " ms";
}

juce::String KickDuck1AudioProcessorEditor::formatPercent(float value)
{
    return juce::String(juce::jlimit(0.0f, 100.0f, value), 1) + " %";
}

void KickDuck1AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto r = getLocalBounds().toFloat().reduced(12.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(r, 16.0f);

    g.setColour(text);
    g.setFont(font(24.0f, true));
    g.drawText("KICKDUCK 1", 28, 20, 260, 30, juce::Justification::left);
    g.setColour(muted);
    g.setFont(font(10.5f, true));
    g.drawText("SIDECHAIN DUCKING / PUMPING", 30, 48, 300, 18, juce::Justification::left);

    const auto connected = processor.isSidechainConnected();
    g.setColour(connected ? bassOutColour : muted);
    g.fillEllipse((float)getWidth() - 302.0f, 29.0f, 9.0f, 9.0f);
    g.setColour(connected ? text : muted);
    g.setFont(font(11.0f, true));
    g.drawText(connected ? "KICK AUX ONLINE" : "KICK AUX WAITING",
               getWidth() - 286, 23, 150, 20, juce::Justification::left);
    g.setColour(grid);
    g.drawHorizontalLine(70, 28.0f, (float)getWidth() - 28.0f);

    auto area = getLocalBounds().withTrimmedTop(82).withTrimmedBottom(164).toFloat().reduced(18.0f, 0.0f);
    auto right = area.removeFromRight(285.0f);
    auto graph = area.reduced(0.0f, 2.0f);
    g.setColour(panel2);
    g.fillRoundedRectangle(graph, 12.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(graph, 12.0f, 1.0f);

    g.setColour(panel2);
    g.fillRoundedRectangle(right, 12.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(right, 12.0f, 1.0f);

    auto bottom = getLocalBounds().removeFromBottom(150).reduced(22, 7);
    g.setColour(panel2);
    g.fillRoundedRectangle(bottom.toFloat(), 12.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(bottom.toFloat(), 12.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(10.0f, true));
    g.drawText("LIVE SIGNAL / DUCKING MAP", graph.getX() + 16, graph.getY() + 12, 230, 18, juce::Justification::left);
    g.drawText("READOUT", right.getX() + 14, right.getY() + 12, 100, 18, juce::Justification::left);
    g.drawText("CONTROL", bottom.getX() + 14, bottom.getY() + 9, 100, 18, juce::Justification::left);

    auto meters = right.reduced(12.0f, 34.0f);
    const float gap = 8.0f;
    const float mw = (meters.getWidth() - gap * 2.0f) / 3.0f;
    auto im = meters.removeFromLeft(mw);
    meters.removeFromLeft(gap);
    auto om = meters.removeFromLeft(mw);
    meters.removeFromLeft(gap);
    auto gm = meters;
    drawMeter(g, im, processor.getInputPeak(), "BASS IN", inputPeakText, false);
    drawMeter(g, om, processor.getOutputPeak(), "BASS OUT", outputPeakText, false);
    drawMeter(g, gm, processor.getGainReductionDb(), "GR", grDbText, true);

    auto info = right.reduced(12.0f, 0.0f);
    info = info.withTop(info.getBottom() - 104.0f);
    const float iw = (info.getWidth() - 8.0f) / 2.0f;
    drawReadout(g, info.removeFromLeft(iw), "INPUT RMS", inputRmsText,
                inputLufsText, bassInColour);
    info.removeFromLeft(8.0f);
    drawReadout(g, info, "OUTPUT RMS", outputRmsText,
                outputLufsText, bassOutColour);

    g.setColour(muted);
    g.setFont(font(9.5f, true));
    g.drawText(kickPeakText + "   /   " + kickRmsText,
               right.getX() + 12, right.getBottom() - 70, right.getWidth() - 24, 18,
               juce::Justification::centred);
    g.setColour(curveColour);
    g.drawText(duckText,
               right.getX() + 12, right.getBottom() - 48, right.getWidth() - 24, 18,
               juce::Justification::centred);
    g.setColour(processor.isSidechainConnected() ? bassOutColour : muted);
    g.drawText(sidechainText, right.getX() + 12, right.getBottom() - 24,
               right.getWidth() - 24, 18, juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop(82).withTrimmedBottom(164).reduced(18, 0);
    auto right = area.removeFromRight(285);
    curveEditor.setBounds(area.reduced(0, 2));

    auto rr = right.reduced(12, 34);
    const int gap = 8;
    const int meterW = (rr.getWidth() - gap * 2) / 3;
    juce::Rectangle<int> m1 = rr.removeFromLeft(meterW);
    rr.removeFromLeft(gap);
    juce::Rectangle<int> m2 = rr.removeFromLeft(meterW);
    rr.removeFromLeft(gap);
    juce::Rectangle<int> m3 = rr;
    inputPeakValue.setBounds(m1.getX(), m1.getBottom() - 42, m1.getWidth(), 18);
    inputRmsValue.setBounds(m1.getX(), m1.getBottom() - 24, m1.getWidth(), 18);
    outputPeakValue.setBounds(m2.getX(), m2.getBottom() - 42, m2.getWidth(), 18);
    outputRmsValue.setBounds(m2.getX(), m2.getBottom() - 24, m2.getWidth(), 18);
    grDbValue.setBounds(m3.getX(), m3.getBottom() - 42, m3.getWidth(), 18);
    grPercentValue.setBounds(m3.getX(), m3.getBottom() - 24, m3.getWidth(), 18);

    auto lower = right.reduced(12, 0);
    lower.removeFromTop(std::max(0, lower.getHeight() - 90));
    kickPeakValue.setBounds(lower.getX(), lower.getY(), lower.getWidth() / 2, 20);
    kickRmsValue.setBounds(lower.getX() + lower.getWidth() / 2, lower.getY(), lower.getWidth() / 2, 20);
    duckValue.setBounds(lower.getX(), lower.getY() + 23, lower.getWidth(), 20);
    sidechainStatus.setBounds(lower.getX(), lower.getY() + 48, lower.getWidth(), 25);

    auto bottom = getLocalBounds().removeFromBottom(150).reduced(22, 7);
    bottom.removeFromTop(24);
    const int cellW = bottom.getWidth() / 7;
    auto place = [&bottom, cellW](juce::Slider& slider, juce::Label& label)
    {
        auto cell = bottom.removeFromLeft(cellW);
        label.setBounds(cell.removeFromTop(18));
        slider.setBounds(cell.reduced(5, 0));
    };
    place(amountSlider, amountLabel); place(lengthSlider, lengthLabel); place(attackSlider, attackLabel);
    place(releaseSlider, releaseLabel); place(mixSlider, mixLabel); place(inputSlider, inputLabel); place(outputSlider, outputLabel);
}

void KickDuck1AudioProcessorEditor::drawMeter(juce::Graphics& g, juce::Rectangle<float> area,
                                               float levelDb, const juce::String& title,
                                               const juce::String& value, bool gainReduction) const
{
    g.setColour(muted); g.setFont(font(9.0f, true));
    g.drawText(title, area.getX(), area.getY(), area.getWidth(), 16, juce::Justification::centred);
    auto bar = area.withTrimmedTop(20).withTrimmedBottom(62).reduced(7, 0);
    g.setColour(juce::Colour::fromRGB(22, 26, 33));
    g.fillRoundedRectangle(bar, 5.0f);
    const float amount = gainReduction ? juce::jlimit(0.0f, 1.0f, -levelDb / 24.0f) : dbNorm(levelDb);
    auto fill = bar.withTrimmedTop(bar.getHeight() * (1.0f - amount));
    g.setColour(gainReduction ? grColour : (levelDb > -6.0f ? kickColour : bassOutColour));
    g.fillRoundedRectangle(fill, 5.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(bar, 5.0f, 1.0f);
    g.setColour(text); g.setFont(font(10.0f, true));
    g.drawText(value, area.getX(), area.getBottom() - 40, area.getWidth(), 18, juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::drawReadout(juce::Graphics& g, juce::Rectangle<float> area,
                                                 const juce::String& title, const juce::String& main,
                                                 const juce::String& sub, juce::Colour accent) const
{
    g.setColour(muted); g.setFont(font(9.0f, true));
    g.drawText(title, area.getX(), area.getY(), area.getWidth(), 14, juce::Justification::centred);
    g.setColour(accent); g.setFont(font(15.0f, true));
    g.drawText(main, area.getX(), area.getY() + 14, area.getWidth(), 22, juce::Justification::centred);
    g.setColour(muted); g.setFont(font(9.0f));
    g.drawText(sub, area.getX(), area.getY() + 36, area.getWidth(), 16, juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::timerCallback()
{
    updateWaveform();
    updateMeters();
    repaint();
}

void KickDuck1AudioProcessorEditor::updateMeters()
{
    const float amount = processor.apvts.getRawParameterValue("amount")->load(std::memory_order_relaxed) * 0.01f;
    curveEditor.setAmount(amount);
    curveEditor.setPhase(processor.getCurrentDuck());

    setCachedNumber(inputPeakText, "PK ", processor.getInputPeak(), " dB");
    setCachedNumber(inputRmsText, "RMS ", processor.getInputRms(), " dB");
    setCachedNumber(inputLufsText, "LUFS ", processor.getInputLufs(), " dB");
    setCachedNumber(outputPeakText, "PK ", processor.getOutputPeak(), " dB");
    setCachedNumber(outputRmsText, "RMS ", processor.getOutputRms(), " dB");
    setCachedNumber(outputLufsText, "LUFS ", processor.getOutputLufs(), " dB");
    setCachedNumber(kickPeakText, "KICK PK ", processor.getKickActivity(), " dB");
    setCachedNumber(kickRmsText, "RMS ", processor.getKickRms(), " dB");
    setCachedNumber(grDbText, "GR ", processor.getGainReductionDb(), " dB");
    setCachedNumber(grPercentText, "", processor.getGainReductionPercent(), " %");
    setCachedNumber(duckText, "DUCK ", processor.getCurrentDuck() * 100.0f, "%");
    char amountNumber[24]{};
    std::snprintf(amountNumber, sizeof(amountNumber), "%.1f", amount * 100.0f);
    duckText.append("   •   AMOUNT ", 64);
    duckText.append(amountNumber, 24);
    duckText.append(" %", 8);
    const bool connected = processor.isSidechainConnected();
    sidechainText.clear();
    sidechainText.append(connected ? "●  SIDECHAIN ACTIVE" : "○  SIDECHAIN NOT CONNECTED", 64);
    sidechainStatus.setColour(juce::Label::textColourId, connected ? bassOutColour : muted);
}

void KickDuck1AudioProcessorEditor::updateWaveform()
{
    const double sr = processor.getSampleRate();
    if (!std::isfinite(sr) || sr <= 0.0)
        return;

    const int samples = juce::jlimit(1, 96000, static_cast<int>(sr * 1.5));
    // CurveEditor owns fixed GUI arrays; no std::vector crosses the thread boundary.
    curveEditor.setWaveformsFromProcessor(processor, samples);
}

KickDuck1AudioProcessorEditor::CurveEditor::CurveEditor()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> KickDuck1AudioProcessorEditor::CurveEditor::graphBounds() const
{
    return getLocalBounds().toFloat().reduced(16.0f, 32.0f);
}

void KickDuck1AudioProcessorEditor::CurveEditor::setPoints(const std::vector<DuckingCurve::Point>& p)
{
    points = p;
    if (points.size() < 2) points = { {0.0f, 1.0f}, {1.0f, 1.0f} };
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setWaveforms(const float* bassIn,
                                                               const float* bassOut,
                                                               const float* kick)
{
    if (bassIn == nullptr || bassOut == nullptr || kick == nullptr)
        return;
    std::copy(bassIn, bassIn + maxWaveformPoints, bassInWaveform.begin());
    std::copy(bassOut, bassOut + maxWaveformPoints, bassOutWaveform.begin());
    std::copy(kick, kick + maxWaveformPoints, kickWaveform.begin());
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setWaveformsFromProcessor(
    KickDuck1AudioProcessor& p, int samplesToCopy)
{
    p.copyWaveformSnapshot(bassInWaveform.data(), bassOutWaveform.data(),
                           kickWaveform.data(), maxWaveformPoints, samplesToCopy);
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
    auto b = graphBounds(); return juce::jlimit(0.0f, 1.0f, (x - b.getX()) / b.getWidth());
}

float KickDuck1AudioProcessorEditor::CurveEditor::yToNorm(float y) const
{
    auto b = graphBounds(); return juce::jlimit(0.0f, 1.0f, 1.0f - (y - b.getY()) / b.getHeight());
}

juce::Point<float> KickDuck1AudioProcessorEditor::CurveEditor::normToPoint(float x, float y) const
{
    auto b = graphBounds(); return { b.getX() + x * b.getWidth(), b.getBottom() - y * b.getHeight() };
}

int KickDuck1AudioProcessorEditor::CurveEditor::findPoint(juce::Point<float> pos) const
{
    int found = -1; float best = 18.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const float d = normToPoint(points[i].x, points[i].y).getDistanceFrom(pos);
        if (d < best) { best = d; found = i; }
    }
    return found;
}

void KickDuck1AudioProcessorEditor::CurveEditor::notifyPointsChanged()
{
    if (onPointsChanged) onPointsChanged(points);
}

void KickDuck1AudioProcessorEditor::CurveEditor::drawWaveform(
    juce::Graphics& g, const std::array<float, maxWaveformPoints>& waveform,
    juce::Rectangle<float> area, juce::Colour colour, float verticalScale) const
{
    const float mid = area.getCentreY();
    const float scale = area.getHeight() * 0.48f * verticalScale;
    for (int i = 1; i < maxWaveformPoints; ++i)
    {
        const float x0 = area.getX() + area.getWidth() * (i - 1) / (maxWaveformPoints - 1.0f);
        const float x1 = area.getX() + area.getWidth() * i / (maxWaveformPoints - 1.0f);
        const float y0 = mid - juce::jlimit(-1.0f, 1.0f, waveform[i - 1]) * scale;
        const float y1 = mid - juce::jlimit(-1.0f, 1.0f, waveform[i]) * scale;
        g.setColour(colour.withAlpha(0.78f));
        g.drawLine(x0, y0, x1, y1, 1.2f);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::drawSmoothCurve(
    juce::Graphics& g, float scale, juce::Colour colour,
    float thickness, bool fill) const
{
    juce::ignoreUnused(fill);
    if (points.size() < 2) return;
    g.setColour(colour);
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto p0 = normToPoint(points[i - 1].x, points[i - 1].y * scale);
        const auto p1 = normToPoint(points[i].x, points[i].y * scale);
        g.drawLine(p0.x, p0.y, p1.x, p1.y, thickness);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::paint(juce::Graphics& g)
{
    auto b = graphBounds();
    g.setColour(grid);
    for (int i = 0; i <= 8; ++i)
    {
        const float x = b.getX() + b.getWidth() * i / 8.0f;
        g.drawVerticalLine((int)x, b.getY(), b.getBottom());
    }
    for (int i = 0; i <= 6; ++i)
    {
        const float y = b.getY() + b.getHeight() * i / 6.0f;
        g.drawHorizontalLine((int)y, b.getX(), b.getRight());
    }

    drawWaveform(g, kickWaveform, { b.getX(), b.getY() + b.getHeight() * 0.03f, b.getWidth(), b.getHeight() * 0.24f }, kickColour, 0.9f);
    drawWaveform(g, bassInWaveform, { b.getX(), b.getY() + b.getHeight() * 0.30f, b.getWidth(), b.getHeight() * 0.33f }, bassInColour, 0.9f);
    drawWaveform(g, bassOutWaveform, { b.getX(), b.getY() + b.getHeight() * 0.65f, b.getWidth(), b.getHeight() * 0.33f }, bassOutColour, 0.9f);

    // The ducking curve occupies the lower signal map. Amount visibly scales
    // its effective depth, so the control and curve are clearly linked.
    drawSmoothCurve(g, amount, curveColour, 3.0f, true);

    const float px = b.getX() + phase * b.getWidth();
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.drawVerticalLine((int)px, b.getY(), b.getBottom());

    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto p = normToPoint(points[i].x, points[i].y);
        const bool selected = static_cast<int>(i) == selectedPoint;
        g.setColour(selected ? juce::Colours::white : curveColour);
        g.fillEllipse(p.x - (selected ? 7.0f : 5.0f), p.y - (selected ? 7.0f : 5.0f),
                      selected ? 14.0f : 10.0f, selected ? 14.0f : 10.0f);
        g.setColour(bg);
        g.drawEllipse(p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f, 1.0f);
    }

    g.setColour(text); g.setFont(font(12.0f, true));
    g.drawText("KICK", b.getX() + 10, b.getY() + 4, 70, 18, juce::Justification::left);
    g.setColour(kickColour); g.fillEllipse(b.getX() + 50, b.getY() + 9, 6, 6);
    g.setColour(text); g.drawText("BASS IN", b.getX() + 10, b.getY() + b.getHeight() * 0.30f + 4, 80, 18, juce::Justification::left);
    g.setColour(bassInColour); g.fillEllipse(b.getX() + 70, b.getY() + b.getHeight() * 0.30f + 9, 6, 6);
    g.setColour(text); g.drawText("BASS OUT", b.getX() + 10, b.getY() + b.getHeight() * 0.65f + 4, 90, 18, juce::Justification::left);
    g.setColour(bassOutColour); g.fillEllipse(b.getX() + 80, b.getY() + b.getHeight() * 0.65f + 9, 6, 6);
    g.setColour(curveColour); g.setFont(font(10.0f, true));
    g.drawText("DUCK CURVE  •  DRAG POINTS  •  DOUBLE-CLICK TO ADD  •  RIGHT-CLICK TO DELETE",
               b.getRight() - 520, b.getBottom() - 22, 510, 18, juce::Justification::right);
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
            notifyPointsChanged(); repaint();
        }
        return;
    }
    if (selectedPoint >= 0) dragging = true;
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging || selectedPoint < 0) return;
    float x = xToNorm(e.position.x), y = yToNorm(e.position.y);
    if (selectedPoint == 0) x = 0.0f;
    if (selectedPoint == static_cast<int>(points.size()) - 1) x = 1.0f;
    if (selectedPoint > 0) x = std::max(x, points[(size_t)selectedPoint - 1].x + 0.002f);
    if (selectedPoint + 1 < static_cast<int>(points.size())) x = std::min(x, points[(size_t)selectedPoint + 1].x - 0.002f);
    points[(size_t)selectedPoint].x = juce::jlimit(0.0f, 1.0f, x);
    points[(size_t)selectedPoint].y = juce::jlimit(0.0f, 1.0f, y);
    notifyPointsChanged(); repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (findPoint(e.position) >= 0) return;
    points.push_back({ xToNorm(e.position.x), yToNorm(e.position.y) });
    std::sort(points.begin(), points.end(), [](auto a, auto b) { return a.x < b.x; });
    if (points.size() > 32) points.resize(32);
    notifyPointsChanged(); repaint();
}
