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
        void setWaveformsFromProcessor(KickDuck1AudioProcessor&, int samplesToCopy);
        void setAmount(float normalizedAmount);
        void setPhase(float normalizedPhase);

        std::function<void(const std::vector<DuckingCurve::Point>&)> onPointsChanged;

        void paint(juce::Graphics&) override;
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

        float amount = 0.75f;
        float phase = 0.0f;
        int selectedPoint = -1;
        bool dragging = false;
    };

    void timerCallback() override;
    void updateWaveform();
    void updateMeters();
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

    juce::Slider amountSlider, lengthSlider, attackSlider, releaseSlider, mixSlider;
    juce::Slider inputSlider, outputSlider;
    juce::Label amountLabel, lengthLabel, attackLabel, releaseLabel, mixLabel;
    juce::Label inputLabel, outputLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    juce::String inputPeakText, inputRmsText, inputLufsText;
    juce::String outputPeakText, outputRmsText, outputLufsText;
    juce::String kickPeakText, kickRmsText;
    juce::String grDbText, grPercentText, duckText, sidechainText;

    int waveformFrameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuck1AudioProcessorEditor)
};
