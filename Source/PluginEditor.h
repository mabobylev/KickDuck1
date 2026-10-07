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
    ~KickDuck1AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class CurveDisplay : public juce::Component
    {
    public:
        CurveDisplay();

        void setPoints(const std::vector<DuckingCurve::Point>&);
        void setWaveforms(const std::vector<float>& bassIn,
                          const std::vector<float>& bassOut,
                          const std::vector<float>& kick);
        void setAmount(float normalizedAmount);
        void setPlayhead(float normalizedPhase);
        void setCurrentDuck(float normalizedDuck);
        void setSidechainConnected(bool);

        std::function<void(const std::vector<DuckingCurve::Point>&)> onPointsChanged;

        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;

    private:
        static constexpr int maxWaveformPoints = 1200;

        float xToNorm(float) const;
        float yToNorm(float) const;
        juce::Point<float> normToPoint(float x, float y) const;
        int findPoint(juce::Point<float>) const;
        float curveValueAt(float x) const;
        float displayedCurveValueAt(float x) const;
        void notifyPointsChanged();
        void copyDownsampled(const std::vector<float>& source,
                             std::array<float, maxWaveformPoints>& dest,
                             int& count);
        void drawWaveform(juce::Graphics&, const std::array<float, maxWaveformPoints>&,
                          int count, juce::Rectangle<float>, juce::Colour,
                          float alpha) const;

        std::vector<DuckingCurve::Point> points;
        std::array<float, maxWaveformPoints> bassInWaveform{};
        std::array<float, maxWaveformPoints> bassOutWaveform{};
        std::array<float, maxWaveformPoints> kickWaveform{};
        int bassInCount = 0, bassOutCount = 0, kickCount = 0;

        float amount = 0.75f;
        float playhead = 0.0f;
        float currentDuck = 0.0f;
        bool sidechainConnected = false;
        int selectedPoint = -1;
        int hoveredPoint = -1;
        bool dragging = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveDisplay)
    };

    class MeterStrip : public juce::Component
    {
    public:
        enum class Mode { Normal, GainReduction };
        explicit MeterStrip(Mode m) : mode(m) {}
        void setLevel(float v) { level = std::isfinite(v) ? v : -60.0f; repaint(); }
        void setText(const juce::String& s) { readout = s; repaint(); }
        void setTitle(const juce::String& s) { title = s; repaint(); }
        void paint(juce::Graphics&) override;
    private:
        Mode mode;
        float level = -60.0f;
        juce::String title, readout;
    };

    void timerCallback() override;
    void updateWaveform();
    void updateMeters();
    void setupKnob(juce::Slider&, juce::Label&, const juce::String&, bool major = false);
    static juce::String formatDb(float);
    static juce::String formatPercent(float);
    static juce::String formatLength(float);

    KickDuck1AudioProcessor& processor;
    CurveDisplay curveDisplay;

    juce::Slider amountSlider, lengthSlider, attackSlider, releaseSlider, mixSlider;
    juce::Slider inputSlider, outputSlider;
    juce::Label amountLabel, lengthLabel, attackLabel, releaseLabel, mixLabel;
    juce::Label inputLabel, outputLabel;
    juce::Label amountValue, lengthValue, attackValue, releaseValue, mixValue;
    juce::Label inputValue, outputValue;
    juce::Label sidechainStatus, grValue;

    MeterStrip inputMeter{MeterStrip::Mode::Normal};
    MeterStrip outputMeter{MeterStrip::Mode::Normal};
    MeterStrip grMeter{MeterStrip::Mode::GainReduction};

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    // Reused every timer tick. The CurveDisplay converts these to fixed arrays.
    std::vector<float> waveformBassInBuffer, waveformBassOutBuffer, waveformKickBuffer;
    float visualPhase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuck1AudioProcessorEditor)
};
