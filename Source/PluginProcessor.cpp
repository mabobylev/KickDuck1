#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float dbToLinear(float db) noexcept
    {
        return std::pow(10.0f, db / 20.0f);
    }

    float linearToDb(float value) noexcept
    {
        if (!std::isfinite(value) || value <= 0.000001f)
            return -60.0f;
        return std::max(-60.0f, 20.0f * std::log10(value));
    }

    float clamp01(float value) noexcept
    {
        return juce::jlimit(0.0f, 1.0f, value);
    }
}

KickDuck1AudioProcessor::KickDuck1AudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Bass", juce::AudioChannelSet::stereo(), true)
        .withInput("Kick Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      waveformHistory(524288)
{
    const auto points = duckingCurve.getPoints();
    audioCurveCacheCount = static_cast<int>(points.size());
    std::copy(points.begin(), points.end(), audioCurveCache.begin());
    publishCurve(points);
}

juce::AudioProcessorValueTreeState::ParameterLayout
KickDuck1AudioProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<Parameter>("input", "Input", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    parameters.push_back(std::make_unique<Parameter>("output", "Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    parameters.push_back(std::make_unique<Parameter>("amount", "Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 70.0f));
    parameters.push_back(std::make_unique<Parameter>("length", "Length", juce::NormalisableRange<float>(0.125f, 2.0f, 0.001f), 0.5f));
    parameters.push_back(std::make_unique<Parameter>("attack", "Tail", juce::NormalisableRange<float>(0.0f, 1000.0f, 0.01f), 50.0f));
    parameters.push_back(std::make_unique<Parameter>("release", "Recover", juce::NormalisableRange<float>(10.0f, 4000.0f, 0.1f), 250.0f));
    parameters.push_back(std::make_unique<Parameter>("mix", "Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    parameters.push_back(std::make_unique<Parameter>("onsetAttack", "Attack", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>("triggerMode", "Trigger Mode", juce::StringArray{"Auto", "Audio Trigger", "Host Sync"}, 0));
    parameters.push_back(std::make_unique<juce::AudioParameterChoice>("cycleDivision", "Cycle Division", juce::StringArray{"1/16", "1/8", "1/4", "1/2", "1 bar (4/4)"}, 2));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("automaticRouting", "Automatic Routing", true));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    return { parameters.begin(), parameters.end() };
}

void KickDuck1AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    phase = 1.0;
    bpmModeWasActive = false;
    duckCycleActive = false;
    kickEnvelope = 0.0f;
    kickCycleArmed = true;
    kickSlowEnvelope = 0.0f;
    samplesSinceTrigger = static_cast<int>(currentSampleRate.load() * 0.03);
    smoothedReduction = 0.0f;
    attackStartReduction = 0.0f;
    const auto initialiseSmoother = [this](auto& smoother, const char* id, float scale)
    {
        smoother.reset(currentSampleRate.load(), 0.02);
        smoother.setCurrentAndTargetValue(apvts.getRawParameterValue(id)->load() * scale);
    };
    initialiseSmoother(inputDbSmoother, "input", 1.0f);
    initialiseSmoother(outputDbSmoother, "output", 1.0f);
    initialiseSmoother(amountSmoother, "amount", 0.01f);
    initialiseSmoother(mixSmoother, "mix", 0.01f);
    initialiseSmoother(bypassSmoother, "bypass", 1.0f);
    waveformHistory.clear();
    triggeredWaveform.clear();

    inputPeak.store(-60.0f, std::memory_order_relaxed);
    outputPeak.store(-60.0f, std::memory_order_relaxed);
    kickActivity.store(-60.0f, std::memory_order_relaxed);
    bassActivity.store(-60.0f, std::memory_order_relaxed);
    inputRms.store(-60.0f, std::memory_order_relaxed);
    outputRms.store(-60.0f, std::memory_order_relaxed);
    kickRms.store(-60.0f, std::memory_order_relaxed);
    inputLufs.store(-60.0f, std::memory_order_relaxed);
    outputLufs.store(-60.0f, std::memory_order_relaxed);
    gainReductionDb.store(0.0f, std::memory_order_relaxed);
    meterGainReductionDb.store(0.0f, std::memory_order_relaxed);
    inputMeter.prepare(currentSampleRate.load());
    outputMeter.prepare(currentSampleRate.load());
    kickMeter.prepare(currentSampleRate.load());
    displayedReduction = 0.0;
    reductionMeterDecay = std::exp(-1.0 / (currentSampleRate.load() * 0.15));
    gainReduction.store(0.0f, std::memory_order_relaxed);
    currentDuck.store(0.0f, std::memory_order_relaxed);
    currentPhase.store(0.0f, std::memory_order_relaxed);
    sidechainConnected.store(false, std::memory_order_relaxed);
    // Curve publications stay valid across prepareToPlay; do not reset slot
    // ownership while the GUI or state thread may be publishing an edit.
}

void KickDuck1AudioProcessor::releaseResources() {}

bool KickDuck1AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();
    if ((mainInput != juce::AudioChannelSet::mono() && mainInput != juce::AudioChannelSet::stereo()) ||
        (mainOutput != juce::AudioChannelSet::mono() && mainOutput != juce::AudioChannelSet::stereo()) ||
        mainInput != mainOutput)
        return false;

    const auto sidechain = layouts.getChannelSet(true, 1);
    return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo();
}

