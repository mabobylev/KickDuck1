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

void KickDuck1AudioProcessorEditor::CompactButton::paintButton(
    juce::Graphics& g, bool highlighted, bool down)
{
    const bool selected = getToggleState();
    const auto box = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(selected ? curveColour.withAlpha(0.18f) : (highlighted || down ? panel3 : panel2));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(selected ? curveColour : gridBright);
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    g.setColour(selected || highlighted ? curveColour : ::text);
    g.setFont(font(10.5f, true));
    g.drawText(getButtonText(), getLocalBounds().reduced(3, 0), juce::Justification::centred);
}

KickDuck1AudioProcessorEditor::CurvePresetButton::CurvePresetButton(DuckingCurve::Preset preset)
    : juce::Button(juce::String("Curve preset: ") + DuckingCurve::presetName(preset)),
      points(DuckingCurve::presetPoints(preset))
{
    setName(getButtonText());
    setTooltip(juce::String(DuckingCurve::presetName(preset)) + " curve (keeps Amount and signal levels)");
}

void KickDuck1AudioProcessorEditor::CurvePresetButton::paintButton(
    juce::Graphics& g, bool highlighted, bool down)
{
    const bool selected = getToggleState();
    const auto box = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(selected ? curveColour.withAlpha(0.18f) : (highlighted || down ? panel3 : panel2));
    g.fillRoundedRectangle(box, 5.0f);
    g.setColour(selected ? curveColour : gridBright);
    g.drawRoundedRectangle(box, 5.0f, 1.0f);
    const auto graph = box.reduced(6.0f, 5.0f).withTrimmedBottom(13.0f);
    g.setColour(selected || highlighted ? curveColour : muted);
    juce::Point<float> previous {graph.getX(), graph.getY() + points.front().y * graph.getHeight()};
    constexpr int steps = 40;
    for (int i = 1; i <= steps; ++i)
    {
        const float x = static_cast<float>(i) / steps;
        const juce::Point<float> next {graph.getX() + x * graph.getWidth(),
            graph.getY() + DuckingCurve::sample(points.data(), static_cast<int>(points.size()), x, true) * graph.getHeight()};
        g.drawLine({previous, next}, 1.6f);
        previous = next;
    }
    g.setColour(selected ? curveColour : ::text);
    g.setFont(font(12.0f, true));
    g.drawText(getButtonText().fromFirstOccurrenceOf(": ", false, false),
               getLocalBounds().withTop(getHeight() - 15), juce::Justification::centred);

}

KickDuck1AudioProcessorEditor::SignalScope::SignalScope()
{
    setName("Signal scope");
    setTooltip("Scrolling history (2 seconds): muted filled half-wave is Bass Output. Light GR line above shows actual gain reduction including Amount and Mix; deeper means stronger ducking. Input/Output gain does not affect GR.");
}

void KickDuck1AudioProcessorEditor::SignalScope::update(KickDuck1AudioProcessor& processor)
{
    const int samples = static_cast<int>(juce::jlimit(1.0, 2147483647.0,
        std::round(processor.getSampleRate() * historySeconds)));
    processor.copyWaveformSnapshot(input.data(), output.data(), kick.data(),
                                   static_cast<int>(input.size()), samples, false, reduction.data());
    rebuildPaths();
    repaint();
}

void KickDuck1AudioProcessorEditor::SignalScope::resized()
{
    rebuildPaths();
}

void KickDuck1AudioProcessorEditor::SignalScope::rebuildPaths()
{
    compressionPath.clear();
    outputShape.clear();
    // clear() retains capacity; reserve for the maximum scope width once.
    compressionPath.preallocateSpace(TriggeredWaveform::points * 3 + 8);
    outputShape.preallocateSpace(TriggeredWaveform::points * 3 + 16);
    const auto graph = getLocalBounds().toFloat().reduced(10.0f, 5.0f).withTrimmedTop(13.0f);
    const int columns = std::min(TriggeredWaveform::points, static_cast<int>(graph.getWidth()));
    if (columns < 2 || graph.getHeight() <= 0.0f) return;
    float peak = 0.08f;
    for (int i = 0; i < TriggeredWaveform::points; ++i)
    {
        if (std::isfinite(input[i])) peak = std::max(peak, std::abs(input[i]));
        if (std::isfinite(output[i])) peak = std::max(peak, std::abs(output[i]));
    }
    const float grTop = graph.getY() + 1.0f;
    const float grHeight = graph.getHeight() * 0.28f;
    const float waveHeight = graph.getHeight() * 0.55f;
    outputShape.startNewSubPath(graph.getX(), graph.getBottom());
    for (int column = 0; column < columns; ++column)
    {
        const int first = column * TriggeredWaveform::points / columns;
        const int last = (column + 1) * TriggeredWaveform::points / columns;
        float amplitude = 0.0f, gainReduction = 0.0f;
        for (int i = first; i < last; ++i)
        {
            if (std::isfinite(output[i])) amplitude = std::max(amplitude, std::abs(output[i]));
            if (std::isfinite(reduction[i])) gainReduction = std::max(gainReduction, reduction[i]);
        }
        const float x = graph.getX() + column * graph.getWidth() / (columns - 1);
        outputShape.lineTo(x, graph.getBottom() - amplitude / peak * waveHeight);
        const float grY = grTop + juce::jlimit(0.0f, 1.0f, gainReduction) * grHeight;
        if (column == 0) compressionPath.startNewSubPath(x, grY);
        else compressionPath.lineTo(x, grY);
    }
    outputShape.lineTo(graph.getRight(), graph.getBottom());
    outputShape.closeSubPath();
}

