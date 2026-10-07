#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    const auto bg = juce::Colour::fromRGB(8, 10, 14);
    const auto panel = juce::Colour::fromRGB(16, 19, 25);
    const auto panel2 = juce::Colour::fromRGB(21, 25, 32);
    const auto grid = juce::Colour::fromRGB(42, 47, 57);
    const auto text = juce::Colour::fromRGB(235, 239, 245);
    const auto muted = juce::Colour::fromRGB(126, 134, 148);
    const auto orange = juce::Colour::fromRGB(255, 151, 65);
    const auto blue = juce::Colour::fromRGB(75, 150, 255);
    const auto green = juce::Colour::fromRGB(67, 218, 151);
    const auto accent = juce::Colour::fromRGB(80, 226, 169);
    float clamp01(float v) { return juce::jlimit(0.0f, 1.0f, v); }
}

void KickDuck1AudioProcessorEditor::MeterStrip::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(5.0f);
    g.setColour(panel2);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(muted);
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText(title, r.withTrimmedBottom(r.getHeight() - 18.0f), juce::Justification::centred);

    auto bar = r.reduced(10.0f);
    bar.removeFromTop(22.0f); bar.removeFromBottom(24.0f);
    g.setColour(bg); g.fillRoundedRectangle(bar, 4.0f);

    constexpr int segments = 24;
    const float gap = 2.0f;
    const float sh = (bar.getHeight() - gap * (segments - 1)) / segments;
    const float norm = mode == Mode::GainReduction
        ? clamp01(level / 100.0f)
        : clamp01((level + 60.0f) / 60.0f);

    for (int i = 0; i < segments; ++i)
    {
        const float y = bar.getBottom() - (i + 1) * (sh + gap);
        auto sr = juce::Rectangle<float>(bar.getX(), y, bar.getWidth(), sh);
        if ((i + 1) / static_cast<float>(segments) <= norm)
        {
            auto c = mode == Mode::GainReduction ? accent : green;
            if (mode == Mode::Normal && i >= 20) c = orange;
            g.setColour(c);
        }
        else g.setColour(grid.withAlpha(0.7f));
        g.fillRoundedRectangle(sr, 1.6f);
    }

    g.setColour(text);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.drawText(readout, r.removeFromBottom(19.0f), juce::Justification::centred);
}

// ============================================================
// CurveDisplay
// ============================================================

KickDuck1AudioProcessorEditor::CurveDisplay::CurveDisplay()
{
    setRepaintsOnMouseActivity(true);
}

