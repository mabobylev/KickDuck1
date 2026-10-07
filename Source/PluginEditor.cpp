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
        // DSP/GUI y is duck depth:
        // 0 = no reduction, 1 = maximum reduction.
        //
        // На экране y = 1 будет находиться внизу графика,
        // то есть первая точка соответствует максимальному воздействию.
        return {
            { 0.00f, 1.00f },
            { 0.18f, 1.00f },
            { 0.72f, 0.58f },
            { 0.93f, 0.12f },
            { 1.00f, 0.00f }
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

    auto curve = processor.getCurvePoints();
    bool needsDefaultCurve = isUnityCurve(curve);

    if (!needsDefaultCurve && curve.size() == 5)
    {
        // Миграция предыдущего UI6 default,
        // в котором первая точка означала отсутствие ducking.
        needsDefaultCurve =
            std::abs(curve[0].x - 0.00f) < 0.01f
            && std::abs(curve[0].y - 0.00f) < 0.01f
            && std::abs(curve[1].x - 0.18f) < 0.02f
            && std::abs(curve[1].y - 0.14f) < 0.03f
            && std::abs(curve[2].x - 0.72f) < 0.02f;
    }

    if (needsDefaultCurve)
    {
        curve = defaultCurve();
        processor.setCurvePoints(curve);
    }

    curveEditor.setPoints(curve);

    curveEditor.onPointsChanged =
        [this](const std::vector<DuckingCurve::Point>& points)
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

    mixAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, "mix", mixSlider);

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

    startTimerHz(24);
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
    slider.setSliderStyle(
        juce::Slider::RotaryHorizontalVerticalDrag);

    slider.setTextBoxStyle(
        juce::Slider::TextBoxBelow,
        false,
        76,
        20);

    slider.setColour(
        juce::Slider::rotarySliderFillColourId,
        curveColour);

    slider.setColour(
        juce::Slider::rotarySliderOutlineColourId,
        gridBright);

    slider.setColour(
        juce::Slider::textBoxTextColourId,
        text);

    slider.setColour(
        juce::Slider::textBoxBackgroundColourId,
        panel2);

    slider.setColour(
        juce::Slider::textBoxOutlineColourId,
        grid);

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
    return juce::String(
               juce::jlimit(0.0f, 100.0f, value),
               1)
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

    auto outer =
        getLocalBounds()
            .toFloat()
            .reduced(10.0f);

    g.setColour(panel);
    g.fillRoundedRectangle(outer, 16.0f);

    g.setColour(grid);
    g.drawRoundedRectangle(
        outer.reduced(0.5f),
        16.0f,
        1.0f);

    g.setColour(text);
    g.setFont(font(24.0f, true));
    g.drawText(
        "KICKDUCK 1",
        28,
        18,
        250,
        30,
        juce::Justification::left);

    g.setColour(muted);
    g.setFont(font(10.0f, true));
    g.drawText(
        "SIDECHAIN DUCKING / PUMPING",
        30,
        46,
        300,
        18,
        juce::Justification::left);

    const bool connected =
        processor.isSidechainConnected();

    const float sx =
        static_cast<float>(getWidth()) - 292.0f;

    g.setColour(connected ? bassOutColour : muted);
    g.fillEllipse(sx, 29.0f, 8.0f, 8.0f);

    g.setColour(connected ? text : muted);
    g.setFont(font(10.5f, true));
    g.drawText(
        connected
            ? "KICK AUX  •  ONLINE"
            : "KICK AUX  •  WAITING",
        sx + 15.0f,
        23.0f,
        170.0f,
        20.0f,
        juce::Justification::left);

    g.setColour(grid);
    g.drawHorizontalLine(
        70,
        26.0f,
        static_cast<float>(getWidth()) - 26.0f);

    auto content =
        getLocalBounds()
            .withTrimmedTop(82)
            .withTrimmedBottom(154)
            .reduced(18, 0)
            .toFloat();

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
    g.drawText(
        "KICK + BASS OUT  /  SNAPSHOT HISTORY",
        graph.getX() + 15,
        graph.getY() + 10,
        300,
        18,
        juce::Justification::left);

    g.setColour(curveColour);
    g.drawText(
        "DUCK CURVE",
        graph.getRight() - 110,
        graph.getY() + 10,
        94,
        18,
        juce::Justification::right);

    g.setColour(muted);
    g.drawText(
        "LEVELS",
        meters.getX() + 14,
        meters.getY() + 10,
        100,
        18,
        juce::Justification::left);

    // Три индикатора расположены в порядке:
    //
    // INPUT | GR | OUTPUT
    //
    // GR находится между входным и выходным сигналами.
    auto m =
        meters
            .withTrimmedTop(38.0f)
            .withHeight(290.0f)
            .reduced(12.0f, 0.0f);

    const float gap = 8.0f;
    const float w =
        (m.getWidth() - gap * 2.0f) / 3.0f;

    auto inputMeter = m.removeFromLeft(w);
    m.removeFromLeft(gap);

    auto grMeter = m.removeFromLeft(w);
    m.removeFromLeft(gap);

    auto outputMeter = m;

    drawMeter(
        g,
        inputMeter,
        processor.getInputPeak(),
        "INPUT",
        inputPeakText,
        false);

    drawMeter(
        g,
        grMeter,
        processor.getGainReductionDb(),
        "GR",
        grDbText,
        true);

    drawMeter(
        g,
        outputMeter,
        processor.getOutputPeak(),
        "OUTPUT",
        outputPeakText,
        false);

    auto readouts =
        meters
            .reduced(12.0f, 0.0f);

    readouts =
        readouts.withTop(
            readouts.getBottom() - 102.0f);

    const float rw =
        (readouts.getWidth() - 8.0f) * 0.5f;

    auto inRead = readouts.removeFromLeft(rw);
    readouts.removeFromLeft(8.0f);

    auto outRead = readouts;

    drawReadout(
        g,
        inRead,
        "INPUT",
        inputRmsText,
        inputLufsText,
        kickColour);

    drawReadout(
        g,
        outRead,
        "OUTPUT",
        outputRmsText,
        outputLufsText,
        bassOutColour);

    g.setColour(kickColour);
    g.setFont(font(9.5f, true));
    g.drawText(
        "KICK  " + kickPeakText + "  /  " + kickRmsText,
        meters.getX() + 12,
        meters.getBottom() - 88,
        meters.getWidth() - 24,
        18,
        juce::Justification::centred);

    g.setColour(curveColour);
    g.drawText(
        duckText,
        meters.getX() + 12,
        meters.getBottom() - 64,
        meters.getWidth() - 24,
        18,
        juce::Justification::centred);

    g.setColour(connected ? bassOutColour : muted);
    g.drawText(
        sidechainText,
        meters.getX() + 12,
        meters.getBottom() - 39,
        meters.getWidth() - 24,
        18,
        juce::Justification::centred);

    auto controls =
        getLocalBounds()
            .removeFromBottom(140)
            .reduced(22, 7)
            .toFloat();

    g.setColour(panel2);
    g.fillRoundedRectangle(controls, 12.0f);

    g.setColour(grid);
    g.drawRoundedRectangle(controls, 12.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(9.5f, true));
    g.drawText(
        "DUCKING CONTROLS",
        controls.getX() + 14,
        controls.getY() + 8,
        150,
        16,
        juce::Justification::left);
}

