#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    const juce::Colour bg =
        juce::Colour::fromRGB(7, 9, 13);

    const juce::Colour panel =
        juce::Colour::fromRGB(16, 19, 25);

    const juce::Colour panel2 =
        juce::Colour::fromRGB(11, 14, 19);

    const juce::Colour panel3 =
        juce::Colour::fromRGB(20, 24, 31);

    const juce::Colour grid =
        juce::Colour::fromRGB(38, 43, 52);

    const juce::Colour gridBright =
        juce::Colour::fromRGB(57, 64, 76);

    const juce::Colour text =
        juce::Colour::fromRGB(236, 240, 247);

    const juce::Colour muted =
        juce::Colour::fromRGB(133, 141, 154);

    const juce::Colour kickColour =
        juce::Colour::fromRGB(255, 158, 68);

    const juce::Colour bassOutColour =
        juce::Colour::fromRGB(69, 222, 153);

    const juce::Colour curveColour =
        juce::Colour::fromRGB(191, 118, 255);

    const juce::Colour grColour =
        juce::Colour::fromRGB(255, 91, 116);


    float dbNorm(float db) noexcept
    {
        return juce::jlimit(
            0.0f,
            1.0f,
            (db + 60.0f) / 60.0f);
    }


    void cacheNumber(
        juce::String& dst,
        float value,
        const char* suffix = "",
        int decimals = 1)
    {
        dst.clear();
        dst.preallocateBytes(48);

        if (!std::isfinite(value) || value <= -59.9f)
        {
            dst = "-∞";
        }
        else
        {
            char buf[32]{};

            std::snprintf(
                buf,
                sizeof(buf),
                "%.*f",
                decimals,
                value);

            dst = buf;
        }

        dst += suffix;
    }


    std::vector<DuckingCurve::Point> makeDefaultCurve()
    {
        return
        {
            { 0.00f, 1.00f },
            { 0.055f, 0.48f },
            { 0.13f, 0.22f },
            { 0.32f, 0.18f },
            { 0.55f, 0.30f },
            { 0.77f, 0.63f },
            { 1.00f, 1.00f }
        };
    }


    bool isFlatUnityCurve(
        const std::vector<DuckingCurve::Point>& p)
    {
        if (p.size() < 2)
            return true;

        for (const auto& point : p)
        {
            if (std::abs(point.y - 1.0f) > 0.0001f)
                return false;
        }

        return true;
    }
}


//=============================================================================
// Editor
//=============================================================================

juce::Font KickDuck1AudioProcessorEditor::makeFont(
    float size,
    bool bold)
{
    return juce::Font(
        juce::FontOptions{}
            .withHeight(size)
            .withStyle(bold ? "Bold" : "Regular"));
}


void KickDuck1AudioProcessorEditor::setLabelStyle(
    juce::Label& label,
    float size,
    juce::Colour colour)
{
    label.setColour(
        juce::Label::textColourId,
        colour);

    label.setFont(
        makeFont(size, true));

    label.setJustificationType(
        juce::Justification::centred);
}


//=============================================================================
// Constructor
//=============================================================================

KickDuck1AudioProcessorEditor::
KickDuck1AudioProcessorEditor(
    KickDuck1AudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p)
{
    setSize(1280, 800);

    setResizable(
        true,
        true);

    setResizeLimits(
        1040,
        660,
        1900,
        1200);


    // If the processor starts with the old unity curve,
    // replace it with a useful ducking curve immediately.
    auto curve = processor.getCurvePoints();

    if (isFlatUnityCurve(curve))
    {
        curve = makeDefaultCurve();

        processor.setCurvePoints(curve);
    }

    curveEditor.setPoints(curve);

    curveEditor.onPointsChanged =
        [this](const std::vector<DuckingCurve::Point>& points)
    {
        processor.setCurvePoints(points);
    };

    addAndMakeVisible(curveEditor);


    setupKnob(
        amountSlider,
        amountLabel,
        "AMOUNT");

    setupKnob(
        lengthSlider,
        lengthLabel,
        "LENGTH");

    setupKnob(
        attackSlider,
        attackLabel,
        "ATTACK");

    setupKnob(
        releaseSlider,
        releaseLabel,
        "RELEASE");

    setupKnob(
        mixSlider,
        mixLabel,
        "MIX");


    amountAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts,
                "amount",
                amountSlider);

    lengthAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts,
                "length",
                lengthSlider);

    attackAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts,
                "attack",
                attackSlider);

    releaseAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts,
                "release",
                releaseSlider);

    mixAttachment =
        std::make_unique<
            juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts,
                "mix",
                mixSlider);


    for (auto* s :
         {
             &inputPeakText,
             &outputPeakText,
             &inputRmsText,
             &outputRmsText,
             &inputLufsText,
             &outputLufsText,
             &kickPeakText,
             &kickRmsText,
             &grDbText,
             &grPercentText,
             &duckText,
             &sidechainText
         })
    {
        s->preallocateBytes(64);
    }


    startTimerHz(24);
}


