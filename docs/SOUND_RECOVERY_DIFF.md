# AFTERIMAGE Sound Recovery Diff (Stage 0b)

**Date:** 2026-08-08  
**Freeze HEAD:** `bb9ebb0` (`pre-regression-recovery` / `sound-recovery` start)  
**Known-good reference (git, not guessed):** `fede4e7` — *Polish RC packaging, Merge energy policy, and mode-aware UI copy* (2026-08-08 12:23 PT)

## Why `fede4e7` is the known-good

| Criterion | Evidence |
|-----------|----------|
| Proven STFT | `Constants.h`: **FFT 2048 / hop 512 / 4×** (Phase 2 validated identity) |
| Stabilized ~200 ms memory | `memoryProfileWindowMs = 200`, freeze capture 200 ms, crossfade 100 ms (`3b8d5c7`) |
| Musical Shadow | Multi-tap **read-only** history path (`buildShadowTail` + current-phase resynthesis); no SpectralTail feedback |
| No random phase scatter | No `diffusion` / per-bin render-phase RNG on Shadow |
| Core spectral-memory clear | Freeze = capture stabilized profile, hold, current-phase; no recursive frozen output into memory |
| No post-chain noise sources | No Tune / Formant / De-Esser / Reverb / Parametric EQ on path |

**Rejected “known-good” candidates:**

- `985a7b9` (v1.0 RC polish): still 2048/4× but **before** stabilized multi-frame profiles (`3b8d5c7`) — Freeze/memory character weaker than the brief’s ~200 ms target.
- `1b808a8` (pre-SpectralTail): already on **4096/8×** (`9a4f1cd`) — not the proven STFT.
- Prior audit `docs/SOUND_REGRESSION_AUDIT.md` (`fb0351e`) concluded “core clean” from identity tests alone; user ear reports (spikes, broadband noise, worse Shadow) override that. Recovery restores architecture, not the audit’s verdict.

---

## Timeline of regressions (`fede4e7` → `bb9ebb0`, 26 commits)

| Commit | Change | Sound risk |
|--------|--------|------------|
| `9a4f1cd` | STFT **2048/4× → 4096/8×**, periodic Hann, hop-synced stereo | Longer latency, denser bins, different WOLA/incoherence; unproven vs Phase 2 identity suite |
| `1b808a8` | Remove input-referenced energy policy; free Influence | Louder wash possible; absolute ceiling only |
| `2ca8b15` | Shadow multi-tap → **SpectralTail feedback** + PV ghost phase + diffusion/shimmer | Recursive energy, phase scatter → grit/spikes/noise growth |
| `c7738c1` | Memory window **200 → 90 ms**; power-domain average | Shorter, less stable memory → less “musical” Shadow |
| `96800cb`…`8cd7a8e` | Tail omega hold, bin diffusion, HF damp, power accumulate, render-phase diffusion | Mitigations on a feedback design; can still wash/noise |
| `23415a7` | Disable Shadow shimmer detune | Partial mitigation only |
| `b45c811`…`e54eaaa` | SpectralBlur Merge path | Phase-decorrelated smear (Merge); risk if coupled to broken phase |
| `f004253` | Soft Shadow defaults Influence 0.50 / Blur 0.22 | Stronger demo; amplifies broken path |
| `cae813a`…`bb9ebb0` | Post-chain Tune / Formant / De-Esser / Reverb / EQ | Extra latency, fake/broken Formant, De-Esser, etc. |

---

## Architecture diff: known-good vs freeze HEAD

### STFT / FFT / hop / overlap / WOLA

| | Known-good `fede4e7` | Freeze `bb9ebb0` |
|--|----------------------|------------------|
| FFT | 2048 | **4096** |
| Hop | 512 | 512 |
| Overlap | **4×** | **8×** |
| Latency (STFT) | 2048 | 4096 |
| Window | Hann / WOLA (Phase 2) | Periodic Hann + hop-synced stereo |
| Identity | Validated float-noise | Tests adapted to 4096; not a restore proof |

**Why introduced:** finer frequency resolution, less hop jitter.  
**Expected benefit:** smoother stereo / less metallic Freeze.  
**Failure modes:** unvalidated WOLA at 8×; double latency; interacts badly with incoherent phase.  
**Necessary for core?** **No.** Stage 3 restores 2048/4× unless measurements prove otherwise.  
**Earlier better without?** Yes — Phase 2 + user report.

### Memory profile / smoothing / energy

