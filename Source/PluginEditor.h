#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>
#include <vector>

class KickDuck1AudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit KickDuck1AudioProcessorEditor(KickDuck1AudioProcessor&);
    ~KickDuck1AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class CompactButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    };

    class SignalScope : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SignalScope();
        void update(KickDuck1AudioProcessor&);
        void paint(juce::Graphics&) override;
        void resized() override;
    private:
        void rebuildPaths();
        std::array<float, TriggeredWaveform::points> input {}, output {}, kick {}, reduction {};
        static constexpr double historySeconds = 2.0;
        juce::Path compressionPath, outputShape;
    };

    class CurvePresetButton : public juce::Button
    {
    public:
        explicit CurvePresetButton(DuckingCurve::Preset preset);
        const std::vector<DuckingCurve::Point>& getPoints() const noexcept { return points; }
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    private:
        std::vector<DuckingCurve::Point> points;
    };

    class CurveEditor : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        CurveEditor();

        void setPoints(const std::vector<DuckingCurve::Point>&);
        void setWaveformsFromProcessor(KickDuck1AudioProcessor&);
        void setAmount(float normalizedAmount);
        void setControls(float musicalLength, float attackMs, float releaseMs, double cycleSeconds, float onsetMs = 0.0f);
        bool isDragging() const noexcept { return dragging; }
        void setPhase(float normalizedPhase);
        void setRounded(bool value) { rounded = value; }

        std::function<void(const std::vector<DuckingCurve::Point>&)> onPointsChanged;
        std::function<void()> onEditStarted, onEditFinished;

        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

    private:
        static constexpr int maxWaveformPoints = 1600;
        static constexpr int maxCurvePoints = 32;

        juce::Rectangle<float> graphBounds() const;
        float xToNorm(float x) const;
        float screenYToVisualNorm(float y) const;
        juce::Point<float> pointToScreen(const DuckingCurve::Point&) const;
        int findPoint(juce::Point<float>) const;
        void notifyPointsChanged();
        void normalizeWaveform(std::array<float, maxWaveformPoints>&);
        void drawWaveform(juce::Graphics&, const std::array<float, maxWaveformPoints>&,
                          juce::Rectangle<float>, juce::Colour, float) const;
        void drawCurve(juce::Graphics&) const;

        std::vector<DuckingCurve::Point> points;
        std::array<float, maxWaveformPoints> bassInWaveform{}; // compatibility only; never drawn
        std::array<float, maxWaveformPoints> bassOutWaveform{};
        std::array<float, maxWaveformPoints> kickWaveform{};

        std::vector<DuckingCurve::Point> storedPoints;
        float length = 0.5f;
        float attack = 50.0f, release = 250.0f, onsetAttack = 0.0f;
        double cycleSeconds = 0.5;
        void updateEffectivePoints();

        float amount = 1.0f;
        float phase = 0.0f;
        std::uint64_t waveformVersion = 0;
        int selectedPoint = -1;
        bool dragging = false;
        bool rounded = true;
    };

    void timerCallback() override;
    void updateWaveform();
    void updateMeters();
    void choosePresetFile(bool save);
    void setupKnob(juce::Slider&, juce::Label&, const juce::String&);
    static juce::Font font(float size, bool bold = false);
    static juce::String formatDb(float);
    static juce::String formatMs(float);
    static juce::String formatPercent(float);
    static juce::String formatLength(float);

    void drawMeter(juce::Graphics&, juce::Rectangle<float>, float levelDb,
                   const juce::String& title, const juce::String& value,
                   bool gainReduction) const;
    void drawReadout(juce::Graphics&, juce::Rectangle<float>, const juce::String& title,
                     const juce::String& main, const juce::String& sub,
                     juce::Colour accent) const;

    KickDuck1AudioProcessor& processor;
    CurveEditor curveEditor;
    SignalScope signalScope;
    juce::TextButton resetCurveButton {"RESET CURVE"};
    juce::TextButton legacyMixButton;
    std::array<CompactButton, 3> modeButtons;
    std::array<CompactButton, 5> divisionButtons;
    CompactButton bypassButton {"BYPASS"};
    CompactButton compareAButton {"A"}, compareBButton {"B"}, copyCompareButton {"A > B"};
    CompactButton undoButton {"UNDO"}, redoButton {"REDO"};
    CompactButton savePresetButton {"SAVE"}, loadPresetButton {"LOAD"};
    std::unique_ptr<juce::FileChooser> presetChooser;
    bool presetDialogOpen = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::array<std::unique_ptr<CurvePresetButton>, DuckingCurve::presetCount> presetButtons;
    juce::TooltipWindow tooltipWindow {this, 500};

    juce::Slider onsetAttackSlider;
    juce::Label onsetAttackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> onsetAttackAttachment;
    juce::Slider amountSlider, lengthSlider, attackSlider, releaseSlider;
    juce::Slider inputSlider, outputSlider;
    juce::Label amountLabel, lengthLabel, attackLabel, releaseLabel;
    juce::Label inputLabel, outputLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    float inputMeterDb = -60.0f, outputMeterDb = -60.0f, grMeterDb = 0.0f;
    juce::String inputPeakText, inputRmsText, inputLufsText;
    juce::String outputPeakText, outputRmsText, outputLufsText;
    juce::String kickPeakText, kickRmsText;
    juce::String grDbText, grPercentText, duckText, sidechainText;

    std::vector<DuckingCurve::Point> curveSnapshot;
    struct Layout
    {
        juce::Rectangle<int> graph, meters, presets, shape, gains, amount;
        juce::Rectangle<int> modes, divisions, compare, history, files, bypass;
    } layout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuck1AudioProcessorEditor)
};
