# Realtime Safety Audit

Audio-thread rules for AFTERIMAGE `processBlock` / `processChunk`.

## Shipping path

| Module | Alloc | Lock/I/O | Notes |
|--------|-------|----------|-------|
| SpectralEngine / STFT / history / modes / tail | No | No | Preallocated at prepare |
| PitchTune (TUNE) | No | No | Preallocated buffers; YIN every hop (amortized), not per-sample O(N²) |
| FormantShifter | No | No | Preallocated FFT scratch; stack envelope |
| DeEsser | No | No | Stack `BiquadCoeffs` |
| PostChainReverb | No | No | JUCE Reverb + stack HPF for SAFE BASS |
| ParametricEQ | No | No | Stack biquads; spectrum probe preallocated |
| Gain Match / Mix / Bypass | No | No | Sample-smoothed |

## Quarantined / obsolete

| Item | Risk | Status |
|------|------|--------|
| `AutoTune.h` | O(N×lag) autocorr spikes; delay-line read jumps; hidden latency | Quarantined header; not called |
| `ScaleAccentuator` (HARMONICS) | 48 BP × ch × sample | Removed from shipping process path; file retained |

## Compile-time stage bypass

`-DAFTERIMAGE_STAGE_BYPASS=<mask>` bits A–F (see `Constants.h`) for developer sound regression. Bit C forces TUNE disable (fixed delay still runs). Not exposed in commercial UI.

## Parameter smoothing

Mix, Output, Bypass, Gain Match, Formant, Reverb Mix, Tune enable/amount/ratio use smoothed transitions. Coeff rebuilds for EQ/Tune scale happen on block boundaries from `updateParameterTargets`, not mid-sample heap factories.
