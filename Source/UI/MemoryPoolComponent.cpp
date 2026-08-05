#include "MemoryPoolComponent.h"
#include "AfterimageLookAndFeel.h"
#include "../Utilities/Constants.h"

MemoryPoolComponent::MemoryPoolComponent()
{
    startTimerHz (afterimage::constants::uiTimerHz);
}

MemoryPoolComponent::~MemoryPoolComponent()
{
    stopTimer();
}

void MemoryPoolComponent::setMode (afterimage::SpectralMode mode) { mode_ = mode; }
void MemoryPoolComponent::setFrozen (bool frozen) { frozen_ = frozen; }
void MemoryPoolComponent::setRecallPosition (float age01) { recallPosition_ = juce::jlimit (0.0f, 1.0f, age01); }
void MemoryPoolComponent::setInfluence (float influence01) { influence_ = juce::jlimit (0.0f, 1.0f, influence01); }
void MemoryPoolComponent::setInputLevel (float level01) { inputLevel_ = juce::jlimit (0.0f, 1.0f, level01); }
void MemoryPoolComponent::setOutputLevel (float level01) { outputLevel_ = juce::jlimit (0.0f, 1.0f, level01); }
void MemoryPoolComponent::setHistoryFill (float fill01) { historyFill_ = juce::jlimit (0.0f, 1.0f, fill01); }

void MemoryPoolComponent::timerCallback()
{
    if (! frozen_)
        phase_ += 0.02f + influence_ * 0.03f;
    repaint();
}

void MemoryPoolComponent::resized() {}

void MemoryPoolComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    juce::ColourGradient radial (AfterimageLookAndFeel::panel().brighter (0.05f),
                                 bounds.getCentreX(), bounds.getCentreY(),
                                 AfterimageLookAndFeel::background(),
                                 bounds.getCentreX(),
                                 bounds.getY() - 40.0f,
                                 true);
    g.setGradientFill (radial);
    g.fillRoundedRectangle (bounds, 10.0f);

    g.setColour (AfterimageLookAndFeel::panelEdge());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    const auto centre = bounds.getCentre();
    const float maxR = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;

    // Faint spectral history rings (placeholder — Phase 7 uses real frames).
    for (int ring = 0; ring < 8; ++ring)
    {
        const float t = static_cast<float> (ring) / 7.0f;
        const float radius = maxR * (0.25f + t * 0.75f);
        const float alpha = (0.04f + 0.08f * (1.0f - t)) * (0.4f + influence_);

        juce::Colour c = AfterimageLookAndFeel::accentCyan();
        if (mode_ == afterimage::SpectralMode::Erase)
            c = AfterimageLookAndFeel::textMuted();
        else if (mode_ == afterimage::SpectralMode::Merge)
            c = AfterimageLookAndFeel::accentViolet();

        g.setColour (c.withAlpha (alpha));

        juce::Path arc;
        const float start = phase_ + t * juce::MathConstants<float>::twoPi * 0.3f;
        const float sweep = juce::MathConstants<float>::pi * (0.6f + inputLevel_ * 0.8f);
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, start + sweep, true);
        g.strokePath (arc, juce::PathStrokeType (1.2f + outputLevel_ * 2.0f));
    }

    // Recall position cursor (normalized age → ring radius).
    {
        const float r = maxR * (0.3f + recallPosition_ * 0.7f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.55f));
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.5f);

        const float angle = -juce::MathConstants<float>::halfPi + recallPosition_ * juce::MathConstants<float>::twoPi;
        const juce::Point<float> tip (centre.x + r * std::cos (angle),
                                      centre.y + r * std::sin (angle));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (tip));
    }

    g.setColour (AfterimageLookAndFeel::textMuted().withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MEMORY POOL", bounds.reduced (16.0f).removeFromTop (18.0f),
                juce::Justification::topLeft, false);

    {
        auto fillBar = bounds.reduced (16.0f).removeFromBottom (6.0f);
        g.setColour (AfterimageLookAndFeel::meterTrack());
        g.fillRoundedRectangle (fillBar, 2.0f);
        g.setColour ((frozen_ ? AfterimageLookAndFeel::accentWarm()
                              : AfterimageLookAndFeel::accentCyan()).withAlpha (0.7f));
        g.fillRoundedRectangle (fillBar.withWidth (fillBar.getWidth() * historyFill_), 2.0f);
    }

    if (frozen_)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
        g.drawText ("FROZEN", bounds.reduced (16.0f).removeFromTop (18.0f),
                    juce::Justification::topRight, false);
    }

    g.setColour (AfterimageLookAndFeel::textMuted().withAlpha (0.45f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText ("Spectral history active — modes arrive in Phase 4",
                      bounds.reduced (16.0f).removeFromBottom (28).toNearestInt(),
                      juce::Justification::centred, 1);
}
