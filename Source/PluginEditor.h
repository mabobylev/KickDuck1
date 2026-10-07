#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>

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
        void setWaveforms(const float* bassIn, const float* bassOut, const float* kick);
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
        juce::Rectangle<float> graphBounds() const;
        float xToNorm(float x) const;
        float yToNorm(float y) const;
        juce::Point<float> normToPoint(float x, float y) const;
        int findPoint(juce::Point<float>) const;
        void notifyPointsChanged();
        void drawWaveform(juce::Graphics&, const std::array<float, maxWaveformPoints>&,
                          juce::Rectangle<float>, juce::Colour, float) const;
        void drawSmoothCurve(juce::Graphics&, float scale, juce::Colour,
                             float thickness, bool fill) const;

        std::vector<DuckingCurve::Point> points;
        std::array<float, maxWaveformPoints> bassInWaveform{}, bassOutWaveform{}, kickWaveform{};
        float amount = 0.75f;
        float phase = 0.0f;
        int selectedPoint = -1;
        bool dragging = false;
    };

    void timerCallback() override;
    void updateWaveform();
    void updateMeters();
    void setupKnob(juce::Slider&, juce::Label&, const juce::String&);
    void setupValueLabel(juce::Label&);
    static juce::String formatDb(float);
    static juce::String formatMs(float);
    static juce::String formatPercent(float);

    void drawMeter(juce::Graphics&, juce::Rectangle<float>, float levelDb,
                   const juce::String& title, const juce::String& value,
                   bool gainReduction) const;
    void drawReadout(juce::Graphics&, juce::Rectangle<float>, const juce::String& title,
                     const juce::String& main, const juce::String& sub,
                     juce::Colour accent) const;

    KickDuck1AudioProcessor& processor;
    CurveEditor curveEditor;

    juce::Slider amountSlider, lengthSlider, attackSlider, releaseSlider;
    juce::Slider mixSlider, inputSlider, outputSlider;
    juce::Label amountLabel, lengthLabel, attackLabel, releaseLabel;
    juce::Label mixLabel, inputLabel, outputLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    juce::Label inputPeakValue, inputRmsValue, inputLufsValue;
    juce::Label outputPeakValue, outputRmsValue, outputLufsValue;
    juce::Label kickPeakValue, kickRmsValue;
    juce::Label grDbValue, grPercentValue, duckValue, sidechainStatus;

    // Preallocated text cache: timer updates values, paint only reads them.
    juce::String inputPeakText, inputRmsText, inputLufsText;
    juce::String outputPeakText, outputRmsText, outputLufsText;
    juce::String kickPeakText, kickRmsText, grDbText, grPercentText, duckText, sidechainText;

    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuck1AudioProcessorEditor)
};
