#include "PeakMeter.h"

#include <cmath>

namespace
{
juce::Colour panel() { return juce::Colour::fromRGB(22, 26, 33); }
juce::Colour border() { return juce::Colour::fromRGB(65, 70, 80); }
juce::Colour text() { return juce::Colour::fromRGB(235, 238, 242); }
juce::Colour muted() { return juce::Colour::fromRGB(145, 150, 160); }
juce::Colour normalFill() { return juce::Colour::fromRGB(67, 218, 151); }
juce::Colour grFill() { return juce::Colour::fromRGB(255, 151, 65); }
}

PeakMeter::PeakMeter(Mode m) : mode(m)
{
    setOpaque(false);
}

void PeakMeter::setLevel(float valueDb) noexcept
{
    if (!std::isfinite(valueDb))
        valueDb = -60.0f;

    levelDb = juce::jlimit(-60.0f, 0.0f, valueDb);
    repaint();
}

void PeakMeter::setLabel(const juce::String& text)
{
    label = text;
    repaint();
}

void PeakMeter::setUnit(const juce::String& text)
{
    unit = text;
    repaint();
}

void PeakMeter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(panel());
    g.fillRoundedRectangle(b, 9.0f);
    g.setColour(border());
    g.drawRoundedRectangle(b.reduced(0.5f), 9.0f, 1.0f);

    g.setColour(text());
    g.setFont(juce::Font(juce::FontOptions()
                             .withHeight(12.5f)
                             .withStyle("Bold")));
    g.drawText(label, 6, 7, getWidth() - 12, 20, juce::Justification::centred);

    g.setColour(muted());
    g.setFont(10.0f);
    g.drawText(unit, 6, 27, getWidth() - 12, 16, juce::Justification::centred);

    auto bar = b.reduced(10.0f, 48.0f);
    bar.removeFromTop(4.0f);

    // Thick, high-contrast bar instead of a thin meter line.
    g.setColour(juce::Colour::fromRGB(9, 12, 16));
    g.fillRoundedRectangle(bar, 5.0f);

    const float normalized = juce::jlimit(0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
    const float filledHeight = bar.getHeight() * normalized;
    auto filled = bar.withTop(bar.getBottom() - filledHeight);

    g.setColour(mode == Mode::GainReduction ? grFill() : normalFill());
    g.fillRoundedRectangle(filled, 5.0f);

    g.setColour(juce::Colour::fromRGB(65, 70, 80));
    g.drawRoundedRectangle(bar, 5.0f, 1.0f);

    // Readable scale.
    g.setFont(9.0f);
    g.setColour(muted());
    const float scaleX = bar.getRight() + 3.0f;
    const float h = bar.getHeight();
    const int marks[] = { 0, -6, -12, -24, -36, -48, -60 };
    for (int db : marks)
    {
        const float y = bar.getBottom() - ((db + 60.0f) / 60.0f) * h;
        g.drawText(juce::String(db), static_cast<int>(scaleX), static_cast<int>(y - 6.0f),
                   30, 12, juce::Justification::left);
    }

    const juce::String value = juce::String(levelDb, 1);
    g.setColour(text());
    g.setFont(juce::Font(juce::FontOptions()
                             .withHeight(15.0f)
                             .withStyle("Bold")));
    g.drawText(value + " dB", 4, getHeight() - 29, getWidth() - 8, 22,
               juce::Justification::centred);
}
