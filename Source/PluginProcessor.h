#pragma once

#include <JuceHeader.h>
#include "DuckingCurve.h"
#include "WaveformHistory.h"
#include "TriggeredWaveform.h"
#include "MeterBallistics.h"
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
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("bypass"); }
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
    float getMeterGainReductionDb() const noexcept;
    float getGainReductionPercent() const noexcept;
    float getCurrentDuck() const noexcept;
    float getCurrentPhase() const noexcept;
    double getSampleRate() const noexcept { return currentSampleRate.load(std::memory_order_relaxed); }
    double getCycleSeconds() const noexcept { return cycleSeconds.load(std::memory_order_relaxed); }
    bool isSidechainConnected() const noexcept;

    // GUI snapshot API: writes into caller-owned fixed arrays only.
    // No std::vector mutation occurs here.
    void copyWaveformSnapshot(float* bassIn,
                              float* bassOut,
                              float* kick,
                              int points,
                              int samplesToCopy, bool includeKick = true, float* reduction = nullptr) const noexcept;

    bool copyTriggeredWaveform(float* bassIn, float* bassOut, float* kick,
                               std::uint64_t& version, double& seconds) const noexcept;

    std::vector<DuckingCurve::Point> getCurvePoints() const;
    void setCurvePoints(const std::vector<DuckingCurve::Point>& points);
    void editCurvePoints(const std::vector<DuckingCurve::Point>& effectivePoints);
    void resetCurve();
    bool isCurveRounded() const noexcept { return curveRounded.load(std::memory_order_relaxed); }
    void setCurveRounded(bool rounded);
    void applyCurvePreset(DuckingCurve::Preset preset);
    bool tryCopyCurvePoints(std::vector<DuckingCurve::Point>& points) const;

private:
    static_assert(std::atomic<float>::is_always_lock_free, "DSP meters must be lock-free");
    static_assert(std::atomic<double>::is_always_lock_free, "Timing snapshots must be lock-free");
    static_assert(std::atomic<int>::is_always_lock_free, "Curve ownership must be lock-free");
    static_assert(std::atomic<bool>::is_always_lock_free, "DSP flags must be lock-free");
    static constexpr int maxCurvePoints = DuckingCurve::maxPoints;
    // Serializes GUI and host state writers; never taken by processBlock.
    std::mutex curveUpdateMutex;

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
    std::atomic<float> meterGainReductionDb { 0.0f };
    MeterBallistics inputMeter, outputMeter, kickMeter;
    double displayedReduction = 0.0, reductionMeterDecay = 0.0;
    std::atomic<float> gainReduction { 0.0f };
    std::atomic<float> currentDuck { 0.0f };
    std::atomic<float> currentPhase { 0.0f };
    std::atomic<bool> sidechainConnected { false };

    std::atomic<double> currentSampleRate {44100.0};
    std::atomic<double> cycleSeconds {0.5};
    double phase = 1.0;
    bool duckCycleActive = false;
    bool bpmModeWasActive = false;
    float kickEnvelope = 0.0f;
    bool kickCycleArmed = true;
    float kickSlowEnvelope = 0.0f;
    int samplesSinceTrigger = 0;
    float attackStartReduction = 0.0f;
    float smoothedReduction = 0.0f;
    juce::SmoothedValue<float> inputDbSmoother;
    juce::SmoothedValue<float> outputDbSmoother;
    juce::SmoothedValue<float> amountSmoother;
    juce::SmoothedValue<float> mixSmoother;
    juce::SmoothedValue<float> bypassSmoother;

    // Three preallocated curve buffers. Writers use an inactive, unreserved slot.
    // Audio reserves the active one for a block, copies it to a local fixed
    // array, then releases it. The spare slot prevents dropping the latest
    // edit while audio reads.
    std::array<std::array<DuckingCurve::Point, maxCurvePoints>, 3> runtimeCurves {};
    std::array<std::atomic<int>, 3> runtimeCurveCounts { 0, 0, 0 };
    std::atomic<int> activeCurveBuffer { 0 };
    // 0 = free, 1 = audio reading, 2 = control/state thread writing.
    std::array<std::atomic<int>, 3> curveBufferState { 0, 0, 0 };
    std::array<DuckingCurve::Point, maxCurvePoints> audioCurveCache {};
    int audioCurveCacheCount = 2;

    std::atomic<bool> curveRounded {true};
    DuckingCurve duckingCurve; // GUI/state-thread only; never touched by audio.
    WaveformHistory waveformHistory;
    TriggeredWaveform triggeredWaveform;

    void publishCurve(const std::vector<DuckingCurve::Point>& points);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KickDuck1AudioProcessor)
};
