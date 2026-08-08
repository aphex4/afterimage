# AFTERIMAGE Effect Engine

Preserves STFT (2048/512), latency, phase strategy (current-frame phase by default), parameter IDs, and Influence=0 / Mix=0 / Bypass identity.

## Product identities

| Mode | Verb |
|------|------|
| **Shadow** | Multi-age spectral delay/reverb tail from stabilized memory profiles + diffusion. |
| **Erase** | Spectral cleaning via relative-prominence familiarity map. |
| **Merge** | Log-envelope morph between short current profile and stabilized memory. |

## Core principle: memory is a short moment

Recalled material is a **SpectralMemoryProfile** (~200 ms Gaussian-weighted window with variance stability), not a single FFT frame. Freeze captures the same stabilized recent window and crossfades (~100 ms).

## Influence mapping

```text
mapped = 1 - (1 - x)^exponent
Shadow exponent 1.70 | Erase 2.05 | Merge 1.45
```

Exactly 0 at 0, exactly 1 at 1; stronger mid-range than linear.

## Forget / retention

```text
ageWeight = exp(-age * decayCoeff(forget²))
historyWeight = floor + (1 - floor) * ageWeight
floors: Shadow 0.22 | Erase 0.15 | Merge 0.25
```

Shadow also maps Forget → exponential tap decay time across the multi-age tail.

## Transient Preserve

```text
effective = mappedInfluence * (1 - transient * preserve * 0.65)
```

## Mode formulas

### Shadow — multi-age spectral tail

```text
tail[k] = Σ_t  tapGain[t] * diffuse(profileAtAge[t][k])
out = current + tail * mixAmount
```

- 5 taps from Recall toward older memory
- Forget → decay seconds (long/short tail)
- Blur → repeated 3-tap spectral diffusion (+ light age drift)
- Energy policy: allow up to `mappedInfluence * 3.5 dB` rise
- Phase: `CurrentPhase` (default). `PropagatedGhostPhase` exists but is OFF until listening proves better.
- Per-bin contrast limiter softens whistle peaks

### Erase — relative familiarity map

Per-channel familiarity updated from **relative prominence** (`mag / broadEnvelope`) of the memory profile (unless Freeze):

```text
attack/release asymmetric one-pole toward soft-compressed relative prominence
attackSec ≈ clamp(Memory * 0.03, 20–220 ms)
releaseSec ≈ Memory * (0.45 + 0.55*(1 - Forget²))
```

**Freeze:** holds the familiarity stencil. **Blur:** widens the suppression mask.

```text
mask = pow(fam * (1 - 0.88 * novelBoost), contrastExp)
attenDb = -(12 + mappedInf * 18) * mixAmount * mask
out = current * max(dbToGain(attenDb), softFloor)
```

### Merge — dual-profile envelope morph

```text
currentProfile ≈ 40–100 ms EMA of current magnitudes (cold-start snaps)
memoryProfile  = 150–250 ms stabilized recall profile
curEnv, histEnv = boxBlur(..., radius = 12 + BlurRadius)
mergedDb = lerp(curEnvDb, histEnvDb, morphAmount)
fine = current / curEnv   // damped by Blur
out = fine * dbToGain(mergedDb)
```

**Blur** is the signature control (envelope width + fine-structure damp). Soft energy match (~94% toward unity, residual capped ±1.25 dB) so mid Influence does not amp several dB before Gain Match. Current phase only.

## DebugAudition

Compile with `-DAFTERIMAGE_DEBUG_AUDITION=<n>` (not in release UI):

| Value | Mode |
|------:|------|
| 0 | Normal |
| 1 | MemoryOnly |
| 2 | ShadowTailOnly |
| 3 | EraseRemovedOnly |
| 4 | MergeDifferenceOnly |

## Factory presets

Retuned for the redesign: Spectral Hall, Vocal Afterglow, Memory Delay, Ghost Pad, Frozen Choir, Loop Cleaner, Resonance Memory, Hollow Repeat, Spectral Dust, Melt, Vocal Blur, Past Into Present, Spectral Fog, Memory Wash (+ Soft Shadow default).

## Gain Match

Optional broadband loudness trim after equal-power Mix (before Bypass / entitlement / Output Gain) — unchanged from prior release.