void KickDuck1AudioProcessorEditor::SignalScope::paint(juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(panel2);
    g.fillRoundedRectangle(box, 7.0f);
    g.setColour(grid);
    g.drawRoundedRectangle(box, 7.0f, 1.0f);
    g.setFont(font(10.0f, true));
    g.setColour(muted);
    g.drawText("SIGNAL", 10, 3, 52, 13, juce::Justification::left);
    g.setColour(juce::Colour::fromRGB(92, 150, 128));
    g.drawText("OUT", 68, 3, 30, 13, juce::Justification::left);
    g.setColour(juce::Colour::fromRGB(211, 220, 232));
    g.drawText("GR", 108, 3, 30, 13, juce::Justification::left);
    g.setColour(muted);
    g.drawText("LIVE 2 s",
               getWidth() - 82, 3, 72, 13, juce::Justification::right);
    const auto graph = getLocalBounds().toFloat().reduced(10.0f, 5.0f).withTrimmedTop(13.0f);
    g.setColour(grid);
    g.drawLine(graph.getX(), graph.getBottom(), graph.getRight(), graph.getBottom());
    g.setColour(juce::Colour::fromRGB(68, 121, 104).withAlpha(0.65f));
    g.fillPath(outputShape);
    g.setColour(juce::Colour::fromRGB(92, 150, 128).withAlpha(0.6f));
    g.strokePath(outputShape, juce::PathStrokeType(0.7f));
    g.setColour(juce::Colour::fromRGB(211, 220, 232));
    g.strokePath(compressionPath, juce::PathStrokeType(1.2f));
}

juce::Font KickDuck1AudioProcessorEditor::font(float size, bool bold)
{
    return juce::Font(
        juce::FontOptions{}
            .withHeight(size)
            .withStyle(bold ? "Bold" : "Regular"));
}

KickDuck1AudioProcessorEditor::KickDuck1AudioProcessorEditor(
    KickDuck1AudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p)
{
    setSize(1260, 800);
    setResizable(true, true);
    setResizeLimits(1050, 680, 1800, 1100);

    curveEditor.setPoints(processor.getCurvePoints());

    curveEditor.onPointsChanged =
        [this](const std::vector<DuckingCurve::Point>& points)
        {
            processor.editCurvePoints(points);
            curveEditor.setAmount(processor.apvts.getRawParameterValue("amount")->load() * 0.01f);
            curveEditor.setControls(processor.apvts.getRawParameterValue("length")->load(),
                                    processor.apvts.getRawParameterValue("attack")->load(),
                                    processor.apvts.getRawParameterValue("release")->load(),
                                    processor.getCycleSeconds(), processor.apvts.getRawParameterValue("onsetAttack")->load());
            curveEditor.setPoints(processor.getCurvePoints());
        };

    curveEditor.setName("Ducking curve");
    addAndMakeVisible(curveEditor);
    addAndMakeVisible(signalScope);
    resetCurveButton.onClick = [this]
    {
        processor.resetCurve();
        updateMeters();
        curveEditor.setPoints(processor.getCurvePoints());
    };
    addAndMakeVisible(resetCurveButton);

    setupKnob(onsetAttackSlider, onsetAttackLabel, "ATTACK");
    onsetAttackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, "onsetAttack", onsetAttackSlider);
    onsetAttackSlider.setTooltip("Time to reach the first point's reduction. Drag the first point horizontally to adjust Attack.");
    setupKnob(amountSlider, amountLabel, "AMOUNT");
    setupKnob(lengthSlider, lengthLabel, "LENGTH");
    setupKnob(attackSlider, attackLabel, "TAIL");
    setupKnob(releaseSlider, releaseLabel, "RECOVER");
    setupKnob(inputSlider, inputLabel, "INPUT");
    setupKnob(outputSlider, outputLabel, "OUTPUT");

    amountAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "amount", amountSlider);

    lengthAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "length", lengthSlider);

    attackAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "attack", attackSlider);

    releaseAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "release", releaseSlider);

    inputAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "input", inputSlider);

    outputAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "output", outputSlider);

    for (auto* s : {
             &inputPeakText,
             &inputRmsText,
             &inputLufsText,
             &outputPeakText,
             &outputRmsText,
             &outputLufsText,
             &kickPeakText,
             &kickRmsText,
             &grDbText,
             &grPercentText,
             &duckText,
             &sidechainText })
    {
        s->preallocateBytes(64);
    }

    inputSlider.setColour(juce::Slider::rotarySliderFillColourId, kickColour);
    inputSlider.setColour(juce::Slider::thumbColourId, kickColour);
    outputSlider.setColour(juce::Slider::rotarySliderFillColourId, bassOutColour);
    outputSlider.setColour(juce::Slider::thumbColourId, bassOutColour);
    amountLabel.setColour(juce::Label::textColourId, curveColour);
    amountLabel.setFont(font(13.0f, true));
    amountSlider.setTooltip("Main ducking depth. Legacy Mix remains available in the host.");
    legacyMixButton.setName("Legacy Mix");
    legacyMixButton.setTooltip("This project uses the legacy Mix parameter. Click to restore Mix to 100%.");
    legacyMixButton.onClick = [this]
    {
        auto* mix = processor.apvts.getParameter("mix");
        mix->beginChangeGesture();
        mix->setValueNotifyingHost(mix->convertTo0to1(100.0f));
        mix->endChangeGesture();
        updateMeters();
    };
    addChildComponent(legacyMixButton);
    bypassButton.setName("Bypass");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, kickColour.withAlpha(0.35f));
    bypassButton.setTooltip("Smoothly bypass ducking and input/output gain. The detector and clock continue running.");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, "bypass", bypassButton);
    const juce::StringArray modeNames {"AUTO", "AUDIO", "HOST SYNC"};
    const juce::StringArray modeTips {
        "Select Audio Trigger with AUX connected, Host Sync without AUX. Silence between kicks does not change mode.",
        "Trigger the curve from kick AUX only.",
        "Repeat the curve on the host transport grid, regardless of AUX. Stop returns dry."};
    for (int i = 0; i < static_cast<int>(modeButtons.size()); ++i)
    {
        auto& button = modeButtons[i];
        button.setButtonText(modeNames[i]);
        button.setName("Mode " + modeNames[i]);
        button.setTooltip(modeTips[i]);
        button.setColour(juce::TextButton::buttonOnColourId, curveColour.withAlpha(0.3f));
        button.onClick = [this, i]
        {
            const auto set = [this](const char* id, float value)
            {
                auto* p = processor.apvts.getParameter(id);
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(value));
                p->endChangeGesture();
            };
            // Set the explicit mode before disabling automatic routing.
            if (i != 0) set("triggerMode", static_cast<float>(i));
            set("automaticRouting", i == 0 ? 1.0f : 0.0f);
            updateMeters();
        };
        addAndMakeVisible(button);
    }
    const juce::StringArray divisions {"1/16", "1/8", "1/4", "1/2", "1 BAR"};
    for (int i = 0; i < static_cast<int>(divisionButtons.size()); ++i)
    {
        auto& button = divisionButtons[i];
        button.setButtonText(divisions[i]);
        button.setName("Division " + divisions[i]);
        button.setColour(juce::TextButton::buttonOnColourId, bassOutColour.withAlpha(0.25f));
        button.setTooltip("Cycle length; also repetition interval in Host Sync. 1 BAR means four quarter notes (4/4).");
        button.onClick = [this, i]
        {
            auto* p = processor.apvts.getParameter("cycleDivision");
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(i)));
            p->endChangeGesture();
            updateMeters();
        };
        addAndMakeVisible(button);
    }
    lengthSlider.setTooltip("Position of point 2: Length / 2 of the selected cycle. Does not delay the trigger.");
    releaseSlider.setTooltip("Time from the kick trigger to point 3 (recovery target). Not a compressor release constant.");
    attackSlider.setTooltip("Duration from the penultimate point to the end of the cycle. Not compressor attack or pre-delay.");
    for (int i = 0; i < DuckingCurve::presetCount; ++i)
    {
        const auto preset = static_cast<DuckingCurve::Preset>(i);
        auto& button = presetButtons[i];
        button = std::make_unique<CurvePresetButton>(preset);
        button->onClick = [this, preset]
        {
            processor.applyCurvePreset(preset);
            processor.tryCopyCurvePoints(curveSnapshot);
            curveEditor.setPoints(curveSnapshot);
            updateMeters();
        };
        addAndMakeVisible(*button);
    }
    resized();
    curveSnapshot.reserve(DuckingCurve::maxPoints);
    processor.tryCopyCurvePoints(curveSnapshot);
    updateMeters();
    updateWaveform();
    startTimerHz(30);
}

