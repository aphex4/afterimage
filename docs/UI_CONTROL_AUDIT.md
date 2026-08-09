# UI Control Audit

Maps every user-visible control after the cleanup rebuild.

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

## Nav (large)

MEMORY | HARMONICS | EQ | FX

## MEMORY

| Control | Param | Works |
|---------|-------|-------|
| Memory Well / Recall | `recallPosition` | Yes |
| FREEZE | `freeze` | Yes |
| MEMORY, FORGET, INFLUENCE, BLUR, TRANSIENT, RANDOM, MIX, OUTPUT | matching IDs | Yes |

## HARMONICS

| Control | Param | Works |
|---------|-------|-------|
| ON | `harmonicsEnabled` | Yes — off = identity |
| Root | `scaleRoot` | Yes |
| Scale | `scaleType` | Yes |
| COLOR | `scaleColor` | Yes |
| TRANSIENT | `scaleTransient` | Yes |

No Autotune / Retune / Humanize. No `unavailable` tooltips on live controls.

## EQ

| Control | Param | Works |
|---------|-------|-------|
| ON | `eqEnabled` | Yes — skips stage |
| Band 1–8 select | — | Yes |
| Band ON | `eqNOn` | Yes |
| Solo S | `eqNSolo` | Yes — exclusive |
| Type / Freq / Gain / Q | `eqN*` | Yes |
| x4 | `eqNX4` | Yes — **one** control for selected band |

Channel modes Stereo/LR/MS **removed** (stereo-linked only).

## FX

| Control | Param | Works |
|---------|-------|-------|
| Reverb ON | `reverbEnabled` | Yes |
| Type / Wet | `reverbType` / `reverbWet` | Yes; wet≈0 identity |
| Pre/Post EQ | `preEq*` / `postEq*` | Wet branch only |
| Formant ON + slider | `formantEnabled` / `formant` | Yes |
| De-Esser ON + intensity | `deEsserEnabled` / `deEsser` | Yes |
