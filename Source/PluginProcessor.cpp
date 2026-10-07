#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

    float linearToDb(float value)
    {
        if (!std::isfinite(value) || value <= 0.000001f)
            return -60.0f;
        return juce::jlimit(-60.0f, 12.0f, 20.0f * std::log10(value));
    }

    float clamp01(float value) { return juce::jlimit(0.0f, 1.0f, value); }
}

KickDuck1AudioProcessor::KickDuck1AudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Bass", juce::AudioChannelSet::stereo(), true)
        .withInput("Kick Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      waveformHistory(88200)
{
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
    waveformHistory.clear();
    inputPeak.store(-60.0f); outputPeak.store(-60.0f);
    kickActivity.store(-60.0f); bassActivity.store(-60.0f);
    inputRms.store(-60.0f); outputRms.store(-60.0f); kickRms.store(-60.0f);
    inputLufs.store(-60.0f); outputLufs.store(-60.0f);
    gainReductionDb.store(0.0f); gainReduction.store(0.0f);
    currentDuck.store(0.0f); sidechainConnected.store(false);
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

float KickDuck1AudioProcessor::runtimeCurveValueAt(float x) const noexcept
{
    x = clamp01(x);
    const int buffer = activeCurveBuffer.load(std::memory_order_acquire);
    const int count = runtimeCurveCounts[buffer].load(std::memory_order_acquire);
    if (count <= 0) return 1.0f;
    if (count == 1) return clamp01(runtimeCurves[buffer][0].y);
    if (x <= runtimeCurves[buffer][0].x) return clamp01(runtimeCurves[buffer][0].y);
    if (x >= runtimeCurves[buffer][count - 1].x) return clamp01(runtimeCurves[buffer][count - 1].y);
    for (int i = 1; i < count; ++i)
    {
        const auto& a = runtimeCurves[buffer][i - 1];
        const auto& b = runtimeCurves[buffer][i];
        if (x <= b.x)
        {
            const float span = std::max(0.000001f, b.x - a.x);
            const float t = juce::jlimit(0.0f, 1.0f, (x - a.x) / span);
            return clamp01(a.y + (b.y - a.y) * t);
        }
    }
    return clamp01(runtimeCurves[buffer][count - 1].y);
}

void KickDuck1AudioProcessor::publishCurve(const std::vector<DuckingCurve::Point>& source)
{
    std::vector<DuckingCurve::Point> points;
    points.reserve(std::min(source.size(), static_cast<std::size_t>(maxCurvePoints)));
    for (const auto& p : source)
        if (std::isfinite(p.x) && std::isfinite(p.y))
            points.push_back({ juce::jlimit(0.0f, 1.0f, p.x), juce::jlimit(0.0f, 1.0f, p.y) });
    std::sort(points.begin(), points.end(), [](auto a, auto b) { return a.x < b.x; });
    if (points.empty()) points = { {0.0f, 1.0f}, {1.0f, 1.0f} };
    if (points.size() > maxCurvePoints) points.resize(maxCurvePoints);

    const int current = activeCurveBuffer.load(std::memory_order_relaxed);
    const int next = 1 - current;
    for (std::size_t i = 0; i < points.size(); ++i) runtimeCurves[next][i] = points[i];
    runtimeCurveCounts[next].store(static_cast<int>(points.size()), std::memory_order_release);
    activeCurveBuffer.store(next, std::memory_order_release);
}

void KickDuck1AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    const float inputDb = apvts.getRawParameterValue("input")->load();
    const float outputDb = apvts.getRawParameterValue("output")->load();
    const float amount = apvts.getRawParameterValue("amount")->load() / 100.0f;
    const float length = apvts.getRawParameterValue("length")->load();
    const float attackMs = apvts.getRawParameterValue("attack")->load();
    const float releaseMs = apvts.getRawParameterValue("release")->load();
    const float mix = apvts.getRawParameterValue("mix")->load() / 100.0f;
    const float inputGain = dbToLinear(inputDb);
    const float outputGain = dbToLinear(outputDb);

    auto bassBuffer = getBusBuffer(buffer, true, 0);
    auto kickBuffer = getBusBuffer(buffer, true, 1);
    const int bassChannels = bassBuffer.getNumChannels();
    const int kickChannels = kickBuffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const bool hasKick = kickChannels > 0;
    sidechainConnected.store(hasKick, std::memory_order_relaxed);
    if (bassChannels <= 0 || numSamples <= 0) return;

    double bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (position->getBpm().hasValue()) bpm = *position->getBpm();
    if (!std::isfinite(bpm) || bpm <= 0.0) bpm = 120.0;
    const double cycleSeconds = length / (bpm / 60.0);
    const float phaseIncrement = cycleSeconds > 0.00001 ? static_cast<float>(1.0 / (cycleSeconds * currentSampleRate)) : 0.001f;
    const float attackCoeff = attackMs <= 0.001f ? 0.0f : std::exp(-1.0f / (static_cast<float>(currentSampleRate) * attackMs * 0.001f));
    const float releaseCoeff = std::exp(-1.0f / (static_cast<float>(currentSampleRate) * std::max(0.001f, releaseMs) * 0.001f));

    float blockInputPeak = 0.0f, blockOutputPeak = 0.0f, blockKickPeak = 0.0f, blockBassPeak = 0.0f, blockGainReduction = 0.0f;
    double inputSumSquares = 0.0, outputSumSquares = 0.0, kickSumSquares = 0.0;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float kickSample = 0.0f;
        if (hasKick)
            kickSample = kickChannels == 1 ? kickBuffer.getSample(0, sample) : 0.5f * (kickBuffer.getSample(0, sample) + kickBuffer.getSample(1, sample));
        const float kickAbs = std::abs(kickSample);
        blockKickPeak = std::max(blockKickPeak, kickAbs);
        kickSumSquares += static_cast<double>(kickSample) * kickSample;
        if (kickAbs > kickEnvelope) kickEnvelope = attackCoeff * kickEnvelope + (1.0f - attackCoeff) * kickAbs;
        else kickEnvelope = releaseCoeff * kickEnvelope + (1.0f - releaseCoeff) * kickAbs;
        const float detector = clamp01(kickEnvelope * 2.5f);
        phase += phaseIncrement;
        if (phase >= 1.0f) phase -= 1.0f;
        const float curve = runtimeCurveValueAt(phase);
        const float reduction = clamp01(amount * curve * detector);
        const float duck = 1.0f - reduction;
        blockGainReduction = std::max(blockGainReduction, reduction);
        float bassInMono = 0.0f, bassOutMono = 0.0f;
        if (bassChannels == 1)
        {
            const float dry = bassBuffer.getSample(0, sample) * inputGain;
            const float output = (dry * (1.0f - mix) + dry * duck * mix) * outputGain;
            bassBuffer.setSample(0, sample, output);
            bassInMono = dry; bassOutMono = output;
        }
        else
        {
            const float dryL = bassBuffer.getSample(0, sample) * inputGain;
            const float dryR = bassBuffer.getSample(1, sample) * inputGain;
            const float outputL = (dryL * (1.0f - mix) + dryL * duck * mix) * outputGain;
            const float outputR = (dryR * (1.0f - mix) + dryR * duck * mix) * outputGain;
            bassBuffer.setSample(0, sample, outputL); bassBuffer.setSample(1, sample, outputR);
            bassInMono = 0.5f * (dryL + dryR); bassOutMono = 0.5f * (outputL + outputR);
        }
        blockInputPeak = std::max(blockInputPeak, std::abs(bassInMono));
        blockOutputPeak = std::max(blockOutputPeak, std::abs(bassOutMono));
        blockBassPeak = std::max(blockBassPeak, std::abs(bassInMono));
        inputSumSquares += static_cast<double>(bassInMono) * bassInMono;
        outputSumSquares += static_cast<double>(bassOutMono) * bassOutMono;
        waveformHistory.push(bassInMono, bassOutMono, kickSample);
        currentDuck.store(reduction, std::memory_order_relaxed);
    }

    const float inputRmsLinear = static_cast<float>(std::sqrt(inputSumSquares / numSamples));
    const float outputRmsLinear = static_cast<float>(std::sqrt(outputSumSquares / numSamples));
    const float kickRmsLinear = static_cast<float>(std::sqrt(kickSumSquares / numSamples));
    const float inputDbNow = linearToDb(inputRmsLinear);
    const float outputDbNow = linearToDb(outputRmsLinear);
    inputPeak.store(linearToDb(blockInputPeak)); outputPeak.store(linearToDb(blockOutputPeak));
    kickActivity.store(linearToDb(blockKickPeak)); bassActivity.store(linearToDb(blockBassPeak));
    inputRms.store(inputDbNow); outputRms.store(outputDbNow); kickRms.store(linearToDb(kickRmsLinear));
    // A compact loudness readout. It follows RMS closely while keeping a stable
    // UI value; a future K-weighted loudness stage can replace this calculation.
    inputLufs.store(juce::jlimit(-60.0f, 0.0f, inputDbNow));
    outputLufs.store(juce::jlimit(-60.0f, 0.0f, outputDbNow));
    const float remainingGain = juce::jmax(0.000001f, 1.0f - blockGainReduction);
    const float grDb = juce::jlimit(-60.0f, 0.0f, 20.0f * std::log10(remainingGain));
    gainReductionDb.store(grDb); gainReduction.store(blockGainReduction * 100.0f);
}

