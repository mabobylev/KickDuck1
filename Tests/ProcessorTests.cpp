#include "PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

bool equalCurves(const std::vector<DuckingCurve::Point>& a,
                 const std::vector<DuckingCurve::Point>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i].x - b[i].x) > 0.00001f || std::abs(a[i].y - b[i].y) > 0.00001f)
            return false;
    return true;
}

void parameter(KickDuck1AudioProcessor& processor, const char* id, float value)
{
    auto* p = processor.apvts.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

void prepare(KickDuck1AudioProcessor& p)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    require(p.setBusesLayout(layout), "Stereo sidechain layout rejected");
    p.prepareToPlay(48000.0, 64);
}

float process(KickDuck1AudioProcessor& p)
{
    juce::AudioBuffer<float> buffer(4, 64);
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 64; ++i)
            buffer.setSample(ch, i, ch < 2 ? 0.5f : 1.0f);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    return buffer.getSample(0, 0);
}

void stateTests()
{
    auto source = std::make_unique<KickDuck1AudioProcessor>();
    const std::vector<DuckingCurve::Point> custom {{0.0f, 0.2f}, {0.2f, 0.2f},
                                                  {0.75f, 0.8f}, {1.0f, 0.0f}};
    source->setCurvePoints(custom);
    parameter(*source, "amount", 63.0f);
    parameter(*source, "length", 1.2f);
    juce::MemoryBlock state;
    source->getStateInformation(state);
    auto restored = std::make_unique<KickDuck1AudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(equalCurves(source->getCurvePoints(), restored->getCurvePoints()), "Curve state round trip failed");
    require(std::abs(restored->apvts.getRawParameterValue("amount")->load() - 63.0f) < 0.001f,
            "Amount state round trip failed");
    prepare(*source);
    prepare(*restored);
    require(std::abs(process(*source) - process(*restored)) < 0.00001f,
            "Restored state produced different audio");

    // Old projects saved APVTS only: restore parameters and install default curve.
    auto legacy = source->apvts.copyState();
    juce::MemoryBlock legacyData;
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), legacyData);
    restored->setStateInformation(legacyData.getData(), static_cast<int>(legacyData.getSize()));
    require(equalCurves(restored->getCurvePoints(), DuckingCurve::defaultPoints()), "Legacy curve migration failed");
    const auto before = restored->getCurvePoints();
    const char invalid[] = "invalid state";
    restored->setStateInformation(invalid, sizeof(invalid));
    restored->setStateInformation(nullptr, 0);
    require(equalCurves(restored->getCurvePoints(), before), "Invalid state changed curve");

    // A deliberately flat curve is a user choice, not a default-curve sentinel.
    source->setCurvePoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    source->getStateInformation(state);
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(equalCurves(source->getCurvePoints(), restored->getCurvePoints()), "Flat curve was replaced");
}

void normalizationTests()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    p->setCurvePoints({{0.8f, 2.0f}, {0.2f, -1.0f}, {nan, 0.5f}, {0.2f, 0.6f}});
    const auto points = p->getCurvePoints();
    require(points.front().x == 0.0f && points.back().x == 1.0f, "Missing curve boundaries");
    for (size_t i = 0; i < points.size(); ++i)
    {
        require(points[i].y >= 0.0f && points[i].y <= 1.0f, "Invalid depth");
        if (i > 0) require(points[i].x > points[i - 1].x, "Duplicate or unordered points");
    }
    require(std::abs(points[1].y - 0.6f) < 0.00001f, "Duplicate point policy failed");
    std::vector<DuckingCurve::Point> many;
    for (int i = 0; i < 100; ++i) many.push_back({0.1f + i * 0.005f, 0.5f});
    p->setCurvePoints(many);
    const auto bounded = p->getCurvePoints();
    require(bounded.size() <= 32 && bounded.front().x == 0.0f && bounded.back().x == 1.0f,
            "Curve capacity discarded boundary points");
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto restored = std::make_unique<KickDuck1AudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(equalCurves(bounded, restored->getCurvePoints()), "Normalized curve did not survive state round trip");
}

void headlessAndMixTests()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    require(equalCurves(p->getCurvePoints(), DuckingCurve::defaultPoints()), "Default curve depends on editor");
    parameter(*p, "amount", 100.0f);
    prepare(*p);
    require(std::abs(process(*p)) < 0.00001f, "First kick sample was not ducked without editor");
    parameter(*p, "mix", 0.0f);
    prepare(*p);
    require(std::abs(process(*p) - 0.5f) < 0.00001f, "Mix zero is not dry");
    parameter(*p, "amount", 0.0f);
    parameter(*p, "mix", 100.0f);
    prepare(*p);
    require(std::abs(process(*p) - 0.5f) < 0.00001f, "Amount zero is not dry");

    // Display operations must leave the canonical state unchanged.
    auto snapshot = p->getCurvePoints();
    snapshot.reserve(DuckingCurve::maxPoints);
    const auto before = p->getCurvePoints();
    require(p->tryCopyCurvePoints(snapshot), "GUI snapshot unavailable without writer");
    for (float length : {0.125f, 0.5f, 2.0f})
    {
        const float x = DuckingCurve::lengthPointX(snapshot.data(), static_cast<int>(snapshot.size()), length);
        require(x > snapshot[0].x && x < snapshot[2].x, "Length broke point ordering");
    }
    require(equalCurves(before, p->getCurvePoints()), "Display mapping changed canonical curve");
    const DuckingCurve::Point crowded[] {{0.0f, 1.0f}, {0.001f, 0.5f}, {0.002f, 0.0f}};
    require(DuckingCurve::lengthPointX(crowded, 3, 2.0f) == crowded[1].x, "Crowded points changed order");
}

void lengthAffectsAudioTest()
{
    auto shortDuck = std::make_unique<KickDuck1AudioProcessor>();
    auto longDuck = std::make_unique<KickDuck1AudioProcessor>();
    parameter(*shortDuck, "amount", 100.0f);
    parameter(*longDuck, "amount", 100.0f);
    parameter(*shortDuck, "length", 0.125f);
    parameter(*longDuck, "length", 2.0f);
    prepare(*shortDuck);
    prepare(*longDuck);
    float shortOutput = 0.0f, longOutput = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        shortOutput = process(*shortDuck);
        longOutput = process(*longDuck);
    }
    require(shortOutput > longOutput + 0.05f, "Length did not extend ducking hold in DSP");
    require(std::abs(shortDuck->getCurrentPhase() - longDuck->getCurrentPhase()) < 0.00001f,
            "Length stretched cycle timebase");
}

void editorDoesNotChangeStateTest()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    const std::vector<DuckingCurve::Point> flat {{0.0f, 1.0f}, {1.0f, 1.0f}};
    p->setCurvePoints(flat);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
    require(equalCurves(p->getCurvePoints(), flat), "Opening editor replaced flat curve");

    const std::vector<DuckingCurve::Point> custom {{0.0f, 0.8f}, {0.15f, 0.7f},
                                                  {0.3f, 0.3f}, {1.0f, 0.0f}};
    p->setCurvePoints(custom);
    parameter(*p, "length", 2.0f);
    // Run a GUI timer tick without showing a native window.
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    require(equalCurves(p->getCurvePoints(), custom), "GUI timer changed stored curve for Length");
    editor.reset();
    require(equalCurves(p->getCurvePoints(), custom), "Closing editor changed stored curve");
}

