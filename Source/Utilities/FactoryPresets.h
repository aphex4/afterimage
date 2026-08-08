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
    int   mode;              // 0 Shadow, 1 Erase (legacy 2 Merge mapped → Shadow on load)
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
    // Post-chain defaults (optional; older preset rows use 0 / centre)
    int   reverbType;        // 0 Spring, 1 Hall, 2 Room
    float reverbWet;
    float formant;           // 0.5 = centre
    float deEsser;
};

/**
    Factory bank — Shadow / Erase only (Merge removed from product).
    Former Merge looks converted to Shadow + subtle Hall for tail smoothing.
    Soft Shadow remains program 0 / APVTS default direction.
*/
inline constexpr Preset kPresets[] = {
    // --- Shadow ---
    { "Soft Shadow",       0, 3.0f, 0.40f, 0.50f, 0.25f, 0.22f, 0.35f, false, 0.00f, 0.0f, 1.0f, 1, 0.00f, 0.50f, 0.00f },
    { "Spectral Hall",     0, 5.5f, 0.52f, 0.58f, 0.18f, 0.48f, 0.40f, false, 0.00f, 0.0f, 1.0f, 1, 0.22f, 0.50f, 0.00f },
    { "Vocal Afterglow",   0, 3.2f, 0.35f, 0.52f, 0.28f, 0.22f, 0.55f, false, 0.00f, 0.0f, 1.0f, 2, 0.10f, 0.55f, 0.15f },
    { "Memory Delay",      0, 4.0f, 0.62f, 0.55f, 0.22f, 0.10f, 0.32f, false, 0.00f, 0.0f, 1.0f, 0, 0.08f, 0.50f, 0.00f },
    { "Ghost Pad",         0, 4.5f, 0.48f, 0.50f, 0.30f, 0.42f, 0.45f, false, 0.08f, 0.0f, 0.95f, 1, 0.18f, 0.48f, 0.00f },
    { "Frozen Choir",      0, 4.0f, 0.45f, 0.62f, 0.15f, 0.35f, 0.30f, true,  0.00f, 0.0f, 1.0f, 1, 0.15f, 0.52f, 0.00f },
    // Former Merge bank → Shadow wash + modest reverb
    { "Melt",              0, 3.5f, 0.45f, 0.58f, 0.22f, 0.58f, 0.40f, false, 0.00f, 0.0f, 1.0f, 1, 0.20f, 0.50f, 0.00f },
    { "Vocal Blur",        0, 3.0f, 0.38f, 0.52f, 0.25f, 0.55f, 0.50f, false, 0.00f, 0.0f, 1.0f, 2, 0.14f, 0.58f, 0.20f },
    { "Past Into Present", 0, 4.5f, 0.55f, 0.65f, 0.28f, 0.52f, 0.36f, false, 0.05f, 0.0f, 1.0f, 1, 0.16f, 0.50f, 0.00f },
    { "Spectral Fog",      0, 5.0f, 0.50f, 0.62f, 0.32f, 0.68f, 0.32f, false, 0.10f, -0.5f, 1.0f, 1, 0.24f, 0.45f, 0.00f },
    { "Memory Wash",       0, 6.0f, 0.60f, 0.70f, 0.35f, 0.72f, 0.28f, false, 0.12f, -1.0f, 0.95f, 1, 0.28f, 0.42f, 0.00f },

    // --- Erase ---
    { "Loop Cleaner",      1, 3.0f, 0.28f, 0.48f, 0.30f, 0.16f, 0.50f, false, 0.00f, 0.0f, 1.0f, 2, 0.00f, 0.50f, 0.10f },
    { "Resonance Memory",  1, 4.0f, 0.40f, 0.55f, 0.22f, 0.28f, 0.42f, false, 0.00f, 0.0f, 1.0f, 1, 0.00f, 0.50f, 0.00f },
    { "Hollow Repeat",     1, 3.5f, 0.35f, 0.65f, 0.25f, 0.22f, 0.45f, false, 0.00f, -0.5f, 1.0f, 2, 0.00f, 0.50f, 0.05f },
    { "Spectral Dust",     1, 4.5f, 0.42f, 0.70f, 0.20f, 0.38f, 0.35f, true,  0.00f, -0.5f, 1.0f, 1, 0.00f, 0.50f, 0.00f },
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
    setBoolParam (apvts, constants::idGainMatch, false);

    setChoiceParam (apvts, constants::idReverbType, pr.reverbType);
    setFloatParam (apvts, constants::idReverbWet, pr.reverbWet);
    setFloatParam (apvts, constants::idFormant, pr.formant);
    setFloatParam (apvts, constants::idDeEsser, pr.deEsser);

    for (int b = 0; b < constants::eqBandsPerStage; ++b)
    {
        setFloatParam (apvts, constants::kPreEqFreqIds[b], constants::kDefaultEqFreqs[b]);
        setFloatParam (apvts, constants::kPreEqGainIds[b], 0.0f);
        setFloatParam (apvts, constants::kPostEqFreqIds[b], constants::kDefaultEqFreqs[b]);
        setFloatParam (apvts, constants::kPostEqGainIds[b], 0.0f);
    }
}

} // namespace factory
} // namespace afterimage