KickDuck1AudioProcessorEditor::~KickDuck1AudioProcessorEditor()
{
    stopTimer();
}

void KickDuck1AudioProcessorEditor::setupKnob(
    juce::Slider& slider,
    juce::Label& label,
    const juce::String& name)
{
    slider.setName(name);
    slider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag);

    slider.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, 76, 20);

    slider.setColour(
        juce::Slider::rotarySliderFillColourId, curveColour);
    slider.setColour(juce::Slider::thumbColourId, curveColour);

    slider.setColour(
        juce::Slider::rotarySliderOutlineColourId, gridBright);

    slider.setColour(
        juce::Slider::textBoxTextColourId, text);

    slider.setColour(
        juce::Slider::textBoxBackgroundColourId, panel2);

    slider.setColour(
        juce::Slider::textBoxOutlineColourId, grid);

    addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setFont(font(12.0f, true));
    label.setColour(juce::Label::textColourId, muted);
    label.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(label);
}

juce::String KickDuck1AudioProcessorEditor::formatDb(float value)
{
    if (!std::isfinite(value) || value <= -59.9f)
        return "-inf dB";

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
    return juce::String(
               juce::jlimit(0.0f, 100.0f, value), 1)
        + " %";
}

juce::String KickDuck1AudioProcessorEditor::formatLength(float value)
{
    if (std::abs(value - 0.125f) < 0.035f)
        return "1/8";

    if (std::abs(value - 0.25f) < 0.05f)
        return "1/4";

    if (std::abs(value - 0.5f) < 0.08f)
        return "1/2";

    if (std::abs(value - 1.0f) < 0.12f)
        return "1";

    if (std::abs(value - 2.0f) < 0.20f)
        return "2";

    return juce::String(value, 2);
}

void KickDuck1AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);

    auto outer = getLocalBounds().toFloat().reduced(10.0f);

    g.setColour(panel);
    g.fillRoundedRectangle(outer, 16.0f);

    g.setColour(grid);
    g.drawRoundedRectangle(
        outer.reduced(0.5f), 16.0f, 1.0f);

    g.setColour(text);
    g.setFont(font(24.0f, true));
    g.drawText(
        "KICKDUCK 1",
        28, 18, 250, 30,
        juce::Justification::left);

    g.setColour(muted);
    g.setFont(font(10.0f, true));
    g.drawText(
        "SIDECHAIN DUCKING / PUMPING",
        30, 46, 300, 18,
        juce::Justification::left);

    const bool connected = processor.isSidechainConnected();
    const float sx = static_cast<float>(getWidth()) - 292.0f;

    g.setColour(connected ? bassOutColour : muted);
    g.fillEllipse(sx, 29.0f, 8.0f, 8.0f);

    g.setColour(connected ? text : muted);
    g.setFont(font(10.5f, true));
    g.drawText(
        connected ? "KICK AUX  |  ONLINE" : "KICK AUX  |  OFFLINE",
        sx + 15.0f, 23.0f, 170.0f, 20.0f,
        juce::Justification::left);

    g.setColour(grid);
    g.drawHorizontalLine(
        70, 26.0f, static_cast<float>(getWidth()) - 26.0f);

    const auto meters = layout.meters.toFloat();
    const auto graph = layout.graph.toFloat();

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
    g.drawText(
        "KICK + BASS OUT  /  TRIGGERED FRAME",
        graph.getX() + 15.0f,
        graph.getY() + 10.0f,
        300.0f, 18.0f,
        juce::Justification::left);

    g.setColour(curveColour);
    g.drawText(
        "DUCK CURVE",
        graph.getRight() - 110.0f,
        graph.getY() + 10.0f,
        94.0f, 18.0f,
        juce::Justification::right);

    g.setColour(muted);
    g.drawText(
        "LEVELS",
        meters.getX() + 14.0f,
        meters.getY() + 10.0f,
        100.0f, 18.0f,
        juce::Justification::left);

    // Meter area and footer share the panel's geometry at every window size.
    auto m = meters.withTrimmedTop(36.0f).withTrimmedBottom(120.0f).reduced(12.0f, 0.0f);
    const float gap = 8.0f;
    const float w = (m.getWidth() - gap * 2.0f) / 3.0f;
    auto inputMeter = m.removeFromLeft(w);
    m.removeFromLeft(gap);
    auto grMeter = m.removeFromLeft(w);
    m.removeFromLeft(gap);
    auto outputMeter = m;

    drawMeter(
        g, inputMeter,
        inputMeterDb,
        "INPUT", inputPeakText, false);

    drawMeter(
        g, grMeter,
        grMeterDb,
        "GR", grDbText, true);

    drawMeter(
        g, outputMeter,
        outputMeterDb,
        "OUTPUT", outputPeakText, false);

    auto footer = meters.withTop(meters.getBottom() - 110.0f).reduced(12.0f, 0.0f);
    auto rmsRow = footer.removeFromTop(38.0f);
    const float rw = (rmsRow.getWidth() - 8.0f) * 0.5f;
    auto inRead = rmsRow.removeFromLeft(rw);
    rmsRow.removeFromLeft(8.0f);
    drawReadout(g, inRead, "INPUT RMS", inputRmsText, {}, kickColour);
    drawReadout(g, rmsRow, "OUTPUT RMS", outputRmsText, {}, bassOutColour);

    const auto footerText = [&g, &footer](const juce::String& value, juce::Colour colour)
    {
        g.setColour(colour);
        g.setFont(font(11.0f, true));
        g.drawText(value, footer.removeFromTop(21.0f), juce::Justification::centred);
    };
    footerText("KICK  " + kickPeakText + "  /  " + kickRmsText, kickColour);
    footerText(duckText, curveColour);
    footerText(sidechainText, connected ? bassOutColour : muted);

    const auto controlPanel = [&g](juce::Rectangle<int> bounds, const char* title, juce::Colour accent)
    {
        const auto area = bounds.toFloat();
        g.setColour(panel2);
        g.fillRoundedRectangle(area, 12.0f);
        g.setColour(accent.withAlpha(0.35f));
        g.drawRoundedRectangle(area.reduced(0.5f), 12.0f, 1.0f);
        g.setColour(accent);
        g.setFont(font(11.0f, true));
        g.drawText(title, bounds.withHeight(26).reduced(14, 0), juce::Justification::left);
    };
    controlPanel(layout.presets, "CURVE PRESETS", curveColour);
    controlPanel(layout.shape, "CURVE TIMING", muted);
    controlPanel(layout.gains, "SIGNAL LEVELS", bassOutColour);
    controlPanel(layout.amount, "DUCKING DEPTH", curveColour);
}

