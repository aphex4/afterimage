#pragma once

#include "../DSP/SpectralModes.h"

#include <juce_core/juce_core.h>

/**
    Canonical AFTERIMAGE tooltip copy.
    First line = title (UPPERCASE control name). Remaining lines = body.
*/
namespace afterimage::tooltips
{
inline constexpr const char* unavailable =
    "This control is currently unavailable.";

inline constexpr const char* afterimage =
    "AFTERIMAGE\n"
    "Remembers the last few seconds of audio and uses that memory to reshape new sounds.";

inline constexpr const char* memory =
    "MEMORY\n"
    "Sets how many seconds of audio the plugin remembers.\n"
    "Higher values let you recall older sounds.";

inline constexpr const char* recall =
    "RECALL\n"
    "Chooses which moment in memory the plugin uses.\n"
    "Move deeper into the Memory Well to recall older audio.";

inline constexpr const char* influenceShadow =
    "INFLUENCE\n"
    "Controls how strongly the remembered sound is added to the current sound.\n"
    "Higher values create a stronger ghost effect.";

inline constexpr const char* influenceMerge =
    "INFLUENCE\n"
    "Controls how much the current sound takes on the tonal character of the remembered sound.\n"
    "Higher values create a stronger transformation.";

inline constexpr const char* influenceErase =
    "INFLUENCE\n"
    "Controls how strongly repeated frequencies are removed.\n"
    "Higher values create a more obvious hollowing effect.";

inline constexpr const char* forget =
    "FORGET\n"
    "Makes older remembered sounds fade faster.\n"
    "Lower values preserve older memories longer.";

inline constexpr const char* blur =
    "BLUR\n"
    "Smooths the remembered frequencies before they are applied.\n"
    "Higher values create a softer, less detailed effect.";

inline constexpr const char* transientPreserve =
    "TRANSIENT PRESERVE\n"
    "Keeps attacks like drums, plucks, and consonants clear.\n"
    "Higher values reduce the effect during sharp transients.";

inline constexpr const char* random =
    "RANDOM\n"
    "Randomly changes the recalled moment over time.\n"
    "Higher values create a less predictable effect.";

inline constexpr const char* mix =
    "MIX\n"
    "Blends the original and processed signals.\n"
    "0% is dry. 100% is fully processed.";

inline constexpr const char* output =
    "OUTPUT\n"
    "Adjusts the final output level.\n"
    "Use this to match volume after processing.";

inline constexpr const char* gainMatch =
    "GAIN MATCH\n"
    "Automatically matches the processed level to the original.\n"
    "This helps compare the effect without being influenced by loudness.";

inline constexpr const char* freeze =
    "FREEZE\n"
    "Stops updating the remembered audio.\n"
    "The current memory stays available until Freeze is turned off.";

inline constexpr const char* bypass =
    "BYPASS\n"
    "Turns processing on or off while keeping latency aligned.\n"
    "Use this for an accurate A/B comparison.";

inline constexpr const char* shadow =
    "SHADOW\n"
    "Adds remembered harmonics behind the current sound.\n"
    "Use it to create ghost-like layers and evolving textures.";

inline constexpr const char* merge =
    "MERGE\n"
    "Transfers the tonal character of the remembered sound onto the current sound.\n"
    "It reshapes the sound without replaying the original audio.";

inline constexpr const char* erase =
    "ERASE\n"
    "Removes frequencies that the plugin recognizes as familiar.\n"
    "Repeated sounds become thinner while new material stays clear.";

inline constexpr const char* memoryWell =
    "MEMORY WELL\n"
    "Shows the audio currently stored in memory.\n"
    "Drag the Recall Ring to choose which moment is used.";

inline constexpr const char* recallRing =
    "RECALL RING\n"
    "Drag to choose a different point in memory.\n"
    "The ring moves from newer audio at the edge to older audio near the center.";

inline constexpr const char* inputMeter =
    "INPUT METER\n"
    "Shows the level entering the plugin.\n"
    "Avoid clipping before processing.";

inline constexpr const char* outputMeter =
    "OUTPUT METER\n"
    "Shows the level leaving the plugin.\n"
    "Use it to monitor the final output level.";

inline constexpr const char* preset =
    "PRESET\n"
    "Loads a factory preset.\n"
    "Presets store parameter values only. Live memory is not saved.";

inline constexpr const char* license =
    "LICENSE\n"
    "Shows the current activation status.\n"
    "Activate the plugin or view license information.";

inline constexpr const char* trial =
    "TRIAL\n"
    "Shows the remaining trial period.\n"
    "Activation removes trial limitations.";

inline constexpr const char* memoryStatus =
    "MEMORY STATUS\n"
    "Shows how much of the available memory is currently filled.\n"
    "The memory fills as audio is played.";

[[nodiscard]] inline const char* influenceForMode (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Erase:  return influenceErase;
        case SpectralMode::Merge:  return influenceMerge;
        case SpectralMode::Shadow: return influenceShadow;
    }
    return influenceShadow;
}

[[nodiscard]] inline const char* modeFor (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Erase:  return erase;
        case SpectralMode::Merge:  return merge;
        case SpectralMode::Shadow: return shadow;
    }
    return shadow;
}
} // namespace afterimage::tooltips