void KickDuck1AudioProcessor::publishCurve(const std::vector<DuckingCurve::Point>& source)
{
    std::array<DuckingCurve::Point, maxCurvePoints> safe{};
    int count = 0;
    for (const auto& p : source)
    {
        if (count >= maxCurvePoints) break;
        if (std::isfinite(p.x) && std::isfinite(p.y))
            safe[count++] = { juce::jlimit(0.0f, 1.0f, p.x), juce::jlimit(0.0f, 1.0f, p.y) };
    }
    if (count == 0)
    {
        safe[0] = { 0.0f, 0.0f };
        safe[1] = { 1.0f, 0.0f };
        count = 2;
    }

    std::sort(safe.begin(), safe.begin() + count,
              [](const auto& a, const auto& b) { return a.x < b.x; });

    const int active = activeCurveBuffer.load(std::memory_order_acquire);
    int next = -1;
    for (int candidate = 0; candidate < static_cast<int>(runtimeCurves.size()); ++candidate)
    {
        if (candidate == active)
            continue;
        int expected = 0;
        if (curveBufferState[candidate].compare_exchange_strong(
                expected, 2, std::memory_order_acquire, std::memory_order_relaxed))
        {
            next = candidate;
            break;
        }
    }
    // With serialized writers and one audio reader, one spare slot is free.
    jassert(next >= 0);
    if (next < 0)
        return;

    for (int i = 0; i < count; ++i)
        runtimeCurves[next][i] = safe[i];

    runtimeCurveCounts[next].store(count, std::memory_order_release);
    activeCurveBuffer.store(next, std::memory_order_release);
    curveBufferState[next].store(0, std::memory_order_release);
}