//=============================================================================

KickDuck1AudioProcessorEditor::
~KickDuck1AudioProcessorEditor()
{
    stopTimer();
}


//=============================================================================
// Knobs
//=============================================================================

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
        21);

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

    slider.setColour(
        juce::Slider::textBoxHighlightColourId,
        curveColour);

    addAndMakeVisible(slider);


    label.setText(
        name,
        juce::dontSendNotification);

    setLabelStyle(
        label,
        10.5f,
        muted);

    addAndMakeVisible(label);
}


//=============================================================================
// Formatting
//=============================================================================

juce::String KickDuck1AudioProcessorEditor::formatDb(
    float value)
{
    if (!std::isfinite(value) || value <= -59.9f)
        return "-∞ dB";

    return juce::String(value, 1) + " dB";
}


juce::String KickDuck1AudioProcessorEditor::formatMs(
    float value)
{
    if (value >= 1000.0f)
        return juce::String(value / 1000.0f, 2) + " s";

    return juce::String(
               value,
               value < 100.0f ? 1 : 0)
           + " ms";
}


juce::String KickDuck1AudioProcessorEditor::formatPercent(
    float value)
{
    return juce::String(
               juce::jlimit(
                   0.0f,
                   100.0f,
                   value),
               1)
           + " %";
}


juce::String KickDuck1AudioProcessorEditor::formatLength(
    float value)
{
    struct LengthName
    {
        float value;
        const char* name;
    };

    constexpr LengthName names[] =
    {
        { 0.125f, "1/8" },
        { 0.25f,  "1/4" },
        { 0.5f,   "1/2" },
        { 1.0f,   "1" },
        { 2.0f,   "2" }
    };

    const auto* best = &names[0];

    float distance =
        std::abs(value - best->value);

    for (const auto& item : names)
    {
        const float d =
            std::abs(value - item.value);

        if (d < distance)
        {
            distance = d;
            best = &item;
        }
    }

    if (distance < 0.08f)
        return best->name;

    return juce::String(value, 2);
}


//=============================================================================
// Main paint
//=============================================================================

