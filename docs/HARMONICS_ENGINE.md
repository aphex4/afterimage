# HARMONICS Engine

**Status:** Design locked for Phase 6 implementation  
**Product role:** Optional polyphonic, scale-aware **spectral sweetener** — not Autotune, not pitch correction.

---

## Identity

AFTERIMAGE remembers spectra. HARMONICS gently emphasizes pitch-classes that belong to a chosen scale (or live MIDI chord), so the memory wash feels more “in key” without forcing monophonic retuning.

| Not this | This |
|----------|------|
| Conventional Autotune / delay-line pitch shift | Scale-aware spectral emphasis |
| Claiming “energy redistribution” without doing it | Honest filter-bank / spectral accent |
| Hidden latency | Same host latency as STFT-only path |
| Default-on coloration | **Off by default** → exact identity |

---

## Signal placement

```text
… → Mix → HARMONICS (opt) → Formant → De-Esser → Reverb → EQ → …
```

When `harmonicsEnabled == false` (or Color ≈ 0): **no processing** (identity).

---

## Parameters

| Control | ID | Range | Role |
|---------|-----|-------|------|
| On/Off | `harmonicsEnabled` | bool | Master enable; skip DSP when false |
| Root | `scaleRoot` | C…B | Scale tonic |
| Scale | `scaleType` | Major…Chromatic | Pitch-class mask |
| Color | `scaleColor` | 0…2 | 0 = dry; 0–1 wet of accent; >1 adds in-key resonance |
| Transient | `scaleTransient` | 0…1 | Reduce accent during attacks |
| Tightness (optional) | `harmonicsTightness` | 0…1 | Narrower BP / stronger in-key bias — only if audition proves useful |

MIDI held notes override the scale mask with the held pitch-class set (same as prior Scale path).

**Removed from product:** `pitchPath` Auto-Tune, `retuneSpeed`, `humanize`. Legacy session values for those IDs are ignored (or migrated: any Auto-Tune path → Harmonics off).

---

## Algorithm (defensible)

1. Build active 12-tone mask from scale (+ MIDI override).  
2. Maintain a bank of constant-Q-ish band-pass filters at MIDI notes C2–B5 (48 bands) using **stack `BiquadCoeffs`** (no heap).  
3. For each band:  
   - **In-key:** weight ≈ 1, optional resonance boost from Color>1  
   - **Out-of-key:** lower weight (attenuation), centre stays at the band’s own pitch (no false “redirect” claim)  
4. `snapped = Σ bandPass(x) * weight` with soft normalize.  
5. `out = dry*(1−wet) + snapped*wet (+ resonance term)`.  
6. Transient detector reduces `wet` on attacks.

This is a **harmonic accent / varnish**, not pitch shifting and not energy-conserving redistribution.

---

## RT / quality rules

- No autocorrelation, no delay-line pitch ratio, no read-head jumps.  
- Coeff rebuild only when root/scale/MIDI mask changes (message or block boundary).  
- Preallocated states; process is `noexcept`.  
- CPU: skip entirely when disabled; consider lighter band count later if needed.

---

## Acceptance

- Enabled=false → bit-identical to input (within float noise of a no-op).  
- Color=0 with enabled=true → identity (wet=0).  
- No change to `getLatencySamples()` vs STFT-only.  
- UI page title: **HARMONICS** (not Scale / Auto-Tune).