void KickDuck1AudioProcessorEditor::CurveDisplay::setPoints(const std::vector<DuckingCurve::Point>& p)
{
    points = p;
    std::sort(points.begin(), points.end(), [](auto a, auto b) { return a.x < b.x; });
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveDisplay::copyDownsampled(
    const std::vector<float>& source,
    std::array<float, maxWaveformPoints>& dest,
    int& count)
{
    count = 0;
    const int n = static_cast<int>(source.size());
    if (n <= 0) return;
    count = std::min(n, maxWaveformPoints);

    for (int i = 0; i < count; ++i)
    {
        const int a = static_cast<int>((static_cast<int64_t>(i) * n) / count);
        const int b = std::min(n, std::max(a + 1,
            static_cast<int>((static_cast<int64_t>(i + 1) * n) / count)));
        float strongest = 0.0f, representative = 0.0f;
        for (int j = a; j < b; ++j)
        {
            const float v = source[static_cast<size_t>(j)];
            if (std::isfinite(v) && std::abs(v) > strongest)
            {
                strongest = std::abs(v);
                representative = v;
            }
        }
        dest[static_cast<size_t>(i)] = representative;
    }
}

void KickDuck1AudioProcessorEditor::CurveDisplay::setWaveforms(
    const std::vector<float>& bassIn,
    const std::vector<float>& bassOut,
    const std::vector<float>& kick)
{
    copyDownsampled(bassIn, bassInWaveform, bassInCount);
    copyDownsampled(bassOut, bassOutWaveform, bassOutCount);
    copyDownsampled(kick, kickWaveform, kickCount);
    repaint();
}

void KickDuck1AudioProcessorEditor::CurveDisplay::setAmount(float v)
{
    amount = clamp01(v); repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::setPlayhead(float v)
{
    playhead = clamp01(v); repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::setCurrentDuck(float v)
{
    currentDuck = clamp01(v); repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::setSidechainConnected(bool v)
{
    sidechainConnected = v; repaint();
}

float KickDuck1AudioProcessorEditor::CurveDisplay::xToNorm(float x) const
{
    auto r = getLocalBounds().toFloat().reduced(18.0f);
    return clamp01((x - r.getX()) / std::max(1.0f, r.getWidth()));
}
float KickDuck1AudioProcessorEditor::CurveDisplay::yToNorm(float y) const
{
    auto r = getLocalBounds().toFloat().reduced(18.0f);
    return clamp01(1.0f - (y - r.getY()) / std::max(1.0f, r.getHeight()));
}
juce::Point<float> KickDuck1AudioProcessorEditor::CurveDisplay::normToPoint(float x, float y) const
{
    auto r = getLocalBounds().toFloat().reduced(18.0f);
    return {r.getX() + clamp01(x) * r.getWidth(), r.getBottom() - clamp01(y) * r.getHeight()};
}

int KickDuck1AudioProcessorEditor::CurveDisplay::findPoint(juce::Point<float> p) const
{
    int best = -1; float distance = 16.0f;
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const auto q = normToPoint(points[static_cast<size_t>(i)].x,
                                   displayedCurveValueAt(points[static_cast<size_t>(i)].x));
        const float d = q.getDistanceFrom(p);
        if (d < distance) { distance = d; best = i; }
    }
    return best;
}

float KickDuck1AudioProcessorEditor::CurveDisplay::curveValueAt(float x) const
{
    if (points.empty()) return 1.0f;
    x = clamp01(x);
    if (points.size() == 1) return clamp01(points.front().y);
    if (x <= points.front().x) return clamp01(points.front().y);
    if (x >= points.back().x) return clamp01(points.back().y);
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& a = points[i - 1]; const auto& b = points[i];
        if (x <= b.x)
        {
            const float t = clamp01((x - a.x) / std::max(0.000001f, b.x - a.x));
            return clamp01(a.y + (b.y - a.y) * t);
        }
    }
    return clamp01(points.back().y);
}

float KickDuck1AudioProcessorEditor::CurveDisplay::displayedCurveValueAt(float x) const
{
    const float raw = curveValueAt(x);
    return 1.0f - amount * (1.0f - raw);
}

void KickDuck1AudioProcessorEditor::CurveDisplay::drawWaveform(
    juce::Graphics& g,
    const std::array<float, maxWaveformPoints>& data,
    int count,
    juce::Rectangle<float> area,
    juce::Colour colour,
    float alpha) const
{
    if (count < 2) return;
    juce::Path path;
    for (int i = 0; i < count; ++i)
    {
        const float x = area.getX() + area.getWidth() * i / static_cast<float>(count - 1);
        const float v = juce::jlimit(-1.0f, 1.0f, data[static_cast<size_t>(i)]);
        const float y = area.getCentreY() - v * area.getHeight() * 0.5f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(colour.withAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

void KickDuck1AudioProcessorEditor::CurveDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(panel); g.fillRoundedRectangle(bounds, 12.0f);
    auto r = bounds.reduced(18.0f);

    // One unified graph: waveform and duck curve share exactly the same space.
    g.setColour(panel2); g.fillRoundedRectangle(r, 9.0f);

    g.setColour(grid.withAlpha(0.65f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = r.getX() + r.getWidth() * i / 8.0f;
        g.drawVerticalLine(static_cast<int>(x), r.getY(), r.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        const float y = r.getY() + r.getHeight() * i / 4.0f;
        g.drawHorizontalLine(static_cast<int>(y), r.getX(), r.getRight());
    }

    auto waveArea = r.reduced(8.0f, 25.0f);
    drawWaveform(g, kickWaveform, kickCount, waveArea, orange, 0.68f);
    drawWaveform(g, bassInWaveform, bassInCount, waveArea, blue, 0.48f);
    drawWaveform(g, bassOutWaveform, bassOutCount, waveArea, green, 0.85f);

    // Single visible user curve. Amount changes its depth directly.
    juce::Path curve;
    constexpr int samples = 256;
    for (int i = 0; i < samples; ++i)
    {
        const float x = i / static_cast<float>(samples - 1);
        const auto p = normToPoint(x, displayedCurveValueAt(x));
        if (i == 0) curve.startNewSubPath(p); else curve.lineTo(p);
    }
    g.setColour(accent.withAlpha(0.16f));
    g.strokePath(curve, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.strokePath(curve, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Playhead and current reduction marker.
    const float px = r.getX() + playhead * r.getWidth();
    g.setColour(text.withAlpha(0.28f));
    g.drawVerticalLine(static_cast<int>(px), r.getY(), r.getBottom());
    const auto marker = normToPoint(playhead, 1.0f - currentDuck);
    g.setColour(accent.withAlpha(0.20f)); g.fillEllipse(marker.x - 9, marker.y - 9, 18, 18);
    g.setColour(accent); g.fillEllipse(marker.x - 4, marker.y - 4, 8, 8);

    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const auto p = normToPoint(points[static_cast<size_t>(i)].x,
                                   displayedCurveValueAt(points[static_cast<size_t>(i)].x));
        const bool hot = i == selectedPoint || i == hoveredPoint;
        if (hot) { g.setColour(accent.withAlpha(0.20f)); g.fillEllipse(p.x-10,p.y-10,20,20); }
        g.setColour(hot ? text : accent);
        g.fillEllipse(p.x - (hot ? 5.5f : 4.0f), p.y - (hot ? 5.5f : 4.0f),
                      hot ? 11.0f : 8.0f, hot ? 11.0f : 8.0f);
    }

    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(orange); g.drawText("KICK", r.getX()+10, r.getY()+7, 45, 16, juce::Justification::left);
    g.setColour(blue); g.drawText("BASS IN", r.getX()+58, r.getY()+7, 58, 16, juce::Justification::left);
    g.setColour(green); g.drawText("BASS OUT", r.getX()+122, r.getY()+7, 68, 16, juce::Justification::left);
    g.setColour(accent); g.drawText("DUCK CURVE", r.getX()+198, r.getY()+7, 90, 16, juce::Justification::left);

    g.setColour(muted); g.setFont(juce::Font(9.0f));
    g.drawText("100%", 2, static_cast<int>(r.getY())-2, 40, 14, juce::Justification::left);
    g.drawText("50%", 2, static_cast<int>(r.getCentreY())-7, 40, 14, juce::Justification::left);
    g.drawText("0%", 2, static_cast<int>(r.getBottom())-12, 40, 14, juce::Justification::left);

    g.setColour(sidechainConnected ? accent : muted);
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText(sidechainConnected ? "SIDECHAIN ●" : "SIDECHAIN ○",
               r.getRight()-105, r.getY()+7, 95, 16, juce::Justification::right);

    if (hoveredPoint >= 0 && hoveredPoint < static_cast<int>(points.size()))
    {
        const auto& p0 = points[static_cast<size_t>(hoveredPoint)];
        const auto p = normToPoint(p0.x, displayedCurveValueAt(p0.x));
        const int w = 126, h = 42;
        const int bx = juce::jlimit(4, getWidth()-w-4, static_cast<int>(p.x+12));
        const int by = juce::jlimit(4, getHeight()-h-4, static_cast<int>(p.y-48));
        g.setColour(panel2); g.fillRoundedRectangle((float)bx,(float)by,(float)w,(float)h,6.0f);
        g.setColour(grid); g.drawRoundedRectangle((float)bx,(float)by,(float)w,(float)h,6.0f,1.0f);
        g.setColour(text); g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("POSITION  " + juce::String(p0.x*100.0f,0) + "%", bx+8,by+5,w-16,14,juce::Justification::left);
        g.setColour(accent);
        g.drawText("DUCK  " + juce::String((1.0f-displayedCurveValueAt(p0.x))*100.0f,0) + "%", bx+8,by+21,w-16,14,juce::Justification::left);
    }
}

void KickDuck1AudioProcessorEditor::CurveDisplay::mouseMove(const juce::MouseEvent& e)
{
    hoveredPoint = findPoint(e.position);
    setMouseCursor(hoveredPoint >= 0 ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
    repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::mouseExit(const juce::MouseEvent&)
{
    hoveredPoint = -1; repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::mouseDown(const juce::MouseEvent& e)
{
    selectedPoint = findPoint(e.position);
    if (e.mods.isRightButtonDown())
    {
        if (selectedPoint > 0 && selectedPoint < static_cast<int>(points.size())-1)
        { points.erase(points.begin()+selectedPoint); selectedPoint=-1; notifyPointsChanged(); repaint(); }
        return;
    }
    dragging = selectedPoint >= 0;
}
void KickDuck1AudioProcessorEditor::CurveDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging || selectedPoint < 0) return;
    float x=xToNorm(e.position.x), y=yToNorm(e.position.y);
    if (selectedPoint==0) x=0.0f;
    if (selectedPoint==static_cast<int>(points.size())-1) x=1.0f;
    if (selectedPoint>0) x=std::max(x,points[static_cast<size_t>(selectedPoint-1)].x+0.001f);
    if (selectedPoint+1<static_cast<int>(points.size())) x=std::min(x,points[static_cast<size_t>(selectedPoint+1)].x-0.001f);
    points[static_cast<size_t>(selectedPoint)].x=clamp01(x);
    points[static_cast<size_t>(selectedPoint)].y=clamp01(y);
    notifyPointsChanged(); repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::mouseUp(const juce::MouseEvent&)
{ dragging=false; }
void KickDuck1AudioProcessorEditor::CurveDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (findPoint(e.position)>=0 || points.size()>=32) return;
    points.push_back({xToNorm(e.position.x),yToNorm(e.position.y)});
    std::sort(points.begin(),points.end(),[](auto a,auto b){return a.x<b.x;});
    notifyPointsChanged(); repaint();
}
void KickDuck1AudioProcessorEditor::CurveDisplay::notifyPointsChanged()
{ if (onPointsChanged) onPointsChanged(points); }

// ============================================================
// Editor
// ============================================================

KickDuck1AudioProcessorEditor::KickDuck1AudioProcessorEditor(KickDuck1AudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1280, 780);
    setResizable(true, true);

    curveDisplay.setPoints(processor.getCurvePoints());
    curveDisplay.onPointsChanged=[this](const std::vector<DuckingCurve::Point>& p){ processor.setCurvePoints(p); };
    addAndMakeVisible(curveDisplay);

    setupKnob(inputSlider,inputLabel,"INPUT");
    setupKnob(outputSlider,outputLabel,"OUTPUT");
    setupKnob(amountSlider,amountLabel,"AMOUNT",true);
    setupKnob(lengthSlider,lengthLabel,"LENGTH");
    setupKnob(attackSlider,attackLabel,"ATTACK");
    setupKnob(releaseSlider,releaseLabel,"RELEASE");
    setupKnob(mixSlider,mixLabel,"MIX");

    amountAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"amount",amountSlider);
    lengthAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"length",lengthSlider);
    attackAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"attack",attackSlider);
    releaseAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"release",releaseSlider);
    mixAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"mix",mixSlider);
    inputAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"input",inputSlider);
    outputAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,"output",outputSlider);

    inputMeter.setTitle("INPUT"); outputMeter.setTitle("OUTPUT"); grMeter.setTitle("GAIN REDUCTION");
    addAndMakeVisible(inputMeter); addAndMakeVisible(outputMeter); addAndMakeVisible(grMeter);

    addAndMakeVisible(sidechainStatus); addAndMakeVisible(grValue);
    sidechainStatus.setFont(juce::Font(10.5f,juce::Font::bold));
    sidechainStatus.setJustificationType(juce::Justification::centredLeft);
    grValue.setFont(juce::Font(20.0f,juce::Font::bold));
    grValue.setColour(juce::Label::textColourId,accent);
    grValue.setJustificationType(juce::Justification::centredRight);

    auto setupValue=[this](juce::Label& l){ addAndMakeVisible(l); l.setFont(juce::Font(13.0f,juce::Font::bold)); l.setColour(juce::Label::textColourId,text); l.setJustificationType(juce::Justification::centred); };
    setupValue(amountValue); setupValue(lengthValue); setupValue(attackValue); setupValue(releaseValue); setupValue(mixValue); setupValue(inputValue); setupValue(outputValue);

    waveformBassInBuffer.resize(4096);
    waveformBassOutBuffer.resize(4096);
    waveformKickBuffer.resize(4096);
    startTimerHz(24);
}

