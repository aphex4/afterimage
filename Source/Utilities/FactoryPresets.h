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
    int   mode;              // 0 Shadow, 1 Erase
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
    int   reverbType;        // 0 Spring, 1 Hall, 2 Room
    float reverbWet;
    float formant;           // 0.5 = centre
    float deEsser;
    bool  harmonicsEnabled;
    bool  formantEnabled;
    bool  deEsserEnabled;
    bool  reverbEnabled;
};

/**
    Factory bank — Shadow / Erase. Soft Shadow (program 0) is spectral-memory core only:
    optional modules off / neutral.
*/
inline constexpr Preset kPresets[] = {
    // --- Shadow (core-first; reverb only when intentionally enabled) ---
    { "Soft Shadow",       0, 3.0f, 0.40f, 0.50f, 0.25f, 0.22f, 0.35f, false, 0.00f, 0.0f, 1.0f, 1, 0.00f, 0.50f, 0.00f, false, false, false, false },
    { "Spectral Hall",     0, 5.5f, 0.52f, 0.58f, 0.18f, 0.48f, 0.40f, false, 0.00f, 0.0f, 1.0f, 1, 0.22f, 0.50f, 0.00f, false, false, false, true },
    { "Vocal Afterglow",   0, 3.2f, 0.35f, 0.52f, 0.28f, 0.22f, 0.55f, false, 0.00f, 0.0f, 1.0f, 2, 0.10f, 0.55f, 0.15f, false, true, true, true },
    { "Memory Delay",      0, 4.0f, 0.62f, 0.55f, 0.22f, 0.10f, 0.32f, false, 0.00f, 0.0f, 1.0f, 0, 0.08f, 0.50f, 0.00f, false, false, false, true },
    { "Ghost Pad",         0, 4.5f, 0.48f, 0.50f, 0.30f, 0.42f, 0.45f, false, 0.08f, 0.0f, 0.95f, 1, 0.18f, 0.48f, 0.00f, false, false, false, true },
    { "Frozen Choir",      0, 4.0f, 0.45f, 0.62f, 0.15f, 0.35f, 0.30f, true,  0.00f, 0.0f, 1.0f, 1, 0.15f, 0.52f, 0.00f, false, false, false, true },
    { "Melt",              0, 3.5f, 0.45f, 0.58f, 0.22f, 0.58f, 0.40f, false, 0.00f, 0.0f, 1.0f, 1, 0.20f, 0.50f, 0.00f, false, false, false, true },
    { "Vocal Blur",        0, 3.0f, 0.38f, 0.52f, 0.25f, 0.55f, 0.50f, false, 0.00f, 0.0f, 1.0f, 2, 0.14f, 0.58f, 0.20f, false, true, true, true },
    { "Past Into Present", 0, 4.5f, 0.55f, 0.65f, 0.28f, 0.52f, 0.36f, false, 0.05f, 0.0f, 1.0f, 1, 0.16f, 0.50f, 0.00f, false, false, false, true },
    { "Spectral Fog",      0, 5.0f, 0.50f, 0.62f, 0.32f, 0.68f, 0.32f, false, 0.10f, -0.5f, 1.0f, 1, 0.24f, 0.45f, 0.00f, false, false, false, true },
    { "Memory Wash",       0, 6.0f, 0.60f, 0.70f, 0.35f, 0.72f, 0.28f, false, 0.12f, -1.0f, 0.95f, 1, 0.28f, 0.42f, 0.00f, false, false, false, true },

    // --- Erase ---
    { "Loop Cleaner",      1, 3.0f, 0.28f, 0.48f, 0.30f, 0.16f, 0.50f, false, 0.00f, 0.0f, 1.0f, 2, 0.00f, 0.50f, 0.10f, false, false, true, false },
    { "Resonance Memory",  1, 4.0f, 0.40f, 0.55f, 0.22f, 0.28f, 0.42f, false, 0.00f, 0.0f, 1.0f, 1, 0.00f, 0.50f, 0.00f, false, false, false, false },
    { "Hollow Repeat",     1, 3.5f, 0.35f, 0.65f, 0.25f, 0.22f, 0.45f, false, 0.00f, -0.5f, 1.0f, 2, 0.00f, 0.50f, 0.05f, false, false, true, false },
    { "Spectral Dust",     1, 4.5f, 0.42f, 0.70f, 0.20f, 0.38f, 0.35f, true,  0.00f, -0.5f, 1.0f, 1, 0.00f, 0.50f, 0.00f, false, false, false, false },
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

    setBoolParam (apvts, constants::idReverbEnabled, pr.reverbEnabled);
    setChoiceParam (apvts, constants::idReverbType, pr.reverbType);
    setFloatParam (apvts, constants::idReverbWet, pr.reverbWet);
    setBoolParam (apvts, constants::idFormantEnabled, pr.formantEnabled);
    setFloatParam (apvts, constants::idFormant, pr.formant);
    setBoolParam (apvts, constants::idDeEsserEnabled, pr.deEsserEnabled);
    setFloatParam (apvts, constants::idDeEsser, pr.deEsser);

    setBoolParam (apvts, constants::idHarmonicsEnabled, pr.harmonicsEnabled);
    setFloatParam (apvts, constants::idScaleColor, 0.0f);
    setFloatParam (apvts, constants::idScaleTransient, 0.35f);
    setBoolParam (apvts, constants::idEqEnabled, false);

    for (int b = 0; b < constants::eqBandsPerStage; ++b)
    {
        setFloatParam (apvts, constants::kPreEqFreqIds[b], constants::kDefaultEqFreqs[b]);
        setFloatParam (apvts, constants::kPreEqGainIds[b], 0.0f);
        setFloatParam (apvts, constants::kPostEqFreqIds[b], constants::kDefaultEqFreqs[b]);
        setFloatParam (apvts, constants::kPostEqGainIds[b], 0.0f);
    }

    for (int b = 0; b < constants::parametricEqBands; ++b)
    {
        const auto n = juce::String (b + 1);
        const auto onId = "eq" + n + "On";
        const auto soloId = "eq" + n + "Solo";
        const auto x4Id = "eq" + n + "X4";
        const auto gainId = "eq" + n + "Gain";
        const auto freqId = "eq" + n + "Freq";
        setBoolParam (apvts, onId.toRawUTF8(), false);
        setBoolParam (apvts, soloId.toRawUTF8(), false);
        setBoolParam (apvts, x4Id.toRawUTF8(), false);
        setFloatParam (apvts, gainId.toRawUTF8(), 0.0f);
        setFloatParam (apvts, freqId.toRawUTF8(), constants::kDefaultParaEqFreqs[b]);
    }
}

} // namespace factory
} // namespace afterimage
