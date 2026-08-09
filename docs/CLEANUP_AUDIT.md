# AFTERIMAGE Cleanup Audit

**Date:** 2026-08-08  
**Branch:** `cursor/phase-1-project-foundation` @ `cae813a`  
**Working tree:** clean at audit start  
**Scope:** Restore coherent AFTERIMAGE (spectral memory). No mastering engine. No new feature sprawl.

---

## Executive snapshot

Recent commit (`cae813a`) stacked post-chain FX, Scale/Auto-Tune, and parametric EQ onto a solid Shadow/Erase spectral core. The core STFT path is largely sound; the post-core layer introduces **RT-unsafe coeff factories**, **hidden AutoTune latency**, **misleading EQ UI**, **reverb Pre-EQ coloring dry at wet=0**, and **product confusion** (conventional Autotune vs creative spectral memory).

**No adaptive mastering / LUFS / MASTER-LOUD-REFINED-CRUNCHY code exists in this repo.** Merge DSP remains for tests only; product UI is Shadow/Erase.

---

## A. Current signal flow

Exact order from `AfterimageAudioProcessor::processBlock` → `processChunk`:

```text
Input (host buffer)
  → handleMidi (note-held → pitch-class mask for Scale/AutoTune)
  → updateParameterTargets()  // every block: APVTS → all modules
  → [chunk loop ≤ 2048]
       wet = buffer chunk
       dryIn = inputScratch copy of undelayed input

       1. SpectralEngine::process(wet)
            STFT 4096 / hop 512 / Hann / WOLA
            history + SpectralMemoryProfile
            Shadow | Erase (Merge selectable only via legacy tests / internal enum)
            latency = fftSize = 4096 samples  (reported via setLatencySamples)

       2. DryWetMixer::processDryDelay(dryIn → delayedDry)
            delay = engine.getLatencySamples() = 4096

       3. Equal-power Mix into wetChunk
            dryGain = cos(mix·π/2), wetGain = sin(mix·π/2)
            sample-smoothed Mix

       4. FormantShifter::process(wet)          // always called
       5. DeEsser::process(wet)                 // always called
       6. PostChainReverb::process(wet)         // always called
            Pre 4-band EQ on FULL signal
            copy “dry” = post-PreEQ
            JUCE dsp::Reverb (100% wet internally)
            Post 4-band EQ on wet
            blend: out = dry*(1−wet) + wet*wetAmount

       7. Pitch path (exclusive by pitchPath choice)
            0 Off      → skip
            1 Scale Snap → ScaleAccentuator (48-band BP bank)
            2 Auto-Tune → AutoTune (autocorr + delay-line shift)

       8. ParametricEQ::process(wet)            // always called; bands default Off

       9. Per-sample:
            Gain Match scalar vs delayedDry power
            Bypass crossfade to delayedDry
            Entitlement dry (licensing)
            Output Gain

       10. Meters: input = delayedDry peak; output = final wet peak
```

**Documented vs code drift:** `HANDOFF.md` still says FFT 2048 / Gain Match immediately after Mix (omits post-chain). `docs/EFFECT_ENGINE.md` still says STFT 2048/512. `Constants.h` / README correctly say **4096/512**.

---

## B. Every user-visible control

### Global chrome (all views)

| Control | Param ID | DSP behavior |
|---------|----------|--------------|
| AFTERIMAGE wordmark | — | Brand / tooltip only |
| License chip | — | Licensing UI (message thread) |
| FILL % | — | `historyFill` viz atomics |
| MEMORY / SCALE / EQ tabs | — | Editor view only |
| Preset | programs | `FactoryPresets::applyPreset` (params only; clears history) |
| SHADOW / ERASE | `mode` | Spectral mode (Merge not in choice list) |
| I/O meters | — | delayedDry vs final peaks |
| MATCH | `gainMatch` | Broadband scalar post-EQ vs delayed dry |
| POWER (Bypass) | `bypass` | Crossfade to delayed dry |

