# Realtime Safety Audit

Post-cleanup status of audio-thread safety.

## Shipping path (processBlock)

| Module | Alloc on audio thread | Locks | Notes |
|--------|----------------------|-------|-------|
| SpectralEngine / STFT | No (prepare-only) | No | Preallocated rings |
| DryWetMixer | No | No | |
| HARMONICS (`ScaleAccentuator`) | No | No | Stack `BiquadCoeffs`; skip if disabled |
| FormantShifter | No | No | Stack biquads; skip if disabled |
| DeEsser | No | No | Stack biquads; skip if disabled |
| PostChainReverb + FourBandEQ | No | No | Stack biquads; wet branch; skip if disabled & settled |
| ParametricEQ | No | No | Stack biquads; skip if inactive |
| Gain Match / Bypass / Out | No | No | |

## Removed from shipping path

| Module | Issue | Status |
|--------|-------|--------|
| `AutoTune` | O(N×lag) autocorr spikes; delay-line read jumps; ~20 ms hidden latency | Quarantined header; not called |
| `IIR::Coefficients::make*` in Formant/DeEsser/FourBandEQ | Heap `Ptr` factories | Replaced with `Biquad.h` |

## Compile-time stage bypass

`-DAFTERIMAGE_STAGE_BYPASS=<mask>` bits A–F (see `Constants.h`) for developer sound regression. Not exposed in commercial UI.

## Latency

Host-reported latency = STFT `fftSize` (4096). All shipping optional modules are zero-delay (IIR). No Autotune delay-line offset.