void KickDuck1AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    // Cache parameter pointers once per callback. Loading APVTS atomics is safe;
    // no parameter object is created or destroyed while the processor runs.
    const float inputDb = apvts.getRawParameterValue("input")->load(std::memory_order_relaxed);
    const float outputDb = apvts.getRawParameterValue("output")->load(std::memory_order_relaxed);
    const float amount = apvts.getRawParameterValue("amount")->load(std::memory_order_relaxed) * 0.01f;
    const float length = apvts.getRawParameterValue("length")->load(std::memory_order_relaxed);
    const float onsetMs = apvts.getRawParameterValue("onsetAttack")->load(std::memory_order_relaxed);
    const float attackMs = apvts.getRawParameterValue("attack")->load(std::memory_order_relaxed);
    const float releaseMs = apvts.getRawParameterValue("release")->load(std::memory_order_relaxed);
    const float mix = apvts.getRawParameterValue("mix")->load(std::memory_order_relaxed) * 0.01f;

    inputDbSmoother.setTargetValue(inputDb);
    outputDbSmoother.setTargetValue(outputDb);
    amountSmoother.setTargetValue(amount);
    mixSmoother.setTargetValue(mix);
    bypassSmoother.setTargetValue(apvts.getRawParameterValue("bypass")->load(std::memory_order_relaxed));

    auto bassBuffer = getBusBuffer(buffer, true, 0);
    auto kickBuffer = getBusBuffer(buffer, true, 1);
    const int bassChannels = bassBuffer.getNumChannels();
    const int kickChannels = kickBuffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const bool hasKick = kickChannels > 0;
    sidechainConnected.store(hasKick, std::memory_order_relaxed);
    if (bassChannels <= 0 || numSamples <= 0)
        return;

    // Reserve the currently active curve before copying it. GUI never writes a
    // buffer while audio has it reserved.
    int curveBuffer = activeCurveBuffer.load(std::memory_order_acquire);
    int freeState = 0;
    bool curveReserved = curveBufferState[curveBuffer].compare_exchange_strong(
        freeState, 1, std::memory_order_acquire, std::memory_order_relaxed);

    if (!curveReserved)
    {
        curveBuffer = activeCurveBuffer.load(std::memory_order_acquire);
        freeState = 0;
        curveReserved = curveBufferState[curveBuffer].compare_exchange_strong(
            freeState, 1, std::memory_order_acquire, std::memory_order_relaxed);
    }

    std::array<DuckingCurve::Point, maxCurvePoints> localCurve = audioCurveCache;
    int curveCount = audioCurveCacheCount;
    if (curveReserved)
    {
        curveCount = juce::jlimit(0, maxCurvePoints,
            runtimeCurveCounts[curveBuffer].load(std::memory_order_acquire));
        for (int i = 0; i < curveCount; ++i)
            localCurve[i] = runtimeCurves[curveBuffer][i];
        audioCurveCache = localCurve;
        audioCurveCacheCount = curveCount;
        curveBufferState[curveBuffer].store(0, std::memory_order_release);
    }

    const bool automaticRouting = apvts.getRawParameterValue("automaticRouting")->load() > 0.5f;
    const int triggerMode = automaticRouting ? (hasKick ? 1 : 2)
        : static_cast<int>(apvts.getRawParameterValue("triggerMode")->load());
    const int division = juce::jlimit(0, 4, static_cast<int>(apvts.getRawParameterValue("cycleDivision")->load()));
    constexpr double divisions[] {0.25, 0.5, 1.0, 2.0, 4.0};
    const double cycleBeats = divisions[division];
    const bool bpmMode = triggerMode == 2 || (triggerMode == 0 && !hasKick);
    bool transportPlaying = false;
    double bpm = 120.0;
    bool hasBeatPosition = false;
    double beatPhase = 0.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
        {
            transportPlaying = position->getIsPlaying();
            if (position->getBpm().hasValue()) bpm = *position->getBpm();
            if (position->getIsPlaying() && position->getPpqPosition().hasValue())
            {
                const double ppq = *position->getPpqPosition();
                if (std::isfinite(ppq))
                {
                    beatPhase = ppq / cycleBeats - std::floor(ppq / cycleBeats);
                    hasBeatPosition = true;
                }
            }
        }
    const bool resyncBeat = bpmMode && hasBeatPosition
        && (phase >= 1.0 || std::abs(beatPhase - phase) > 0.1);
    const bool enteringBpmMode = bpmMode && !bpmModeWasActive;
    const bool leavingBpmMode = !bpmMode && bpmModeWasActive;
    if (bpmMode)
    {
        if (hasBeatPosition) phase = beatPhase;
        else if (enteringBpmMode) phase = 0.0;
    }
    else if (leavingBpmMode)
    {
        duckCycleActive = false;
        kickCycleArmed = true;
    }
    bpmModeWasActive = bpmMode;
    const bool clockRunning = bpmMode && (triggerMode != 2 || transportPlaying);
    if (bpmMode && !clockRunning) duckCycleActive = false;
    if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;

    // A trigger starts one selected musical cycle. The curve remains active after the transient
    // decays, so its recovery and final duck can both be heard.
    const double windowSeconds = 60.0 / bpm * cycleBeats;
    cycleSeconds.store(windowSeconds, std::memory_order_relaxed);
    const double sr = currentSampleRate.load(std::memory_order_relaxed);
    const double phaseIncrement = 1.0 / (std::max(0.001, windowSeconds) * sr);
    // Detector release is separate from the curve's ATTACK/RELEASE handles.
    const float detectorDecay = std::exp(-1.0f / static_cast<float>(sr * 0.003));
    const float slowDetectorDecay = std::exp(-1.0f / static_cast<float>(sr * 0.08));
    const int retriggerHoldSamples = std::max(1, static_cast<int>(sr * 0.03));
    const float reductionStep = 1.0f / static_cast<float>(sr * 0.002);

    const bool rounded = isCurveRounded();
    auto effectiveCurve = localCurve;
    DuckingCurve::applyControls(effectiveCurve.data(), curveCount,
                               length, attackMs, releaseMs, windowSeconds, onsetMs);

    float blockGainReduction = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float kickSample = 0.0f;
        if (hasKick)
            kickSample = kickChannels == 1
                ? kickBuffer.getSample(0, sample)
                : 0.5f * (kickBuffer.getSample(0, sample) + kickBuffer.getSample(1, sample));

        const float kickAbs = kickChannels == 2
            ? std::max(std::abs(kickBuffer.getSample(0, sample)),
                       std::abs(kickBuffer.getSample(1, sample)))
            : std::abs(kickSample);
        const double kickPower = kickChannels == 2
            ? (static_cast<double>(kickBuffer.getSample(0, sample)) * kickBuffer.getSample(0, sample)
               + static_cast<double>(kickBuffer.getSample(1, sample)) * kickBuffer.getSample(1, sample)) * 0.5
            : static_cast<double>(kickSample) * kickSample;
        kickMeter.push(kickAbs, kickPower);
        constexpr float triggerOn = 0.02f;
        constexpr float triggerOff = 0.008f;
        // Compare with the previous slow peak before the new transient updates it.
        // This detects a fresh hit over a tail, without treating zero crossings
        // as silence. The hold prevents multiple triggers within one transient.
        const bool freshOnset = kickAbs >= triggerOn && kickAbs > kickSlowEnvelope * 1.6f;
        kickEnvelope = std::max(kickAbs, kickEnvelope * detectorDecay);
        kickSlowEnvelope = std::max(kickAbs, kickSlowEnvelope * slowDetectorDecay);
        // A decaying sinusoidal tail can briefly dip below the fast threshold
        // between half-waves. Require the slow peak to leave the trigger range
        // too, so those dips cannot manufacture new hits in the waveform.
        if (kickEnvelope <= triggerOff && kickSlowEnvelope <= triggerOn)
            kickCycleArmed = true;
        const bool kickTriggered = !bpmMode && hasKick && samplesSinceTrigger >= retriggerHoldSamples
            && kickAbs >= triggerOn && (kickCycleArmed || freshOnset);
        const bool beatTriggered = clockRunning && ((sample == 0 && (enteringBpmMode || resyncBeat))
            || !duckCycleActive || phase >= 1.0 - phaseIncrement * 0.001);
        if (clockRunning && phase >= 1.0 - phaseIncrement * 0.001) phase = std::max(0.0, phase - 1.0);
        const bool triggered = kickTriggered || beatTriggered;
        if (triggered)
        {
            attackStartReduction = smoothedReduction;
            if (kickTriggered) phase = 0.0;
            duckCycleActive = true;
            kickCycleArmed = false;
            samplesSinceTrigger = 0;
            const int frameSamples = static_cast<int>(juce::jlimit(1.0, 2147483647.0,
                                                                  std::round(windowSeconds * sr)));
            triggeredWaveform.trigger(frameSamples, windowSeconds);
        }
        if (samplesSinceTrigger < retriggerHoldSamples)
            ++samplesSinceTrigger;

        const float active = 1.0f - bypassSmoother.getNextValue();
        const float inputGain = dbToLinear(inputDbSmoother.getNextValue());
        const float outputGain = dbToLinear(outputDbSmoother.getNextValue());
        const float sampleAmount = amountSmoother.getNextValue();
        const float sampleMix = mixSmoother.getNextValue();
        // Bound discontinuities from curve edits, geometry automation and the
        // end of the cycle. Attack=0 retains immediate first-sample ducking.
        const float curve = DuckingCurve::sample(effectiveCurve.data(), curveCount, static_cast<float>(phase), rounded);
        float targetReduction = duckCycleActive ? clamp01(sampleAmount * curve) : 0.0f;
        float step = reductionStep;
        if (duckCycleActive && effectiveCurve[0].x > 0.0f && phase < effectiveCurve[0].x)
        {
            const float t = static_cast<float>(phase) / effectiveCurve[0].x;
            const float progress = rounded ? t * t * (3.0f - 2.0f * t) : t;
            // Retrigger from the current gain, avoiding a jump back to dry.
            targetReduction += attackStartReduction * (1.0f - progress);
            step = std::max(step, 1.5f / static_cast<float>(sr * windowSeconds * effectiveCurve[0].x));
        }
        if (triggered && effectiveCurve[0].x == 0.0f)
            smoothedReduction = targetReduction;
        else
            smoothedReduction += juce::jlimit(-step, step, targetReduction - smoothedReduction);
        const float reduction = smoothedReduction;
        const float duck = 1.0f - reduction;
        blockGainReduction = std::max(blockGainReduction, reduction * sampleMix * active);

        double inputPower = 0.0, outputPower = 0.0;
        float displayBassIn = 0.0f, displayBassOut = 0.0f;

        if (bassChannels == 1)
        {
            const float raw = bassBuffer.getSample(0, sample);
            const float dry = raw * inputGain;
            const float processed = dry * ((1.0f - sampleMix) + duck * sampleMix) * outputGain;
            const float output = active == 1.0f ? processed : raw + (processed - raw) * active;
            const float meteredInput = active == 1.0f ? dry : raw + (dry - raw) * active;
            bassBuffer.setSample(0, sample, output);
            inputPower = static_cast<double>(meteredInput) * meteredInput;
            outputPower = static_cast<double>(output) * output;
            displayBassIn = meteredInput;
            displayBassOut = output;
        }
        else
        {
            const float rawL = bassBuffer.getSample(0, sample), rawR = bassBuffer.getSample(1, sample);
            const float dryL = rawL * inputGain, dryR = rawR * inputGain;
            const float wetGain = (1.0f - sampleMix) + duck * sampleMix;
            const float processedL = dryL * wetGain * outputGain, processedR = dryR * wetGain * outputGain;
            const float outputL = active == 1.0f ? processedL : rawL + (processedL - rawL) * active;
            const float outputR = active == 1.0f ? processedR : rawR + (processedR - rawR) * active;
            const float inputL = active == 1.0f ? dryL : rawL + (dryL - rawL) * active;
            const float inputR = active == 1.0f ? dryR : rawR + (dryR - rawR) * active;
            bassBuffer.setSample(0, sample, outputL);
            bassBuffer.setSample(1, sample, outputR);
            inputPower = (static_cast<double>(inputL) * inputL + static_cast<double>(inputR) * inputR) * 0.5;
            outputPower = (static_cast<double>(outputL) * outputL + static_cast<double>(outputR) * outputR) * 0.5;
            const bool useLeft = std::abs(dryL) >= std::abs(dryR);
            displayBassIn = useLeft ? inputL : inputR;
            displayBassOut = useLeft ? outputL : outputR;
        }

        inputMeter.push(std::abs(displayBassIn), inputPower);
        outputMeter.push(std::abs(displayBassOut), outputPower);
        const double actualReduction = reduction * sampleMix * active;
        displayedReduction = actualReduction >= displayedReduction ? actualReduction
            : actualReduction + reductionMeterDecay * (displayedReduction - actualReduction);

        // Fixed-capacity atomic DSP history. No vector, mutex or allocation.
        // Show the strongest signed AUX channel, preserving antiphase kicks.
        const float displayKick = kickChannels == 2
            && std::abs(kickBuffer.getSample(1, sample)) > std::abs(kickBuffer.getSample(0, sample))
                ? kickBuffer.getSample(1, sample) : (hasKick ? kickBuffer.getSample(0, sample) : 0.0f);
        waveformHistory.push(displayBassIn, displayBassOut, displayKick, reduction * sampleMix * active);
        triggeredWaveform.push(displayBassIn, displayBassOut, displayKick,
            static_cast<int>(juce::jlimit(1.0, 2147483647.0, std::round(windowSeconds * sr))), windowSeconds);
        currentDuck.store(reduction * active, std::memory_order_relaxed);
        currentPhase.store(static_cast<float>(phase), std::memory_order_relaxed);
        if (duckCycleActive)
        {
            phase = !bpmMode ? std::min(1.0, phase + phaseIncrement) : phase + phaseIncrement;
            if (!bpmMode && phase >= 1.0) duckCycleActive = false;
        }
    }

    const float inputDbNow = linearToDb(inputMeter.getRms());
    const float outputDbNow = linearToDb(outputMeter.getRms());
    inputPeak.store(linearToDb(inputMeter.getPeak()), std::memory_order_relaxed);
    outputPeak.store(linearToDb(outputMeter.getPeak()), std::memory_order_relaxed);
    kickActivity.store(linearToDb(kickMeter.getPeak()), std::memory_order_relaxed);
    bassActivity.store(linearToDb(inputMeter.getPeak()), std::memory_order_relaxed);
    inputRms.store(inputDbNow, std::memory_order_relaxed);
    outputRms.store(outputDbNow, std::memory_order_relaxed);
    kickRms.store(linearToDb(kickMeter.getRms()), std::memory_order_relaxed);
    // Legacy API aliases only; these values are RMS, not loudness measurements.
    inputLufs.store(juce::jlimit(-60.0f, 0.0f, inputDbNow), std::memory_order_relaxed);
    outputLufs.store(juce::jlimit(-60.0f, 0.0f, outputDbNow), std::memory_order_relaxed);
    meterGainReductionDb.store(linearToDb(static_cast<float>(1.0 - displayedReduction)), std::memory_order_relaxed);

    const float remainingGain = juce::jmax(0.000001f, 1.0f - blockGainReduction);
    const float grDb = juce::jlimit(-60.0f, 0.0f, 20.0f * std::log10(remainingGain));
    gainReductionDb.store(grDb, std::memory_order_relaxed);
    gainReduction.store(blockGainReduction * 100.0f, std::memory_order_relaxed);
}