### MEMORY view — dock

| Control | Param ID | DSP |
|---------|----------|-----|
| MEMORY | `memoryLength` | Active history search window (0.1–10 s) |
| FORGET | `forget` | Retention / Shadow tap decay |
| INFLUENCE | `influence` | Mode mix amount (mapped curve) |
| BLUR | `blur` | Mode-specific spectral smear/widen |
| TRANSIENT | `transientPreserve` | Duck effect on attacks |
| RANDOM | `randomRecall` | Slow wander around Recall |
| MIX | `mix` | Equal-power dry/wet |
| OUTPUT | `outputGain` | Final gain dB |
| FREEZE | `freeze` | Capture/hold memory profile |
| Recall ring (Well) | `recallPosition` | Age into history (0=new … 1=old) |

### MEMORY view — PostChainPanel

| Control | Param ID | DSP |
|---------|----------|-----|
| Reverb Type | `reverbType` | Spring / Hall / Room param set |
| Reverb Wet | `reverbWet` | Post-reverb blend amount |
| Pre EQ B1–B4 gain (+freq via APVTS) | `preEqNFreq/Gain` | FourBandEQ before verb |
| Post EQ B1–B4 | `postEqNFreq/Gain` | FourBandEQ on wet return |
| Formant | `formant` | 0=Low … 0.5=centre … 1=High peaking tilt |
| De-Esser | `deEsser` | Dynamic HF shelf intensity |

### SCALE view

| Control | Param ID | DSP |
|---------|----------|-----|
| Pitch Path | `pitchPath` | Off / Scale Snap / Auto-Tune |
| Root | `scaleRoot` | Scale root PC |
| Scale Type | `scaleType` | Major…Chromatic masks |
| COLOR | `scaleColor` | ScaleAccentuator wet 0–1 + resonance >1 |
| TRANSIENT | `scaleTransient` | Attack duck of snap wet |
| RETUNE | `retuneSpeed` | AutoTune slew |
| HUMANIZE | `humanize` | AutoTune reduce correction when stable |
| MIDI hint | — | Held notes override scale mask |

### EQ view

| Control | Param ID | DSP |
|---------|----------|-----|
| EQ Mode | `eqChannelMode` | Stereo / LR / MS routing |
| Band 1–8 select | — | UI only |
| ON | `eqNOn` | Enable band |
| S | `eqNSolo` | Audition band (first solo+enabled) |
| Type | `eqNType` | LP/HP/LS/HS/Bell/Notch |
| FREQ / GAIN / Q | `eqNFreq/Gain/Q` | Band params |
| x4 | `eqNX4` | Cascade 4× LP/HP only |
| Spectrum / nodes | — | Probe + interactive drag |

---

## C. Broken / misleading controls

| Issue | Severity | Detail |
|-------|----------|--------|
| **EQ ×4 stacked buttons** | High | All 8 `x4Buttons_` are `addAndMakeVisible`; `resized()` only places `x4Buttons_[selected_]`. Others keep stale/zero bounds → **stacked invisible hit-targets**. |
| **EQ Solo not exclusive** | High | Multiple `eqNSolo` can be true; DSP takes **first** solo∩enabled. GUI/DSP disagree with “solo” mental model. Solo ignored if band Off. |
| **EQ Stereo / LR / MS** | High (product) | One param set for all modes. LR/MS only duplicate filter **state**, not independent controls → **fake channel modes**. Prefer Option A: stereo-linked only. |
| **Scale Root tooltip = unavailable** | Med | `ScalePanel` sets `rootBox_.setTooltip(tooltips::unavailable)` and never replaces it. |
| **Reverb wet=0 ≠ identity** | High | Pre-EQ always processes the full signal; dry path is post-PreEQ. Non-zero Pre gains color output at wet=0. Reverb/Post-EQ still run (CPU + state). |
| **Formant/DeEsser always in chain** | Med | No enable flags; “neutral” relies on defaults. Formant centre is dry-blend identity for audio but still updates coeffs. |
| **ScaleAccentuator “redistribution” claim** | Med | Comment claims out-of-key energy redirected; implementation is **48 BP filters summed** with weights — not true energy redistribution. Can sound filtered/phasey. |
| **Auto-Tune as product feature** | High | Conventional monophonic delay-line tuner conflicts with AFTERIMAGE identity; poor quality, RT spikes, hidden latency. |
| **Merge in docs/tests** | Low | Correctly removed from UI; docs/HANDOFF still mention Merge as product in places. |
| **Preset apply incomplete** | Med | `applyPreset` does not reset `pitchPath`, scale, EQ, Gain Match already off — loading Soft Shadow can leave prior EQ/Scale engaged. |
| **View tabs tiny / buried** | Med | MEMORY\|SCALE\|EQ share crowded top bar; SCALE not “HARMONICS”; no FX page (FX buried in Memory right column). |
| **Pre/Post EQ freq** | Low | Freq only via automation/APVTS; UI shows Hz as knob **name**, not a dedicated freq control — easy to miss. |

