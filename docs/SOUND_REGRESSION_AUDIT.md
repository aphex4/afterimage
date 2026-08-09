# AFTERIMAGE Sound Regression Audit (Phase 2 / Phase A)

**Date:** 2026-08-09  
**Branch:** `cursor/phase-1-project-foundation` @ `fb0351e`  
**Method:** Code inspection + `AFTERIMAGE_STAGE_BYPASS` architecture review + existing unit/acceptance tests.  
**Honesty:** No Ableton/DAW ear A/B in this audit. Measurement + stage-isolation logic only. Limited Standalone listening is deferred to later phases if a binary is available on the engineer machine.

---

## Stage-bypass map (`Constants.h`)

| Bit | Mask | Stage |
|-----|------|-------|
| A | 1 | SpectralEngine (leave wet = undelayed input copy) |
| B | 2 | Force Mix dry (output delayed dry after mix) |
| C | 4 | HARMONICS (`ScaleAccentuator`) |
| D | 8 | Formant / De-Esser / Reverb |
| E | 16 | Parametric EQ |
| F | 32 | Gain Match |

Recommended compile flags for isolation:

| Test | `-DAFTERIMAGE_STAGE_BYPASS=` | Intent |
|------|------------------------------|--------|
| A Dry | `62` (`B\|C\|D\|E\|F`) | Latency-aligned dry; all creative stages skipped |
| B Spectral core | `60` (`C\|D\|E\|F`) | Shadow/Erase + Mix only |
| C+ Harmonics | `56` (`D\|E\|F`) | Core + HARMONICS |
| C+ FX | `52` (`C\|E\|F`) | Core + Formant/DeEsser/Reverb |
| C+ EQ | `44` (`C\|D\|F`) | Core + Parametric EQ |
| C+ Match | `28` (`C\|D\|E`) | Core + Gain Match |

---

## TEST A — Latency-aligned dry

**Setup (measurement):** Soft Shadow / Mix=0 / all optional enables OFF / Gain Match OFF. Equivalent to shipping defaults with Mix forced dry.

**Evidence (`./build/AFTERIMAGE_Tests`, 2026-08-09):**

- `Cleanup: optional modules off = Mix0 dry identity... Mix0+modules-off maxErr=0 lat=4096`
- Effect-strength Mix0: `Mix0 maxErr=0 latency=4096`
- STFT round-trip identity: maxErr ≤ ~7.5e-8 across 44.1/48/96 kHz and varied block sizes; impulse peak exactly at reported latency 4096

**Listen / measure criteria:** Output must match input delayed by `getLatencySamples()` (4096). No tone color, no noise floor rise, no combing.

**Verdict:** **PASS.** Dry path is clean. PDC report matches impulse peak.

---

## TEST B — Spectral core only (Shadow / Erase)

**Setup:** Bypass mask `60` (HARMONICS+FX+EQ+Match off). Soft Shadow defaults (Influence 0.50, Blur 0.22) and Erase at mid Influence. Mix = 1.

**Evidence:**

| Check | Result |
|-------|--------|
| Influence 0% Shadow identity | PASS (ValidationTests) |
| Influence 0% Erase identity | PASS |
| Engine Influence0 maxErr | ~4e-8 … 6e-8 vs delayed dry |
| Shadow measurable vs identity | Non-zero cumulative diff after history fill (expected wash) |
| Round 2: no isolated ringing partials | PASS |
| Round 2: stable tail pitch (centroid) | PASS |
| Round 2: HF damps faster than low-mids | PASS |
| Erase musical-noise frame variance | Finite, bounded (`varOut < 1e3`) |
| Soft Shadow factory | All optional modules OFF |

**Git core history (relevant):** Shadow multi-tap → SpectralTail (`2ca8b15`…), shimmer disabled (`23415a7`), power-domain tail / omega hold / diffusion / HF damp — all aimed at reducing metallic/jitter artifacts. Post-core FX stacked later (`cae813a`, cleaned in `fb0351e`).

**Listen criteria (when ear A/B available):** Soft Shadow should sound like a diffused spectral ghost of recent audio — not a vocoder, not pitched partials ringing, not pinkish resynth noise. Erase should hollow repeats without exploding musical noise.

**Verdict:** **Spectral core is NOT guilty of monotonic/resynthesized/noisy product failure when optional stages are off.** Do **not** rewrite `SpectralEngine` / `SpectralModes` / `STFTProcessor` / history / profile / tail in Phase 2 unless new ear evidence contradicts these measurements.

**Phase B action:** No core restore required. Preserve Shadow/Erase baseline; port only proven bugfixes if later listening finds a specific regression.

---

## TEST C — Optional stages one at a time

Artifacts below are **code-risk classifications** (what each stage can introduce). Full ear confirmation pending Standalone / DAW listen.

