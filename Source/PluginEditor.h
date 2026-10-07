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
    class CurveEditor : public juce::Component
    {
    public:
        CurveEditor();

        void setPoints(const std::vector<DuckingCurve::Point>&);
        void setWaveforms(const float* bassIn,
                          const float* bassOut,
                          const float* kick);
        void setWaveformsFromProcessor(KickDuck1AudioProcessor&,
                                        int samplesToCopy);
        void setAmount(float normalizedAmount);
        void setPhase(float normalizedPhase);

        std::function<void(const std::vector<DuckingCurve::Point>&)> onPointsChanged;

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

    private:
        static constexpr int maxWaveformPoints = 2048;
        static constexpr int maxCurvePoints = 32;

        juce::Rectangle<float> graphBounds() const;

        float xToNorm(float x) const;
        float yToNorm(float y) const;

        juce::Point<float> normToPoint(float x, float y) const;

        int findPoint(juce::Point<float>) const;

        void notifyPointsChanged();

        void drawWaveform(
            juce::Graphics&,
            const std::array<float, maxWaveformPoints>&,
            juce::Rectangle<float>,
            juce::Colour,
            float) const;

        void drawDuckCurve(juce::Graphics&) const;

        float curveVisualValue(float y) const noexcept;

        std::vector<DuckingCurve::Point> points;

        std::array<float, maxWaveformPoints> bassInWaveform{};
        std::array<float, maxWaveformPoints> bassOutWaveform{};
        std::array<float, maxWaveformPoints> kickWaveform{};

        float amount = 0.75f;
        float phase = 0.0f;

        int selectedPoint = -1;
        bool dragging = false;
    };

    void timerCallback() override;

    void updateWaveform();
    void updateMeters();

    void setupKnob(
        juce::Slider&,
        juce::Label&,
        const juce::String&);

    static juce::Font makeFont(
        float size,
        bool bold = false);

    static void setLabelStyle(
        juce::Label&,
        float size,
        juce::Colour);

    static juce::String formatDb(float);
    static juce::String formatMs(float);
    static juce::String formatPercent(float);
    static juce::String formatLength(float);

    void drawMeterCard(
        juce::Graphics&,
        juce::Rectangle<float>,
        const juce::String&,
        float levelDb,
        const juce::String&,
        bool gainReduction) const;

    void drawSmallReadout(
        juce::Graphics&,
        juce::Rectangle<float>,
        const juce::String&,
        const juce::String&,
        const juce::String&,
        juce::Colour) const;

    void drawControlCard(
        juce::Graphics&,
        juce::Rectangle<float>,
        const juce::String&,
        const juce::String&,
        juce::Colour) const;

    KickDuck1AudioProcessor& processor;

    CurveEditor curveEditor;

    juce::Slider amountSlider;
    juce::Slider lengthSlider;
    juce::Slider attackSlider;
    juce::Slider releaseSlider;
    juce::Slider mixSlider;

    juce::Label amountLabel;
    juce::Label lengthLabel;
    juce::Label attackLabel;
    juce::Label releaseLabel;
    juce::Label mixLabel;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>
        amountAttachment;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>
        lengthAttachment;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>
        attackAttachment;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>
        releaseAttachment;

    std::unique_ptr<
        juce::AudioProcessorValueTreeState::SliderAttachment>
        mixAttachment;

    juce::String inputPeakText;
    juce::String outputPeakText;

    juce::String inputRmsText;
    juce::String outputRmsText;

    juce::String inputLufsText;
    juce::String outputLufsText;

    juce::String kickPeakText;
    juce::String kickRmsText;

    juce::String grDbText;
    juce::String grPercentText;

    juce::String duckText;
    juce::String sidechainText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        KickDuck1AudioProcessorEditor)
};
