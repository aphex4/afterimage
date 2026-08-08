#include "ModeSelector.h"
#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"
#include "AfterimageTooltips.h"

#include <cmath>

namespace
{
constexpr int kNumModes = 2; // Shadow / Erase (Merge removed from product)
}

ModeSelector::ModeSelector()
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (afterimage::tooltips::shadow);
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
        case afterimage::SpectralMode::Erase:  return 1;
        case afterimage::SpectralMode::Shadow:
        case afterimage::SpectralMode::Merge:  // legacy → display as Shadow
        default: return 0;
    }
}

juce::Rectangle<float> ModeSelector::segmentBounds (int index) const noexcept
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float w = r.getWidth() / (float) kNumModes;
    return { r.getX() + w * (float) index, r.getY(), w, r.getHeight() };
}

afterimage::SpectralMode ModeSelector::modeAt (juce::Point<float> p) const noexcept
{
    for (int i = 0; i < kNumModes; ++i)
        if (segmentBounds (i).contains (p))
            return i == 1 ? afterimage::SpectralMode::Erase
                          : afterimage::SpectralMode::Shadow;
    return currentMode_;
}

void ModeSelector::setMode (afterimage::SpectralMode mode)
{
    if (mode == afterimage::SpectralMode::Merge)
        mode = afterimage::SpectralMode::Shadow;
    currentMode_ = mode;
    if (hoverIndex_ < 0)
        setTooltip (afterimage::tooltips::modeFor (mode));
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
        setTooltip (afterimage::tooltips::modeFor (h == 1 ? afterimage::SpectralMode::Erase
                                                          : afterimage::SpectralMode::Shadow));
        repaint();
    }
}

void ModeSelector::mouseExit (const juce::MouseEvent&)
{
    hoverIndex_ = -1;
    setTooltip (afterimage::tooltips::modeFor (currentMode_));
    repaint();
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    const auto mode = modeAt (e.position);
    if (mode == currentMode_)
        return;
    currentMode_ = mode;
    setTooltip (afterimage::tooltips::modeFor (mode));
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
        idx = juce::jmin (kNumModes - 1, idx + 1);
    else
        return false;

    const auto mode = idx == 1 ? afterimage::SpectralMode::Erase
                               : afterimage::SpectralMode::Shadow;
    if (mode != currentMode_)
    {
        currentMode_ = mode;
        setTooltip (afterimage::tooltips::modeFor (mode));
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

    {
        const float segW = (bounds.getWidth() - 4.0f) / (float) kNumModes;
        auto pill = juce::Rectangle<float> (bounds.getX() + 2.0f + animPos_ * segW,
                                            bounds.getY() + 2.0f,
                                            segW,
                                            bounds.getHeight() - 4.0f);
        g.setColour (AfterimageLookAndFeel::accentViolet().withAlpha (0.32f));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.55f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 6.0f, 1.0f);
    }

    static constexpr const char* labels[] = { "SHADOW", "ERASE" };
    g.setFont (AfterimageFonts::get (AfterimageFontRole::Mode));

    for (int i = 0; i < kNumModes; ++i)
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
