#pragma once

#include "../DSP/SpectralModes.h"

#include <juce_core/juce_core.h>

/**
    Canonical AFTERIMAGE tooltip copy.
    First line = title (UPPERCASE control name). Remaining lines = body.
    ASCII only in user-facing strings.
*/
namespace afterimage::tooltips
{
/** Do not attach to working controls - reserved for truly disabled UI. */
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

inline constexpr const char* tuneEnable =
    "TUNE ON\n"
    "Enables automatic monophonic pitch correction.\n"
    "Off is a pure delay (identity) with the same fixed latency.";

inline constexpr const char* tuneRoot =
    "ROOT\n"
    "Tonic pitch-class for scale snapping.";

inline constexpr const char* tuneScale =
    "SCALE\n"
    "Target scale. Detected pitch snaps to the nearest in-scale note.";

inline constexpr const char* tuneRetune =
    "RETUNE\n"
    "How quickly pitch moves to the target note.\n"
    "Higher values correct faster.";

inline constexpr const char* tuneHumanize =
    "HUMANIZE\n"
    "Softens correction when the note is already close,\n"
    "preserving natural vibrato and drift.";

inline constexpr const char* tuneAmount =
    "AMOUNT\n"
    "How much correction is applied.\n"
    "0% leaves pitch unchanged (still latency-aligned).";

inline constexpr const char* reverbEnable =
    "REVERB ON\n"
    "Enables the post-mix reverb. Off (and Mix 0) is identity.";

inline constexpr const char* reverbWet =
    "REVERB MIX\n"
    "Equal-power blend of dry signal and reverb return.\n"
    "0% is off.";

inline constexpr const char* reverbType =
    "REVERB TYPE\n"
    "Room, Hall, or Spring character.";

inline constexpr const char* reverbSafeBass =
    "SAFE BASS\n"
    "High-passes the wet reverb return near 125 Hz (~24 dB/oct).\n"
    "Dry path stays full range to keep the low end clean.";

inline constexpr const char* formantEnable =
    "FORMANT ON\n"
    "Enables true spectral-envelope formant shifting.\n"
    "Off is identity.";

inline constexpr const char* formant =
    "FORMANT\n"
    "Warps the spectral envelope (vowel colour) without shifting pitch.\n"
    "Center is transparent. Low / High move formants.";

inline constexpr const char* deEsserEnable =
    "DE-ESSER ON\n"
    "Enables dynamic high-frequency control. Off is identity.";

inline constexpr const char* deEsser =
    "DE-ESSER\n"
    "Reduces harsh high frequencies dynamically.\n"
    "Higher values duck sibilance more.";

inline constexpr const char* eqEnable =
    "EQ ON\n"
    "Enables the parametric EQ stage. Off skips EQ processing.";

inline constexpr const char* eqType =
    "TYPE\n"
    "Filter type for the selected band:\n"
    "Low Pass, High Pass, shelves, Bell, or Notch.";

inline constexpr const char* eqSlope =
    "SLOPE\n"
    "Low/High Pass slope: 12 dB or 48 dB per octave.\n"
    "Hidden for Bell, Shelf, and Notch.";

inline constexpr const char* eqSolo =
    "SOLO\n"
    "Auditions only this band exclusively.";

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
        case SpectralMode::Merge:
        case SpectralMode::Shadow: return influenceShadow;
    }
    return influenceShadow;
}

[[nodiscard]] inline const char* blurForMode (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Erase:  return blurErase;
        case SpectralMode::Merge:
        case SpectralMode::Shadow: return blurShadow;
    }
    return blurShadow;
}

[[nodiscard]] inline const char* modeFor (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Erase:  return erase;
        case SpectralMode::Merge:
        case SpectralMode::Shadow: return shadow;
    }
    return shadow;
}
} // namespace afterimage::tooltips
