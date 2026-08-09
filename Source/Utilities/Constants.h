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
    // STFT configuration (sound-recovery: proven Phase-2 2048 / hop 512 / 4×)
    // -------------------------------------------------------------------------
    constexpr int    fftOrder           = 11;          // 2^11 = 2048
    constexpr int    fftSize            = 1 << fftOrder;
    constexpr int    hopSize            = 512;         // 4x overlap
    constexpr int    overlapFactor      = fftSize / hopSize;
    constexpr int    numBins            = fftSize / 2 + 1;

    // Incoherent OLA loses sqrt(R) of amplitude vs coherent content at the same magnitude.
    // Experimental / gated paths only — never the dry or production Shadow path.
    inline const float incoherentOlaCompensation = std::sqrt (static_cast<float> (overlapFactor));

    // -------------------------------------------------------------------------
    // Spectral history
    // -------------------------------------------------------------------------
    constexpr float  memoryLengthMinSec = 0.1f;
    constexpr float  memoryLengthMaxSec = 10.0f;
    constexpr float  memoryLengthDefaultSec = 3.0f;

    // Fresh-instance APVTS defaults (conservative Soft Shadow for v1 recovery).
    constexpr float  influenceDefault = 0.38f;
    constexpr float  blurDefault      = 0.20f;

    // Stabilized memory-profile window (internal — not a user parameter).
    // Hop 512 @ 44.1 kHz ≈ 11.6 ms/frame → 200 ms ≈ 17 frames.
    constexpr float  memoryProfileWindowMs    = 200.0f;
    constexpr float  memoryProfileMaxWindowMs = 280.0f;
    constexpr float  freezeCaptureWindowMs    = 200.0f;
    constexpr float  freezeCrossfadeMs        = 100.0f; // 75–150 ms range

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

    // Plugin tail: max memory + FFT latency @ 44.1 kHz + reconstruction margin + post reverb.
    // Freeze can sustain indefinitely; hosts require a finite value (documented).
    constexpr double pluginTailMemorySec = (double) memoryLengthMaxSec;
    constexpr double pluginTailFftLatencySec = (double) fftSize / 44100.0;
    constexpr double pluginTailMarginSec = 0.25;
    constexpr double pluginTailReverbSec = 4.0; // conventional post-chain reverb decay budget
    constexpr double pluginTailLengthSec = pluginTailMemorySec
                                         + pluginTailFftLatencySec
                                         + pluginTailMarginSec
                                         + pluginTailReverbSec;

    // Random Recall: slow LPF wander around Recall Position (not per-hop chaos)
    constexpr float  randomRecallMaxDepth  = 0.35f;  // max |age| offset at Random = 100%
    constexpr float  randomRecallCutoffHz  = 0.28f;  // wander bandwidth

    // -------------------------------------------------------------------------
    // Post-chain FX (after Mix, before Gain Match)
    // -------------------------------------------------------------------------
    constexpr float  formantSmoothSec    = 0.05f;
    constexpr float  deEsserSmoothSec    = 0.03f;
    constexpr float  reverbWetSmoothSec  = 0.05f;
    constexpr int    spectrumProbeBins   = 96;
    constexpr int    spectrumProbeFftOrder = 11; // 2048 (UI analyzer; 4096 optional via define)
    constexpr int    eqBandsPerStage     = 4;

    // Sound-recovery Stage 1: hard-remove aux DSP from the production path.
    // Not "mix=0" — Tune / Formant / De-Esser / Reverb / EQ are compiled out of
    // processChunk and do not contribute latency. Re-enable only after Stage 9
    // isolated validation (`-DAFTERIMAGE_ENABLE_AUX_DSP=1`).
#if ! defined (AFTERIMAGE_ENABLE_AUX_DSP)
    #define AFTERIMAGE_ENABLE_AUX_DSP 0
#endif
    constexpr bool auxDspEnabled = (AFTERIMAGE_ENABLE_AUX_DSP != 0);

    // Compile-time stage bypass for sound regression (OR bits; not a commercial UI).
    // A=1 SpectralEngine  B=2 Force Mix dry  C=4 Tune
    // D=8 Formant/DeEsser/Reverb  E=16 Parametric EQ  F=32 Gain Match
#if ! defined (AFTERIMAGE_STAGE_BYPASS)
    #define AFTERIMAGE_STAGE_BYPASS 0