float KickDuck1AudioProcessor::getInputPeak() const noexcept { return inputPeak.load(); }
float KickDuck1AudioProcessor::getOutputPeak() const noexcept { return outputPeak.load(); }
float KickDuck1AudioProcessor::getKickActivity() const noexcept { return kickActivity.load(); }
float KickDuck1AudioProcessor::getBassActivity() const noexcept { return bassActivity.load(); }
float KickDuck1AudioProcessor::getInputRms() const noexcept { return inputRms.load(); }
float KickDuck1AudioProcessor::getOutputRms() const noexcept { return outputRms.load(); }
float KickDuck1AudioProcessor::getKickRms() const noexcept { return kickRms.load(); }
float KickDuck1AudioProcessor::getInputLufs() const noexcept { return inputLufs.load(); }
float KickDuck1AudioProcessor::getOutputLufs() const noexcept { return outputLufs.load(); }
float KickDuck1AudioProcessor::getGainReduction() const noexcept { return getGainReductionPercent(); }
float KickDuck1AudioProcessor::getGainReductionDb() const noexcept { return gainReductionDb.load(); }
float KickDuck1AudioProcessor::getGainReductionPercent() const noexcept { return gainReduction.load(); }
float KickDuck1AudioProcessor::getCurrentDuck() const noexcept { return currentDuck.load(); }
bool KickDuck1AudioProcessor::isSidechainConnected() const noexcept { return sidechainConnected.load(); }