void KickDuck1AudioProcessorEditor::setupKnob(juce::Slider& s, juce::Label& l, const juce::String& name, bool major)
{
    addAndMakeVisible(s); addAndMakeVisible(l);
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
    s.setColour(juce::Slider::rotarySliderFillColourId,major?accent:juce::Colour::fromRGB(92,101,114));
    s.setColour(juce::Slider::rotarySliderOutlineColourId,grid);
    s.setColour(juce::Slider::thumbColourId,text);
    l.setText(name,juce::dontSendNotification); l.setFont(juce::Font(9.5f,juce::Font::bold));
    l.setColour(juce::Label::textColourId,muted); l.setJustificationType(juce::Justification::centred);
}

juce::String KickDuck1AudioProcessorEditor::formatDb(float v)
{ return juce::String(juce::jmax(-60.0f,std::isfinite(v)?v:-60.0f),1)+" dB"; }
juce::String KickDuck1AudioProcessorEditor::formatPercent(float v)
{ return juce::String(juce::jlimit(0.0f,100.0f,std::isfinite(v)?v:0.0f),1)+"%"; }
juce::String KickDuck1AudioProcessorEditor::formatLength(float v)
{
    if(v<=0.18f)return "1/8"; if(v<=0.34f)return "1/4"; if(v<=0.50f)return "3/8";
    if(v<=0.75f)return "1/2"; if(v<=1.05f)return "1"; if(v<=1.55f)return "1.5"; return "2";
}

