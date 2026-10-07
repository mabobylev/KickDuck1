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
        return juce::jlimit(-60.0f, 12.0f, 20.0f * std::log10(value));
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
      waveformHistory(96000)
{
    audioCurveCache[0] = {0.0f, 0.0f};
    audioCurveCache[1] = {1.0f, 0.0f};
    publishCurve(duckingCurve.getPoints());
}

juce::AudioProcessorValueTreeState::ParameterLayout
KickDuck1AudioProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameters;
    parameters.push_back(std::make_unique<Parameter>("input", "Input", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    parameters.push_back(std::make_unique<Parameter>("output", "Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    parameters.push_back(std::make_unique<Parameter>("amount", "Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 75.0f));
    parameters.push_back(std::make_unique<Parameter>("length", "Length", juce::NormalisableRange<float>(0.125f, 2.0f, 0.001f), 0.5f));
    parameters.push_back(std::make_unique<Parameter>("attack", "Attack", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 8.0f));
    parameters.push_back(std::make_unique<Parameter>("release", "Release", juce::NormalisableRange<float>(10.0f, 1000.0f, 0.1f), 220.0f));
    parameters.push_back(std::make_unique<Parameter>("mix", "Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    return { parameters.begin(), parameters.end() };
}

void KickDuck1AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    phase = 0.0f;
    kickEnvelope = 0.0f;
    kickCycleArmed = true;
    waveformHistory.clear();

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
    gainReduction.store(0.0f, std::memory_order_relaxed);
    currentDuck.store(0.0f, std::memory_order_relaxed);
    currentPhase.store(0.0f, std::memory_order_relaxed);
    sidechainConnected.store(false, std::memory_order_relaxed);
    curveBufferState[0].store(0, std::memory_order_release);
    curveBufferState[1].store(0, std::memory_order_release);
    publishCurve(duckingCurve.getPoints());
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

float KickDuck1AudioProcessor::runtimeCurveValueAt(
    const std::array<DuckingCurve::Point, maxCurvePoints>& curve,
    int count,
    float x) const noexcept
{
    x = clamp01(x);
    if (count <= 0) return 0.0f;
    if (count == 1) return clamp01(curve[0].y);
    if (x <= curve[0].x) return clamp01(curve[0].y);
    if (x >= curve[count - 1].x) return clamp01(curve[count - 1].y);

    for (int i = 1; i < count; ++i)
    {
        const auto& a = curve[i - 1];
        const auto& b = curve[i];
        if (x <= b.x)
        {
            const float span = std::max(0.000001f, b.x - a.x);
            const float t = juce::jlimit(0.0f, 1.0f, (x - a.x) / span);
            return clamp01(a.y + (b.y - a.y) * t);
        }
    }
    return clamp01(curve[count - 1].y);
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
    const int next = 1 - active;

    int expected = 0;
    if (!curveBufferState[next].compare_exchange_strong(
            expected, 2, std::memory_order_acquire, std::memory_order_relaxed))
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
    const float attackMs = apvts.getRawParameterValue("attack")->load(std::memory_order_relaxed);
    const float releaseMs = apvts.getRawParameterValue("release")->load(std::memory_order_relaxed);
    const float mix = apvts.getRawParameterValue("mix")->load(std::memory_order_relaxed) * 0.01f;

    const float inputGain = dbToLinear(inputDb);
    const float outputGain = dbToLinear(outputDb);

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

    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (position->getBpm().hasValue()) bpm = *position->getBpm();
    if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;

    // The graph/timebase is fixed. LENGTH no longer stretches the waveform or
    // the phase clock. It controls where the second curve point sits: the
    // longer the setting, the longer the kick keeps the bass suppressed before
    // the recovery part of the curve begins.
    const double displayWindowSeconds = 2.0 / (bpm / 60.0);
    const float phaseIncrement = static_cast<float>(1.0 /
        (std::max(0.001, displayWindowSeconds) * currentSampleRate));

    // Keep the editable curve ordered while making LENGTH the musical position
    // of point #2. 0.125..2 beats maps to 0.05..0.68 of the fixed two-beat
    // ducking window. The remaining part is always available for recovery.
    const float lengthNorm = clamp01((length - 0.125f) / (2.0f - 0.125f));
    const float lengthPointX = 0.05f + lengthNorm * 0.63f;
    const float attackCoeff = attackMs <= 0.001f
        ? 0.0f
        : std::exp(-1.0f / (static_cast<float>(currentSampleRate) * attackMs * 0.001f));
    const float releaseCoeff = std::exp(-1.0f / (static_cast<float>(currentSampleRate) * std::max(0.001f, releaseMs) * 0.001f));

    auto effectiveCurve = localCurve;
    if (curveCount >= 2)
    {
        effectiveCurve[1].x = juce::jlimit(effectiveCurve[0].x + 0.002f,
                                           0.68f, lengthPointX);
    }

    float blockInputPeak = 0.0f, blockOutputPeak = 0.0f;
    float blockKickPeak = 0.0f, blockBassPeak = 0.0f, blockGainReduction = 0.0f;
    double inputSumSquares = 0.0, outputSumSquares = 0.0, kickSumSquares = 0.0;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float kickSample = 0.0f;
        if (hasKick)
            kickSample = kickChannels == 1
                ? kickBuffer.getSample(0, sample)
                : 0.5f * (kickBuffer.getSample(0, sample) + kickBuffer.getSample(1, sample));

        const float kickAbs = std::abs(kickSample);
        blockKickPeak = std::max(blockKickPeak, kickAbs);
        kickSumSquares += static_cast<double>(kickSample) * kickSample;

        if (kickAbs > kickEnvelope)
            kickEnvelope = attackCoeff * kickEnvelope + (1.0f - attackCoeff) * kickAbs;
        else
            kickEnvelope = releaseCoeff * kickEnvelope + (1.0f - releaseCoeff) * kickAbs;

        // Kick onset starts a fresh ducking cycle. LENGTH therefore has a
        // musical and immediately audible meaning: it is the duration of
        // the complete duck/recovery curve after each kick.
        const float triggerOn = 0.02f;
        const float triggerOff = 0.008f;
        if (kickCycleArmed && std::max(kickAbs, kickEnvelope) >= triggerOn)
        {
            phase = 0.0f;
            kickCycleArmed = false;
        }
        else if (!kickCycleArmed && kickEnvelope <= triggerOff)
        {
            kickCycleArmed = true;
        }

        // Use the raw kick transient together with the smoothed envelope. This
        // makes the first loud part of the kick capable of reaching maximum
        // ducking immediately instead of waiting for the envelope attack.
        const float detector = clamp01(std::max(kickAbs, kickEnvelope) * 8.0f);

        phase += phaseIncrement;
        if (phase > 1.0f)
            phase = 1.0f;

        // Curve Y is duck depth: 0 = no reduction, 1 = maximum reduction.
        const float curve = runtimeCurveValueAt(effectiveCurve, curveCount, phase);
        const float reduction = clamp01(amount * curve * detector);
        const float duck = 1.0f - reduction;
        blockGainReduction = std::max(blockGainReduction, reduction);

        float bassInMono = 0.0f;
        float bassOutMono = 0.0f;

        if (bassChannels == 1)
        {
            const float dry = bassBuffer.getSample(0, sample) * inputGain;
            const float output = dry * ((1.0f - mix) + duck * mix) * outputGain;
            bassBuffer.setSample(0, sample, output);
            bassInMono = dry;
            bassOutMono = output;
        }
        else
        {
            const float dryL = bassBuffer.getSample(0, sample) * inputGain;
            const float dryR = bassBuffer.getSample(1, sample) * inputGain;
            const float wetGain = (1.0f - mix) + duck * mix;
            const float outputL = dryL * wetGain * outputGain;
            const float outputR = dryR * wetGain * outputGain;
            bassBuffer.setSample(0, sample, outputL);
            bassBuffer.setSample(1, sample, outputR);
            bassInMono = 0.5f * (dryL + dryR);
            bassOutMono = 0.5f * (outputL + outputR);
        }

        blockInputPeak = std::max(blockInputPeak, std::abs(bassInMono));
        blockOutputPeak = std::max(blockOutputPeak, std::abs(bassOutMono));
        blockBassPeak = std::max(blockBassPeak, std::abs(bassInMono));
        inputSumSquares += static_cast<double>(bassInMono) * bassInMono;
        outputSumSquares += static_cast<double>(bassOutMono) * bassOutMono;

        // Fixed-capacity atomic DSP history. No vector, mutex or allocation.
        waveformHistory.push(bassInMono, bassOutMono, kickSample);
        currentDuck.store(reduction, std::memory_order_relaxed);
        currentPhase.store(phase, std::memory_order_relaxed);
    }

    const float inputRmsLinear = static_cast<float>(std::sqrt(inputSumSquares / numSamples));
    const float outputRmsLinear = static_cast<float>(std::sqrt(outputSumSquares / numSamples));
    const float kickRmsLinear = static_cast<float>(std::sqrt(kickSumSquares / numSamples));

    const float inputDbNow = linearToDb(inputRmsLinear);
    const float outputDbNow = linearToDb(outputRmsLinear);

    inputPeak.store(linearToDb(blockInputPeak), std::memory_order_relaxed);
    outputPeak.store(linearToDb(blockOutputPeak), std::memory_order_relaxed);
    kickActivity.store(linearToDb(blockKickPeak), std::memory_order_relaxed);
    bassActivity.store(linearToDb(blockBassPeak), std::memory_order_relaxed);
    inputRms.store(inputDbNow, std::memory_order_relaxed);
    outputRms.store(outputDbNow, std::memory_order_relaxed);
    kickRms.store(linearToDb(kickRmsLinear), std::memory_order_relaxed);
    inputLufs.store(juce::jlimit(-60.0f, 0.0f, inputDbNow), std::memory_order_relaxed);
    outputLufs.store(juce::jlimit(-60.0f, 0.0f, outputDbNow), std::memory_order_relaxed);

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
float KickDuck1AudioProcessor::getGainReductionPercent() const noexcept { return gainReduction.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getCurrentDuck() const noexcept { return currentDuck.load(std::memory_order_relaxed); }
float KickDuck1AudioProcessor::getCurrentPhase() const noexcept { return currentPhase.load(std::memory_order_relaxed); }
bool KickDuck1AudioProcessor::isSidechainConnected() const noexcept { return sidechainConnected.load(std::memory_order_relaxed); }

void KickDuck1AudioProcessor::copyWaveformSnapshot(float* bassIn, float* bassOut, float* kick,
                                                   int points, int samplesToCopy) const noexcept
{
    waveformHistory.copyLatest(bassIn, bassOut, kick, points, samplesToCopy);
}

std::vector<DuckingCurve::Point> KickDuck1AudioProcessor::getCurvePoints() const
{
    return duckingCurve.getPoints();
}

void KickDuck1AudioProcessor::setCurvePoints(const std::vector<DuckingCurve::Point>& points)
{
    std::vector<DuckingCurve::Point> safe;
    safe.reserve(std::min(points.size(), static_cast<std::size_t>(maxCurvePoints)));
    for (const auto& p : points)
        if (std::isfinite(p.x) && std::isfinite(p.y))
            safe.push_back({ juce::jlimit(0.0f, 1.0f, p.x), juce::jlimit(0.0f, 1.0f, p.y) });

    std::sort(safe.begin(), safe.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    if (safe.empty()) safe = { {0.0f, 1.0f}, {1.0f, 1.0f} };

    duckingCurve.setPoints(safe);
    publishCurve(safe);
}

void KickDuck1AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void KickDuck1AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* KickDuck1AudioProcessor::createEditor()
{
    return new KickDuck1AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KickDuck1AudioProcessor();
}