---

## D. RT-safety violations

Reachable from `processBlock` / `processChunk`:

| Location | Problem |
|----------|---------|
| **`AutoTune::updatePitchDetect`** | Brute autocorrelation O(N×lag) ~ every 45 ms of audio (`corrBuf` ≈ 0.045·sr). At 48 kHz: N≈2160, lags ~53–800 → **~1M+ MACs** in one callback spike. |
| **`AutoTune` read-head snap** | `r = w - targetBehind` when behind drifts → **discontinuous delay read** (clicks/glitches). |
| **`FormantShifter::updateFilters`** | `IIR::Coefficients::makePeakFilter` → **heap `Ptr` alloc** every 64 samples while processing. |
| **`DeEsser::updateShelf`** | `makeHighShelf` → **heap alloc** every 32 samples when active. |
| **`FourBandEQ::updateBand`** | `makePeakFilter` → **heap alloc** when Pre/Post EQ knobs move (called from `updateParameterTargets` on audio thread). |
| **`ScaleAccentuator::rebuildFilters`** | Called from `setParams` on audio thread when root/scale/MIDI mask changes — stack biquads OK, but **48 bands × trig** in callback; heavy. |
| **`ScaleAccentuator::process`** | 48 biquads × channels × samples every block when Snap on — CPU heavy (not alloc, but RT risk). |
| **`ParametricEQ::setBand` / `updateBandCoeffs`** | Uses stack `Biquad` helpers — **OK** (no JUCE Coefficients Ptr). |
| **`updateParameterTargets` every block** | Touches all modules; Fine if setters are cheap; currently triggers FourBandEQ coeff rebuilds with alloc. |

Core spectral path (`SpectralEngine`, STFT, history, modes): prepare-time allocation; process paths appear preallocated. Licensing refresh is message-thread only.

---

## E. Latency

| Path | Actual latency | Reported (`setLatencySamples`) |
|------|----------------|----------------------------------|
| Spectral STFT wet | **4096 samples** (`fftSize`) | **4096** ✅ |
| Dry delay alignment | 4096 | Matches STFT ✅ |
| Mix / Formant / DeEsser / Reverb / EQ (biquad) | ~0 samples (IIR group delay negligible vs FFT) | N/A |
| **AutoTune delay line** | **~0.02·sr (~882 @ 44.1k)** additional when path=2 | **NOT reported** ❌ |
| ScaleAccentuator | ~0 (filters) | OK |
| Bypass / Mix=0 | Aligned to delayed dry | Correct relative to reported latency ✅ |

**Risk:** Host PDC compensates 4096, but Auto-Tune path adds ~20 ms → **dry/wet phase mismatch**, combing, and PDC-wrong A/B when Autotune engaged.

---

## F. Sound-quality risks (ranked)

