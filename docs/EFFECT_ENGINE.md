# AFTERIMAGE Effect Engine Retune

Preserves STFT (2048/512), latency, phase strategy (current-frame phase), parameter IDs, and Influence=0 / Mix=0 / Bypass identity.

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

Max shutoff ~65% so attacks retain ≥~35% historical contribution.

## Transient detector

`transientStrength = clamp((flux/denom) * 3.2)` (was `* 4.0`).

## Recalled energy normalisation

Bounded makeup −6…+9 dB, smoothed; mode target ratios ~0.78 / 0.90 / 0.85 of current RMS. Silent history not amplified.

## Mode formulas

**Shadow:** `out = current + normalizedHistory * mixAmount`  
Energy policy: allow up to `mappedInfluence * 3 dB` rise; do not force input=output energy.

**Erase:** `ratio = hist/(cur+ε); overlap = ratio/(ratio+0.32); out = cur * (1 - min(mix*overlap, 0.92))`  
No upward energy restore (soft floor only).

**Merge:** log-magnitude morph with ±18 dB per-bin clamp; soft energy stabilise (±3 dB partial).

## Blur

History-only box blur; after blur, RMS energy renormalised (0.5…2×) so Blur ≠ unintended attenuation. Blur=0 exact.

## Default parameter changes

| Param | Was | Now | Notes |
|-------|-----|-----|-------|
| Recall | 45% | 40% | Saved sessions keep saved values |
| Influence | 50% | 40% | |
| Forget | 35% | 25% | |
| Blur | 15% | 12% | |
| Transient Preserve | 50% | 35% | |

## Factory presets

Retuned into Subtle / Medium / Extreme categories. Default program `Soft Shadow` is immediately demonstrative.

## DebugAudition

Compile with `-DAFTERIMAGE_DEBUG_AUDITION=1` (RecalledOnly) or `=2` (SpectralDelta). Not in release UI.