void KickDuck1AudioProcessorEditor::resized()
{
    auto content =
        getLocalBounds()
            .withTrimmedTop(82)
            .withTrimmedBottom(154)
            .reduced(18, 0);

    auto meters = content.removeFromRight(290);
    curveEditor.setBounds(content);

    auto controls =
        getLocalBounds()
            .removeFromBottom(140)
            .reduced(22, 7);

    controls.removeFromTop(25);

    const int gap = 8;
    const int cellW =
        (controls.getWidth() - gap * 6) / 7;

    auto place =
        [&controls, cellW, gap](
            juce::Slider& slider,
            juce::Label& label)
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
        area.reduced(0.5f),
        8.0f,
        1.0f);

    g.setColour(muted);
    g.setFont(font(8.5f, true));
    g.drawText(
        title,
        area.getX(),
        area.getY() + 7.0f,
        area.getWidth(),
        15.0f,
        juce::Justification::centred);

    auto bar =
        area.reduced(8.0f, 29.0f);

    bar.removeFromBottom(34.0f);

    g.setColour(juce::Colour::fromRGB(8, 11, 15));
    g.fillRoundedRectangle(bar, 4.0f);

    const float norm =
        gainReduction
            ? juce::jlimit(0.0f, 1.0f, -levelDb / 24.0f)
            : dbNorm(levelDb);

    juce::Rectangle<float> fill;

    if (gainReduction)
    {
        // GR заполняется сверху вниз:
        //
        // 0 dB   — верхняя граница,
        // -6 dB  — частичное заполнение,
        // -12 dB — более глубокое заполнение,
        // -24 dB — заполнение почти до низа.
        fill =
            bar.withHeight(
                bar.getHeight() * norm);
    }
    else
    {
        // INPUT и OUTPUT заполняются снизу вверх.
        fill =
            bar.withTop(
                bar.getBottom()
                - bar.getHeight() * norm);
    }

    g.setColour(
        gainReduction
            ? grColour
            : (levelDb > -6.0f
                ? kickColour
                : bassOutColour));

    g.fillRoundedRectangle(fill, 4.0f);

    g.setColour(gridBright);
    g.drawRoundedRectangle(bar, 4.0f, 1.0f);

    g.setColour(muted);
    g.setFont(font(7.0f));

    if (gainReduction)
    {
        // Отдельная шкала gain reduction.
        for (int gr : { 0, -6, -12, -18, -24 })
        {
            const float normalized =
                static_cast<float>(-gr) / 24.0f;

            const float y =
                bar.getY()
                + normalized * bar.getHeight();

            g.drawText(
                juce::String(gr),
                bar.getRight() + 3.0f,
                y - 5.0f,
                28.0f,
                11.0f,
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
                bar.getBottom()
                - normalized * bar.getHeight();

            g.drawText(
                juce::String(db),
                bar.getRight() + 3.0f,
                y - 5.0f,
                28.0f,
                11.0f,
                juce::Justification::left);
        }
    }

    g.setColour(text);
    g.setFont(font(10.0f, true));
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
    g.setFont(font(8.0f, true));
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
    updateMeters();
    updateWaveform();
    repaint();
}

void KickDuck1AudioProcessorEditor::updateMeters()
{
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

    curveEditor.setLength(length);
    curveEditor.setPhase(processor.getCurrentPhase());

    cacheNumber(
        inputPeakText,
        processor.getInputPeak(),
        " dB");

    cacheNumber(
        inputRmsText,
        processor.getInputRms(),
        " dB");

    cacheNumber(
        inputLufsText,
        processor.getInputLufs(),
        " LUFS");

    cacheNumber(
        outputPeakText,
        processor.getOutputPeak(),
        " dB");

    cacheNumber(
        outputRmsText,
        processor.getOutputRms(),
        " dB");

    cacheNumber(
        outputLufsText,
        processor.getOutputLufs(),
        " LUFS");

   