1. **AutoTune read-head jumps + monophonic delay shift** — glitches, metallic artifacts on poly/complex material.  
2. **Unreported Autotune latency** — combing vs dry, broken Gain Match A/B.  
3. **Autocorr CPU spikes** — dropouts under load.  
4. **IIR::Coefficients heap on audio thread** (Formant/DeEsser/FourBandEQ) — priority inversion / jitter.  
5. **Reverb Pre-EQ on dry at wet=0** — unexpected tone change; “off” not clean.  
6. **ScaleAccentuator 48-band sum** — comb/phase mush; oversold as harmonic redistribution.  
7. **Optional modules always running** — subtle state/denormal/CPU even when “off”.  
8. **EQ multi-solo / wrong solo audition** — users hear truncated band-only signal, misjudge boosts.  
9. **Post-chain after Mix at defaults with Influence up** — Soft Shadow + any leftover FX from session = degraded “default” perception.  
10. **Docs/HANDOFF FFT 2048 vs real 4096** — wrong latency expectations for integrators.

---

## G. Product-scope violations

| Item | Verdict |
|------|---------|
| Adaptive mastering / LUFS / clipper / decision EQ | **Absent** — nothing to quarantine |
| Conventional Auto-Tune product path | **Out of scope for AFTERIMAGE identity** — remove from shipping path |
| Scale Snap as “pitch correction” framing | **Rework → HARMONICS** spectral sweetener |
| Merge as product mode | **Already removed** from UI; keep DSP for tests / git history |
| Post reverb / formant / de-esser | **Secondary FX** — keep only if identity-safe with explicit enables |
| Parametric EQ | **In scope** as optional creative stage — repair, don’t master |
| Gain Match | **In scope** (utility, not mastering) |

---

## H. Keep / Rework / Remove matrix

| Feature | Decision | Notes |
|---------|----------|-------|
| STFT + history + SpectralMemoryProfile | **KEEP** | Core product |
| Shadow / Erase | **KEEP** | Product modes |
| Merge DSP | **KEEP (legacy/tests)** | Not in UI; migrate sessions → Shadow |
| Influence / Recall / Forget / Blur / Transient / Freeze / Random / Mix | **KEEP** | Core controls |
| Output Gain / Bypass / Gain Match | **KEEP** | Utility |
| Factory presets | **REWORK** | Default = spectral core only; reset all optional modules; Soft Shadow reverbWet=0 |
| Parametric EQ | **REWORK** | Fix ×4, exclusive solo, remove channel modes, smooth coeffs, `eqEnabled` |
| ScaleAccentuator | **REWORK → HARMONICS** | Spectral sweetener; honest docs; identity when off |
| AutoTune | **REMOVE from shipping** | Quarantine file via git; no process path |
| Formant | **EVALUATE → KEEP if RT-safe + enable** | Neutral at centre; skip when disabled |
| DeEsser | **EVALUATE → KEEP if RT-safe + enable** | Skip when intensity/enable off |
| PostChainReverb | **REWORK** | dry ‖ (pre→verb→post) → mix; wet=0 / disabled = identity |
| Pre/Post reverb EQ | **KEEP** on wet branch only | |
| EQ LR/MS modes | **REMOVE** (Option A) | Stereo-linked only |
| `pitchPath` Off/Snap/Tune | **REWORK** | → `harmonicsEnabled` + HARMONICS params |
| Retune Speed / Humanize | **REMOVE** with Autotune | Optional Tightness only if HARMONICS needs it |
| UI MEMORY\|SCALE\|EQ | **REWORK** | MEMORY \| HARMONICS \| EQ \| FX |
| Mastering code | **N/A** | None present |

---

## I. Proposed final signal flow

**Preferred (matches product brief):**

```text
INPUT
  → SPECTRAL MEMORY (STFT + Shadow/Erase + history)
  → LATENCY-ALIGNED DRY/WET MIX
  → HARMONICS (opt, skip if !harmonicsEnabled)
  → FORMANT (opt)
  → DE-ESSER (opt)
  → REVERB wet branch (opt): dry ‖ (PreEQ → Verb → PostEQ) → mix
  → PARAMETRIC EQ (opt, skip if !eqEnabled / all bands off)
  → GAIN MATCH
  → PLUGIN BYPASS
  → OUTPUT GAIN
  → OUTPUT
```

