/**
    Effect-strength diagnostics (Phase 1 baseline + post-retune).

    Reports effective mix amounts, transient distribution, energy ratios, and
    compensation by mode. Quiet unless AFTERIMAGE_PRINT_MEASUREMENTS=1.
*/
#include "EffectStrengthMeasure.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/SpectralModes.h"
#include "Utilities/Constants.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace afterimage
{
namespace measure
{

namespace
{
struct MixReport
{
    float mappedInfluence = 0.0f;
    float historyWeight = 0.0f;
    float mixAmount = 0.0f;
};

MixReport probeMix (SpectralMode mode,
                    float influence,
                    float forget,
                    float recallAge,
                    float transientStrength,
                    float transientPreserve) noexcept
{
    MixReport r;
    r.mappedInfluence = mapInfluenceForMode (mode, influence);
    r.historyWeight = remappedHistoryWeight (mode, recallAge, forget); // retained for logs only
    const float preserve = juce::jlimit (0.0f, 1.0f, transientPreserve);
    const float reduction = juce::jlimit (0.0f, 1.0f, transientStrength)
                            * preserve * kMaxTransientReduction;
    // Phase 4: mixAmount no longer multiplies remappedHistoryWeight / retention floor.
    r.mixAmount = r.mappedInfluence * (1.0f - reduction);
    return r;
}

/** Pre-retune mix (linear Influence, no retention floor, full transient kill). */
float legacyMixAmount (float influence,
                       float forget,
                       float recallAge,
                       float transientStrength,
                       float transientPreserve) noexcept
{
    const float preserve = juce::jlimit (0.0f, 1.0f, transientPreserve);
    const float tr = juce::jlimit (0.0f, 1.0f, transientStrength);
    const float effective = juce::jlimit (0.0f, 1.0f, influence) * (1.0f - tr * preserve);
    return effective * ageWeightFromForget (recallAge, forget);
}

struct EnergyProbe
{
    double energyIn = 0.0;
    double energyOut = 0.0;
    float peakDelta = 0.0f;
    float lastEnergyScale = 1.0f;
};

EnergyProbe probeModeEnergy (SpectralMode mode,
                             float influence,
                             float forget,
                             float recallAge) noexcept
{
    SpectralModeProcessor proc;
    proc.prepare (constants::numBins, 48000.0, 1);
    proc.reset();

    std::vector<float> cur ((size_t) constants::numBins, 0.2f);
    std::vector<float> hist ((size_t) constants::numBins, 0.0f);
    hist[40] = 4.0f;
    hist[80] = 2.5f;
    hist[120] = 1.5f;

    ModeParams p;
    p.influence = influence;
    p.forget = forget;
    p.blur = 0.0f;
    p.transientPreserve = 0.0f;
    p.transientStrength = 0.0f;
    p.recallAge01 = recallAge;

    double eIn = 0.0;
    for (int i = 0; i < constants::numBins; ++i)
        eIn += (double) cur[(size_t) i] * cur[(size_t) i];

    auto work = cur;
    switch (mode)
    {
        case SpectralMode::Shadow:
            proc.applyShadowMagnitudes (work.data(), hist.data(), constants::numBins, p, 0);
            break;
        case SpectralMode::Erase:
            hist = cur;
            hist[40] = 4.0f;
            for (int i = 0; i < constants::numBins; ++i)
                if (i != 40)
                    hist[(size_t) i] = cur[(size_t) i];
            proc.applyEraseMagnitudes (work.data(), hist.data(), constants::numBins, p, 0);
            break;
        case SpectralMode::Merge:
            for (int i = 0; i < constants::numBins; ++i)
                hist[(size_t) i] = 1.5f;
            proc.applyMergeMagnitudes (work.data(), hist.data(), constants::numBins, p, 0);
            break;
    }

    EnergyProbe out;
    out.energyIn = eIn;
    out.lastEnergyScale = proc.getLastEnergyScale (0);
    for (int i = 0; i < constants::numBins; ++i)
    {
        out.energyOut += (double) work[(size_t) i] * work[(size_t) i];
        out.peakDelta = std::max (out.peakDelta, std::abs (work[(size_t) i] - cur[(size_t) i]));
    }
    return out;
}

void printTransientDistribution()
{
    const float calib = kTransientFluxCalibration;
    struct Case { const char* name; float rawFluxOverDenom; };
    const Case cases[] = {
        { "sine-steady", 0.00f },
        { "pad-slow", 0.02f },
        { "vocal", 0.08f },
        { "kick-onset", 0.35f },
        { "snare-onset", 0.55f },
        { "drum-loop-peak", 0.45f },
        { "bass-note", 0.12f },
    };

    std::cout << "  transientStrength = clamp(raw * " << calib << ") [was *4.0]:\n";
    for (const auto& c : cases)
    {
        const float tNew = juce::jlimit (0.0f, 1.0f, c.rawFluxOverDenom * calib);
        const float tOld = juce::jlimit (0.0f, 1.0f, c.rawFluxOverDenom * 4.0f);
        std::cout << "    " << c.name << " raw=" << c.rawFluxOverDenom
                  << " old=" << tOld << " new=" << tNew << "\n";
    }
}
} // namespace

void runEffectStrengthMeasurements (bool verbose)
{
    if (! verbose)
        return;

    std::cout << "\n=== AFTERIMAGE effect-strength measurements ===\n";

    const float influences[] = { 0.25f, 0.40f, 0.50f, 0.60f, 1.0f };
    const SpectralMode modes[] = {
        SpectralMode::Shadow, SpectralMode::Erase, SpectralMode::Merge
    };

    std::cout << "\n-- PRE-RETUNE baseline (analytical legacy mix) --\n";
    std::cout << "  Influence 50%, Forget 35%, age 45%, TP 50%:\n";
    std::cout << "    steady(tr=0.05) mix=" << legacyMixAmount (0.50f, 0.35f, 0.45f, 0.05f, 0.50f)
              << " attack(tr=0.90) mix=" << legacyMixAmount (0.50f, 0.35f, 0.45f, 0.90f, 0.50f) << "\n";

    std::cout << "\n-- POST-RETUNE mixAmount at representative settings --\n";
    std::cout << "  (Influence 50%, Recall 45%, Forget 35%, TransientPreserve 50%)\n";
    for (auto mode : modes)
    {
        auto steady = probeMix (mode, 0.50f, 0.35f, 0.45f, 0.05f, 0.50f);
        auto attack = probeMix (mode, 0.50f, 0.35f, 0.45f, 0.90f, 0.50f);
        std::cout << "  " << spectralModeName (mode)
                  << " steady mix=" << steady.mixAmount
                  << " (mappedInf=" << steady.mappedInfluence
                  << " histW=" << steady.historyWeight << ")"
                  << " | attack mix=" << attack.mixAmount << "\n";
    }

    std::cout << "\n-- mixAmount vs Influence (Forget 0.25, age 0.4, no transient) --\n";
    for (auto mode : modes)
    {
        std::cout << "  " << spectralModeName (mode) << ":";
        for (float inf : influences)
        {
            auto r = probeMix (mode, inf, 0.25f, 0.40f, 0.0f, 0.0f);
            std::cout << " " << (int) (inf * 100) << "%=" << r.mixAmount;
        }
        std::cout << "\n";
    }

    std::cout << "\n-- energy in/out by mode (influence 0.4 / 0.6) --\n";
    for (auto mode : modes)
    {
        for (float inf : { 0.4f, 0.6f })
        {
            auto e = probeModeEnergy (mode, inf, 0.25f, 0.35f);
            const double ratio = (e.energyIn > 1e-12) ? e.energyOut / e.energyIn : 0.0;
            const float db = (ratio > 1e-12)
                                 ? constants::gainToDb ((float) std::sqrt (ratio))
                                 : -120.0f;
            std::cout << "  " << spectralModeName (mode) << " inf=" << inf
                      << " energyRatio=" << ratio << " (~" << db << " dB amp)"
                      << " peakDelta=" << e.peakDelta
                      << " energyScale=" << e.lastEnergyScale << "\n";
        }
    }

    std::cout << "\n-- transient detector calibration --\n";
    printTransientDistribution();
    std::cout << "=== end measurements ===\n\n";
}

bool envWantsMeasurements()
{
    if (const char* v = std::getenv ("AFTERIMAGE_PRINT_MEASUREMENTS"))
        return v[0] != '\0' && v[0] != '0';
    return false;
}

} // namespace measure
} // namespace afterimage
