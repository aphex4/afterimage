# AFTERIMAGE Effect Engine

Preserves STFT (2048/512), latency, phase strategy (current-frame phase), parameter IDs, and Influence=0 / Mix=0 / Bypass identity.

## Product identities

| Mode | Verb |
|------|------|
| **Shadow** | Add the past behind the present (unchanged additive ghost). |
| **Erase** | Remove what the sound has repeated (familiarity envelope suppression). |
| **Merge** | Impose the spectral identity of the past onto the present (envelope + landmarks). |

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
floors: Shadow 0.20 | Erase 0.15 | Merge 0.25
```

## Transient Preserve

```text
effective = mappedInfluence * (1 - transient * preserve * 0.65)
```

## Mode formulas

### Shadow (bit-stable — do not retune casually)

```text
out = current + normalizedHistory * mixAmount
```

Energy policy: allow up to `mappedInfluence * 3 dB` rise; do not force input=output energy.  
Recalled spectra: bounded makeup −6…+9 dB, target ~0.78× current RMS.

### Erase — familiarity envelope

Per-channel preallocated envelope `eraseFamiliarity[bin]`, updated from the recalled history frame each hop (unless Freeze):

```text
attack/release asymmetric one-pole toward historyMagnitude * ageSoft
attackSec ≈ clamp(Memory * 0.025, 15–180 ms)
releaseSec ≈ Memory * (0.40 + 0.60*(1 - Forget²))   // Forget accelerates fade
```

**Recall (Erase):** selects the historical age whose magnitudes feed the familiarity update.  
**Memory:** persistence horizon (release time).  
**Forget:** how quickly familiarity decays.  
**Freeze:** stops envelope updates; continues applying the frozen stencil.

Suppression (no upward energy restore):

```text
famPresence = fam / (fam + knee)
currentExcess = max(0, cur - fam) / (cur + fam + knee)
mask = pow(famPresence * (1 - 0.90 * currentExcess), contrastExp)
attenDb = -(14 + mappedInf * 20) * mixAmount * mask
out = current * max(dbToGain(attenDb), softFloor)
```

Mask is spatially smoothed (base radius + Blur widening) and lightly smoothed across frames.  
Blur widens erasure regions; it is not a loudness control.

### Merge — envelope transfer + landmarks

Current remains the carrier. History provides identity (not `current + history`).

```text
curEnv, histEnv = boxBlur(current / history, radius = 10 + BlurRadius)
transferDb = clamp(histEnvDb - curEnvDb, ±(6 + mappedInf * 18))
prominence = history / (histEnv + ε)
landmarkDb = clamp(gainToDb(prominence), 0, 9) if prominence > 1.55 else 0
out = current * dbToGain((transferDb + landmarkDb * 0.55) * mixAmount)
```

**Blur (Merge):** broadens the transferred envelope.  
Soft energy stabilisation ±~2.5 dB. Current phase only (production default).

## Blur (Shadow)

History-only box blur with RMS energy renormalised (0.5…2×). Blur=0 exact.

## DebugAudition

Compile with `-DAFTERIMAGE_DEBUG_AUDITION=<n>` (not in release UI):

| Value | Mode |
|------:|------|
| 0 | Normal |
| 1 | RecalledOnly |
| 2 | SpectralDelta |
| 3 | EraseMask (audition removed material) |
| 4 | MergeTransfer (audition imposed identity delta) |

## Factory presets

Erase/Merge presets retuned for familiarity carve and identity transfer (not louder Output). Shadow presets unchanged in intent.

## Gain Match

Optional broadband loudness trim after equal-power Mix (before Bypass / entitlement / Output Gain):

- Measures latency-aligned dry vs completed mix (mean-square envelopes, ~450 ms)
- Applies one stereo-linked scalar only — no filtering or spectral processing
- Asymmetric dB correction (faster attenuation, slower recovery), deadband, silence gate
- Enable/disable crossfade ~50 ms; presets / state load reset adaptive state
- Mix=0% → dry vs dry → ~unity; Bypass → dry unaltered by GM