**Critical evaluation vs current:**

| Change | Why |
|--------|-----|
| HARMONICS before Formant/DeEsser/Reverb | Sweetener on spectral mix before space/tone; avoids reverb wash of filter-bank mush |
| Reverb after Formant/DeEsser | Matches brief; Formant then DeEsser then space is conventional and fine |
| EQ last creative stage | Correct — sculpt final wet before Match |
| Gain Match after all creative FX | Already correct (measures completed chain) |
| Identical reported latency on all paths | No delay-line pitch shifter → STFT-only latency |

**Possible alternate:** HARMONICS after EQ — rejected for v1; sweetener should flavor the memory mix, EQ remains the final sculpt.

**Compile-time stage bypass A–F** (developer, not commercial UI): gate Spectral / Mix / Harmonics / FX / EQ / Match for regression listening.

---

## J. Proposed UI

```text
┌─ Top bar (simplified) ─────────────────────────────────────────────┐
│ AFTERIMAGE │ FILL │ preset │ SHADOW/ERASE │ meters │ MATCH │ BYPASS │
├─ Large nav ────────────────────────────────────────────────────────┤
│   [ MEMORY ]   [ HARMONICS ]   [ EQ ]   [ FX ]                     │
├─ Page body ────────────────────────────────────────────────────────┤
│ MEMORY: Well + Freeze + dock (MEMORY…OUTPUT)                       │
│ HARMONICS: On/Off, Root, Scale, Color, Transient (+ Tightness?)    │
│ EQ: spectrum, 8 bands, one ×4 for selection, exclusive Solo        │
│ FX: Reverb+Pre/Post EQ, Formant, DeEsser — each with enable        │
└────────────────────────────────────────────────────────────────────┘
```

- Nav tabs large / impossible to miss (not tiny top-right chips).  
- MATCH global (keep).  
- No Autotune panel.  
- Tooltips: never `unavailable` on working controls.  
- Merge = legacy only in docs.

---

## K. Files to change (implementation plan)

### Phase 1 — Inventory acted (this doc)
- `docs/CLEANUP_AUDIT.md` ✅

### Phase 2 — RT-safety + latency
- `Source/DSP/FormantShifter.h` — replace JUCE Coefficients Ptr with `Biquad.h`; smooth coeffs  
- `Source/DSP/DeEsser.h` — same  
- `Source/DSP/FourBandEQ.h` — same  
- `Source/DSP/AutoTune.h` — disconnect from processor (file retained until Phase 6 delete/quarantine)  
- `Source/PluginProcessor.cpp/.h` — stop Autotune process; latency remains STFT-only  
- `Source/DSP/PostChainReverb.h` — begin identity-safe wet routing (complete in Phase 7)

### Phase 3 — Clean baseline + stage bypass
- `Source/Utilities/Constants.h` — `AFTERIMAGE_STAGE_BYPASS` bitmask A–F  
- `Source/PluginProcessor.cpp` — skip optional modules when disabled/neutral  
- `Source/Utilities/FactoryPresets.h` — Soft Shadow = core only; reset pitch/EQ/FX enables  
- Defaults: harmonics/formant/deEsser/reverb/eq off or identity

### Phase 4 — UI rebuild
- `Source/PluginEditor.h/.cpp` — four pages, large nav, simplify top bar  
- Rename Scale → Harmonics panel; move PostChainPanel → FX page  
- `Source/UI/ScalePanel.h` → HarmonicsPanel (or rewrite)  
- `Source/UI/PostChainPanel.h` — FX page layout + enables  
- `Source/UI/AfterimageTooltips.h` — fix unavailable; new copy