void KickDuck1AudioProcessorEditor::resized()
{
    resetCurveButton.setBounds(getWidth() - 120, 24, 98, 25);
    signalScope.setBounds(300, 14, getWidth() - 610, 50);
    auto content = getLocalBounds().withTrimmedTop(82).withTrimmedBottom(210).reduced(18, 0);
    layout.meters = content.removeFromRight(320);
    content.removeFromRight(10);
    layout.graph = content;
    curveEditor.setBounds(layout.graph);

    auto controls = getLocalBounds().removeFromBottom(196).reduced(18, 7);
    layout.amount = controls.removeFromRight(180);
    controls.removeFromRight(8);
    layout.gains = controls.removeFromRight(200);
    controls.removeFromRight(8);
    layout.presets = controls.removeFromLeft(270);
    controls.removeFromLeft(8);
    layout.shape = controls;

    const auto place = [](juce::Rectangle<int> cell, juce::Slider& slider, juce::Label& label)
    {
        label.setBounds(cell.removeFromTop(20));
        slider.setBounds(cell.reduced(2, 0));
    };
    auto launch = layout.shape.withTrimmedTop(132).reduced(8, 0);
    const int launchWidth = std::min(320, launch.getWidth());
    launch = launch.withSizeKeepingCentre(launchWidth, launch.getHeight());
    auto modes = launch.removeFromTop(20);
    const int modeWidth = modes.getWidth() / 4;
    for (auto& button : modeButtons)
        button.setBounds(modes.removeFromLeft(modeWidth).reduced(2, 0));
    bypassButton.setBounds(modes.reduced(2, 0));
    launch.removeFromTop(4);
    auto divisions = launch.removeFromTop(20);
    const int divisionWidth = divisions.getWidth() / 5;
    for (auto& button : divisionButtons)
        button.setBounds(divisions.removeFromLeft(divisionWidth).reduced(2, 0));
    auto shape = layout.shape.withTrimmedTop(28).reduced(4, 0);
    // Keep timing controls close together even in a wide editor.
    const int cellWidth = std::min(110, shape.getWidth() / 4);
    shape.removeFromLeft((shape.getWidth() - cellWidth * 4) / 2);
    place(shape.removeFromLeft(cellWidth), onsetAttackSlider, onsetAttackLabel);
    place(shape.removeFromLeft(cellWidth), lengthSlider, lengthLabel);
    place(shape.removeFromLeft(cellWidth), releaseSlider, releaseLabel);
    place(shape.removeFromLeft(cellWidth), attackSlider, attackLabel);
    auto gains = layout.gains.withTrimmedTop(28).reduced(10, 0);
    place(gains.removeFromLeft(gains.getWidth() / 2), inputSlider, inputLabel);
    place(gains, outputSlider, outputLabel);
    for (auto* slider : {&onsetAttackSlider, &lengthSlider, &attackSlider, &releaseSlider, &inputSlider, &outputSlider})
        slider->setBounds(slider->getBounds().withHeight(82));
    auto amount = layout.amount.withTrimmedTop(24).reduced(10, 0);
    legacyMixButton.setBounds(amount.removeFromBottom(22).reduced(4, 1));
    place(amount, amountSlider, amountLabel);
    auto presets = layout.presets.withTrimmedTop(32).reduced(10, 0);
    const int gap = 6;
    const int width = (presets.getWidth() - gap * 2) / 3;
    for (int i = 0; i < DuckingCurve::presetCount; ++i)
    {
        if (presetButtons[i])
            presetButtons[i]->setBounds(presets.getX() + (i % 3) * (width + gap),
                                        presets.getY() + (i / 3) * 58, width, 50);
    }
}

