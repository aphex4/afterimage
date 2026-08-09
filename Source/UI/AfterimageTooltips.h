#pragma once

#include "../DSP/SpectralModes.h"

#include <juce_core/juce_core.h>

/**
    Canonical AFTERIMAGE tooltip copy.
    First line = title (UPPERCASE control name). Remaining lines = body.
*/
namespace afterimage::tooltips
{
/** Do not attach to working controls — reserved for truly disabled UI. */
inline constexpr const char* unavailable =
    "This control is currently unavailable.";

inline constexpr const char* harmonicsColor =
    "COLOR\n"
    "How strongly in-key harmonics are accented.\n"
    "0% is off. Past 100% adds extra in-key resonance.";

inline constexpr const char* harmonicsTransient =
    "TRANSIENT\n"
    "Keeps attacks clearer while HARMONICS accents sustained tone.";

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

inline constexpr const char* influenceErase =
    "INFLUENCE\n"
    "Controls how strongly repeated frequencies are removed.\n"
    "Higher values create a more obvious hollowing effect.";

inline constexpr const char* forget =
    "FORGET\n"
    "Makes older remembered sounds fade faster.\n"
    "Lower values preserve older memories longer.";

inline constexpr const char* blurShadow =
    "BLUR\n"
    "Spreads the remembered ghost across nearby frequencies.\n"
    "Higher values make the spectral tail softer and more diffuse.";

inline constexpr const char* blurErase =
    "BLUR\n"
    "Widens the frequencies removed by Erase.\n"
    "Higher values carve broader, less precise hollows.";

inline constexpr const char* blur =
    "BLUR\n"
    "Smooths remembered frequencies before they are applied.\n"
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
    "Captures a short, stabilized moment of recent audio and holds it as the current memory.";

inline constexpr const char* bypass =
    "BYPASS\n"
    "Turns processing on or off while keeping latency aligned.\n"
    "Use this for an accurate A/B comparison.";

inline constexpr const char* shadow =
    "SHADOW\n"
    "Creates a smooth spectral tail from earlier audio. It behaves like a delay or reverb made from remembered frequencies.";

inline constexpr const char* erase =
    "ERASE\n"
    "Reduces frequencies that keep repeating over time. New material stays clearer while familiar content is gradually removed.";

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
    "Shows the latency-aligned dry level (the Gain Match reference).\n"
    "Avoid clipping before processing.";

inline constexpr const char* outputMeter =
    "OUTPUT METER\n"
    "Shows the final audible output after Match, Bypass, and Output Gain.\n"
    "With Match on, this should agree with the dry reference level.";

inline constexpr const char* reverb =
    "REVERB\n"
    "Conventional algorithmic reverb after the spectral Mix.\n"
    "Use a little wet to smooth grainy Shadow tails.";

inline constexpr const char* reverbWet =
    "REVERB WET\n"
    "Blends the post-chain reverb with the dry spectral mix.\n"
    "0% is off. Modest values smooth Shadow tails.";

inline constexpr const char* reverbType =
    "REVERB TYPE\n"
    "Spring, Hall, or Room character for the post-chain reverb.";

inline constexpr const char* formant =
    "FORMANT\n"
    "Shifts vowel-like tone colour from Low to High.\n"
    "Centre is neutral. Conventional filter-bank colour, not a pitch shifter.";

inline constexpr const char* deEsser =
    "DE-ESSER\n"
    "Reduces harsh high frequencies dynamically.\n"
    "Intensity only — higher values duck sibilance more.";

inline constexpr const char* preEq =
    "PRE EQ\n"
    "Shapes the signal entering the reverb.\n"
    "Four peaking bands with a live spectrum view.";

inline constexpr const char* postEq =
    "POST EQ\n"
    "Shapes the reverb return before it blends back.\n"
    "Four peaking bands with a live spectrum view.";

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
        case SpectralMode::Merge:  // legacy
        case SpectralMode::Shadow: return influenceShadow;
    }
    return influenceShadow;
}

[[nodiscard]] inline const char* blurForMode (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Erase:  return blurErase;
        case SpectralMode::Merge:  // legacy
        case SpectralMode::Shadow: return blurShadow;
    }
    return blurShadow;
}

[[nodiscard]] inline const char* modeFor (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Erase:  return erase;
        case SpectralMode::Merge:  // legacy → present as Shadow
        case SpectralMode::Shadow: return shadow;
    }
    return shadow;
}
} // namespace afterimage::tooltips