void KickDuck1AudioProcessorEditor::paint(
    juce::Graphics& g)
{
    g.fillAll(bg);


    const auto outer =
        getLocalBounds()
            .toFloat()
            .reduced(10.0f);

    g.setColour(panel);

    g.fillRoundedRectangle(
        outer,
        16.0f);

    g.setColour(grid);

    g.drawRoundedRectangle(
        outer.reduced(0.5f),
        16.0f,
        1.0f);


    // Header
    //-------------------------------------------------------------------------

    g.setColour(text);

    g.setFont(
        makeFont(23.0f, true));

    g.drawText(
        "KICKDUCK 1",
        28,
        19,
        220,
        28,
        juce::Justification::left);


    g.setColour(muted);

    g.setFont(
        makeFont(10.0f, true));

    g.drawText(
        "SIDECHAIN DUCKING",
        30,
        46,
        180,
        17,
        juce::Justification::left);


    const bool connected =
        processor.isSidechainConnected();

    const float statusX =
        static_cast<float>(getWidth()) - 275.0f;


    g.setColour(
        connected
            ? bassOutColour
            : muted);

    g.fillEllipse(
        statusX,
        30.0f,
        8.0f,
        8.0f);


    g.setColour(
        connected
            ? text
            : muted);

    g.setFont(
        makeFont(10.5f, true));

    g.drawText(
        connected
            ? "KICK AUX  •  ONLINE"
            : "KICK AUX  •  WAITING",
        statusX + 15.0f,
        24.0f,
        170.0f,
        20.0f,
        juce::Justification::left);


    g.setColour(grid);

    g.drawHorizontalLine(
        70,
        26.0f,
        static_cast<float>(getWidth()) - 26.0f);


    // Main layout
    //-------------------------------------------------------------------------

    auto content =
        getLocalBounds()
            .withTrimmedTop(82)
            .withTrimmedBottom(154)
            .reduced(18, 0)
            .toFloat();


    auto meters =
        content.removeFromRight(245.0f);

    auto graph =
        content.reduced(0.0f, 1.0f);


    // Graph card
    g.setColour(panel2);

    g.fillRoundedRectangle(
        graph,
        12.0f);

    g.setColour(grid);

    g.drawRoundedRectangle(
        graph,
        12.0f,
        1.0f);


    // Meter card
    g.setColour(panel2);

    g.fillRoundedRectangle(
        meters,
        12.0f);

    g.setColour(grid);

    g.drawRoundedRectangle(
        meters,
        12.0f,
        1.0f);


    g.setColour(muted);

    g.setFont(
        makeFont(9.5f, true));

    g.drawText(
        "KICK + BASS OUT  /  LIVE",
        graph.getX() + 15.0f,
        graph.getY() + 10.0f,
        250.0f,
        16.0f,
        juce::Justification::left);


    g.setColour(curveColour);

    g.drawText(
        "DUCK CURVE",
        graph.getRight() - 110.0f,
        graph.getY() + 10.0f,
        94.0f,
        16.0f,
        juce::Justification::right);


    g.setColour(muted);

    g.setFont(
        makeFont(9.5f, true));

    g.drawText(
        "LEVELS",
        meters.getX() + 13.0f,
        meters.getY() + 10.0f,
        meters.getWidth() - 26.0f,
        16.0f,
        juce::Justification::left);


    // Bottom controls
    //-------------------------------------------------------------------------

    auto controls =
        getLocalBounds()
            .removeFromBottom(140)
            .reduced(22, 8)
            .toFloat();


    g.setColour(panel2);

    g.fillRoundedRectangle(
        controls,
        12.0f);

    g.setColour(grid);

    g.drawRoundedRectangle(
        controls,
        12.0f,
        1.0f);


    g.setColour(muted);

    g.setFont(
        makeFont(9.5f, true));

    g.drawText(
        "DUCKING CONTROLS",
        controls.getX() + 14.0f,
        controls.getY() + 8.0f,
        150.0f,
        16.0f,
        juce::Justification::left);


    // Status text.
    g.setColour(muted);

    g.setFont(
        makeFont(9.0f));

    g.drawText(
        sidechainText,
        meters.getX() + 12.0f,
        meters.getBottom() - 24.0f,
        meters.getWidth() - 24.0f,
        17.0f,
        juce::Justification::centred);
}


//=============================================================================
// Resized
//=============================================================================

void KickDuck1AudioProcessorEditor::resized()
{
    auto content =
        getLocalBounds()
            .withTrimmedTop(82)
            .withTrimmedBottom(154)
            .reduced(18, 0);


    auto meters =
        content.removeFromRight(245);


    curveEditor.setBounds(
        content.reduced(0, 1));


    auto meterArea =
        meters.reduced(12, 34);

    const int gap = 8;

    const int cardW =
        (meterArea.getWidth() - gap * 2) / 3;


    auto m1 =
        meterArea.removeFromLeft(cardW);

    meterArea.removeFromLeft(gap);

    auto m2 =
        meterArea.removeFromLeft(cardW);

    meterArea.removeFromLeft(gap);

    auto m3 = meterArea;


    // Meter values are drawn directly in paint().
    // There are deliberately no child labels here.
    juce::ignoreUnused(
        m1,
        m2,
        m3);


    // Controls
    auto controls =
        getLocalBounds()
            .removeFromBottom(140)
            .reduced(22, 8);

    controls.removeFromTop(25);


    const int count = 5;
    const int gapPx = 8;

    const int cellW =
        (controls.getWidth()
         - gapPx * (count - 1))
        / count;


    auto place =
        [&controls, cellW, gapPx]
        (juce::Slider& slider,
         juce::Label& label)
    {
        auto cell =
            controls.removeFromLeft(cellW);

        controls.removeFromLeft(gapPx);

        label.setBounds(
            cell.removeFromTop(18));

        slider.setBounds(
            cell.reduced(5, 0));
    };


    place(
        amountSlider,
        amountLabel);

    place(
        lengthSlider,
        lengthLabel);

    place(
        attackSlider,
        attackLabel);

    place(
        releaseSlider,
        releaseLabel);

    place(
        mixSlider,
        mixLabel);
}