void KickDuck1AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto outer=getLocalBounds().toFloat().reduced(10.0f);
    g.setColour(panel); g.fillRoundedRectangle(outer,14.0f);

    g.setColour(text); g.setFont(juce::Font(25.0f,juce::Font::bold));
    g.drawText("KICKDUCK 1",28,16,250,32,juce::Justification::left);
    g.setColour(muted); g.setFont(juce::Font(10.5f,juce::Font::bold));
    g.drawText("SIDECHAIN DUCKING / BASS SHAPER",30,46,280,17,juce::Justification::left);
    g.drawText("REAL AUX  •  LIVE WAVEFORM  •  EDITABLE CURVE",330,46,340,17,juce::Justification::left);
    g.setColour(grid); g.drawHorizontalLine(72,25.0f,(float)getWidth()-25.0f);

    g.setColour(muted); g.setFont(juce::Font(10.0f,juce::Font::bold));
    g.drawText("A/B",getWidth()-150,27,32,18,juce::Justification::centred);
    g.drawText("BYPASS",getWidth()-100,27,65,18,juce::Justification::centred);

    auto controls=getLocalBounds().removeFromBottom(150).reduced(20,8);
    g.setColour(panel2); g.fillRoundedRectangle(controls.toFloat(),10.0f);
    g.setColour(grid); g.drawHorizontalLine(controls.getY(),controls.getX()+8,controls.getRight()-8);
    g.setColour(muted); g.setFont(juce::Font(9.0f));
    g.drawText("DRY",controls.getX()+15,controls.getBottom()-18,35,14,juce::Justification::left);
    g.drawText("DUCKED",controls.getRight()-55,controls.getBottom()-18,45,14,juce::Justification::right);
}