### Phase 5 — Parametric EQ repair
- `Source/UI/ParametricEqPanel.h` — single ×4 for selected band; exclusive solo radio; remove mode combo  
- `Source/DSP/ParametricEQ.h` — stereo-linked only; exclusive solo; coeff smoothing; skip if all off  
- Remove / ignore `eqChannelMode` (migrate away)

### Phase 6 — HARMONICS
- **First:** `docs/HARMONICS_ENGINE.md`  
- Rework `ScaleAccentuator.h` (or new `HarmonicsEngine.h`)  
- Remove Autotune from shipping includes/CMake usage  
- Params: `harmonicsEnabled`, root, scale, color, transient  

### Phase 7 — FX evaluation
- Fix `PostChainReverb` routing; explicit `reverbEnabled`, `formantEnabled`, `deEsserEnabled`  
- Identity tests for wet=0 / enables off  

### Phase 8 — Tests + docs
- `Tests/ValidationTests.cpp` (+ new cases): identity, params, latency impulse  
- `docs/UI_CONTROL_AUDIT.md`, `docs/REALTIME_SAFETY_AUDIT.md`  
- Update `README.md`, `HANDOFF.md`, `docs/EFFECT_ENGINE.md`, `docs/HARMONICS_ENGINE.md`

---

## Must-investigate checklist (user-flagged)

| # | Item | Audit finding |
|---|------|---------------|
| 1 | AutoTune remove | Confirmed conventional; remove from shipping |
| 2 | Autocorr + read jumps | Confirmed RT/perf/glitch |
| 3 | Hidden latency | ~20 ms unreported when Autotune on |
| 4 | IIR coeff on audio thread | Formant, DeEsser, FourBandEQ confirmed Ptr alloc |
| 5 | EQ ×4 stacking | Confirmed layout bug |
| 6 | EQ exclusive solo | Not exclusive today |
| 7 | Fake LR/MS | Confirmed; Option A remove |
| 8 | Scale → HARMONICS | Plan Phase 6 |
| 9 | ScaleAccentuator honesty | Filter bank, not redistribution |
| 10 | Reverb dry path | Pre-EQ affects dry; fix Phase 7 |
| 11 | Neutral optional modules | Missing enables; Formant/DeEsser/Reverb always process |
| 12 | Default preset core-only | Soft Shadow OK-ish; presets don’t reset EQ/Scale |
| 13 | Stage bypass A–F | Partial DebugAudition exists for modes; need full chain gates |
| 14 | UI four pages | Currently 3; FX buried in Memory |
| 15 | UI_CONTROL_AUDIT.md | Phase 8 |
| 16 | Tooltip audit | Root = unavailable; fix |
| 17 | Merge legacy only | UI OK; docs drift |
| 18 | Explicit module enables | Missing — add |
| 19 | REALTIME_SAFETY_AUDIT.md | Phase 8 |
| 20 | Identity + latency tests | Extend Phase 8 |
| 21 | HARMONICS_ENGINE.md first | Before Phase 6 code |

---

## Priorities (execution order)

1. Restore excellent sound quality  
2. Keep AFTERIMAGE ≠ mastering  
3. Eliminate RT/audio bugs  
4. Every visible control works  
5. Major pages impossible to miss  
6. EQ reliable  
7. Replace Autotune with HARMONICS  
8. Clean routing  
9. Evaluate secondary FX  
10. Polish visuals  

---

## Definition of Done (acceptance)

- Open plugin → spectral memory is obvious  
- Nav: MEMORY \| HARMONICS \| EQ \| FX  
- EQ: one ×4, exclusive solo, stereo-linked, no fake modes  
- Every button real; no unavailable tooltips on live controls  
- HARMONICS = polyphonic scale-aware spectral sweetener, not Autotune  
- Defaults not degraded; bypass restores core  
- No stacked buttons / fake channel modes / hidden latency / brute pitch / audio-thread alloc / mastering code  

---

*End of Phase 0 audit. Implementation begins Phase 1→8 sequentially after this file.*