void KickDuck1AudioProcessor::copyWaveformHistory(std::vector<float>& bassIn, std::vector<float>& bassOut, std::vector<float>& kick, int samplesToCopy) const
{
    waveformHistory.copyLatest(bassIn, bassOut, kick, samplesToCopy);
}

std::vector<DuckingCurve::Point> KickDuck1AudioProcessor::getCurvePoints() const { return duckingCurve.getPoints(); }

void KickDuck1AudioProcessor::setCurvePoints(const std::vector<DuckingCurve::Point>& points)
{
    std::vector<DuckingCurve::Point> safe;
    safe.reserve(std::min(points.size(), static_cast<std::size_t>(maxCurvePoints)));
    for (const auto& p : points)
        if (std::isfinite(p.x) && std::isfinite(p.y)) safe.push_back({ juce::jlimit(0.0f,1.0f,p.x), juce::jlimit(0.0f,1.0f,p.y) });
    std::sort(safe.begin(), safe.end(), [](auto a, auto b){ return a.x < b.x; });
    if (safe.empty()) safe = { {0.0f,1.0f}, {1.0f,1.0f} };
    duckingCurve.setPoints(safe);
    publishCurve(safe);
}

void KickDuck1AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void KickDuck1AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* KickDuck1AudioProcessor::createEditor() { return new KickDuck1AudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KickDuck1AudioProcessor(); }
