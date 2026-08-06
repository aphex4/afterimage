#pragma once

#include "../Utilities/Constants.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace afterimage
{

/**
    Perceptually transparent broadband Gain Match.

    Measures latency-aligned dry vs the completed equal-power mix (before bypass /
    entitlement / Output Gain), then applies one stereo-linked scalar to that mix.

    Detector: sample-domain mean-square envelopes (block-size independent).
    Correction: smoothed in dB with asymmetric attack/recovery, deadband, silence gate.
    Audible path: scalar multiply only — no filtering or spectral processing.
*/
class GainMatchController
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = std::max (1.0, sampleRate);
        // Per-sample one-pole coefficients (Option A — identical across block sizes).
        detectorCoeff_ = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::gainMatchDetectorTauSec));
        attenCoeff_    = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::gainMatchAttenuationSec));
        recoveryCoeff_ = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::gainMatchRecoverySec));
        gateReturnCoeff_ = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::gainMatchGateReturnSec));
        reset();
    }

    void reset() noexcept
    {
        dryPowerEnv_ = 0.0;
        mixedPowerEnv_ = 0.0;
        correctionDbSmoothed_ = 0.0f;
        gateOpen_ = false;
        debugCorrectionDb_.store (0.0f, std::memory_order_relaxed);
    }

    /**
        Advance detector from one sample of linked mean-square powers and return
        the linear gain scalar to apply to the completed mix (before enable blend).
    */
    [[nodiscard]] float advanceAndGetScalar (float dryMeanSquare,
                                             float mixedMeanSquare) noexcept
    {
        dryPowerEnv_ += (double) detectorCoeff_ * ((double) dryMeanSquare - dryPowerEnv_);
        mixedPowerEnv_ += (double) detectorCoeff_ * ((double) mixedMeanSquare - mixedPowerEnv_);

        const float dryDb = powerToDb ((float) dryPowerEnv_);
        const float mixedDb = powerToDb ((float) mixedPowerEnv_);

        updateGate (dryDb, mixedDb);

        float targetDb = 0.0f;
        if (gateOpen_)
        {
            // Dry silent → do not invent makeup from a quiet reference.
            if (dryDb > constants::gainMatchSilenceCloseDb)
                targetDb = dryDb - mixedDb;
            else
                targetDb = 0.0f;

            targetDb = std::clamp (targetDb,
                                   -constants::gainMatchMaxAttenDb,
                                   constants::gainMatchMaxMakeupDb);
        }

        const float err = targetDb - correctionDbSmoothed_;
        if (! gateOpen_)
        {
            correctionDbSmoothed_ += gateReturnCoeff_ * (0.0f - correctionDbSmoothed_);
        }
        else if (std::abs (err) > constants::gainMatchDeadbandDb)
        {
            // Attenuation (lower gain) uses faster coeff; makeup/recovery is slower.
            const float coeff = (err < 0.0f) ? attenCoeff_ : recoveryCoeff_;
            correctionDbSmoothed_ += coeff * err;
            correctionDbSmoothed_ = std::clamp (correctionDbSmoothed_,
                                                -constants::gainMatchMaxAttenDb,
                                                constants::gainMatchMaxMakeupDb);
        }
        // Inside deadband: hold.

        debugCorrectionDb_.store (correctionDbSmoothed_, std::memory_order_relaxed);
        return constants::dbToGain (correctionDbSmoothed_);
    }

    [[nodiscard]] float getCorrectionDb() const noexcept { return correctionDbSmoothed_; }

    /** UI / test safe atomic readout of current correction (dB). */
    [[nodiscard]] float loadDebugCorrectionDb() const noexcept
    {
        return debugCorrectionDb_.load (std::memory_order_relaxed);
    }

private:
    static float powerToDb (float power) noexcept
    {
        return 10.0f * std::log10 (std::max (power, constants::gainMatchPowerEpsilon));
    }

    void updateGate (float dryDb, float mixedDb) noexcept
    {
        const float peakDb = std::max (dryDb, mixedDb);
        if (gateOpen_)
        {
            if (peakDb < constants::gainMatchSilenceCloseDb)
                gateOpen_ = false;
        }
        else
        {
            if (peakDb > constants::gainMatchSilenceOpenDb)
                gateOpen_ = true;
        }
    }

    double sampleRate_ = 44100.0;
    double dryPowerEnv_ = 0.0;
    double mixedPowerEnv_ = 0.0;
    float correctionDbSmoothed_ = 0.0f;
    float detectorCoeff_ = 0.0f;
    float attenCoeff_ = 0.0f;
    float recoveryCoeff_ = 0.0f;
    float gateReturnCoeff_ = 0.0f;
    bool gateOpen_ = false;
    std::atomic<float> debugCorrectionDb_ { 0.0f };
};

} // namespace afterimage