std::vector<float> renderCycle(KickDuck1AudioProcessor& p, int blockSize = 128,
                               bool kickEnabled = true, bool invertRightKick = false)
{
    prepare(p);
    constexpr int samples = 26000;
    std::vector<float> output(samples);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = std::min(blockSize, samples - offset);
        juce::AudioBuffer<float> buffer(4, count);
        for (int i = 0; i < count; ++i)
        {
            const int position = offset + i;
            const float kick = kickEnabled && position < 240 ? 0.8f : 0.0f;
            buffer.setSample(0, i, 0.5f);
            buffer.setSample(1, i, 0.5f);
            buffer.setSample(2, i, kick);
            buffer.setSample(3, i, invertRightKick ? -kick : kick);
        }
        p.processBlock(buffer, midi);
        std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + count, output.begin() + offset);
    }
    return output;
}

void allFivePointsAffectAudioTest()
{
    const auto defaults = DuckingCurve::defaultPoints();
    require(defaults.size() == 5 && defaults[0].y == 1.0f && defaults[4].y == 1.0f,
            "Factory endpoints are not both at the bottom");
    require(defaults[1].x == 0.25f && defaults[2].x == 0.5f && defaults[2].y == 0.0f,
            "Factory midpoint/peak layout differs from requested shape");
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    parameter(*p, "amount", 100.0f);
    const auto baseline = renderCycle(*p);
    // A 5 ms kick must not erase later portions of a 500 ms curve.
    require(baseline[12000] > 0.49f && baseline[23900] < 0.05f,
            "A short kick did not play the recovery and final duck of the full curve");
    require(std::abs(baseline.back() - 0.5f) < 0.00001f, "Duck cycle did not return to idle");
    for (int i = 24000; i < 24120; ++i)
        require(std::abs(baseline[i] - baseline[i - 1]) < 0.0053f,
                "End of cycle returned to dry with a discontinuity");
    const int probes[] {0, 6000, 12000, 21600, 23900};
    for (int point = 0; point < 5; ++point)
    {
        p->resetCurve();
        auto changed = defaults;
        changed[point].y = defaults[point].y > 0.5f ? defaults[point].y - 0.3f : defaults[point].y + 0.3f;
        p->editCurvePoints(changed);
        const auto edited = renderCycle(*p);
        require(std::abs(edited[probes[point]] - baseline[probes[point]]) > 0.1f,
                "Editing one of the five depths had no effect on its audio segment");
        std::array<float, 1600> in {}, out {}, kick {};
        p->copyWaveformSnapshot(in.data(), out.data(), kick.data(), 1600, 26000);
        float snapshotDifference = 0.0f;
        for (size_t i = 0; i < out.size(); ++i) snapshotDifference = std::max(snapshotDifference, std::abs(in[i] - out[i]));
        require(snapshotDifference > 0.1f, "Waveform history does not contain real ducked bass");
        for (size_t i = 0; i < out.size(); ++i)
        {
            const auto audioIndex = i * edited.size() / out.size();
            require(std::abs(out[i] - edited[audioIndex]) < 0.00001f,
                    "Waveform snapshot differs from rendered audio after a point edit");
        }
        std::cout << "Point " << point + 1 << " depth: delta=" << std::abs(edited[probes[point]] - baseline[probes[point]]) << '\n';
    }
    for (int point = 1; point < 4; ++point)
    {
        p->resetCurve();
        auto changed = defaults;
        changed[point].x += point == 3 ? -0.08f : 0.08f;
        p->editCurvePoints(changed);
        const auto edited = renderCycle(*p);
        float difference = 0.0f;
        for (size_t i = 0; i < edited.size(); ++i) difference = std::max(difference, std::abs(edited[i] - baseline[i]));
        require(difference > 0.03f, "Moving an interior point in time had no effect on DSP");
    }
    p->resetCurve();
    const auto silent = renderCycle(*p, 128, false);
    for (float sample : silent) require(std::abs(sample - 0.5f) < 0.00001f, "Bass ducked with no kick trigger");
    parameter(*p, "amount", 100.0f);
    const auto opposite = renderCycle(*p, 128, true, true);
    for (size_t i = 0; i < opposite.size(); ++i)
        require(std::abs(opposite[i] - baseline[i]) < 0.00001f, "Opposite-phase sidechain cancelled the trigger");
    const auto smallBlocks = renderCycle(*p, 17);
    for (size_t i = 0; i < smallBlocks.size(); ++i)
        require(std::abs(smallBlocks[i] - baseline[i]) < 0.00001f, "Curve depends on block size");
}

juce::Component* namedChild(juce::Component& parent, const juce::String& name)
{
    for (auto* child : parent.getChildren())
        if (child->getName() == name) return child;
    throw std::runtime_error("Missing named GUI component");
}

void dragPoint(juce::Component& curve, juce::Point<float> from, juce::Point<float> to)
{
    const auto graph = curve.getLocalBounds().toFloat().reduced(18.0f, 34.0f);
    const auto screen = [&graph](juce::Point<float> p)
    { return juce::Point<float>{graph.getX() + p.x * graph.getWidth(), graph.getY() + p.y * graph.getHeight()}; };
    const auto down = screen(from);
    const auto time = juce::Time::getCurrentTime();
    const auto event = [&](juce::Point<float> p, bool dragged)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), screen(p),
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &curve, &curve, time, down, time, 1, dragged);
    };
    curve.mouseDown(event(from, false));
    curve.mouseDrag(event(to, true));
    curve.mouseUp(event(to, true));
}

void curveAndKnobsRoundTripTest()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    parameter(*p, "amount", 100.0f);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
    auto& curve = *namedChild(*editor, "Ducking curve");
    const auto knob = [&](const char* name) -> juce::Slider&
    { return *dynamic_cast<juce::Slider*>(namedChild(*editor, name)); };
    dragPoint(curve, {0.25f, 0.85f}, {0.3f, 0.8f});
    require(std::abs(knob("LENGTH").getValue() - 0.6) < 0.001, "Point #2 did not rotate LENGTH");
    dragPoint(curve, {0.5f, 0.0f}, {0.6f, 0.15f});
    require(std::abs(knob("RECOVER").getValue() - 300.0) < 0.11, "Point #3 did not rotate RELEASE");
    dragPoint(curve, {0.9f, 0.1f}, {0.85f, 0.2f});
    require(std::abs(knob("TAIL").getValue() - 75.0) < 0.02, "Point #4 did not rotate ATTACK");
    dragPoint(curve, {0.0f, 1.0f}, {0.0f, 0.6f});
    dragPoint(curve, {1.0f, 1.0f}, {1.0f, 0.7f});
    require(std::abs(knob("AMOUNT").getValue() - 80.0) < 0.02, "Changing deepest points did not rotate AMOUNT");
    const auto points = p->getCurvePoints();
    require(std::abs(points.front().y * 0.8f - 0.6f) < 0.001f,
            "Editing last point moved the first point's displayed depth");
    const float rawX = points[1].x;
    knob("LENGTH").setValue(0.4, juce::sendNotificationSync);
    require(std::abs(p->apvts.getRawParameterValue("length")->load() - 0.4f) < 0.001f,
            "Rotating LENGTH did not change host parameter");
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    const auto stored = p->getCurvePoints();
    require(std::abs(stored[1].x - rawX) < 0.001f, "GUI timer rewrote canonical point coordinates");
    auto effective = stored;
    DuckingCurve::applyControls(effective.data(), static_cast<int>(effective.size()), 0.4f, 75.0f, 300.0f, 0.5);
    require(std::abs(effective[1].x - 0.2f) < 0.001f, "LENGTH knob did not move effective point #2");

    // Save and reload with the editor already open; rendering must stay read-only.
    juce::MemoryBlock state;
    p->getStateInformation(state);
    p->resetCurve();
    p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(equalCurves(stored, p->getCurvePoints()), "Edited five-point state did not round trip");
    p->resetCurve();
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    juce::Image preview(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics graphics(preview);
    editor->paintEntireComponent(graphics, true);
    auto stream = juce::File::getCurrentWorkingDirectory().getChildFile("curve-preview.png").createOutputStream();
    if (stream)
    {
        stream->setPosition(0);
        stream->truncate();
        juce::PNGImageFormat().writeImageToStream(preview, *stream);
    }
}

