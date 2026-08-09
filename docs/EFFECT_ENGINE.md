# AFTERIMAGE Effect Engine

Preserves STFT (**4096**/512), phase strategy (current-frame phase by default), parameter IDs, and Influence=0 / Mix=0 / Bypass identity.

Host latency = STFT (4096) + TUNE fixed delay (1024) = **5120 samples**, whether TUNE is on or off.

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
  → TUNE (always; OFF = pure delay of 1024; ON = YIN + OLA pitch correct)
  → latency-aligned dry delay (4096 + 1024)
  → equal-power Mix
  → Formant (opt; spectral-envelope warp)
  → De-esser (opt)
  → Reverb (opt): ON / TYPE / MIX / SAFE BASS
  → Parametric EQ (opt; 8-band stereo-linked)
  → Gain Match (vs latency-aligned dry)
  → Bypass → entitlement dry → Output Gain
  → Output
```

**TUNE placement decision:** After SpectralEngine, before Mix. Keeps dry/wet PDC-aligned with a shared fixed Tune latency. Operating on the spectral wet before Mix lets Shadow/Erase ghosts participate in correction without retuning material entering history. Documented in `docs/SOUND_REGRESSION_AUDIT.md`.

Editor pages: **MEMORY | TUNE | EQ | FX**. **MATCH** is global.

Optional modules: `tuneEnabled`, `formantEnabled`, `deEsserEnabled`, `reverbEnabled`, `eqEnabled`. HARMONICS (`harmonicsEnabled` / Color / Transient) retained as obsolete params for session compat only — not on the DSP path.

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

## TUNE

Monophonic YIN/NACF detector (~55-1500 Hz) with confidence / voiced gate. Snaps to nearest note in Root+Scale. Retune / Humanize / Amount. Fixed-latency OLA pitch shift (no delay-line read-head jumps). Backs off when confidence is low.

## Formant

Spectral-envelope warp (log-magnitude envelope on log-frequency axis) with fine structure + phase preserved. Center transparent. Not whole-signal pitch shift.

## Reverb

ON, TYPE (Spring/Hall/Room), MIX, SAFE BASS (~125 Hz / ~24 dB/oct HPF on wet return only). Pre-EQ and Post-EQ removed.

## Debug / stage bypass

- Mode audition: `-DAFTERIMAGE_DEBUG_AUDITION=<n>` (engine-internal).
- Chain stage bypass: `-DAFTERIMAGE_STAGE_BYPASS=<mask>` bits A–F in `Constants.h` (developer builds). Bit C = Tune disable (delay still runs).

## Factory presets

Soft Shadow (program 0) = spectral memory core only (optional modules off). Other presets may enable reverb/formant/de-esser intentionally. Every preset sets module enables explicitly.

## Gain Match

Optional broadband loudness trim after the completed creative chain (before Bypass / entitlement / Output Gain).