void KickDuck1AudioProcessorEditor::resized()
{
    auto area=getLocalBounds(); area.removeFromTop(82);
    auto controls=area.removeFromBottom(150).reduced(20,8);
    auto meters=area.removeFromRight(232).reduced(10,0);
    auto graph=area.reduced(10,0);
    curveDisplay.setBounds(graph);

    auto m=meters.reduced(8,24); const int gap=7; const int w=(m.getWidth()-gap*2)/3;
    auto a=m.removeFromLeft(w); m.removeFromLeft(gap); auto b=m.removeFromLeft(w); m.removeFromLeft(gap); auto c=m;
    inputMeter.setBounds(a); outputMeter.setBounds(b); grMeter.setBounds(c);
    sidechainStatus.setBounds(meters.getX()+10,meters.getBottom()-35,meters.getWidth()-95,22);
    grValue.setBounds(meters.getRight()-88,meters.getBottom()-37,78,24);

    const int cell=controls.getWidth()/7;
    auto place=[&](juce::Rectangle<int> r,juce::Slider& s,juce::Label& l,juce::Label& v,int size){ l.setBounds(r.removeFromTop(22)); s.setBounds(r.withSizeKeepingCentre(size,size+8)); v.setBounds(r.removeFromBottom(22)); };
    auto c0=controls.removeFromLeft(cell),c1=controls.removeFromLeft(cell),c2=controls.removeFromLeft(cell),c3=controls.removeFromLeft(cell),c4=controls.removeFromLeft(cell),c5=controls.removeFromLeft(cell),c6=controls;
    place(c0,inputSlider,inputLabel,inputValue,58); place(c1,outputSlider,outputLabel,outputValue,58); place(c2,amountSlider,amountLabel,amountValue,78); place(c3,lengthSlider,lengthLabel,lengthValue,58); place(c4,attackSlider,attackLabel,attackValue,58); place(c5,releaseSlider,releaseLabel,releaseValue,58); place(c6,mixSlider,mixLabel,mixValue,58);
}