void automatedBlockSizeTest()
{
    const auto render = [](int blockSize)
    {
        auto p = std::make_unique<KickDuck1AudioProcessor>();
        prepare(*p);
        juce::MidiBuffer midi;
        std::vector<float> audio(30000);
        for (int offset = 0; offset < static_cast<int>(audio.size());)
        {
            // Split callbacks at automation events to keep their sample times
            // identical for all block sizes.
            if (offset == 9600)
            {
                parameter(*p, "input", 6.0f);
                parameter(*p, "output", -3.0f);
                parameter(*p, "amount", 50.0f);
                parameter(*p, "mix", 70.0f);
                parameter(*p, "length", 0.8f);
                parameter(*p, "release", 300.0f);
                parameter(*p, "attack", 80.0f);
                p->setCurvePoints({{0.0f, 1.0f}, {0.25f, 0.3f}, {0.5f, 0.2f},
                                   {0.9f, 0.4f}, {1.0f, 0.7f}});
            }
            const int boundary = offset < 9600 ? 9600 : static_cast<int>(audio.size());
            const int count = std::min(blockSize, boundary - offset);
            juce::AudioBuffer<float> buffer(4, count);
            for (int i = 0; i < count; ++i)
            {
                const int position = offset + i;
                float kick = 0.0f;
                for (int hit : {0, 2400, 4800, 7200})
                    if (position >= hit)
                        kick += 0.4f * std::exp(-(position - hit) / 4800.0f);
                buffer.setSample(0, i, 0.5f);
                buffer.setSample(1, i, 0.5f);
                buffer.setSample(2, i, kick);
                buffer.setSample(3, i, -kick);
            }
            p->processBlock(buffer, midi);
            std::copy_n(buffer.getReadPointer(0), count, audio.begin() + offset);
            offset += count;
        }
        return audio;
    };
    const auto reference = render(1);
    for (int size : {17, 64, 512})
    {
        const auto output = render(size);
        for (size_t i = 0; i < output.size(); ++i)
            require(std::abs(output[i] - reference[i]) < 0.00001f,
                    "Detector or automation smoothing depends on block size");
    }
}

void detectorAndSmoothingTests()
{
    for (double rate : {44100.0, 48000.0, 192000.0})
    {
        auto p = std::make_unique<KickDuck1AudioProcessor>();
        parameter(*p, "amount", 100.0f);
        prepare(*p);
        p->prepareToPlay(rate, 1);
        juce::AudioBuffer<float> buffer(4, 1);
        juce::MidiBuffer midi;
        const auto sample = [&](float kick)
        {
            buffer.setSample(0, 0, 0.5f);
            buffer.setSample(1, 0, 0.5f);
            buffer.setSample(2, 0, kick);
            buffer.setSample(3, 0, -kick);
            p->processBlock(buffer, midi);
            return buffer.getSample(0, 0);
        };
        int triggers = 0;
        // Four new transients on overlapping 100 ms tails, 50 ms apart.
        const int interval = static_cast<int>(rate * 0.05);
        for (int i = 0; i < interval * 4; ++i)
        {
            float kick = 0.0f;
            for (int hit = 0; hit < 4; ++hit)
            {
                const int age = i - hit * interval;
                if (age >= 0)
                    kick += 0.4f * std::exp(-static_cast<float>(age / (rate * 0.1)));
            }
            sample(kick);
            if (p->getCurrentPhase() == 0.0f) ++triggers;
        }
        require(triggers == 4, "Overlapping kick tails lost hits or generated extra triggers");

        p->prepareToPlay(rate, 1);
        triggers = 0;
        for (int i = 0; i < static_cast<int>(rate * 0.3); ++i)
        {
            sample(0.4f * std::sin(static_cast<float>(i * 2.0 * juce::MathConstants<double>::pi * 50.0 / rate)));
            if (p->getCurrentPhase() == 0.0f) ++triggers;
        }
        require(triggers == 1, "Sustained kick oscillation retriggered at zero crossings");

        p->prepareToPlay(rate, 1);
        triggers = 0;
        for (int i = 0; i < static_cast<int>(rate * 0.5); ++i)
        {
            const float time = static_cast<float>(i / rate);
            sample(0.8f * std::exp(-time * 30.0f)
                * std::cos(time * 2.0f * juce::MathConstants<float>::pi * 55.0f));
            if (p->getCurrentPhase() == 0.0f) ++triggers;
        }
        require(triggers == 1, "Decaying oscillating tail manufactured a new kick frame");

        p->prepareToPlay(rate, 1);
        sample(0.019f);
        require(p->getCurrentDuck() == 0.0f, "Subthreshold signal triggered ducking");
        sample(0.025f);
        require(p->getCurrentPhase() == 0.0f && p->getCurrentDuck() > 0.99f,
                "Weak above-threshold hit missed its first sample");

        // A flat curve isolates control changes from the moving cycle.
        p->setCurvePoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
        p->prepareToPlay(rate, 1);
        sample(0.8f);
        parameter(*p, "mix", 0.0f);
        float previous = 0.0f;
        for (int i = 0; i < static_cast<int>(rate * 0.025); ++i)
        {
            const float out = sample(0.8f);
            require(std::abs(out - previous) < 0.002f, "Mix automation produced a step");
            previous = out;
        }
        require(std::abs(previous - 0.5f) < 0.00001f, "Mix ramp did not reach dry");
        require(p->getGainReductionPercent() < 0.001f, "Dry Mix still reports gain reduction");
        for (const char* id : {"input", "output"})
        {
            parameter(*p, id, 12.0f);
            for (int i = 0; i < static_cast<int>(rate * 0.025); ++i)
            {
                const float out = sample(0.8f);
                require(std::abs(out - previous) < 0.03f, "Gain automation produced a step");
                previous = out;
            }
        }
        require(std::abs(previous - 0.5f * std::pow(10.0f, 24.0f / 20.0f)) < 0.001f,
                "Gain ramps did not reach their targets");
        parameter(*p, "input", 0.0f);
        parameter(*p, "output", 0.0f);
        parameter(*p, "mix", 100.0f);
        p->prepareToPlay(rate, 1);
        previous = sample(0.8f);
        parameter(*p, "amount", 0.0f);
        for (int i = 0; i < static_cast<int>(rate * 0.025); ++i)
        {
            const float out = sample(0.8f);
            require(std::abs(out - previous) < 0.002f, "Amount automation produced a step");
            previous = out;
        }
        require(std::abs(previous - 0.5f) < 0.00001f, "Amount ramp did not reach zero reduction");
        parameter(*p, "amount", 100.0f);
        p->prepareToPlay(rate, 1);
        previous = sample(0.8f);
        p->setCurvePoints({{0.0f, 0.0f}, {1.0f, 0.0f}});
        for (int i = 0; i < static_cast<int>(rate * 0.005); ++i)
        {
            const float out = sample(0.8f);
            require(std::abs(out - previous) <= 0.5f / static_cast<float>(rate * 0.002) + 0.00001f,
                    "Curve edit produced an unbounded discontinuity");
            previous = out;
        }
        require(std::abs(previous - 0.5f) < 0.00001f, "Curve edit smoothing did not settle");
    }
}