void KickDuck1AudioProcessorEditor::drawMeter(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    float levelDb,
    const juce::String& title,
    const juce::String& value,
    bool gainReduction) const
{
    g.setColour(panel3);
    g.fillRoundedRectangle(area, 8.0f);

    g.setColour(grid);
    g.drawRoundedRectangle(
        area.reduced(0.5f), 8.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(12.0f, true));
    g.drawText(
        title,
        area.getX(),
        area.getY() + 7.0f,
        area.getWidth(),
        15.0f,
        juce::Justification::centred);

    // Reserve space on the right for readable scale labels.
    auto bar = area.reduced(8.0f, 29.0f);
    bar.removeFromRight(27.0f);
    bar.removeFromBottom(34.0f);

    g.setColour(juce::Colour::fromRGB(8, 11, 15));
    g.fillRoundedRectangle(bar, 4.0f);

    const float safeLevelDb =
        std::isfinite(levelDb)
            ? levelDb
            : (gainReduction ? 0.0f : -60.0f);

    const float norm =
        gainReduction
            ? juce::jlimit(0.0f, 1.0f, -safeLevelDb / 24.0f)
            : dbNorm(safeLevelDb);

    const auto fill =
        gainReduction
            ? bar.withHeight(bar.getHeight() * norm)
            : bar.withTop(
                  bar.getBottom() - bar.getHeight() * norm);

    if (norm > 0.0f)
    {
        g.setColour(
            gainReduction
                ? grColour
                : (safeLevelDb >= 0.0f ? grColour
                   : (safeLevelDb > -6.0f ? kickColour : bassOutColour)));

        g.fillRoundedRectangle(fill, 4.0f);
    }

    g.setColour(gridBright);
    g.drawRoundedRectangle(bar, 4.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(11.0f));

    if (gainReduction)
    {
        // GR falls from the top towards the bottom.
        // The bar saturates at -24 dB; the numeric readout
        // continues to show the actual processor value.
        for (int gr : { 0, -6, -12, -18, -24 })
        {
            const float normalized =
                static_cast<float>(-gr) / 24.0f;

            const float y =
                bar.getY() + normalized * bar.getHeight();

            g.drawText(
                juce::String(gr),
                bar.getRight() + 3.0f,
                y - 7.0f,
                28.0f,
                15.0f,
                juce::Justification::left);
        }
    }
    else
    {
        for (int db : { 0, -12, -24, -36, -48, -60 })
        {
            const float normalized =
                static_cast<float>(db + 60) / 60.0f;

            const float y =
                bar.getBottom() - normalized * bar.getHeight();

            g.drawText(
                juce::String(db),
                bar.getRight() + 3.0f,
                y - 7.0f,
                28.0f,
                15.0f,
                juce::Justification::left);
        }
    }

    g.setColour(text);
    g.setFont(font(13.0f, true));
    g.drawText(
        value,
        area.getX(),
        area.getBottom() - 25.0f,
        area.getWidth(),
        18.0f,
        juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::drawReadout(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    const juce::String& title,
    const juce::String& main,
    const juce::String& sub,
    juce::Colour accent) const
{
    g.setColour(muted);
    g.setFont(font(11.0f, true));
    g.drawText(
        title,
        area.getX(),
        area.getY(),
        area.getWidth(),
        14.0f,
        juce::Justification::centred);

    g.setColour(accent);
    g.setFont(font(13.0f, true));
    g.drawText(
        main,
        area.getX(),
        area.getY() + 15.0f,
        area.getWidth(),
        20.0f,
        juce::Justification::centred);

    g.setColour(muted);
    g.setFont(font(8.0f));
    g.drawText(
        sub,
        area.getX(),
        area.getY() + 37.0f,
        area.getWidth(),
        14.0f,
        juce::Justification::centred);
}

void KickDuck1AudioProcessorEditor::timerCallback()
{
    // Reuse capacity: state restoration is reflected without allocating here.
    if (!curveEditor.isDragging() && processor.tryCopyCurvePoints(curveSnapshot))
        curveEditor.setPoints(curveSnapshot);
    updateMeters();
    updateWaveform();
    repaint();
}

void KickDuck1AudioProcessorEditor::updateMeters()
{
    const bool rounded = processor.isCurveRounded();
    curveEditor.setRounded(rounded);
    std::array<DuckingCurve::Point, DuckingCurve::maxPoints> effective {};
    const int count = static_cast<int>(curveSnapshot.size());
    std::copy(curveSnapshot.begin(), curveSnapshot.end(), effective.begin());
    DuckingCurve::applyControls(effective.data(), count,
        processor.apvts.getRawParameterValue("length")->load(),
        processor.apvts.getRawParameterValue("attack")->load(),
        processor.apvts.getRawParameterValue("release")->load(), processor.getCycleSeconds(), processor.apvts.getRawParameterValue("onsetAttack")->load());
    // Restrict manual controls to the geometry actually available at this BPM.
    // setRange/setValue without notifications never rewrite saved automation.
    const auto limitKnob = [](juce::Slider& slider, double low, double high,
                              double actual, double interval)
    {
        high = std::max(low + interval, high);
        slider.setRange(low, high, interval);
        slider.setValue(actual, juce::dontSendNotification);
    };
    const double ms = processor.getCycleSeconds() * 1000.0;
    if (count >= 2)
        limitKnob(onsetAttackSlider, 0.0, std::min(100.0, std::max(0.0, (effective[1].x - 0.002) * ms)),
                  effective[0].x * ms, 0.01);
    if (count >= 5)
    {
        const double recoverLow = std::max(10.0, (effective[1].x + 0.002) * ms);
        const double recoverHigh = std::min(4000.0, (effective[3].x - 0.002) * ms);
        limitKnob(releaseSlider, recoverLow, recoverHigh, effective[2].x * ms, 0.1);
        const int tail = count - 2;
        const double tailLow = std::max(0.0, (1.0 - curveSnapshot[tail + 1].x + 0.002) * ms);
        const double tailHigh = std::min(1000.0, (1.0 - effective[tail - 1].x - 0.002) * ms);
        limitKnob(attackSlider, tailLow, tailHigh, (1.0 - effective[tail].x) * ms, 0.01);
    }
    for (auto& button : presetButtons)
    {
        if (!button) continue;
        const auto& preset = button->getPoints();
        bool matches = rounded && count == static_cast<int>(preset.size());
        for (int i = 0; matches && i < count; ++i)
            matches = std::abs(effective[i].x - preset[i].x) < 0.0005f
                && std::abs(effective[i].y - preset[i].y) < 0.0005f;
        button->setToggleState(matches, juce::dontSendNotification);
    }
    const float legacyMix = processor.apvts.getRawParameterValue("mix")->load();
    legacyMixButton.setVisible(std::abs(legacyMix - 100.0f) > 0.01f);
    legacyMixButton.setButtonText("MIX " + juce::String(legacyMix, 0) + "%  -> 100%");
    const auto* amountParam =
        processor.apvts.getRawParameterValue("amount");

    const float amount =
        amountParam != nullptr
            ? amountParam->load(std::memory_order_relaxed) * 0.01f
            : 0.75f;

    curveEditor.setAmount(amount);

    const auto* lengthParam =
        processor.apvts.getRawParameterValue("length");

    const float length =
        lengthParam != nullptr
            ? lengthParam->load(std::memory_order_relaxed)
            : 0.5f;

    curveEditor.setControls(length,
        processor.apvts.getRawParameterValue("attack")->load(),
        processor.apvts.getRawParameterValue("release")->load(),
        processor.getCycleSeconds(), processor.apvts.getRawParameterValue("onsetAttack")->load());
    curveEditor.setPhase(processor.getCurrentPhase());

    inputMeterDb = processor.getInputPeak();
    outputMeterDb = processor.getOutputPeak();
    grMeterDb = processor.getMeterGainReductionDb();
    cacheNumber(
        inputPeakText, inputMeterDb, " dB");

    cacheNumber(
        inputRmsText, processor.getInputRms(), " dB");

    cacheNumber(
        inputLufsText, processor.getInputLufs(), " LUFS");

    cacheNumber(
        outputPeakText, outputMeterDb, " dB");

    cacheNumber(
        outputRmsText, processor.getOutputRms(), " dB");

    cacheNumber(
        outputLufsText, processor.getOutputLufs(), " LUFS");

    cacheNumber(
        kickPeakText, processor.getKickActivity(), " dB");

    cacheNumber(
        kickRmsText, processor.getKickRms(), " dB");

    cacheNumber(
        grDbText, grMeterDb, " dB");

    cacheNumber(
        grPercentText, (1.0f - std::pow(10.0f, grMeterDb / 20.0f)) * 100.0f, " %");

    duckText =
        "DUCK  "
        + formatPercent(processor.getCurrentDuck() * 100.0f)
        + "   |   AMOUNT  "
        + formatPercent(amount * 100.0f);

    sidechainText =
        processor.isSidechainConnected()
            ? "SIDECHAIN ACTIVE"
            : "AUX OFF";
    const bool automatic = processor.apvts.getRawParameterValue("automaticRouting")->load() > 0.5f;
    const bool connected = processor.isSidechainConnected();
    const int legacyMode = static_cast<int>(processor.apvts.getRawParameterValue("triggerMode")->load());
    const bool synced = automatic ? !connected : (legacyMode == 2 || (legacyMode == 0 && !connected));
    const juce::String modeName = synced ? "HOST SYNC" : "AUDIO TRIGGER";
    const int selectedMode = automatic || legacyMode == 0 ? 0 : legacyMode;
    for (int i = 0; i < static_cast<int>(modeButtons.size()); ++i)
        modeButtons[i].setToggleState(i == selectedMode, juce::dontSendNotification);
    const int division = static_cast<int>(processor.apvts.getRawParameterValue("cycleDivision")->load());
    for (int i = 0; i < static_cast<int>(divisionButtons.size()); ++i)
        divisionButtons[i].setToggleState(i == division, juce::dontSendNotification);
    sidechainText = (automatic || legacyMode == 0 ? "AUTO | " : "") + modeName;

}

void KickDuck1AudioProcessorEditor::updateWaveform()
{
    curveEditor.setWaveformsFromProcessor(processor);
    signalScope.update(processor);
}

KickDuck1AudioProcessorEditor::CurveEditor::CurveEditor()
{
    points.reserve(maxCurvePoints);
    storedPoints.reserve(maxCurvePoints);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float>
KickDuck1AudioProcessorEditor::CurveEditor::graphBounds() const
{
    return getLocalBounds().toFloat().reduced(18.0f, 34.0f);
}

void KickDuck1AudioProcessorEditor::CurveEditor::setPoints(
    const std::vector<DuckingCurve::Point>& p)
{
    storedPoints.assign(p.begin(), p.begin() + std::min(p.size(), static_cast<size_t>(maxCurvePoints)));
    if (storedPoints.size() < 2)
        storedPoints = DuckingCurve::defaultPoints();
    updateEffectivePoints();
}

void KickDuck1AudioProcessorEditor::CurveEditor::updateEffectivePoints()
{
    points.assign(storedPoints.begin(), storedPoints.end());
    DuckingCurve::applyControls(points.data(), static_cast<int>(points.size()),
                               length, attack, release, cycleSeconds, onsetAttack);
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::normalizeWaveform(
    std::array<float, maxWaveformPoints>& waveform)
{
    float peak = 0.0f;

    for (float v : waveform)
    {
        if (std::isfinite(v))
            peak = std::max(peak, std::abs(v));
    }

    if (peak < 0.00001f)
    {
        waveform.fill(0.0f);
        return;
    }

    const float gain = 0.94f / peak;

    for (float& v : waveform)
    {
        v = std::isfinite(v)
            ? juce::jlimit(-1.0f, 1.0f, v * gain)
            : 0.0f;
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::setWaveformsFromProcessor(
    KickDuck1AudioProcessor& p)
{
    double frameSeconds = 0.0;
    if (!p.copyTriggeredWaveform(bassInWaveform.data(), bassOutWaveform.data(),
                                 kickWaveform.data(), waveformVersion, frameSeconds))
        return;

    // Visual-only scaling.
    //
    // Bass Out uses Bass In as the reference. Do not normalize
    // Bass Out independently: attenuation must remain visible.
    //
    // Unlike the old upper gain limits of 0.92 and 1.0,
    // a bounded gain above unity can lift quiet waveforms.
    // This changes display data only, never the audio signal.
    float bassReferencePeak = 0.0f;
    float kickPeak = 0.0f;

    for (int i = 0; i < maxWaveformPoints; ++i)
    {
        const auto index = static_cast<size_t>(i);

        if (!std::isfinite(bassInWaveform[index]))
            bassInWaveform[index] = 0.0f;

        if (!std::isfinite(bassOutWaveform[index]))
            bassOutWaveform[index] = 0.0f;

        if (!std::isfinite(kickWaveform[index]))
            kickWaveform[index] = 0.0f;

        bassReferencePeak =
            std::max(
                bassReferencePeak,
                std::abs(bassInWaveform[index]));

        kickPeak =
            std::max(
                kickPeak,
                std::abs(kickWaveform[index]));
    }

    constexpr float targetDisplayPeak = 0.94f;
    constexpr float maxDisplayGain = 32.0f;

    if (bassReferencePeak > 0.00001f)
    {
        const float gain =
            juce::jlimit(
                0.001f,
                maxDisplayGain,
                targetDisplayPeak / bassReferencePeak);

        for (float& v : bassOutWaveform)
            v = juce::jlimit(-1.0f, 1.0f, v * gain);
    }
    else
    {
        bassOutWaveform.fill(0.0f);
    }

    if (kickPeak > 0.00001f)
    {
        const float gain =
            juce::jlimit(
                0.001f,
                maxDisplayGain,
                targetDisplayPeak / kickPeak);

        for (float& v : kickWaveform)
            v = juce::jlimit(-1.0f, 1.0f, v * gain);
    }
    else
    {
        kickWaveform.fill(0.0f);
    }

    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setAmount(float v)
{
    amount = juce::jlimit(0.0f, 1.0f, v);
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setControls(
    float musicalLength, float attackMs, float releaseMs, double seconds, float onsetMs)
{
    if (length == musicalLength && attack == attackMs && release == releaseMs && cycleSeconds == seconds && onsetAttack == onsetMs)
        return;
    length = musicalLength;
    attack = attackMs;
    release = releaseMs;
    cycleSeconds = seconds;
    onsetAttack = onsetMs;
    updateEffectivePoints();
}

void KickDuck1AudioProcessorEditor::CurveEditor::setPhase(float v)
{
    phase = juce::jlimit(0.0f, 1.0f, v);
    repaint();
}

float KickDuck1AudioProcessorEditor::CurveEditor::xToNorm(float x) const
{
    auto b = graphBounds();

    return juce::jlimit(
        0.0f,
        1.0f,
        (x - b.getX()) / b.getWidth());
}

float
KickDuck1AudioProcessorEditor::CurveEditor::screenYToVisualNorm(
    float y) const
{
    auto b = graphBounds();

    // Screen orientation:
    // top = no reduction, bottom = maximum reduction.
    return juce::jlimit(
        0.0f,
        1.0f,
        (y - b.getY()) / b.getHeight());
}

juce::Point<float>
KickDuck1AudioProcessorEditor::CurveEditor::pointToScreen(
    const DuckingCurve::Point& point) const
{
    auto b = graphBounds();

    // DSP Y remains duck depth:
    // 0 = no reduction, 1 = maximum reduction.
    //
    // The GUI now places maximum depth at the bottom.
    // AMOUNT scales the displayed effective depth.
    const float visual =
        juce::jlimit(0.0f, 1.0f, point.y * amount);

    return {
        b.getX() + point.x * b.getWidth(),
        b.getY() + visual * b.getHeight()
    };
}

int KickDuck1AudioProcessorEditor::CurveEditor::findPoint(
    juce::Point<float> pos) const
{
    int found = -1;
    float best = 18.0f;

    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const float d =
            pointToScreen(points[static_cast<size_t>(i)])
                .getDistanceFrom(pos);

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
    storedPoints.assign(points.begin(), points.end());
    for (auto& p : storedPoints) p.y *= amount;
    if (onPointsChanged)
        onPointsChanged(storedPoints);
    updateEffectivePoints();
}

void KickDuck1AudioProcessorEditor::CurveEditor::drawWaveform(
    juce::Graphics& g,
    const std::array<float, maxWaveformPoints>& waveform,
    juce::Rectangle<float> area,
    juce::Colour colour,
    float thickness) const
{
    const float mid = area.getCentreY();

    // Use nearly the full graph height while retaining
    // a small margin around waveform peaks.
    const float scale = area.getHeight() * 0.49f;

    g.setColour(colour.withAlpha(0.92f));

    for (int i = 1; i < maxWaveformPoints; ++i)
    {
        const float x0 =
            area.getX()
            + area.getWidth()
                * static_cast<float>(i - 1)
                / static_cast<float>(maxWaveformPoints - 1);

        const float x1 =
            area.getX()
            + area.getWidth()
                * static_cast<float>(i)
                / static_cast<float>(maxWaveformPoints - 1);

        const float y0 =
            mid
            - juce::jlimit(
                  -1.0f,
                  1.0f,
                  waveform[static_cast<size_t>(i - 1)])
                * scale;

        const float y1 =
            mid
            - juce::jlimit(
                  -1.0f,
                  1.0f,
                  waveform[static_cast<size_t>(i)])
                * scale;

        g.drawLine(x0, y0, x1, y1, thickness);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::drawCurve(
    juce::Graphics& g) const
{
    if (points.size() < 2)
        return;

    g.setColour(curveColour);
    const auto graph = graphBounds();
    const int steps = std::clamp(static_cast<int>(graph.getWidth()), 256, 1600);
    auto previous = pointToScreen({0.0f, DuckingCurve::sample(points.data(), static_cast<int>(points.size()), 0.0f, rounded)});
    for (int i = 1; i <= steps; ++i)
    {
        const float x = static_cast<float>(i) / steps;
        const float depth = DuckingCurve::sample(points.data(), static_cast<int>(points.size()), x, rounded);
        const juce::Point<float> next {graph.getX() + x * graph.getWidth(),
                                       graph.getY() + depth * amount * graph.getHeight()};
        g.drawLine({previous, next}, 3.0f);
        previous = next;
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::paint(
    juce::Graphics& g)
{
    auto b = graphBounds();

    // Grid.
    g.setColour(grid);

    for (int i = 0; i <= 8; ++i)
    {
        const float x =
            b.getX()
            + b.getWidth() * static_cast<float>(i) / 8.0f;

        g.drawVerticalLine(
            static_cast<int>(x), b.getY(), b.getBottom());
    }

    for (int i = 0; i <= 6; ++i)
    {
        const float y =
            b.getY()
            + b.getHeight() * static_cast<float>(i) / 6.0f;

        g.drawHorizontalLine(
            static_cast<int>(y), b.getX(), b.getRight());
    }

    // Shared zero line for Kick and Bass Out.
    const float mid = b.getCentreY();

    g.setColour(gridBright);
    g.drawHorizontalLine(
        static_cast<int>(mid), b.getX(), b.getRight());

    // Both signals share the centre line.
    // Bass Out retains its size relative to Bass In.
    // Kick uses an independent visual-only normalization.
    drawWaveform(
        g, kickWaveform, b, kickColour, 1.9f);

    drawWaveform(
        g, bassOutWaveform, b, bassOutColour, 1.15f);

    // Duck curve overlays the same graph.
    drawCurve(g);

    // Subtle playhead; waveform remains a held snapshot.
    const float px = b.getX() + phase * b.getWidth();

    g.setColour(juce::Colours::white.withAlpha(0.20f));
    g.drawVerticalLine(
        static_cast<int>(px), b.getY(), b.getBottom());

    // Legend.
    g.setFont(font(10.0f, true));

    g.setColour(kickColour);
    g.fillEllipse(
        b.getX() + 5.0f, b.getY() + 7.0f, 7.0f, 7.0f);

    g.setColour(text);
    g.drawText(
        "KICK",
        b.getX() + 17.0f, b.getY(),
        45.0f, 20.0f,
        juce::Justification::left);

    g.setColour(bassOutColour);
    g.fillEllipse(
        b.getX() + 72.0f, b.getY() + 7.0f, 7.0f, 7.0f);

    g.setColour(text);
    g.drawText(
        "BASS OUT",
        b.getX() + 84.0f, b.getY(),
        70.0f, 20.0f,
        juce::Justification::left);

    g.setColour(curveColour);
    g.fillEllipse(
        b.getX() + 174.0f, b.getY() + 7.0f, 7.0f, 7.0f);

    g.setColour(text);
    g.drawText(
        "DUCK",
        b.getX() + 186.0f, b.getY(),
        45.0f, 20.0f,
        juce::Justification::left);

    g.setColour(muted);
    g.setFont(font(8.5f, true));
    g.drawText(
        "ONE CYCLE  |  DRAG POINTS  |  DOUBLE-CLICK ADD  |  RIGHT-CLICK DELETE",
        b.getRight() - 440.0f,
        b.getBottom() - 19.0f,
        430.0f,
        15.0f,
        juce::Justification::right);

    g.setColour(curveColour);
    g.setFont(font(9.0f, true));
    g.drawText(
        "AMOUNT  " + juce::String(amount * 100.0f, 0) + "%",
        b.getRight() - 95.0f,
        b.getY(),
        85.0f,
        18.0f,
        juce::Justification::right);

    // Curve points.
    for (size_t i = 0; i < points.size(); ++i)
    {
        const auto p = pointToScreen(points[i]);

        const bool selected =
            static_cast<int>(i) == selectedPoint;

        const float r = selected ? 7.0f : 6.0f;

        g.setColour(
            selected ? juce::Colours::white : curveColour);

        g.fillEllipse(
            p.x - r,
            p.y - r,
            2.0f * r,
            2.0f * r);

        g.setColour(bg);
        g.fillEllipse(
            p.x - 2.0f,
            p.y - 2.0f,
            4.0f,
            4.0f);
    }
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseMove(const juce::MouseEvent& e)
{
    const int index = findPoint(e.position);
    if (index == static_cast<int>(points.size()) - 1)
        setTooltip("End depth at " + juce::String(cycleSeconds * 1000.0, 0)
            + " ms. Shapes the final TAIL segment. A new kick can restart the cycle before it reaches this point.");
    else if (index == 0)
        setTooltip("Depth reached after ATTACK. Drag horizontally to set attack time, vertically to set ducking depth.");
    else
        setTooltip("Drag points to shape gain reduction. Top: no ducking; bottom: Amount depth.");
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDown(
    const juce::MouseEvent& e)
{
    selectedPoint = findPoint(e.position);

    if (e.mods.isRightButtonDown())
    {
        if (selectedPoint > 0
            && selectedPoint < static_cast<int>(points.size()) - 1)
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

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDrag(
    const juce::MouseEvent& e)
{
    if (!dragging || selectedPoint < 0)
        return;

    float x = xToNorm(e.position.x);

    const float visual =
        screenYToVisualNorm(e.position.y);

    if (selectedPoint == 0)
    {
        x = juce::jlimit(0.0f, std::min(100.0f / static_cast<float>(cycleSeconds * 1000.0),
                                      std::max(0.0f, points[1].x - 0.002f)), x);
    }
    else if (selectedPoint == static_cast<int>(points.size()) - 1)
    {
        x = 1.0f;
    }
    else
    {
        x = std::max(
            x,
            points[static_cast<size_t>(selectedPoint - 1)].x
                + 0.002f);

        if (selectedPoint + 1 < static_cast<int>(points.size()))
        {
            x = std::min(
                x,
                points[static_cast<size_t>(selectedPoint + 1)].x
                    - 0.002f);
        }
    }

    // Raising the deepest reduction also raises AMOUNT. Rescale the other
    // normalized depths so their displayed positions stay exactly where they were.
    if (visual > amount)
    {
        const float scale = amount / std::max(0.000001f, visual);
        for (auto& p : points) p.y *= scale;
        amount = visual;
    }
    const float y = amount > 0.000001f ? visual / amount : 0.0f;

    points[static_cast<size_t>(selectedPoint)].x =
        juce::jlimit(0.0f, 1.0f, x);

    points[static_cast<size_t>(selectedPoint)].y =
        juce::jlimit(0.0f, 1.0f, y);

    notifyPointsChanged();
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseUp(
    const juce::MouseEvent&)
{
    dragging = false;
}

void KickDuck1AudioProcessorEditor::CurveEditor::mouseDoubleClick(
    const juce::MouseEvent& e)
{
    if (findPoint(e.position) >= 0
        || points.size() >= maxCurvePoints)
    {
        return;
    }

    const float x = xToNorm(e.position.x);

    const float visual =
        screenYToVisualNorm(e.position.y);

    if (visual > amount)
    {
        const float scale = amount / std::max(0.000001f, visual);
        for (auto& p : points) p.y *= scale;
        amount = visual;
    }
    const float y = amount > 0.000001f ? visual / amount : 0.0f;

    points.push_back({
        x,
        juce::jlimit(0.0f, 1.0f, y)
    });

    std::sort(
        points.begin(),
        points.end(),
        [](const auto& a, const auto& b)
        {
            return a.x < b.x;
        });

    notifyPointsChanged();
    repaint();
}
