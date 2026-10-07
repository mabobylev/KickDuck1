#pragma once

#include <JuceHeader.h>

class PeakMeter : public juce::Component
{
public:
    enum class Mode
    {
        Normal,
        GainReduction
    };

    explicit PeakMeter(Mode mode);

    void setLevel(float levelDb) noexcept;
    void setLabel(const juce::String& text);
    void setUnit(const juce::String& text);

    void paint(juce::Graphics&) override;
    void resized() override {}

private:
    Mode mode;
    float levelDb = -60.0f;
    juce::String label;
    juce::String unit = "dBFS";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakMeter)
};