void triggeredFrameTests()
{
    for (double sr : {44100.0, 48000.0, 192000.0})
    {
        auto p = std::make_unique<KickDuck1AudioProcessor>();
        parameter(*p, "amount", 100.0f);
        prepare(*p);
        p->prepareToPlay(sr, 64);
        std::array<float, TriggeredWaveform::points> input {}, output {}, kick {};
        std::uint64_t version = 0;
        double seconds = 0.0;
        require(p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds),
                "Prepare did not clear the waveform frame");
        const int beatSamples = static_cast<int>(sr * 0.5);
        juce::MidiBuffer midi;
        const auto render = [&](int samples, bool hit)
        {
            for (int offset = 0; offset < samples; offset += 64)
            {
                const int count = std::min(64, samples - offset);
                juce::AudioBuffer<float> buffer(4, count);
                for (int i = 0; i < count; ++i)
                {
                    buffer.setSample(0, i, 0.5f);
                    buffer.setSample(1, i, 0.5f);
                    const float value = hit && offset + i < 10 ? 0.8f : 0.0f;
                    buffer.setSample(2, i, value);
                    buffer.setSample(3, i, -value);
                }
                p->processBlock(buffer, midi);
            }
        };
        render(beatSamples / 2, true);
        require(!p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds),
                "An unfinished frame replaced the held waveform");
        render(beatSamples - beatSamples / 2, false);
        require(p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds),
                "A finished frame was not published");
        require(std::abs(seconds - 0.5) < 0.000001 && kick[0] == 0.8f,
                "Kick did not start at the left edge, or antiphase waveform vanished");
        require(input[0] == 0.5f && output[0] < 0.001f && output[800] > 0.49f,
                "Triggered waveform does not show actual processed bass");
        render(beatSamples, false);
        require(p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds),
                "Bass-only beat frame was not published");
        for (int i = 0; i < TriggeredWaveform::points; ++i)
            require(kick[i] == 0.0f && input[i] == 0.5f && (i < 8 || output[i] > 0.49f),
                    "Bass-only frame retained a kick or ducked the bass");
        render(beatSamples / 4, true);
        render(1, true);
        require(p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds),
                "Next trigger did not finish the previous partial frame");
        require(kick[0] == 0.8f && input[399] == 0.5f && input[400] == 0.0f,
                "Partial frame stretched in time or included the next kick");
        for (size_t i = 400; i < kick.size(); ++i)
            require(kick[i] == 0.0f && output[i] == 0.0f, "Partial frame reused stale tail data");
        p->prepareToPlay(sr * 0.5, 64);
        require(p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds),
                "Sample rate change did not invalidate the old frame");
        for (float value : kick) require(value == 0.0f, "Reset retained a stale waveform");
    }

    // Each sample triple in a reserved frame must belong to one publication.
    auto frames = std::make_unique<TriggeredWaveform>();
    std::atomic<bool> done {false};
    std::thread audio([&]
    {
        for (int frame = 1; frame <= 1000; ++frame)
        {
            frames->trigger(TriggeredWaveform::points, 0.5);
            for (int i = 0; i < TriggeredWaveform::points; ++i)
                frames->push(static_cast<float>(frame), frame * 0.5f, -static_cast<float>(frame));
        }
        done.store(true, std::memory_order_release);
    });
    std::array<float, TriggeredWaveform::points> in {}, out {}, kick {};
    std::uint64_t version = 0;
    double seconds = 0.0;
    bool coherent = true;
    while (!done.load(std::memory_order_acquire))
    {
        if (!frames->copy(in.data(), out.data(), kick.data(), version, seconds)) continue;
        for (int i = 0; i < TriggeredWaveform::points; ++i)
            if (in[i] != in[0] || out[i] != in[i] * 0.5f || kick[i] != -in[i]) coherent = false;
    }
    audio.join();
    require(coherent, "Concurrent frame capture exposed partially written data");
    frames->copy(in.data(), out.data(), kick.data(), version, seconds);
    require(in[0] == 1000.0f, "Latest complete frame was lost");
}

void editorLayoutPreviewsTest()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    prepare(*p);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < 96000; offset += 64)
    {
        const int count = std::min(64, 96000 - offset);
        juce::AudioBuffer<float> buffer(4, count);
        for (int i = 0; i < count; ++i)
        {
            const float time = (offset + i) / 48000.0f;
            const float bass = 0.4f * std::sin(time * 2.0f * juce::MathConstants<float>::pi * 80.0f);
            const float beatTime = std::fmod(time, 0.5f);
            const float kick = 0.8f * std::exp(-beatTime * 30.0f)
                * std::cos(beatTime * 2.0f * juce::MathConstants<float>::pi * 55.0f);
            buffer.setSample(0, i, bass);
            buffer.setSample(1, i, bass);
            buffer.setSample(2, i, kick);
            buffer.setSample(3, i, -kick);
        }
        p->processBlock(buffer, midi);
    }
    parameter(*p, "mix", 63.0f);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
    for (auto size : {juce::Point<int>{1050, 680}, {1260, 800}, {1800, 1100}, {1050, 1100}, {1800, 680}})
    {
        editor->setSize(size.x, size.y);
        juce::Thread::sleep(100);
        juce::Timer::callPendingTimersSynchronously();
        const auto scope = namedChild(*editor, "Signal scope")->getBounds();
        require(editor->getLocalBounds().contains(scope) && scope.getX() >= 280
                && scope.getRight() <= size.x - 300 && scope.getBottom() < 70,
                "Header scope overlaps title, AUX status or main graph after resize");
        std::vector<juce::Rectangle<int>> knobs;
        for (const char* name : {"ATTACK", "LENGTH", "RECOVER", "TAIL", "INPUT", "OUTPUT", "AMOUNT"})
        {
            const auto bounds = namedChild(*editor, name)->getBounds();
            require(editor->getLocalBounds().contains(bounds), "A knob escaped the editor after resize");
            for (const auto& other : knobs)
                require(!bounds.intersects(other), "Knobs overlap after resize");
            knobs.push_back(bounds);
        }
        for (auto* child : editor->getChildren())
        {
            if (!child->getName().startsWith("Curve preset:") && child->getName() != "Round curve") continue;
            require(editor->getLocalBounds().contains(child->getBounds()), "Preset row escaped the editor");
            for (const auto& knob : knobs)
                require(!child->getBounds().intersects(knob), "Preset row overlaps a knob after resize");
        }
        const auto* firstPreset = namedChild(*editor, "Curve preset: Classic");
        for (int i = 0; i < DuckingCurve::presetCount; ++i)
        {
            const auto* button = namedChild(*editor, juce::String("Curve preset: ")
                + DuckingCurve::presetName(static_cast<DuckingCurve::Preset>(i)));
            require(button->getX() < knobs.front().getX(), "Presets are not to the left of the timing controls");
            require(button->getY() == firstPreset->getY() + (i / 3) * 58,
                    "Preset tiles are not arranged in two rows of three");
        }
        require(knobs.back().getHeight() > knobs.front().getHeight(), "Main Amount knob is not larger");
        for (auto* child : editor->getChildren())
            require(child->getName() != "MIX", "Duplicate Mix knob is still visible");
        juce::Image image(juce::Image::ARGB, size.x, size.y, true);
        juce::Graphics graphics(image);
        editor->paintEntireComponent(graphics, true);
        int outputPixels = 0, grPixels = 0;
        for (int y = scope.getY() + 18; y < scope.getBottom() - 5; ++y)
            for (int x = scope.getX() + 10; x < scope.getRight() - 10; ++x)
            {
                const auto colour = image.getPixelAt(x, y);
                if (colour.getGreen() > colour.getRed() + 20 && colour.getGreen() > colour.getBlue() + 5)
                    ++outputPixels;
                if (colour.getRed() > 110 && colour.getGreen() > 110 && colour.getBlue() > 110
                    && y < scope.getY() + 29)
                    ++grPixels;
            }
        require(outputPixels > 100 && grPixels > 100,
                "Header scope did not render the muted half-wave and light compression trace");
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(
            "editor-preview-" + juce::String(size.x) + "x" + juce::String(size.y) + ".png");
        auto stream = file.createOutputStream();
        require(stream != nullptr, "Could not open editor preview");
        stream->setPosition(0);
        stream->truncate();
        require(juce::PNGImageFormat().writeImageToStream(image, *stream),
                "Could not write editor preview");
    }
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto restored = std::make_unique<KickDuck1AudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(std::abs(restored->apvts.getRawParameterValue("mix")->load() - 63.0f) < 0.001f,
            "Removing Mix from GUI changed legacy project state");
    auto* legacyButton = dynamic_cast<juce::TextButton*>(namedChild(*editor, "Legacy Mix"));
    require(legacyButton != nullptr && legacyButton->isVisible(), "Legacy Mix value is hidden from user");
    legacyButton->onClick();
    require(p->apvts.getRawParameterValue("mix")->load() == 100.0f && !legacyButton->isVisible(),
            "Legacy Mix reset did not restore 100% and hide the compatibility button");
    auto depth = std::make_unique<KickDuck1AudioProcessor>();
    parameter(*depth, "amount", 63.0f);
    parameter(*depth, "mix", 100.0f);
    parameter(*restored, "amount", 100.0f);
    depth->setCurvePoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    restored->setCurvePoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    prepare(*depth);
    prepare(*restored);
    require(std::abs(process(*depth) - process(*restored)) < 0.00001f,
            "Amount/Mix equivalence for a constant curve regressed");
}

