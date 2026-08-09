# UI Control Audit

Maps every user-visible control after Phase 2 sound restoration.

## Global top bar

| Control | Param | Notes |
|---------|-------|-------|
| AFTERIMAGE | — | Brand |
| License | — | Licensing chip |
| FILL | — | History fill % |
| Preset | programs | Factory bank |
| SHADOW / ERASE | `mode` | Product modes only |
| Meters | — | Dry ref / final out |
| MATCH | `gainMatch` | Global |
| POWER | `bypass` | Global |

## Nav (premium glass)

MEMORY | TUNE | EQ | FX

## MEMORY

| Control | Param | Works |
|---------|-------|-------|
| Memory Well / Recall | `recallPosition` | Yes |
| FREEZE | `freeze` | Yes |
| MEMORY, FORGET, INFLUENCE, BLUR, TRANSIENT, RANDOM, MIX, OUTPUT | matching IDs | Yes |

## TUNE

| Control | Param | Works |
|---------|-------|-------|
| ON | `tuneEnabled` | Yes — off = pure delay identity |
| Root | `scaleRoot` | Yes |
| Scale | `scaleType` | Yes |
| RETUNE | `retune` | Yes |
| HUMANIZE | `humanize` | Yes |
| AMOUNT | `tuneAmount` | Yes |

Obsolete HARMONICS params (`harmonicsEnabled`, `scaleColor`, `scaleTransient`) remain in APVTS for session compat only — no UI.

## EQ

| Control | Param | Works |
|---------|-------|-------|
| ON | `eqEnabled` | Yes — skips stage |
| Band 1–8 select | — | Yes |
| Band ON | `eqNOn` | Yes (chip) |
| SOLO | `eqNSolo` | Yes — exclusive (chip) |
| TYPE / FREQ / GAIN / Q | `eqN*` | Yes |
| SLOPE | `eqNX4` | Yes — 12 dB / 48 dB for LP/HP only; hidden otherwise |
| Spectrum | — | Always-live pre-EQ probe (FFT 2048, Hann, overlap, dB -90..0) |

## FX

| Control | Param | Works |
|---------|-------|-------|
| Reverb ON | `reverbEnabled` | Yes |
| TYPE / MIX | `reverbType` / `reverbWet` | Yes; Mix 0 identity |
| SAFE BASS | `reverbSafeBass` | Yes — HPF wet ~125 Hz |
| Formant ON + slider | `formantEnabled` / `formant` | Yes — envelope warp; Center transparent |
| De-Esser ON + intensity | `deEsserEnabled` / `deEsser` | Yes |

Pre-EQ / Post-EQ UI and active DSP removed (obsolete `preEq*` / `postEq*` IDs retained for session compat).