| Stage | Enable | Likely artifact if dirty | Identity when off? |
|-------|--------|--------------------------|--------------------|
| **HARMONICS** (`ScaleAccentuator`) | `harmonicsEnabled` | Comb/phase mush, resonator ring, filtered “in-key varnish”; 48 BP × ch × sample | Yes (skip process) |
| **Formant** (3 peaking EQ) | `formantEnabled` | Fake vowel EQ tilt — not true formant shift; can sound nasal/EQ-y | Centre ≈ dry blend; disable skips |
| **De-Esser** | `deEsserEnabled` | Dynamic HF duck (intended); mis-set can dull | Skip when off |
| **Reverb** | `reverbEnabled` + wet | Space wash; Pre/Post EQ still present on wet branch (complexity, not dry-color after cleanup) | Off / wet≈0 identity |
| **Parametric EQ** | `eqEnabled` + bands | Tone sculpt; spectrum probe crude (`sqrt(peak)*2.5`, FFT 512) — UI misleading more than audio dirty | Off / no bands = skip |
| **Gain Match** | `gainMatch` | Slow level trim; wrong if latency misaligned | Off = identity scalar path |

**Primary product-sound suspects for “monotonic / resynthesized / noisy” perception (ranked):**

1. **HARMONICS 48-band BP bank** — classic resonator-bank mush when Color > 0 (see below).  
2. **Fake FormantShifter** — three peaking filters sold as formant shift.  
3. **Session leftovers** — older sessions with FX/EQ left on while judging “Soft Shadow.”  
4. **Spectral core** — **not indicated** by measurement when options off.

Legacy `AutoTune.h` remains in tree but is **not** on the shipping process path after Phase 1 cleanup. Re-introducing delay-line Autotune would restore read-head jumps + unreported latency — Phase G replaces with a proper TUNE engine instead.

---

## HARMONICS / ScaleAccentuator — resonator-bank risk

`ScaleAccentuator` runs **48 band-pass biquads** (MIDI C2–B5) per channel every sample when enabled:

- In-key weight ≈ 1, out-of-key ≈ 0.35 (attenuation, not true energy redistribution).  
- Color 0–1 = wet of summed BP bank; Color > 1 adds extra in-key resonance.  
- Risk: comb filtering, phase cancellation, metallic ring, “synthy” varnish — often described as monotonic / resynthesized even though it is time-domain filtering.

**Phase C decision:** Remove HARMONICS from shipping UI + DSP path entirely. Preserve obsolete param IDs (`harmonicsEnabled`, `scaleColor`, `scaleTransient`) for session compat only. Replace page with automatic pitch-correction **TUNE**.

---

## DSP order decision (TUNE placement)

**Implemented order:**

```text
INPUT → SPECTRAL MEMORY → TUNE (fixed 1024-sample delay; OFF = identity delay)
      → latency-aligned Mix (dry delay = 4096 + 1024)
      → FORMANT (opt) → DE-ESSER (opt) → REVERB (opt)
      → EQ (opt) → GAIN MATCH → BYPASS → OUTPUT
```

**Decision:** TUNE sits **after SpectralEngine, before Mix** (not post-mix).

**Why:** A post-mix TUNE with fixed latency misaligns dry vs wet at the Mix stage unless an extra wet delay is inserted. Pre-mix TUNE shares one fixed delay with the dry path, so PDC and Bypass stay correct whether TUNE is on or off. Correction still applies to the spectral wet (Shadow/Erase ghosts) before equal-power Mix. Alternate (TUNE before memory) would retune material entering history and change Freeze/Recall character — rejected for v1.

Host latency report: **5120 samples** (4096 STFT + 1024 TUNE).

---

## Phase A → next

| Phase | Status |
|-------|--------|
| A Audit | Done |
| B Core restore | **Not required** — core proven clean |
| C Remove HARMONICS → TUNE | Done |
| D EQ | Done (SLOPE, pre-EQ spectrum, analyzer) |
| E Reverb | Done (SAFE BASS; Pre/Post EQ removed) |
| F Formant | Done (spectral-envelope warp) |
| G TUNE engine | Done (YIN + fixed-latency OLA) |
| H Nav/UI | Done (MEMORY\|TUNE\|EQ\|FX glass nav + chips) |
| I Tooltips | Done |
| J ASCII | Done (user-facing Center / x / ...) |
| K Docs/tests | Done |

---

## Listening honesty log

| Date | Method | Result |
|------|--------|--------|
| 2026-08-09 | Unit/acceptance tests only | Core identity + Mix0 identity PASS; Soft Shadow optional-off PASS |
| 2026-08-09 (Phase 2) | Unit/acceptance after TUNE latency | ctest PASS; Mix0 identity with latency 5120 |
| — | Standalone ear | Not performed as formal A/B |
| — | Ableton/DAW ear | Not performed |