//=============================================================================
// Meter
//=============================================================================

void KickDuck1AudioProcessorEditor::drawMeterCard(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    const juce::String& title,
    float levelDb,
    const juce::String& value,
    bool gainReduction) const
{
    g.setColour(panel3);

    g.fillRoundedRectangle(
        area,
        8.0f);

    g.setColour(grid);

    g.drawRoundedRectangle(
        area.reduced(0.5f),
        8.0f,
        1.0f);


    g.setColour(muted);

    g.setFont(
        makeFont(8.5f, true));

    g.drawText(
        title,
        area.getX(),
        area.getY() + 7.0f,
        area.getWidth(),
        16.0f,
        juce::Justification::centred);


    auto bar =
        area.reduced(8.0f, 30.0f);

    bar.removeFromBottom(34.0f);


    g.setColour(
        juce::Colour::fromRGB(
            8,
            11,
            15));

    g.fillRoundedRectangle(
        bar,
        4.0f);


    float normalized = 0.0f;

    if (gainReduction)
    {
        normalized =
            juce::jlimit(
                0.0f,
                1.0f,
                -levelDb / 24.0f);
    }
    else
    {
        normalized =
            dbNorm(levelDb);
    }


    auto fill =
        bar.withTop(
            bar.getBottom()
            - bar.getHeight()
              * normalized);


    g.setColour(
        gainReduction
            ? grColour
            : (levelDb > -6.0f
                   ? kickColour
                   : bassOutColour));

    g.fillRoundedRectangle(
        fill,
        4.0f);


    g.setColour(gridBright);

    g.drawRoundedRectangle(
        bar,
        4.0f,
        1.0f);


    // Scale
    g.setColour(muted);

    g.setFont(
        makeFont(7.5f));

    const int marks[] =
    {
        0,
        -12,
        -24,
        -36,
        -48,
        -60
    };


    for (const int db : marks)
    {
        const float y =
            bar.getBottom()
            - ((db + 60.0f) / 60.0f)
              * bar.getHeight();


        g.drawText(
            juce::String(db),
            bar.getRight() + 3.0f,
            y - 5.0f,
            24.0f,
            11.0f,
            juce::Justification::left);
    }


    // Digital value.
    g.setColour(text);

    g.setFont(
        makeFont(11.0f, true));

    g.drawText(
        value,
        area.getX() + 3.0f,
        area.getBottom() - 29.0f,
        area.getWidth() - 6.0f,
        18.0f,
        juce::Justification::centred);
}


//=============================================================================
// Small readout
//=============================================================================

void KickDuck1AudioProcessorEditor::drawSmallReadout(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    const juce::String& title,
    const juce::String& main,
    const juce::String& sub,
    juce::Colour accent) const
{
    g.setColour(muted);

    g.setFont(
        makeFont(8.5f, true));

    g.drawText(
        title,
        area.getX(),
        area.getY(),
        area.getWidth(),
        14.0f,
        juce::Justification::centred);


    g.setColour(accent);

    g.setFont(
        makeFont(13.0f, true));

    g.drawText(
        main,
        area.getX(),
        area.getY() + 14.0f,
        area.getWidth(),
        20.0f,
        juce::Justification::centred);


    g.setColour(muted);

    g.setFont(
        makeFont(8.5f));

    g.drawText(
        sub,
        area.getX(),
        area.getY() + 35.0f,
        area.getWidth(),
        14.0f,
        juce::Justification::centred);
}


//=============================================================================
// Control card
//=============================================================================

void KickDuck1AudioProcessorEditor::drawControlCard(
    juce::Graphics& g,
    juce::Rectangle<float> area,
    const juce::String& title,
    const juce::String& value,
    juce::Colour accent) const
{
    g.setColour(panel3);

    g.fillRoundedRectangle(
        area,
        8.0f);

    g.setColour(grid);

    g.drawRoundedRectangle(
        area.reduced(0.5f),
        8.0f,
        1.0f);


    g.setColour(accent);

    g.fillRoundedRectangle(
        area.getX() + 1.0f,
        area.getY() + 1.0f,
        3.0f,
        area.getHeight() - 2.0f,
        2.0f);


    g.setColour(muted);

    g.setFont(
        makeFont(8.0f, true));

    g.drawText(
        title,
        area.getX() + 8.0f,
        area.getBottom() - 20.0f,
        area.getWidth() - 16.0f,
        12.0f,
        juce::Justification::centred);


    g.setColour(text);

    g.setFont(
        makeFont(10.0f, true));

    g.drawText(
        value,
        area.getX() + 8.0f,
        area.getY() + 8.0f,
        area.getWidth() - 16.0f,
        18.0f,
        juce::Justification::centred);
}


