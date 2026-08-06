#include "ModeSelector.h"
#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"

#include <cmath>

ModeSelector::ModeSelector()
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("MODE\n"
                "SHADOW - Adds recalled harmonics behind the current sound.\n"
                "ERASE - Removes spectral material the sound has repeated.\n"
                "MERGE - Transfers recalled spectral identity onto the present.");
    startTimerHz (60);
}

ModeSelector::~ModeSelector()
{
    stopTimer();
}

int ModeSelector::modeIndex (afterimage::SpectralMode mode) const noexcept
{
    switch (mode)
    {
        case afterimage::SpectralMode::Shadow: return 0;
        case afterimage::SpectralMode::Erase:  return 1;
        case afterimage::SpectralMode::Merge:  return 2;
    }
    return 0;
}

juce::Rectangle<float> ModeSelector::segmentBounds (int index) const noexcept
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float w = r.getWidth() / 3.0f;
    return { r.getX() + w * (float) index, r.getY(), w, r.getHeight() };
}

afterimage::SpectralMode ModeSelector::modeAt (juce::Point<float> p) const noexcept
{
    for (int i = 0; i < 3; ++i)
        if (segmentBounds (i).contains (p))
            return static_cast<afterimage::SpectralMode> (i);
    return currentMode_;
}

void ModeSelector::setMode (afterimage::SpectralMode mode)
{
    currentMode_ = mode;
    repaint();
}

void ModeSelector::resized() {}

void ModeSelector::timerCallback()
{
    const float target = (float) modeIndex (currentMode_);
    const float next = animPos_ + (target - animPos_) * 0.22f;
    if (std::abs (next - animPos_) > 0.001f)
    {
        animPos_ = next;
        repaint();
    }
    else if (std::abs (animPos_ - target) > 0.0001f)
    {
        animPos_ = target;
        repaint();
    }
}

void ModeSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = modeIndex (modeAt (e.position));
    if (h != hoverIndex_)
    {
        hoverIndex_ = h;
        repaint();
    }
}

void ModeSelector::mouseExit (const juce::MouseEvent&)
{
    hoverIndex_ = -1;
    repaint();
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    const auto mode = modeAt (e.position);
    if (mode == currentMode_)
        return;
    currentMode_ = mode;
    if (onModeChanged)
        onModeChanged (mode);
    repaint();
}

bool ModeSelector::keyPressed (const juce::KeyPress& key)
{
    int idx = modeIndex (currentMode_);
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
        idx = juce::jmax (0, idx - 1);
    else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
        idx = juce::jmin (2, idx + 1);
    else
        return false;

    const auto mode = static_cast<afterimage::SpectralMode> (idx);
    if (mode != currentMode_)
    {
        currentMode_ = mode;
        if (onModeChanged)
            onModeChanged (mode);
        repaint();
    }
    return true;
}

void ModeSelector::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (AfterimageLookAndFeel::panel().brighter (0.03f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (AfterimageLookAndFeel::panelEdge());
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    // Selection pill
    {
        const float segW = (bounds.getWidth() - 4.0f) / 3.0f;
        auto pill = juce::Rectangle<float> (bounds.getX() + 2.0f + animPos_ * segW,
                                            bounds.getY() + 2.0f,
                                            segW,
                                            bounds.getHeight() - 4.0f);
        g.setColour (AfterimageLookAndFeel::accentViolet().withAlpha (0.32f));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.55f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 6.0f, 1.0f);
    }

    static constexpr const char* labels[] = { "SHADOW", "ERASE", "MERGE" };
    g.setFont (AfterimageFonts::get (AfterimageFontRole::Mode));

    for (int i = 0; i < 3; ++i)
    {
        const auto seg = segmentBounds (i);
        const bool selected = modeIndex (currentMode_) == i;
        const bool hovered = hoverIndex_ == i;

        if (i > 0)
        {
            g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.55f));
            g.drawLine (seg.getX(), seg.getY() + 8.0f, seg.getX(), seg.getBottom() - 8.0f, 1.0f);
        }

        g.setColour (selected ? AfterimageLookAndFeel::textPrimary()
                              : (hovered ? AfterimageLookAndFeel::textPrimary().withAlpha (0.75f)
                                         : AfterimageLookAndFeel::textMuted()));
        g.drawFittedText (labels[i], seg.toNearestInt(), juce::Justification::centred, 1);
    }

    if (hasKeyboardFocus (true))
    {
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.45f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.2f);
    }
}