void roundedCurvesAndPresetsTest()
{
    for (int i = 0; i < DuckingCurve::presetCount; ++i)
    {
        const auto preset = static_cast<DuckingCurve::Preset>(i);
        const auto points = DuckingCurve::presetPoints(preset);
        const int count = static_cast<int>(points.size());
        for (const auto& point : points)
            require(std::abs(DuckingCurve::sample(points.data(), count, point.x, true) - point.y) < 0.000001f,
                    "Rounded curve missed a control point");
        for (int segment = 1; segment < count; ++segment)
        {
            float previous = points[segment - 1].y;
            for (int j = 1; j <= 100; ++j)
            {
                const float x = points[segment - 1].x
                    + (points[segment].x - points[segment - 1].x) * j / 100.0f;
                const float value = DuckingCurve::sample(points.data(), count, x, true);
                require(value >= std::min(points[segment - 1].y, points[segment].y) - 0.000001f
                        && value <= std::max(points[segment - 1].y, points[segment].y) + 0.000001f,
                        "Rounded curve overshot a segment");
                require(points[segment].y >= points[segment - 1].y ? value >= previous - 0.000001f
                        : value <= previous + 0.000001f, "Rounded monotonic segment reversed direction");
                previous = value;
            }
        }
        for (int j = 1; j < count - 1; ++j)
        {
            constexpr float epsilon = 0.00001f;
            const float y = points[j].y;
            const float left = (y - DuckingCurve::sample(points.data(), count, points[j].x - epsilon, true)) / epsilon;
            const float right = (DuckingCurve::sample(points.data(), count, points[j].x + epsilon, true) - y) / epsilon;
            require(std::abs(left - right) < 0.15f, "Rounded curve has a sharp corner at a handle");
        }
        auto p = std::make_unique<KickDuck1AudioProcessor>();
        parameter(*p, "amount", 45.0f);
        parameter(*p, "input", -3.0f);
        parameter(*p, "output", -2.0f);
        parameter(*p, "mix", 63.0f);
        p->applyCurvePreset(preset);
        require(equalCurves(points, p->getCurvePoints()) && p->isCurveRounded(), "Preset did not install the rounded shape");
        require(p->apvts.getRawParameterValue("amount")->load() == 45.0f
                && p->apvts.getRawParameterValue("input")->load() == -3.0f
                && p->apvts.getRawParameterValue("output")->load() == -2.0f
                && p->apvts.getRawParameterValue("mix")->load() == 63.0f,
                "Shape preset changed depth or signal levels");
        parameter(*p, "amount", 100.0f);
        parameter(*p, "input", 0.0f);
        parameter(*p, "output", 0.0f);
        parameter(*p, "mix", 100.0f);
        const auto output = renderCycle(*p);
        for (int sample = 0; sample < 23950; sample += 31)
        {
            const float displayedDepth = DuckingCurve::sample(points.data(), count, sample / 24000.0f, true);
            require(std::abs(output[sample] - 0.5f * (1.0f - displayedDepth)) < 0.0001f,
                    "Rounded preset DSP differs from the displayed geometry");
        }
        juce::MemoryBlock state;
        p->getStateInformation(state);
        auto restored = std::make_unique<KickDuck1AudioProcessor>();
        restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(restored->isCurveRounded() && equalCurves(points, restored->getCurvePoints()),
                "Rounded preset did not survive a state round trip");
    }
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    parameter(*p, "amount", 100.0f);
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    auto oldState = juce::ValueTree::fromXml(*xml);
    oldState.removeProperty("curveRounded", nullptr);
    oldState.setProperty("stateVersion", 2, nullptr);
    juce::AudioProcessor::copyXmlToBinary(*oldState.createXml(), state);
    p->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(!p->isCurveRounded(), "Loading a v2 project silently changed its interpolation");
    const auto oldAudio = renderCycle(*p);
    const auto points = DuckingCurve::defaultPoints();
    for (int sample = 0; sample < 23950; sample += 31)
        require(std::abs(oldAudio[sample] - 0.5f * (1.0f - DuckingCurve::sample(points.data(),
            static_cast<int>(points.size()), sample / 24000.0f, false))) < 0.0001f,
                "Legacy linear audio changed during migration");
    p->getStateInformation(state);
    auto linear = std::make_unique<KickDuck1AudioProcessor>();
    linear->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(!linear->isCurveRounded(), "Saved linear mode was not restored");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
    for (auto* child : editor->getChildren())
        require(child->getName() != "Round curve", "ROUND button is still visible");
    require(!p->isCurveRounded(), "Opening the new GUI changed a legacy linear project");
    for (int i = 0; i < DuckingCurve::presetCount; ++i)
    {
        const auto preset = static_cast<DuckingCurve::Preset>(i);
        auto* button = dynamic_cast<juce::Button*>(namedChild(*editor,
            juce::String("Curve preset: ") + DuckingCurve::presetName(preset)));
        require(button != nullptr, "Preset thumbnail button is missing");
        button->onClick();
        require(button->getToggleState() && equalCurves(p->getCurvePoints(), DuckingCurve::presetPoints(preset)),
                "Clicking a thumbnail failed to apply/highlight its preset");
    }
    parameter(*p, "amount", 0.0f);
    p->applyCurvePreset(DuckingCurve::Preset::smooth);
    require(equalCurves(p->getCurvePoints(), DuckingCurve::presetPoints(DuckingCurve::Preset::smooth)),
            "Selecting a shape at Amount=0 erased its geometry");
    p->resetCurve();
    require(p->isCurveRounded() && p->apvts.getRawParameterValue("amount")->load() == 70.0f,
            "Reset did not restore rounded factory settings");
}