//=============================================================================
// Timer
//=============================================================================

void KickDuck1AudioProcessorEditor::timerCallback()
{
    updateWaveform();
    updateMeters();

    repaint();
}


//=============================================================================
// Meters / parameters
//=============================================================================

void KickDuck1AudioProcessorEditor::updateMeters()
{
    const auto* amountParam =
        processor.apvts.getRawParameterValue("amount");

    const auto* lengthParam =
        processor.apvts.getRawParameterValue("length");

    const auto* attackParam =
        processor.apvts.getRawParameterValue("attack");

    const auto* releaseParam =
        processor.apvts.getRawParameterValue("release");

    const auto* mixParam =
        processor.apvts.getRawParameterValue("mix");


    const float amount =
        amountParam != nullptr
            ? amountParam->load(
                  std::memory_order_relaxed) * 0.01f
            : 0.75f;


    const float length =
        lengthParam != nullptr
            ? lengthParam->load(
                  std::memory_order_relaxed)
            : 0.5f;


    const float attack =
        attackParam != nullptr
            ? attackParam->load(
                  std::memory_order_relaxed)
            : 8.0f;


    const float release =
        releaseParam != nullptr
            ? releaseParam->load(
                  std::memory_order_relaxed)
            : 220.0f;


    const float mix =
        mixParam != nullptr
            ? mixParam->load(
                  std::memory_order_relaxed)
            : 100.0f;


    curveEditor.setAmount(amount);


    // Current processor API exposes current duck amount.
    // It is used as a subtle live activity marker.
    curveEditor.setPhase(
        processor.getCurrentDuck());


    cacheNumber(
        inputPeakText,
        processor.getInputPeak(),
        " dB");


    cacheNumber(
        outputPeakText,
        processor.getOutputPeak(),
        " dB");


    cacheNumber(
        inputRmsText,
        processor.getInputRms(),
        " dB");


    cacheNumber(
        outputRmsText,
        processor.getOutputRms(),
        " dB");


    cacheNumber(
        inputLufsText,
        processor.getInputLufs(),
        " LUFS");


    cacheNumber(
        outputLufsText,
        processor.getOutputLufs(),
        " LUFS");


    cacheNumber(
        kickPeakText,
        processor.getKickActivity(),
        " dB");


    cacheNumber(
        kickRmsText,
        processor.getKickRms(),
        " dB");


    cacheNumber(
        grDbText,
        processor.getGainReductionDb(),
        " dB");


    cacheNumber(
        grPercentText,
        processor.getGainReductionPercent(),
        " %");


    duckText =
        "DUCK "
        + formatPercent(
              processor.getCurrentDuck()
              * 100.0f);


    duckText +=
        "  •  "
        + formatPercent(
              amount * 100.0f)
        + " AMOUNT";


    sidechainText =
        processor.isSidechainConnected()
            ? "●  SIDECHAIN ACTIVE"
            : "○  SIDECHAIN NOT CONNECTED";


    juce::ignoreUnused(
        length,
        attack,
        release,
        mix);
}


//=============================================================================
// Waveform update
//=============================================================================

void KickDuck1AudioProcessorEditor::updateWaveform()
{
    const double sr =
        processor.getSampleRate();


    if (!std::isfinite(sr) || sr <= 0.0)
        return;


    // 1.5 seconds of history.
    // All GUI storage is fixed-size.
    const int samples =
        juce::jlimit(
            1,
            96000,
            static_cast<int>(
                sr * 1.5));


    curveEditor.setWaveformsFromProcessor(
        processor,
        samples);
}


//=============================================================================
// Curve editor
//=============================================================================

KickDuck1AudioProcessorEditor::
CurveEditor::CurveEditor()
{
    setMouseCursor(
        juce::MouseCursor::PointingHandCursor);
}


//-----------------------------------------------------------------------------