float KickDuck1AudioProcessor::getInputPeak() const noexcept { return inputPeak.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getOutputPeak() const noexcept { return outputPeak.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getKickActivity() const noexcept { return kickActivity.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getBassActivity() const noexcept { return bassActivity.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getInputRms() const noexcept { return inputRms.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getOutputRms() const noexcept { return outputRms.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getKickRms() const noexcept { return kickRms.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getInputLufs() const noexcept { return inputLufs.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getOutputLufs() const noexcept { return outputLufs.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getGainReduction() const noexcept { return getGainReductionPercent(); }
float KickDuck1AudioProcessor::getGainReductionDb() const noexcept { return gainReductionDb.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getMeterGainReductionDb() const noexcept { return meterGainReductionDb.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getGainReductionPercent() const noexcept { return gainReduction.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getCurrentDuck() const noexcept { return currentDuck.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getCurrentPhase() const noexcept { return currentPhase.load(std::memory_order_relaxed); }
bool KickDuck1AudioProcessor::isSidechainConnected() const noexcept { return sidechainConnected.load(std::memory_order_relaxed); }

void KickDuck1AudioProcessor::copyWaveformSnapshot(float* bassIn, float* bassOut, float* kick,
                                                   int points, int samplesToCopy, bool includeKick, float* reduction) const noexcept
{
    waveformHistory.copyLatest(bassIn, bassOut, kick, points, samplesToCopy, includeKick, reduction);
}

bool KickDuck1AudioProcessor::copyTriggeredWaveform(float* bassIn, float* bassOut, float* kick,
                                                    std::uint64_t& version, double& seconds) const noexcept
{
    return triggeredWaveform.copy(bassIn, bassOut, kick, version, seconds);
}

std::vector<DuckingCurve::Point> KickDuck1AudioProcessor::getCurvePoints() const
{
    return duckingCurve.getPoints();
}

void KickDuck1AudioProcessor::setCurvePoints(const std::vector<DuckingCurve::Point>& points)
{
    std::lock_guard<std::mutex> lock(curveUpdateMutex);
    duckingCurve.setPoints(points);
    // Publish the exact normalized model, including its boundary points.
    publishCurve(duckingCurve.getPoints());
}

namespace
{
void setCurveParameter(juce::AudioProcessorValueTreeState& state, const char* id, float value)
{
    auto* p = state.getParameter(id);
    const float normalized = p->convertTo0to1(value);
    if (std::abs(p->getValue() - normalized) < 0.000001f) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(normalized);
    p->endChangeGesture();
}
}

void KickDuck1AudioProcessor::editCurvePoints(const std::vector<DuckingCurve::Point>& effectivePoints)
{
    // Input is the displayed reduction, including AMOUNT. Preserve all other
    // visible depths when the deepest point changes the global Amount value.
    auto normalized = effectivePoints;
    float depth = 0.0f;
    for (const auto& p : normalized) depth = std::max(depth, p.y);
    depth = clamp01(depth);
    for (auto& p : normalized) p.y = depth > 0.000001f ? p.y / depth : 0.0f;
    setCurveParameter(apvts, "amount", depth * 100.0f);
    if (normalized.size() >= 3)
        setCurveParameter(apvts, "length", normalized[1].x * 2.0f);
    if (normalized.size() >= 5)
    {
        const float ms = static_cast<float>(getCycleSeconds() * 1000.0);
        setCurveParameter(apvts, "release", normalized[2].x * ms);
        setCurveParameter(apvts, "attack", (1.0f - normalized[normalized.size() - 2].x) * ms);
    }
    if (!normalized.empty())
    {
        setCurveParameter(apvts, "onsetAttack", normalized[0].x * static_cast<float>(getCycleSeconds() * 1000.0));
        normalized[0].x = 0.0f;
    }
    setCurvePoints(normalized);
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

void KickDuck1AudioProcessor::resetCurve()
{
    setCurveParameter(apvts, "amount", 70.0f);
    applyCurvePreset(DuckingCurve::Preset::classic);
}

void KickDuck1AudioProcessor::setCurveRounded(bool rounded)
{
    curveRounded.store(rounded, std::memory_order_relaxed);
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

void KickDuck1AudioProcessor::applyCurvePreset(DuckingCurve::Preset preset)
{
    setCurveRounded(true);
    setCurveParameter(apvts, "onsetAttack", 0.0f);
    const auto points = DuckingCurve::presetPoints(preset);
    const float ms = static_cast<float>(getCycleSeconds() * 1000.0);
    setCurveParameter(apvts, "length", points[1].x * 2.0f);
    setCurveParameter(apvts, "release", points[2].x * ms);
    setCurveParameter(apvts, "attack", (1.0f - points[points.size() - 2].x) * ms);
    setCurvePoints(points);
    updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
}

bool KickDuck1AudioProcessor::tryCopyCurvePoints(
    std::vector<DuckingCurve::Point>& points) const
{
    return duckingCurve.tryCopyPoints(points);
}

void KickDuck1AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("stateVersion", 8, nullptr);
    state.setProperty("curveRounded", isCurveRounded(), nullptr);
    for (auto oldCurve = state.getChildWithName("DuckingCurve"); oldCurve.isValid();
         oldCurve = state.getChildWithName("DuckingCurve"))
        state.removeChild(oldCurve, nullptr);

    juce::ValueTree curveState("DuckingCurve");
    for (const auto& point : duckingCurve.getPoints())
    {
        juce::ValueTree child("Point");
        child.setProperty("x", point.x, nullptr);
        child.setProperty("y", point.y, nullptr);
        curveState.addChild(child, -1, nullptr);
    }
    state.addChild(curveState, -1, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void KickDuck1AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto xml = std::unique_ptr<juce::XmlElement>(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    const auto curveState = state.getChildWithName("DuckingCurve");
    std::vector<DuckingCurve::Point> points;
    points.reserve(maxCurvePoints);
    for (int i = 0; i < curveState.getNumChildren() && points.size() < maxCurvePoints; ++i)
    {
        const auto child = curveState.getChild(i);
        if (child.hasType("Point") && child.hasProperty("x") && child.hasProperty("y"))
            points.push_back({static_cast<float>(child.getProperty("x")),
                              static_cast<float>(child.getProperty("y"))});
    }
    // Legacy states contain only APVTS parameters; their curve was never saved.
    if (points.empty())
        points = DuckingCurve::defaultPoints();
    const int version = static_cast<int>(state.getProperty("stateVersion", 0));
    // States before v3 used straight segments. Keep their sound until the user
    // explicitly selects ROUND, a preset or RESET CURVE.
    curveRounded.store(static_cast<bool>(state.getProperty("curveRounded", false)),
                       std::memory_order_relaxed);
    bool hasOnsetAttack = false;
    for (const auto& child : state)
        hasOnsetAttack = hasOnsetAttack || child.getProperty("id").toString() == "onsetAttack";
    if (!hasOnsetAttack)
    {
        juce::ValueTree parameterState("PARAM");
        parameterState.setProperty("id", "onsetAttack", nullptr);
        parameterState.setProperty("value", 0.0f, nullptr);
        state.addChild(parameterState, -1, nullptr);
    }
    for (const auto* id : {"triggerMode", "cycleDivision", "automaticRouting", "bypass"})
    {
        bool found = false;
        for (const auto& child : state) found = found || child.getProperty("id").toString() == id;
        if (!found)
        {
            juce::ValueTree parameterState("PARAM");
            parameterState.setProperty("id", id, nullptr);
            parameterState.setProperty("value", juce::String(id) == "cycleDivision" ? 2 : 0, nullptr);
            state.addChild(parameterState, -1, nullptr);
        }
    }
    // v7 automatic routing ignored Cycle Division. Preserve its effective
    // quarter-note cycle before making divisions available again.
    if (version == 7)
    {
        bool automatic = false;
        for (const auto& child : state)
            if (child.getProperty("id").toString() == "automaticRouting")
                automatic = static_cast<float>(child.getProperty("value")) > 0.5f;
        if (automatic)
            for (auto child : state)
                if (child.getProperty("id").toString() == "cycleDivision")
                    child.setProperty("value", 2.0f, nullptr);
    }
    apvts.replaceState(state);
    if (version < 2 && points.size() >= 3)
    {
        // Preserve custom shapes from old projects. The old factory shape gets
        // the corrected five-point default, without altering Input/Output/Mix.
        const auto oldDefault = std::vector<DuckingCurve::Point>{{0.0f, 1.0f}, {0.18f, 1.0f},
            {0.72f, 0.58f}, {0.93f, 0.12f}, {1.0f, 0.0f}};
        bool factory = points.size() == oldDefault.size();
        for (size_t i = 0; factory && i < points.size(); ++i)
            factory = std::abs(points[i].x - oldDefault[i].x) < 0.00001f
                   && std::abs(points[i].y - oldDefault[i].y) < 0.00001f;
        if (factory || !curveState.isValid())
        {
            resetCurve();
            setCurveParameter(apvts, "amount", 100.0f);
            curveRounded.store(false, std::memory_order_relaxed);
            return;
        }
        setCurveParameter(apvts, "length", points[1].x * 2.0f);
        if (points.size() >= 5)
        {
            const float ms = static_cast<float>(getCycleSeconds() * 1000.0);
            setCurveParameter(apvts, "release", points[2].x * ms);
            setCurveParameter(apvts, "attack", (1.0f - points[points.size() - 2].x) * ms);
        }
    }
    if (version >= 2 && version < 5)
    {
        // Bake the old clamped positions before adopting neighbour-aware limits.
        DuckingCurve::applyLegacyControls(points.data(), static_cast<int>(points.size()),
            apvts.getRawParameterValue("length")->load(),
            apvts.getRawParameterValue("attack")->load(),
            apvts.getRawParameterValue("release")->load(), getCycleSeconds(),
            apvts.getRawParameterValue("onsetAttack")->load());
        if (points.size() >= 5)
        {
            const float ms = static_cast<float>(getCycleSeconds() * 1000.0);
            setCurveParameter(apvts, "length", points[1].x * 2.0f);
            setCurveParameter(apvts, "release", points[2].x * ms);
            setCurveParameter(apvts, "attack", (1.0f - points[points.size() - 2].x) * ms);
        }
        if (!points.empty()) points[0].x = 0.0f;
    }
    setCurvePoints(points);
}

juce::AudioProcessorEditor* KickDuck1AudioProcessor::createEditor()
{
    return new KickDuck1AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickDuck1AudioProcessor();
}