void factoryDefaultAndEndPointTest()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    require(p->apvts.getRawParameterValue("amount")->load() == 70.0f, "New instance does not default to 70% Amount");
    prepare(*p);
    require(std::abs(process(*p) - 0.15f) < 0.00001f, "70% Amount does not apply the expected gain");
    parameter(*p, "amount", 92.0f);
    juce::MemoryBlock state;
    p->getStateInformation(state);
    auto restored = std::make_unique<KickDuck1AudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored->apvts.getRawParameterValue("amount")->load() == 92.0f,
            "The new default overwrote a saved Amount value");
    // Old automation IDs retain the same X mappings after the display rename.
    parameter(*p, "release", 300.0f);
    parameter(*p, "attack", 75.0f);
    auto points = p->getCurvePoints();
    DuckingCurve::applyControls(points.data(), static_cast<int>(points.size()), 0.5f, 75.0f, 300.0f, 0.5);
    require(std::abs(points[2].x - 0.6f) < 0.00001f && std::abs(points[3].x - 0.85f) < 0.00001f,
            "Renamed controls changed existing automation semantics");

    const auto render = [](float lastDepth, int interval)
    {
        auto processor = std::make_unique<KickDuck1AudioProcessor>();
        auto shape = DuckingCurve::defaultPoints();
        shape.back().y = lastDepth;
        processor->setCurvePoints(shape);
        prepare(*processor);
        std::vector<float> output(24000);
        juce::MidiBuffer midi;
        for (int offset = 0; offset < 24000; offset += 64)
        {
            const int count = std::min(64, 24000 - offset);
            juce::AudioBuffer<float> buffer(4, count);
            for (int i = 0; i < count; ++i)
            {
                const int position = offset + i;
                const float kick = position % interval < 10 ? 0.8f : 0.0f;
                buffer.setSample(0, i, 0.5f);
                buffer.setSample(1, i, 0.5f);
                buffer.setSample(2, i, kick);
                buffer.setSample(3, i, kick);
            }
            processor->processBlock(buffer, midi);
            std::copy_n(buffer.getReadPointer(0), count, output.begin() + offset);
        }
        return output;
    };
    const auto slowBottom = render(1.0f, 24000);
    const auto slowTop = render(0.0f, 24000);
    require(slowTop[23900] > slowBottom[23900] + 0.3f, "Final point does not shape late-cycle ducking");
    const auto fastBottom = render(1.0f, 12000);
    const auto fastTop = render(0.0f, 12000);
    for (size_t i = 0; i < fastBottom.size(); ++i)
        require(std::abs(fastBottom[i] - fastTop[i]) < 0.00001f,
                "An endpoint beyond the retrigger interval affected the early part of the cycle");
}

void onsetAttackAndBassOnlyTests()
{
    struct Tempo : juce::AudioPlayHead
    {
        double bpm = 90.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo position;
            position.setBpm(bpm);
            return position;
        }
    } tempo;
    for (double rate : {44100.0, 48000.0, 192000.0})
        for (int block : {1, 17, 64, 512})
            for (float attackTime : {1.0f, 10.0f, 50.0f, 100.0f})
            {
                auto p = std::make_unique<KickDuck1AudioProcessor>();
                parameter(*p, "onsetAttack", attackTime);
                prepare(*p);
                p->prepareToPlay(rate, block);
                auto curve = p->getCurvePoints();
                DuckingCurve::applyControls(curve.data(), static_cast<int>(curve.size()),
                                           0.5f, 50.0f, 250.0f, 0.5, attackTime);
                juce::MidiBuffer midi;
                const int total = static_cast<int>(rate * (attackTime * 0.001 + 0.01));
                for (int offset = 0; offset < total; offset += block)
                {
                    const int count = std::min(block, total - offset);
                    juce::AudioBuffer<float> buffer(4, count);
                    for (int i = 0; i < count; ++i)
                    {
                        buffer.setSample(0, i, 0.5f);
                        buffer.setSample(1, i, 0.5f);
                        buffer.setSample(2, i, offset + i == 0 ? 1.0f : 0.0f);
                        buffer.setSample(3, i, 0.0f);
                    }
                    p->processBlock(buffer, midi);
                    for (int i = 0; i < count; ++i)
                    {
                        const float x = static_cast<float>((offset + i) / (rate * 0.5));
                        const float expected = 0.5f * (1.0f - 0.7f * DuckingCurve::sample(curve.data(),
                            static_cast<int>(curve.size()), x, true));
                        require(std::abs(buffer.getSample(0, i) - expected) < 0.00002f,
                                "Attack audio does not follow shared geometry across sample rates/block sizes");
                    }
                }
                juce::MemoryBlock saved;
                p->getStateInformation(saved);
                parameter(*p, "onsetAttack", 30.0f);
                p->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
                require(std::abs(p->apvts.getRawParameterValue("onsetAttack")->load() - attackTime) < 0.001f,
                        "Attack state was not restored");
            }
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    auto old = p->apvts.copyState();
    for (int i = old.getNumChildren() - 1; i >= 0; --i)
        if (old.getChild(i).getProperty("id").toString() == "onsetAttack") old.removeChild(i, nullptr);
    old.setProperty("stateVersion", 3, nullptr);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), legacy);
    parameter(*p, "onsetAttack", 40.0f);
    p->setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    require(p->apvts.getRawParameterValue("onsetAttack")->load() == 0.0f,
            "Legacy state did not retain instantaneous attack");
    auto edited = p->getCurvePoints();
    edited[0].x = 0.04f;
    p->editCurvePoints(edited);
    require(std::abs(p->apvts.getRawParameterValue("onsetAttack")->load() - 20.0f) < 0.001f
            && p->getCurvePoints()[0].x == 0.0f, "First handle did not control Attack independently of stored boundaries");
    parameter(*p, "onsetAttack", 0.0f);
    prepare(*p);
    p->setPlayHead(&tempo);
    std::array<float, TriggeredWaveform::points> input {}, output {}, kick {};
    std::uint64_t version = 0;
    double seconds = 0.0;
    p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds);
    juce::MidiBuffer midi;
    for (double bpm : {90.0, 150.0})
    {
        tempo.bpm = bpm;
        const int beat = static_cast<int>(std::round(48000.0 * 60.0 / bpm));
        for (int offset = 0; offset < beat; offset += 64)
        {
            juce::AudioBuffer<float> buffer(4, std::min(64, beat - offset));
            buffer.clear();
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample(0, i, 0.25f);
            p->processBlock(buffer, midi);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                require(buffer.getSample(0, i) == 0.25f, "Bass-only mode applied gain reduction");
        }
        require(p->copyTriggeredWaveform(input.data(), output.data(), kick.data(), version, seconds)
                && std::abs(seconds - 60.0 / bpm) < 0.000001, "Bass-only capture did not follow host BPM");
        for (int i = 0; i < TriggeredWaveform::points; ++i)
            require(input[i] == 0.25f && output[i] == 0.25f && kick[i] == 0.0f,
                    "Bass-only capture had missing samples or stale kick data");
    }
    p->setPlayHead(nullptr);
}