juce::Rectangle<float>
KickDuck1AudioProcessorEditor::
CurveEditor::graphBounds() const
{
    return getLocalBounds()
        .toFloat()
        .reduced(18.0f, 34.0f);
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::setPoints(
    const std::vector<DuckingCurve::Point>& p)
{
    points = p;

    if (points.size() < 2)
        points = makeDefaultCurve();

    if (points.size() > maxCurvePoints)
        points.resize(maxCurvePoints);

    repaint();
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::setWaveforms(
    const float* bassIn,
    const float* bassOut,
    const float* kick)
{
    if (bassIn == nullptr ||
        bassOut == nullptr ||
        kick == nullptr)
        return;


    std::copy(
        bassIn,
        bassIn + maxWaveformPoints,
        bassInWaveform.begin());


    std::copy(
        bassOut,
        bassOut + maxWaveformPoints,
        bassOutWaveform.begin());


    std::copy(
        kick,
        kick + maxWaveformPoints,
        kickWaveform.begin());


    repaint();
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::setWaveformsFromProcessor(
    KickDuck1AudioProcessor& p,
    int samplesToCopy)
{
    // The current WaveformHistory API requires
    // all three destinations.
    //
    // BASS IN is copied only for API compatibility.
    // It is NOT rendered by the new UI.

    p.copyWaveformSnapshot(
        bassInWaveform.data(),
        bassOutWaveform.data(),
        kickWaveform.data(),
        maxWaveformPoints,
        samplesToCopy);

    repaint();
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::setAmount(
    float v)
{
    amount =
        juce::jlimit(
            0.0f,
            1.0f,
            v);

    repaint();
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::setPhase(
    float v)
{
    phase =
        juce::jlimit(
            0.0f,
            1.0f,
            v);

    repaint();
}


//-----------------------------------------------------------------------------

float KickDuck1AudioProcessorEditor::
CurveEditor::xToNorm(float x) const
{
    const auto b =
        graphBounds();

    return juce::jlimit(
        0.0f,
        1.0f,
        (x - b.getX())
        / b.getWidth());
}


//-----------------------------------------------------------------------------

float KickDuck1AudioProcessorEditor::
CurveEditor::yToNorm(float y) const
{
    const auto b =
        graphBounds();

    return juce::jlimit(
        0.0f,
        1.0f,
        1.0f
        - (y - b.getY())
          / b.getHeight());
}


//-----------------------------------------------------------------------------

juce::Point<float>
KickDuck1AudioProcessorEditor::
CurveEditor::normToPoint(
    float x,
    float y) const
{
    const auto b =
        graphBounds();

    return
    {
        b.getX()
            + x * b.getWidth(),

        b.getBottom()
            - y * b.getHeight()
    };
}


//-----------------------------------------------------------------------------

int KickDuck1AudioProcessorEditor::
CurveEditor::findPoint(
    juce::Point<float> pos) const
{
    int found = -1;

    float best = 15.0f;


    for (int i = 0;
         i < static_cast<int>(points.size());
         ++i)
    {
        const auto p =
            normToPoint(
                points[static_cast<size_t>(i)].x,
                curveVisualValue(
                    points[static_cast<size_t>(i)].y));


        const float distance =
            p.getDistanceFrom(pos);


        if (distance < best)
        {
            best = distance;
            found = i;
        }
    }


    return found;
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::notifyPointsChanged()
{
    if (onPointsChanged)
        onPointsChanged(points);
}


//-----------------------------------------------------------------------------

float KickDuck1AudioProcessorEditor::
CurveEditor::curveVisualValue(
    float y) const noexcept
{
    // AMOUNT = 0:
    // completely flat unity response.
    //
    // AMOUNT = 1:
    // exact stored curve.

    return
        1.0f
        - amount
          * (1.0f
             - juce::jlimit(
                   0.0f,
                   1.0f,
                   y));
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::drawWaveform(
    juce::Graphics& g,
    const std::array<float, maxWaveformPoints>& waveform,
    juce::Rectangle<float> area,
    juce::Colour colour,
    float verticalScale) const
{
    const float mid =
        area.getCentreY();


    const float scale =
        area.getHeight()
        * 0.5f
        * verticalScale;


    g.setColour(
        colour.withAlpha(0.88f));


    for (int i = 1;
         i < maxWaveformPoints;
         ++i)
    {
        const float x0 =
            area.getX()
            + area.getWidth()
              * static_cast<float>(i - 1)
              / static_cast<float>(
                    maxWaveformPoints - 1);


        const float x1 =
            area.getX()
            + area.getWidth()
              * static_cast<float>(i)
              / static_cast<float>(
                    maxWaveformPoints - 1);


        const float y0 =
            mid
            - juce::jlimit(
                  -1.0f,
                  1.0f,
                  waveform[
                      static_cast<size_t>(i - 1)])
              * scale;


        const float y1 =
            mid
            - juce::jlimit(
                  -1.0f,
                  1.0f,
                  waveform[
                      static_cast<size_t>(i)])
              * scale;


        g.drawLine(
            x0,
            y0,
            x1,
            y1,
            1.35f);
    }
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::drawDuckCurve(
    juce::Graphics& g) const
{
    if (points.size() < 2)
        return;


    g.setColour(
        curveColour.withAlpha(0.95f));


    for (size_t i = 1;
         i < points.size();
         ++i)
    {
        const auto a =
            normToPoint(
                points[i - 1].x,
                curveVisualValue(
                    points[i - 1].y));


        const auto b =
            normToPoint(
                points[i].x,
                curveVisualValue(
                    points[i].y));


        g.drawLine(
            a.x,
            a.y,
            b.x,
            b.y,
            2.5f);
    }
}


//=============================================================================
// Curve paint
//=============================================================================

void KickDuck1AudioProcessorEditor::
CurveEditor::paint(
    juce::Graphics& g)
{
    const auto b =
        graphBounds();


    const float mid =
        b.getCentreY();


    // Grid
    //-------------------------------------------------------------------------

    g.setColour(
        grid.withAlpha(0.78f));


    for (int i = 0;
         i <= 8;
         ++i)
    {
        const float x =
            b.getX()
            + b.getWidth()
              * static_cast<float>(i)
              / 8.0f;


        g.drawVerticalLine(
            static_cast<int>(x),
            b.getY(),
            b.getBottom());
    }


    for (int i = 0;
         i <= 6;
         ++i)
    {
        const float y =
            b.getY()
            + b.getHeight()
              * static_cast<float>(i)
              / 6.0f;


        g.drawHorizontalLine(
            static_cast<int>(y),
            b.getX(),
            b.getRight());
    }


    // Strong zero line.
    // KICK and BASS OUT are deliberately centered here.
    //-------------------------------------------------------------------------

    g.setColour(
        gridBright.withAlpha(0.9f));


    g.drawHorizontalLine(
        static_cast<int>(mid),
        b.getX(),
        b.getRight());


    // Both waveforms use EXACTLY the same area.
    //-------------------------------------------------------------------------

    drawWaveform(
        g,
        kickWaveform,
        b,
        kickColour,
        0.72f);


    drawWaveform(
        g,
        bassOutWaveform,
        b,
        bassOutColour,
        0.72f);


    // Duck curve.
    //-------------------------------------------------------------------------

    drawDuckCurve(g);


    // Live activity marker.
    //-------------------------------------------------------------------------

    const float markerX =
        b.getX()
        + phase * b.getWidth();


    g.setColour(
        juce::Colours::white
            .withAlpha(0.28f));


    g.drawVerticalLine(
        static_cast<int>(markerX),
        b.getY(),
        b.getBottom());


    // Legend.
    //-------------------------------------------------------------------------

    g.setFont(
        juce::Font(
            juce::FontOptions{}
                .withHeight(9.5f)
                .withStyle("Bold")));


    g.setColour(kickColour);

    g.fillEllipse(
        b.getX() + 3.0f,
        b.getY() + 5.0f,
        7.0f,
        7.0f);


    g.setColour(text);

    g.drawText(
        "KICK",
        b.getX() + 14.0f,
        b.getY(),
        48.0f,
        18.0f,
        juce::Justification::left);


    g.setColour(bassOutColour);

    g.fillEllipse(
        b.getX() + 68.0f,
        b.getY() + 5.0f,
        7.0f,
        7.0f);


    g.setColour(text);

    g.drawText(
        "BASS OUT",
        b.getX() + 79.0f,
        b.getY(),
        72.0f,
        18.0f,
        juce::Justification::left);


    g.setColour(curveColour);

    g.fillEllipse(
        b.getX() + 170.0f,
        b.getY() + 5.0f,
        7.0f,
        7.0f);


    g.setColour(text);

    g.drawText(
        "DUCK CURVE",
        b.getX() + 181.0f,
        b.getY(),
        88.0f,
        18.0f,
        juce::Justification::left);


    // Editing hint.
    g.setColour(muted);

    g.setFont(
        juce::Font(
            juce::FontOptions{}
                .withHeight(8.5f)
                .withStyle("Bold")));


    g.drawText(
        "DRAG POINTS  •  DOUBLE-CLICK ADD  •  RIGHT-CLICK DELETE",
        b.getRight() - 370.0f,
        b.getBottom() - 19.0f,
        360.0f,
        15.0f,
        juce::Justification::right);


    // Amount.
    g.setColour(curveColour);

    g.setFont(
        juce::Font(
            juce::FontOptions{}
                .withHeight(9.0f)
                .withStyle("Bold")));


    g.drawText(
        "AMOUNT  "
        + juce::String(
              amount * 100.0f,
              0)
        + "%",
        b.getRight() - 105.0f,
        b.getY(),
        95.0f,
        18.0f,
        juce::Justification::right);


    // Curve points.
    //-------------------------------------------------------------------------

    for (size_t i = 0;
         i < points.size();
         ++i)
    {
        const auto& point =
            points[i];


        const auto p =
            normToPoint(
                point.x,
                curveVisualValue(point.y));


        const bool selected =
            static_cast<int>(i)
            == selectedPoint;


        const float radius =
            selected
                ? 6.5f
                : 5.0f;


        g.setColour(
            selected
                ? juce::Colours::white
                : curveColour);


        g.fillEllipse(
            p.x - radius,
            p.y - radius,
            radius * 2.0f,
            radius * 2.0f);


        g.setColour(bg);

        g.fillEllipse(
            p.x - 1.7f,
            p.y - 1.7f,
            3.4f,
            3.4f);
    }
}


//=============================================================================
// Mouse
//=============================================================================

void KickDuck1AudioProcessorEditor::
CurveEditor::mouseDown(
    const juce::MouseEvent& e)
{
    selectedPoint =
        findPoint(e.position);


    if (e.mods.isRightButtonDown())
    {
        if (selectedPoint > 0 &&
            selectedPoint
                < static_cast<int>(
                    points.size()) - 1)
        {
            points.erase(
                points.begin()
                + selectedPoint);


            selectedPoint = -1;

            notifyPointsChanged();

            repaint();
        }

        return;
    }


    if (selectedPoint >= 0)
        dragging = true;
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::mouseDrag(
    const juce::MouseEvent& e)
{
    if (!dragging ||
        selectedPoint < 0)
        return;


    float x =
        xToNorm(e.position.x);


    const float visualY =
        yToNorm(e.position.y);


    if (selectedPoint == 0)
    {
        x = 0.0f;
    }
    else if (
        selectedPoint
        == static_cast<int>(
               points.size()) - 1)
    {
        x = 1.0f;
    }
    else
    {
        x =
            std::max(
                x,
                points[
                    static_cast<size_t>(
                        selectedPoint) - 1].x
                + 0.002f);


        if (selectedPoint + 1
            < static_cast<int>(
                points.size()))
        {
            x =
                std::min(
                    x,
                    points[
                        static_cast<size_t>(
                            selectedPoint) + 1].x
                    - 0.002f);
        }
    }


    // Convert visible curve position
    // back to stored curve coordinates.
    float y;


    if (amount > 0.001f)
    {
        y =
            1.0f
            - (1.0f - visualY)
              / amount;
    }
    else
    {
        y = 1.0f;
    }


    points[
        static_cast<size_t>(
            selectedPoint)].x =
        juce::jlimit(
            0.0f,
            1.0f,
            x);


    points[
        static_cast<size_t>(
            selectedPoint)].y =
        juce::jlimit(
            0.0f,
            1.0f,
            y);


    notifyPointsChanged();

    repaint();
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::mouseUp(
    const juce::MouseEvent&)
{
    dragging = false;
}


//-----------------------------------------------------------------------------

void KickDuck1AudioProcessorEditor::
CurveEditor::mouseDoubleClick(
    const juce::MouseEvent& e)
{
    if (findPoint(e.position) >= 0)
        return;


    if (points.size() >= maxCurvePoints)
        return;


    float x =
        xToNorm(e.position.x);


    const float visualY =
        yToNorm(e.position.y);


    float y =
        amount > 0.001f
            ? 1.0f
              - (1.0f - visualY)
                / amount
            : 1.0f;


    points.push_back(
        {
            juce::jlimit(
                0.0f,
                1.0f,
                x),

            juce::jlimit(
                0.0f,
                1.0f,
                y)
        });


    std::sort(
        points.begin(),
        points.end(),
        [](const auto& a,
           const auto& b)
        {
            return a.x < b.x;
        });


    notifyPointsChanged();

    repaint();
}