void KickDuck1AudioProcessorEditor::timerCallback()
{
    updateWaveform(); updateMeters();
    const float length=processor.apvts.getRawParameterValue("length")->load();
    double bpm=120.0;
    if(auto* ph=processor.getPlayHead()) if(auto pos=ph->getPosition()) if(pos->getBpm().hasValue()) bpm=*pos->getBpm();
    if(!std::isfinite(bpm)||bpm<=0.0)bpm=120.0;
    const float cycle=static_cast<float>(std::max(0.05,length/(bpm/60.0)));
    visualPhase+=static_cast<float>((1.0/24.0)/cycle); if(visualPhase>=1.0f)visualPhase-=std::floor(visualPhase);
    curveDisplay.setPlayhead(visualPhase);
}

void KickDuck1AudioProcessorEditor::updateMeters()
{
    const float in=processor.getInputPeak(), out=processor.getOutputPeak(), gr=processor.getGainReductionPercent(), grDb=processor.getGainReductionDb();
    inputMeter.setLevel(in); inputMeter.setText(formatDb(in)); outputMeter.setLevel(out); outputMeter.setText(formatDb(out)); grMeter.setLevel(gr); grMeter.setText(formatDb(grDb));
    grValue.setText(formatDb(grDb),juce::dontSendNotification);
    const bool sc=processor.isSidechainConnected();
    sidechainStatus.setText(sc?"●  SIDECHAIN CONNECTED":"○  SIDECHAIN NO SIGNAL",juce::dontSendNotification);
    sidechainStatus.setColour(juce::Label::textColourId,sc?accent:muted);

    const float amount=processor.apvts.getRawParameterValue("amount")->load();
    const float length=processor.apvts.getRawParameterValue("length")->load();
    const float attack=processor.apvts.getRawParameterValue("attack")->load();
    const float release=processor.apvts.getRawParameterValue("release")->load();
    const float mix=processor.apvts.getRawParameterValue("mix")->load();
    const float input=processor.apvts.getRawParameterValue("input")->load();
    const float output=processor.apvts.getRawParameterValue("output")->load();
    amountValue.setText(formatPercent(amount),juce::dontSendNotification); lengthValue.setText(formatLength(length),juce::dontSendNotification);
    attackValue.setText(juce::String(attack,0)+" ms",juce::dontSendNotification); releaseValue.setText(juce::String(release,0)+" ms",juce::dontSendNotification);
    mixValue.setText(formatPercent(mix),juce::dontSendNotification); inputValue.setText(juce::String(input,1)+" dB",juce::dontSendNotification); outputValue.setText(juce::String(output,1)+" dB",juce::dontSendNotification);
    curveDisplay.setAmount(amount/100.0f); curveDisplay.setCurrentDuck(processor.getCurrentDuck()); curveDisplay.setSidechainConnected(sc);
}

void KickDuck1AudioProcessorEditor::updateWaveform()
{
    // Deliberately bounded. The previous GUI requested ~1.5 s / 88200 samples
    // every timer tick. We only transfer 4096 samples and immediately downsample
    // them into fixed-size arrays owned by CurveDisplay.
    constexpr int guiSamples=4096;
    processor.copyWaveformHistory(waveformBassInBuffer,waveformBassOutBuffer,waveformKickBuffer,guiSamples);
    curveDisplay.setWaveforms(waveformBassInBuffer,waveformBassOutBuffer,waveformKickBuffer);
}