#endif
    constexpr int stageBypassMask = AFTERIMAGE_STAGE_BYPASS;
    constexpr int stageBypassSpectral  = 1;
    constexpr int stageBypassMixDry    = 2;
    constexpr int stageBypassTune      = 4;
    constexpr int stageBypassHarmonics = 4; // legacy alias (= Tune bit)
    constexpr int stageBypassFx        = 8;
    constexpr int stageBypassEq        = 16;
    constexpr int stageBypassGainMatch = 32;

    // -------------------------------------------------------------------------
    // UI
    // -------------------------------------------------------------------------
    constexpr int    editorDefaultWidth  = 1180;
    constexpr int    editorDefaultHeight = 780;
    constexpr int    editorMinWidth      = 980;
    constexpr int    editorMinHeight     = 640;
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

    // Post-chain (new IDs — absent from older sessions → APVTS defaults).
    inline constexpr const char* idReverbType        = "reverbType";   // 0 Spring, 1 Hall, 2 Room
    inline constexpr const char* idReverbWet         = "reverbWet";
    inline constexpr const char* idFormant           = "formant";      // 0=Low … 0.5=center … 1=High
    inline constexpr const char* idDeEsser           = "deEsser";      // intensity 0..1

    inline constexpr const char* idPreEq1Freq        = "preEq1Freq";
    inline constexpr const char* idPreEq1Gain        = "preEq1Gain";
    inline constexpr const char* idPreEq2Freq        = "preEq2Freq";
    inline constexpr const char* idPreEq2Gain        = "preEq2Gain";
    inline constexpr const char* idPreEq3Freq        = "preEq3Freq";
    inline constexpr const char* idPreEq3Gain        = "preEq3Gain";
    inline constexpr const char* idPreEq4Freq        = "preEq4Freq";
    inline constexpr const char* idPreEq4Gain        = "preEq4Gain";

    inline constexpr const char* idPostEq1Freq       = "postEq1Freq";
    inline constexpr const char* idPostEq1Gain       = "postEq1Gain";
    inline constexpr const char* idPostEq2Freq       = "postEq2Freq";
    inline constexpr const char* idPostEq2Gain       = "postEq2Gain";
    inline constexpr const char* idPostEq3Freq       = "postEq3Freq";
    inline constexpr const char* idPostEq3Gain       = "postEq3Gain";
    inline constexpr const char* idPostEq4Freq       = "postEq4Freq";
    inline constexpr const char* idPostEq4Gain       = "postEq4Gain";

    inline constexpr const char* kPreEqFreqIds[eqBandsPerStage] = {
        idPreEq1Freq, idPreEq2Freq, idPreEq3Freq, idPreEq4Freq
    };
    inline constexpr const char* kPreEqGainIds[eqBandsPerStage] = {
        idPreEq1Gain, idPreEq2Gain, idPreEq3Gain, idPreEq4Gain
    };
    inline constexpr const char* kPostEqFreqIds[eqBandsPerStage] = {
        idPostEq1Freq, idPostEq2Freq, idPostEq3Freq, idPostEq4Freq
    };
    inline constexpr const char* kPostEqGainIds[eqBandsPerStage] = {
        idPostEq1Gain, idPostEq2Gain, idPostEq3Gain, idPostEq4Gain
    };

    // Default EQ centre frequencies (Hz)
    inline constexpr float kDefaultEqFreqs[eqBandsPerStage] = {
        200.0f, 800.0f, 2500.0f, 8000.0f
    };

    // TUNE (automatic monophonic pitch correction)
    inline constexpr const char* idTuneEnabled       = "tuneEnabled";
    inline constexpr const char* idScaleRoot         = "scaleRoot";      // 0=C .. 11=B
    inline constexpr const char* idScaleType         = "scaleType";      // 0 Major ... 5 Chromatic
    inline constexpr const char* idRetune            = "retune";         // 0 slow .. 1 fast
    inline constexpr const char* idHumanize          = "humanize";       // 0..1
    inline constexpr const char* idTuneAmount        = "tuneAmount";     // 0..1

    // Explicit optional-module enables (skip DSP when false)
    inline constexpr const char* idFormantEnabled    = "formantEnabled";
    inline constexpr const char* idDeEsserEnabled    = "deEsserEnabled";
    inline constexpr const char* idReverbEnabled     = "reverbEnabled";
    inline constexpr const char* idReverbSafeBass    = "reverbSafeBass";
    inline constexpr const char* idEqEnabled         = "eqEnabled";

    // Obsolete IDs retained in layout / migration for session compat (not on shipping DSP).
    inline constexpr const char* idHarmonicsEnabled  = "harmonicsEnabled";
    inline constexpr const char* idScaleColor        = "scaleColor";
    inline constexpr const char* idScaleTransient    = "scaleTransient";
    inline constexpr const char* idPitchPathLegacy   = "pitchPath";
    inline constexpr const char* idRetuneSpeedLegacy = "retuneSpeed";
    inline constexpr const char* idHumanizeLegacy    = "humanizeLegacy";
    inline constexpr const char* idEqChannelModeLegacy = "eqChannelMode";

    // Parametric EQ — last creative stage before Gain Match (stereo-linked)
    constexpr int    parametricEqBands   = 8;

    // Per-band IDs use eqN* where N=1..8 (helpers below).
    inline constexpr const char* idEq1On = "eq1On";
    inline constexpr const char* idEq1Type = "eq1Type";
    inline constexpr const char* idEq1Freq = "eq1Freq";
    inline constexpr const char* idEq1Gain = "eq1Gain";
    inline constexpr const char* idEq1Q = "eq1Q";
    inline constexpr const char* idEq1X4 = "eq1X4";
    inline constexpr const char* idEq1Solo = "eq1Solo";

    inline constexpr float kDefaultParaEqFreqs[parametricEqBands] = {
        40.0f, 80.0f, 200.0f, 500.0f, 1200.0f, 3000.0f, 7000.0f, 12000.0f
    };

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