| | Known-good | Freeze |
|--|------------|--------|
| Profile window | **200 ms** | **90 ms** (`c7738c1`) |
| Freeze capture | 200 ms | 200 ms (constant still) |
| Freeze crossfade | 100 ms | 100 ms |
| Averaging | Stabilized multi-frame (`3b8d5c7`) | Power-domain RMS, shorter window |

**Why 90 ms:** reduce partial smearing.  
**Failure mode:** less temporal stabilization → noisier, less musical recall.  
**Necessary?** **No** for v1 recovery — restore 150–250 ms (target 200).

### Shadow algorithm

| | Known-good | Freeze |
|--|------------|--------|
| Source | Multi-tap ages on **history buffer** (read-only) | **SpectralTail** per-bin feedback accumulator |
| Blend | `current + remembered * amount` (magnitude) | Tail processHop + complex add + OLA compensation |
| Phase | Current-frame phase (default) | PV ghost ω + **render-time Gaussian phase diffusion** |
| Feedback into memory | No | Tail sustains independently; Freeze can arm on tail |

**Conceptual restore target:**  
`shadowMagnitude = current + remembered * shadowAmount` with stabilized history, bounded gain, current-phase resynthesis; **no random phase; no uncontrolled feedback.**

### Phase scatter / diffusion

- Introduced: `2ca8b15` / `2ce08d6` (per-hop render offset).  
- Softened later (`tp.diffusion = blur * 0.45f`) but still ON in production.  
- **Stage 2:** remove from production; gate behind `#if AFTERIMAGE_EXPERIMENTAL_PHASE_DIFFUSION` (default OFF).

### SpectralTail

- Introduced to replace multi-tap for sustaining wash.  
- Failure modes: feedback energy growth, broadband grain, spikes when diffusion + OLA compensation mis-scale.  
- **Necessary for core?** **No.** Bypass/remove from production; Shadow uses read-only historical memory only.

### Freeze

- Known-good: ~200 ms magnitude history, stabilize, hold, current-phase, ~100 ms crossfade.  
- Later: Freeze arm waits for history **and Shadow tail** (`4f0dbb8`) — couples Freeze to feedback path.  
- Disable advanced PV Freeze / ghost oscillators / experimental phase locking for v1.0 recovery.

### Post chain (Tune / Formant / De-Esser / Reverb / EQ)

| Module | Why introduced | Expected benefit | Failure modes | Core? | Earlier better without? |
|--------|----------------|------------------|---------------|-------|-------------------------|
| **Tune** (`PitchTune`) | Replace HARMONICS | Pitch correction | Fixed +1024 latency always; YIN glitches | No | Yes |
| **Formant** | “Vowel” FX | Envelope warp | OLA ring (accum 2×FFT vs FIFO); may not be true cepstral warp; user: nonfunctional | No | Yes |
| **De-Esser** | Tame sibilance | HF duck | Coeff stepping; ineffective/incorrect | No | Yes |
| **Reverb** | Space | Wash | Extra complexity / energy | No | Yes |
| **Parametric EQ** | Tone | Transparent EQ | Misuse; spectrum probe misleading | No | Neutral if off |

Host latency freeze HEAD: **5120** (4096 STFT + 1024 Tune). Known-good: **2048**.

### Gain / Influence / defaults

- Soft Shadow bumped Influence 0.50 / Blur 0.22 (`f004253`) vs more conservative earlier.  
- Recovery defaults target: Memory ~3 s, Recall ~40%, Influence ~35–40%, Forget ~25–35%, Blur ~15–25%, Transient ~35%, Mix 100%.

---

## Recovery plan (binding order)

1. **Stage 1** — Hard-disable aux DSP on production path (not mix=0).  
2. **Stage 2** — Restore Shadow to known-good architecture; kill production phase scatter + SpectralTail feedback.  
3. **Stage 3** — Restore STFT 2048/512/4×; identity suite must pass.  
4. **Stage 4** — Restore ~200 ms memory + conservative Freeze.  
5. **Stage 5** — Hard numerical safety + spike/noise tests.  
6. **Stage 6** — Evaluate Erase/Merge independently.  
7. **Stages 7–9** — Formant / De-Esser / optional reintro one-at-a-time; keep disabled until proven.  
8. **Stage 10** — Full release regression + report.

**Preferred v1.0 if aux fails:** CORE (Shadow/Erase/Merge if stable) + Memory/Recall/Freeze/Blur/Forget/Transient/Random/Mix + Gain Match + Output. Hide broken aux UI.

---

## Listening honesty

| Method | Status |
|--------|--------|
| Git archaeology + code diff | Done (this doc) |
| Automated ctest / measurements | After each stage |
| Standalone ear | Only if performed and logged |
| Ableton / DAW audition | **Not claimed unless done** |
