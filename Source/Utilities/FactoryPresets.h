#pragma once

#include "Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstddef>

namespace afterimage
{
namespace factory
{

/** Parameter values only — never includes spectral history. */
struct Preset
{
    const char* name;
    int   mode;              // 0 Shadow, 1 Erase, 2 Merge
    float memoryLengthSec;
    float recallPosition;
    float influence;
    float forget;
    float blur;
    float transientPreserve;
    bool  freeze;
    float randomRecall;
    float outputGainDb;
    float mix;
};

inline constexpr Preset kPresets[] = {
    // Shadow starting points
    { "Soft Shadow",       0, 3.0f, 0.40f, 0.45f, 0.30f, 0.20f, 0.55f, false, 0.00f, 0.0f, 1.0f },
    { "Deep Ghost",        0, 5.0f, 0.65f, 0.72f, 0.20f, 0.25f, 0.40f, false, 0.00f, 0.0f, 1.0f },
    { "Halo Pad",          0, 4.0f, 0.35f, 0.55f, 0.45f, 0.55f, 0.70f, false, 0.15f, 0.0f, 0.85f },

    // Erase starting points
    { "Erase Carve",       1, 2.5f, 0.30f, 0.70f, 0.25f, 0.10f, 0.60f, false, 0.00f, 0.0f, 1.0f },
    { "Whisper Erase",     1, 3.5f, 0.50f, 0.40f, 0.50f, 0.35f, 0.45f, false, 0.10f, 0.0f, 0.90f },

    // Merge starting points
    { "Merge Morph",       2, 3.0f, 0.45f, 0.60f, 0.30f, 0.20f, 0.50f, false, 0.00f, 0.0f, 1.0f },
    { "Slow Dissolve",     2, 6.0f, 0.70f, 0.80f, 0.55f, 0.40f, 0.35f, false, 0.00f, -1.0f, 1.0f },

    // Creative variants
    { "Frozen Afterimage", 0, 4.0f, 0.55f, 0.65f, 0.15f, 0.30f, 0.30f, true,  0.00f, 0.0f, 1.0f },
    { "Drift Memory",      0, 3.5f, 0.45f, 0.58f, 0.35f, 0.25f, 0.50f, false, 0.70f, 0.0f, 1.0f },
    { "Transient Shadow",  0, 2.0f, 0.25f, 0.50f, 0.20f, 0.05f, 0.90f, false, 0.00f, 0.0f, 1.0f },
};

inline constexpr int kNumPresets = static_cast<int> (sizeof (kPresets) / sizeof (kPresets[0]));

inline void setFloatParam (juce::AudioProcessorValueTreeState& apvts,
                           const char* id,
                           float actualValue) noexcept
{
    if (auto* p = apvts.getParameter (id))
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            p->setValueNotifyingHost (ranged->convertTo0to1 (actualValue));
    }
}

inline void setChoiceParam (juce::AudioProcessorValueTreeState& apvts,
                            const char* id,
                            int index) noexcept
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
    {
        const int n = p->choices.size();
        if (n > 0)
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (juce::jlimit (0, n - 1, index))));
    }
}

inline void setBoolParam (juce::AudioProcessorValueTreeState& apvts,
                          const char* id,
                          bool on) noexcept
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (on ? 1.0f : 0.0f);
}

/** Apply factory preset by index. Parameters only — caller should clear history. */
inline void applyPreset (juce::AudioProcessorValueTreeState& apvts, int index) noexcept
{
    if (index < 0 || index >= kNumPresets)
        return;

    const auto& pr = kPresets[static_cast<std::size_t> (index)];

    setChoiceParam (apvts, constants::idMode, pr.mode);
    setFloatParam (apvts, constants::idMemoryLength, pr.memoryLengthSec);
    setFloatParam (apvts, constants::idRecallPosition, pr.recallPosition);
    setFloatParam (apvts, constants::idInfluence, pr.influence);
    setFloatParam (apvts, constants::idForget, pr.forget);
    setFloatParam (apvts, constants::idBlur, pr.blur);
    setFloatParam (apvts, constants::idTransientPreserve, pr.transientPreserve);
    setBoolParam (apvts, constants::idFreeze, pr.freeze);
    setFloatParam (apvts, constants::idRandomRecall, pr.randomRecall);
    setFloatParam (apvts, constants::idOutputGain, pr.outputGainDb);
    setFloatParam (apvts, constants::idMix, pr.mix);
    setBoolParam (apvts, constants::idBypass, false);
}

} // namespace factory
} // namespace afterimage
