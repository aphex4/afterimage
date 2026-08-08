#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace afterimage
{
namespace constants
{
    // -------------------------------------------------------------------------
    // STFT configuration
    // -------------------------------------------------------------------------
    constexpr int    fftOrder           = 12;          // 2^12 = 4096
    constexpr int    fftSize            = 1 << fftOrder;
    constexpr int    hopSize            = 512;         // 8x overlap
    constexpr int    overlapFactor      = fftSize / hopSize;
    constexpr int    numBins            = fftSize / 2 + 1;

    // Incoherent OLA loses sqrt(R) of amplitude vs coherent content at the same magnitude.
    // Apply to randomised / independently-propagated tail only — never the dry path.
    inline const float incoherentOlaCompensation = std::sqrt (static_cast<float> (overlapFactor));

    // -------------------------------------------------------------------------
    // Spectral history
    // -------------------------------------------------------------------------
    constexpr float  memoryLengthMinSec = 0.1f;
    constexpr float  memoryLengthMaxSec = 10.0f;
    constexpr float  memoryLengthDefaultSec = 3.0f;

    // Stabilized memory-profile window (internal — not a user parameter).
    // Hop 512 @ 44.1 kHz ≈ 11.6 ms/frame → 200 ms ≈ 17 frames.
    constexpr float  memoryProfileWindowMs    = 200.0f;
    constexpr float  memoryProfileMaxWindowMs = 280.0f;
    constexpr float  freezeCaptureWindowMs    = 200.0f;
    constexpr float  freezeCrossfadeMs        = 100.0f; // 50–150 ms range

    // -------------------------------------------------------------------------
    // Spectral blur
    // -------------------------------------------------------------------------
    constexpr int    maxBlurRadiusBins  = 48;

    // -------------------------------------------------------------------------
    // Parameter smoothing times (seconds)
    // -------------------------------------------------------------------------
    constexpr float  gainSmoothSec      = 0.03f;
    constexpr float  mixSmoothSec       = 0.03f;
    constexpr float  influenceSmoothSec = 0.06f;
    constexpr float  recallSmoothSec    = 0.18f;
    constexpr float  forgetSmoothSec    = 0.08f;
    constexpr float  blurSmoothSec      = 0.08f;
    constexpr float  randomSmoothSec    = 0.15f;
    constexpr float  modeCrossfadeSec   = 0.08f;
    constexpr float  bypassSmoothSec    = 0.02f;

    // -------------------------------------------------------------------------
    // Gain Match (broadband loudness trim on completed mix — not a compressor)
    // Selected for perceptual transparency: slow detector + asymmetric correction
    // so transients / sibilance do not modulate gain like a de-esser.
    // -------------------------------------------------------------------------
    constexpr float  gainMatchSmoothSec       = 0.05f;  // enable/disable crossfade
    constexpr float  gainMatchDetectorTauSec  = 0.45f;  // mean-square envelope (~300–600 ms)
    constexpr float  gainMatchAttenuationSec  = 0.35f;  // correction toward quieter (~250–500 ms)
    constexpr float  gainMatchRecoverySec     = 1.00f;  // correction toward louder (~750–1500 ms)
    constexpr float  gainMatchGateReturnSec   = 0.50f;  // return to 0 dB when silence-gated
    constexpr float  gainMatchMaxAttenDb      = 6.0f;   // max attenuation of mixed signal
    constexpr float  gainMatchMaxMakeupDb     = 4.0f;   // max makeup (stricter than atten)
    constexpr float  gainMatchDeadbandDb      = 0.50f;  // hold inside final dB-error deadband
    constexpr float  gainMatchSilenceOpenDb   = -75.0f; // open gate above this (hysteresis)
    constexpr float  gainMatchSilenceCloseDb  = -80.0f; // close gate below this
    constexpr float  gainMatchPowerEpsilon    = 1.0e-20f;

    // Plugin tail: max memory + FFT latency @ 44.1 kHz + reconstruction margin.
    // Freeze can sustain indefinitely; hosts require a finite value (documented).
    constexpr double pluginTailMemorySec = (double) memoryLengthMaxSec;
    constexpr double pluginTailFftLatencySec = (double) fftSize / 44100.0;
    constexpr double pluginTailMarginSec = 0.25;
    constexpr double pluginTailLengthSec = pluginTailMemorySec
                                         + pluginTailFftLatencySec
                                         + pluginTailMarginSec;

    // Random Recall: slow LPF wander around Recall Position (not per-hop chaos)
    constexpr float  randomRecallMaxDepth  = 0.35f;  // max |age| offset at Random = 100%
    constexpr float  randomRecallCutoffHz  = 0.28f;  // wander bandwidth

    // -------------------------------------------------------------------------
    // UI
    // -------------------------------------------------------------------------
    constexpr int    editorDefaultWidth  = 1000;
    constexpr int    editorDefaultHeight = 720;
    constexpr int    editorMinWidth      = 800;
    constexpr int    editorMinHeight     = 560;
    constexpr int    uiTimerHz           = 60;
    /** Wall-clock license refresh while the editor is open (message thread only). */
    constexpr int    licenseRefreshIntervalSec = 45;
    constexpr int    maxInternalBlockSize = 2048; // chunk size for oversized host callbacks


    // -------------------------------------------------------------------------
    // Parameter IDs (stable — do not rename)
    // -------------------------------------------------------------------------
    inline constexpr const char* idMode              = "mode";
    inline constexpr const char* idMemoryLength      = "memoryLength";
    inline constexpr const char* idRecallPosition    = "recallPosition";
    inline constexpr const char* idInfluence         = "influence";
    inline constexpr const char* idForget            = "forget";
    inline constexpr const char* idBlur              = "blur";
    inline constexpr const char* idTransientPreserve = "transientPreserve";
    inline constexpr const char* idFreeze            = "freeze";
    inline constexpr const char* idRandomRecall      = "randomRecall";
    inline constexpr const char* idOutputGain        = "outputGain";
    inline constexpr const char* idMix               = "mix";
    inline constexpr const char* idBypass            = "bypass";
    // New optional bool (default off). Old sessions without this ID load fine via APVTS.
    inline constexpr const char* idGainMatch         = "gainMatch";

    inline float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    inline float gainToDb (float gain) noexcept
    {
        return 20.0f * std::log10 (std::max (gain, 1.0e-8f));
    }
} // namespace constants
} // namespace afterimage