void bpmOfflineAndTimingLimitsTests()
{
    struct Tempo : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm(bpm);
            info.setPpqPosition(ppq);
            info.setIsPlaying(true);
            return info;
        }
    } tempo;
    for (int block : {1, 17, 64, 512})
    {
        auto p = std::make_unique<KickDuck1AudioProcessor>();
        auto layout = p->getBusesLayout();
        layout.inputBuses.set(1, juce::AudioChannelSet::disabled());
        require(p->setBusesLayout(layout), "AUX disabled layout rejected");
        p->prepareToPlay(48000.0, block);
        const auto curve = p->getCurvePoints();
        juce::MidiBuffer midi;
        for (int offset = 0; offset < 48000; offset += block)
        {
            juce::AudioBuffer<float> buffer(2, std::min(block, 48000 - offset));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i) buffer.setSample(ch, i, 0.5f);
            p->processBlock(buffer, midi);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float phase = ((offset + i) % 24000) / 24000.0f;
                const float expected = 0.5f * (1.0f - 0.7f * DuckingCurve::sample(curve.data(),
                    static_cast<int>(curve.size()), phase, true));
                require(std::abs(buffer.getSample(0, i) - expected) < 0.00005f,
                        "Disabled AUX did not repeat ducking each BPM beat independent of block size");
            }
        }
        require(!p->isSidechainConnected(), "BPM mode reported AUX online");
        p->setPlayHead(&tempo);
        for (double bpm : {90.0, 150.0})
            for (double ppq : {0.0, 0.5, 3.25, -0.75})
            {
                tempo.bpm = bpm;
                tempo.ppq = ppq;
                juce::AudioBuffer<float> buffer(2, 64);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 64; ++i) buffer.setSample(ch, i, 0.5f);
                p->processBlock(buffer, midi);
                auto effective = curve;
                DuckingCurve::applyControls(effective.data(), static_cast<int>(effective.size()),
                    0.5f, 50.0f, 250.0f, 60.0 / bpm);
                const float expected = 0.5f * (1.0f - 0.7f * DuckingCurve::sample(effective.data(),
                    static_cast<int>(effective.size()), static_cast<float>(ppq - std::floor(ppq) + 63.0 * bpm / (48000.0 * 60.0)), true));
                if (std::abs(buffer.getSample(0, 63) - expected) >= 0.00005f)
                    std::cerr << "BPM=" << bpm << " PPQ=" << ppq << " actual=" << buffer.getSample(0, 63)
                              << " expected=" << expected << " block=" << block << '\n';
                require(std::abs(buffer.getSample(0, 63) - expected) < 0.00005f,
                        "Offline BPM ducking did not track transport beat/seek/tempo");
            }
        p->setPlayHead(nullptr);
    }
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    prepare(*p);
    parameter(*p, "release", 4000.0f);
    parameter(*p, "attack", 1000.0f);
    auto legacyGeometry = p->getCurvePoints();
    DuckingCurve::applyLegacyControls(legacyGeometry.data(), static_cast<int>(legacyGeometry.size()),
                                     0.5f, 1000.0f, 4000.0f, 0.5);
    juce::MemoryBlock saved;
    p->getStateInformation(saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    auto oldState = juce::ValueTree::fromXml(*xml);
    oldState.setProperty("stateVersion", 4, nullptr);
    juce::AudioProcessor::copyXmlToBinary(*oldState.createXml(), saved);
    auto restored = std::make_unique<KickDuck1AudioProcessor>();
    restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    auto migrated = restored->getCurvePoints();
    DuckingCurve::applyControls(migrated.data(), static_cast<int>(migrated.size()),
        restored->apvts.getRawParameterValue("length")->load(),
        restored->apvts.getRawParameterValue("attack")->load(),
        restored->apvts.getRawParameterValue("release")->load(), 0.5);
    require(equalCurves(legacyGeometry, migrated), "Migration changed legacy clamped geometry");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
    auto& recover = *dynamic_cast<juce::Slider*>(namedChild(*editor, "RECOVER"));
    auto& tail = *dynamic_cast<juce::Slider*>(namedChild(*editor, "TAIL"));
    require(recover.getValue() < 500.0 && tail.getValue() < 500.0,
            "Timing controls display unreachable multi-second values");
    require(p->apvts.getRawParameterValue("release")->load() == 4000.0f
            && p->apvts.getRawParameterValue("attack")->load() == 1000.0f,
            "Opening GUI rewrote legacy automation");
    recover.setValue(4000.0, juce::sendNotificationSync);
    tail.setValue(1000.0, juce::sendNotificationSync);
    require(recover.getValue() <= recover.getMaximum() && tail.getValue() <= tail.getMaximum(),
            "Timing knobs rotated beyond neighbouring points");
    parameter(*p, "release", 250.0f);
    parameter(*p, "attack", 1.0f);
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    require(recover.getMaximum() > 490.0, "RECOVER still limited by stale stored Tail position");
    parameter(*p, "release", 300.0f);
    // The JUCE timer thread queues callbacks asynchronously; wait for the
    // resulting bound rather than assuming one callback arrives within 60 ms.
    for (int attempt = 0; attempt < 50 && tail.getMaximum() >= 200.0; ++attempt)
    {
        juce::Thread::sleep(20);
        juce::Timer::callPendingTimersSynchronously();
    }
    require(tail.getMaximum() < 200.0, "TAIL did not stop at current RECOVER point");
}

void scrollingHistoryTests()
{
    auto history = std::make_unique<WaveformHistory>(1024);
    std::array<float, 8> in {}, out {}, kick {};
    for (int i = 0; i < 128; ++i) history->push(i == 127 ? 1.0f : 0.0f, i == 127 ? 0.5f : 0.0f, 0.0f);
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false);
    require(in[7] == 1.0f && out[7] == 0.5f, "New live audio did not enter at right edge");
    for (int i = 0; i < 7; ++i) require(in[i] == 0.0f, "Startup history was not left-padded");
    for (int i = 0; i < 128; ++i) history->push(0.0f, 0.0f, 0.0f);
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false);
    require(in[6] == 1.0f && in[7] == 0.0f, "Live waveform did not scroll left between triggers");
    for (int i = 0; i < 896; ++i) history->push(0.0f, 0.0f, 0.0f);
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false);
    for (float value : in) require(value == 0.0f, "Old live samples did not leave the window");
    history->push(0.1f, 0.05f, 1.0f);
    history->push(0.9f, 0.45f, 0.0f);
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false);
    require(in[7] == 0.9f && out[7] == 0.45f, "Kick masked the bass peak in the live scope");
    history->clear();
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false);
    for (float value : in) require(value == 0.0f, "Reset exposed stale ring samples");

    auto p = std::make_unique<KickDuck1AudioProcessor>();
    prepare(*p);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < 512; offset += 64)
    {
        juce::AudioBuffer<float> buffer(4, 64);
        buffer.clear();
        for (int i = 0; i < 64; ++i)
        {
            buffer.setSample(0, i, 0.25f);
            buffer.setSample(1, i, -0.25f);
        }
        p->processBlock(buffer, midi);
    }
    p->copyWaveformSnapshot(in.data(), out.data(), kick.data(), 8, 512, false);
    for (int i = 0; i < 8; ++i)
        require(in[i] == 0.25f && out[i] == 0.25f, "Live scope cancelled opposite-phase stereo bass");
}

void longerScopeHistoryTests()
{
    for (double rate : {44100.0, 48000.0, 192000.0})
    {
        auto history = std::make_unique<WaveformHistory>(524288);
        const int second = static_cast<int>(rate);
        const int window = second * 2;
        require(history->capacity() > window, "History truncates the two-second scope window");
        for (int i = 0; i < second; ++i) history->push(0.0f, 0.0f, 0.0f);
        history->push(0.8f, 0.4f, 0.0f, 0.5f);
        for (int i = 0; i < second; ++i) history->push(0.0f, 0.0f, 0.0f);
        std::array<float, 1600> input {}, output {}, kick {}, reduction {};
        history->copyLatest(input.data(), output.data(), kick.data(), 1600, window, false, reduction.data());
        int peak = -1;
        for (int i = 0; i < 1600; ++i)
            if (output[i] == 0.4f) peak = i;
        require(peak >= 798 && peak <= 801 && reduction[peak] == 0.5f,
                "One-second-old output/GR did not remain aligned halfway across the scope");
        for (int i = 0; i < second + 1; ++i) history->push(0.0f, 0.0f, 0.0f);
        history->copyLatest(input.data(), output.data(), kick.data(), 1600, window, false, reduction.data());
        for (int i = 0; i < 1600; ++i)
            require(output[i] == 0.0f && reduction[i] == 0.0f, "Old output/GR did not leave the two-second window");
    }
}

