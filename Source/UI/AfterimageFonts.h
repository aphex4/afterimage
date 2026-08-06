#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Centralized AFTERIMAGE typography.

    Uses host system geometric sans fonts (prefer Avenir Next / Helvetica Neue
    on Apple platforms, with JUCE default fallback). No embedded commercial fonts.
    See Assets/Fonts/README.md for licensing notes.
*/
enum class AfterimageFontRole
{
    Wordmark,
    Mode,
    ControlLabel,
    ParameterValue,
    TooltipTitle,
    TooltipBody,
    Status,
    Caption
};

namespace AfterimageFonts
{
    /** Preferred typeface name for AFTERIMAGE UI (system-installed). */
    [[nodiscard]] juce::String preferredTypefaceName();

    [[nodiscard]] juce::Font get (AfterimageFontRole role);
    [[nodiscard]] float height (AfterimageFontRole role) noexcept;
}
