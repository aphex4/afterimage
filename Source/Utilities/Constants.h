#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace afterimage
{
namespace constants
{
    // -------------------------------------------------------------------------
    // STFT defaults (Phase 2+) — not yet active in Phase 1 pass-through.
    // -------------------------------------------------------------------------
    constexpr int    fftOrder           = 11;          // 2^11 = 2048
    constexpr int    fftSize            = 1 << fftOrder;
    constexpr int    hopSize            = 512;         // 4x overlap
    constexpr int    overlapFactor      = fftSize / hopSize;
    constexpr int    numBins            = fftSize / 2 + 1;

    // -------------------------------------------------------------------------
    // Spectral history
    // -------------------------------------------------------------------------
    constexpr float  memoryLengthMinSec = 0.1f;
    constexpr float  memoryLengthMaxSec = 10.0f;
    constexpr float  memoryLengthDefaultSec = 3.0f;

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
    // UI
    // -------------------------------------------------------------------------
    constexpr int    editorDefaultWidth  = 900;
    constexpr int    editorDefaultHeight = 600;
    constexpr int    editorMinWidth      = 720;
    constexpr int    editorMinHeight     = 480;
    constexpr int    uiTimerHz           = 30;

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
