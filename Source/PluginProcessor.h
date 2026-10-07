#pragma once

#include <JuceHeader.h>
#include "DuckingCurve.h"
#include "WaveformHistory.h"
#include <array>
#include <atomic>
#include <vector>

class KickDuck1AudioProcessor : public juce::AudioProcessor
{
public:
    KickDuck1AudioProcessor();
    ~KickDuck1AudioProcessor() override = default;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    float getInputPeak() const noexcept;
    float getOutputPeak() const noexcept;
    float getKickActivity() const noexcept;
    float getBassActivity() const noexcept;
    float getInputRms() const noexcept;
    float getOutputRms() const noexcept;
    float getKickRms() const noexcept;
    float getInputLufs() const noexcept;
    float getOutputLufs() const noexcept;
    float getGainReduction() const noexcept;
    float getGainReductionDb() const noexcept;
    float getGainReductionPercent() const noexcept;
    float getCurrentDuck() const noexcept;
    double getSampleRate() const noexcept { return currentSampleRate; }
    bool isSidechainConnected() const noexcept;

    // GUI snapshot API: writes into caller-owned fixed arrays only.
    // No std::vector mutation occurs here.
    void copyWaveformSnapshot(float* bassIn,
                              float* bassOut,
                              float* kick,
                              int points,
                              int samplesToCopy) const noexcept;

    std::vector<DuckingCurve::Point> getCurvePoints() const;
    void setCurvePoints(const std::vector<DuckingCurve::Point>& points);

private:
    static constexpr int maxCurvePoints = 32;

    std::atomic<float> inputPeak { -60.0f };
    std::atomic<float> outputPeak { -60.0f };
    std::atomic<float> kickActivity { -60.0f };
    std::atomic<float> bassActivity { -60.0f };
    std::atomic<float> inputRms { -60.0f };
    std::atomic<float> outputRms { -60.0f };
    std::atomic<float> kickRms { -60.0f };
    std::atomic<float> inputLufs { -60.0f };
    std::atomic<float> outputLufs { -60.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> gainReduction { 0.0f };
    std::atomic<float> currentDuck { 0.0f };
    std::atomic<bool> sidechainConnected { false };

    double currentSampleRate = 44100.0;
    float phase = 0.0f;
    float kickEnvelope = 0.0f;

    // Two preallocated curve buffers. The GUI writes only the inactive one.
    // Audio reserves the active one for a block, copies it to a local fixed
    // array, then releases it. This makes the double-buffer exchange safe.
    std::array<std::array<DuckingCurve::Point, maxCurvePoints>, 2> runtimeCurves {};
    std::array<std::atomic<int>, 2> runtimeCurveCounts { 0, 0 };
    std::atomic<int> activeCurveBuffer { 0 };
    // 0 = free, 1 = audio reading, 2 = GUI writing.
    std::array<std::atomic<int>, 2> curveBufferState { 0, 0 };
    std::array<DuckingCurve::Point, maxCurvePoints> audioCurveCache {};
    int audioCurveCacheCount = 2;

    DuckingCurve duckingCurve; // GUI/state-thread only; never touched by audio.
    WaveformHistory waveformHistory;

    float runtimeCurveValueAt(const std::array<DuckingCurve::Point, maxCurvePoints>& curve,
                              int count, float x) const noexcept;
    void publishCurve(const std::vector<DuckingCurve::Point>& points);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuck1AudioProcessor)
};
