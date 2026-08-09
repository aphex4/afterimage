# AFTERIMAGE Effect Engine

Preserves STFT (**4096**/512), latency, phase strategy (current-frame phase by default), parameter IDs, and Influence=0 / Mix=0 / Bypass identity.

## Product identities

| Mode | Verb |
|------|------|
| **Shadow** | Multi-age spectral delay/reverb tail from stabilized memory profiles + diffusion. |
| **Erase** | Spectral cleaning via relative-prominence familiarity map. |

**Merge** was removed from the product surface (UI / APVTS choices / presets). Legacy saved `mode=Merge` migrates to **Shadow**. Merge DSP remains available to unit tests only.

## Full signal order

```text
Input
  → SpectralEngine (STFT + Shadow/Erase)
  → latency-aligned dry delay
  → equal-power Mix
  → HARMONICS (opt; scale-aware spectral sweetener)
  → Formant (opt)
  → De-esser (opt)
  → Reverb: dry ‖ (Pre-EQ → verb → Post-EQ) → wet mix (opt)
  → Parametric EQ (opt; 8-band stereo-linked)
  → Gain Match (vs latency-aligned dry)
  → Bypass → entitlement dry → Output Gain
  → Output
```

Editor pages: **MEMORY | HARMONICS | EQ | FX**. **MATCH** is global.

Optional modules have explicit enables (`harmonicsEnabled`, `formantEnabled`, `deEsserEnabled`, `reverbEnabled`, `eqEnabled`). When disabled (or reverb wet≈0), processing is skipped / identity.

Conventional Autotune is **not** on the shipping path (see `docs/HARMONICS_ENGINE.md`, `docs/CLEANUP_AUDIT.md`).

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

### Erase — relative familiarity map

Relative-prominence familiarity with asymmetric attack/release; Blur widens the suppression mask.

### Merge — legacy / tests only

Spectral blur complex-write path retained for unit tests; not selectable in product UI.

## Debug / stage bypass

- Mode audition: `-DAFTERIMAGE_DEBUG_AUDITION=<n>` (engine-internal).
- Chain stage bypass: `-DAFTERIMAGE_STAGE_BYPASS=<mask>` bits A–F in `Constants.h` (developer builds).

## Factory presets

Soft Shadow (program 0) = spectral memory core only (optional modules off). Other presets may enable reverb/formant/de-esser intentionally.

## Gain Match

Optional broadband loudness trim after the completed creative chain (before Bypass / entitlement / Output Gain).