void liveCompressionHistoryTests()
{
    auto history = std::make_unique<WaveformHistory>(1024);
    std::array<float, 8> in {}, out {}, kick {}, gr {};
    history->push(0.9f, 0.7f, 0.0f, 0.2f);
    history->push(0.1f, 0.03f, 0.0f, 0.7f);
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false, gr.data());
    require(in[7] == 0.9f && gr[7] == 0.7f,
            "Compression history was replaced by reduction at the loudest bass sample");
    for (int i = 0; i < 128; ++i) history->push(0.0f, 0.0f, 0.0f, 0.0f);
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false, gr.data());
    require(gr[6] == 0.7f && gr[7] == 0.0f, "Compression trace did not scroll with the waveform");
    history->clear();
    history->copyLatest(in.data(), out.data(), kick.data(), 8, 1024, false, gr.data());
    for (float value : gr) require(value == 0.0f, "Cleared compression history retained old reduction");

    auto p = std::make_unique<KickDuck1AudioProcessor>();
    p->setCurvePoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    parameter(*p, "amount", 80.0f);
    parameter(*p, "mix", 50.0f);
    parameter(*p, "input", -12.0f);
    parameter(*p, "output", 12.0f);
    prepare(*p);
    juce::MidiBuffer midi;
    for (int offset = 0; offset < 512; offset += 64)
    {
        juce::AudioBuffer<float> buffer(4, 64);
        buffer.clear();
        for (int i = 0; i < 64; ++i)
        {
            buffer.setSample(0, i, 0.2f);
            buffer.setSample(1, i, -0.2f);
            buffer.setSample(2, i, offset + i == 0 ? 1.0f : 0.0f);
        }
        p->processBlock(buffer, midi);
    }
    p->copyWaveformSnapshot(in.data(), out.data(), kick.data(), 8, 512, false, gr.data());
    for (int i = 0; i < 8; ++i)
        require(std::abs(gr[i] - 0.4f) < 0.00001f && std::abs(out[i] - 0.12f) < 0.00001f,
                "Live GR did not reflect actual Amount/Mix independently of Input/Output gains");
}

void stereoMeterTests()
{
    const auto db = [](double value) { return 20.0 * std::log10(value); };
    for (double rate : {44100.0, 48000.0, 192000.0})
    {
        MeterBallistics meter;
        meter.prepare(rate);
        meter.push(0.5, 0.25);
        for (int i = 0; i < static_cast<int>(rate * 0.1); ++i) meter.push(0.0, 0.0);
        require(std::abs(meter.getPeak() - 0.5f) < 0.00001f, "Peak hold did not last 100 ms");
        for (int i = 0; i < static_cast<int>(rate * 0.1); ++i) meter.push(0.0, 0.0);
        require(std::abs(db(meter.getPeak()) - (db(0.5) - 2.4)) < 0.0001,
                "Peak release did not follow 24 dB/s independently of sample rate");
        for (int mode = 0; mode < 4; ++mode)
            for (int block : {1, 17, 64, 512})
            {
                auto p = std::make_unique<KickDuck1AudioProcessor>();
                const bool mono = mode == 3;
                auto layout = p->getBusesLayout();
                layout.inputBuses.set(0, mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                layout.outputBuses.set(0, mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                layout.inputBuses.set(1, mono ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                require(p->setBusesLayout(layout), "Meter test layout rejected");
                parameter(*p, "amount", 0.0f);
                p->prepareToPlay(rate, block);
                const int total = static_cast<int>(rate * 0.3);
                juce::MidiBuffer midi;
                for (int offset = 0; offset < total; offset += block)
                {
                    juce::AudioBuffer<float> buffer(mono ? 2 : 4, std::min(block, total - offset));
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                    {
                        buffer.setSample(0, i, mode == 2 ? 0.0f : 0.5f);
                        buffer.setSample(1, i, mono ? 0.5f : (mode == 1 ? -0.5f : 0.5f));
                        if (!mono)
                        {
                            buffer.setSample(2, i, mode == 2 ? 0.0f : 0.5f);
                            buffer.setSample(3, i, mode == 1 ? -0.5f : 0.5f);
                        }
                    }
                    p->processBlock(buffer, midi);
                }
                const double power = mode == 2 ? 0.125 : 0.25;
                const double expectedRms = db(std::sqrt(power * (1.0 - std::exp(-1.0))));
                require(std::abs(p->getInputPeak() - db(0.5)) < 0.0001
                        && std::abs(p->getOutputPeak() - db(0.5)) < 0.0001,
                        "Stereo Peak cancelled phase or missed the right channel");
                require(std::abs(p->getInputRms() - expectedRms) < 0.0002
                        && std::abs(p->getOutputRms() - expectedRms) < 0.0002
                        && std::abs(p->getKickRms() - expectedRms) < 0.0002,
                        "Stereo/mono RMS used mono sum, strongest channel, or block-dependent averaging");
                p->prepareToPlay(rate, block);
                require(p->getInputPeak() == -60.0f && p->getInputRms() == -60.0f
                        && p->getMeterGainReductionDb() == 0.0f, "Prepare retained old meter levels");
            }
    }
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    p->setCurvePoints({{0.0f, 1.0f}, {1.0f, 1.0f}});
    parameter(*p, "amount", 80.0f);
    parameter(*p, "mix", 50.0f);
    prepare(*p);
    process(*p);
    require(std::abs(p->getMeterGainReductionDb() - db(0.6)) < 0.0001,
            "Display GR did not attack immediately or omitted Mix");
    parameter(*p, "mix", 0.0f);
    for (int i = 0; i < 20; ++i) process(*p);
    require(p->getGainReductionPercent() == 0.0f && p->getMeterGainReductionDb() < -0.1f,
            "GR display release altered the actual reduction or did not retain smooth decay");
    for (int i = 0; i < 1200; ++i) process(*p);
    require(std::abs(p->getMeterGainReductionDb()) < 0.001f, "Display GR did not recover to zero");
    parameter(*p, "amount", 0.0f);
    parameter(*p, "input", 12.0f);
    prepare(*p);
    process(*p);
    require(p->getInputPeak() > 5.9f, "Above-full-scale peak was hidden by the meter");
}

void concurrentPublicationTest()
{
    auto p = std::make_unique<KickDuck1AudioProcessor>();
    prepare(*p);
    parameter(*p, "amount", 100.0f);
    std::atomic<bool> start {false};
    std::atomic<bool> valid {true};
    std::thread audio([&]
    {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 4000; ++i)
        {
            const float output = process(*p);
            if (!std::isfinite(output) || output < 0.0f || output > 0.50001f) valid.store(false);
        }
    });
    std::thread writer([&]
    {
        while (!start.load()) std::this_thread::yield();
        for (int i = 0; i < 1000; ++i)
            p->setCurvePoints({{0.0f, 0.1f}, {1.0f, 0.1f}});
    });
    start.store(true);
    for (int i = 0; i < 1000; ++i)
        p->setCurvePoints({{0.0f, 0.9f}, {1.0f, 0.9f}});
    writer.join();
    audio.join();
    require(valid.load(), "Concurrent curve edits produced invalid audio");
    p->setCurvePoints({{0.0f, 0.25f}, {1.0f, 0.25f}});
    prepare(*p);
    require(std::abs(process(*p) - 0.375f) < 0.00001f, "Last curve publication lost");
}
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;
    try
    {
        stateTests();
        normalizationTests();
        headlessAndMixTests();
        lengthAffectsAudioTest();
        editorDoesNotChangeStateTest();
        allFivePointsAffectAudioTest();
        curveAndKnobsRoundTripTest();
        automatedBlockSizeTest();
        detectorAndSmoothingTests();
        triggeredFrameTests();
        editorLayoutPreviewsTest();
        roundedCurvesAndPresetsTest();
        factoryDefaultAndEndPointTest();
        onsetAttackAndBassOnlyTests();
        bpmOfflineAndTimingLimitsTests();
        scrollingHistoryTests();
        longerScopeHistoryTests();
        liveCompressionHistoryTests();
        stereoMeterTests();
        concurrentPublicationTest();
        std::cout << "PASS: state, migration, normalization, headless DSP, Mix, Length, editor lifecycle, all five depths/time handles, knob round trips, detector tails/oscillation/weak hits, parameter and curve smoothing, block invariance, concurrent curve publication\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
