# AFTERIMAGE Sound Recovery Report (Stage 10)

**Branch:** `sound-recovery`  
**Freeze reference:** `pre-regression-recovery` @ `bb9ebb0`  
**Known-good reference:** `fede4e7` (FFT 2048/4×, multi-tap Shadow, ~200 ms memory)  
**Date:** 2026-08-08  

## Listening honesty

| Method | Result |
|--------|--------|
| Automated ctest (Release) | Run after Stages 1–5 |
| Standalone ear A/B | **Not performed as formal listening** |
| Ableton / DAW audition | **Not performed** |

Prefer Standalone + measurements for sign-off until a human ear pass is logged.

---

## §50 recovery items

1. **Root causes of regression**  
   STFT raised to 4096/8× (`9a4f1cd`); Shadow replaced by SpectralTail feedback (`2ca8b15`); per-hop phase diffusion (`2ce08d6`); memory window cut 200→90 ms (`c7738c1`); post-chain Tune/Formant/DeEsser/Reverb/EQ (`cae813a`…).

2. **Phase diffusion**  
   Gated behind `#if AFTERIMAGE_EXPERIMENTAL_PHASE_DIFFUSION` (default **OFF**). Production Shadow does not use it.

3. **SpectralTail**  
   Bypassed from production Shadow. Files remain as legacy/experimental only. Shadow is read-only multi-tap history (`fede4e7`).

4. **FFT before / after**  
   Before recovery: **4096 / hop 512 / 8×**. After: **2048 / hop 512 / 4×** (periodic Hann, hop-synced stereo).

5. **Memory profile**  
   Restored `memoryProfileWindowMs = 200`, freeze capture 200 ms, crossfade 100 ms. Freeze arms after ~100 ms ready threshold; capture still ~200 ms.

6. **Shadow algorithm**  
   `current + remembered * amount` via multi-age taps; stabilized profiles; contrast limiter (~15 dB); energy policy; **current-phase** resynthesis (default). No random phase; no feedback accumulator.

7. **Freeze algorithm**  
   Conservative: arm until history ready → capture ~200 ms stabilized magnitudes → hold → current-phase writeback → ~100 ms crossfade. No single-frame freeze; no recursive frozen output into memory; PropagatedGhostPhase remains OFF.

8. **Formant bugs**  
   Still present in code (OLA accum vs FIFO risk; unverified cepstral warp). **Hard-disabled** in production (`AFTERIMAGE_ENABLE_AUX_DSP=0`). UI pages hidden.

9. **De-Esser**  
   Not fixed this recovery. **Hard-disabled**; UI hidden.

10. **Disabled modules (production)**  
    Tune, Formant, De-Esser, Reverb, Parametric EQ (compile-time path removal + nav hide). Gain Match retained.

11. **Restored components**  
    Proven STFT 2048/4×; multi-tap Shadow; ~200 ms memory; fede Erase/Merge DSP; Soft Shadow conservative defaults; Debug safety counters; spike/noise-growth test.

12. **Peaks**  
    Emergency ceiling ±2.0 (~+6 dBFS) on STFT output. Spike test requires peak < 2.05.

13. **Noise floor / growth**  
    8 s noise stress: late RMS must stay < 4× early RMS (no uncontrolled feedback).

14. **Spike tests**  
    `testShadowSpikeAndNoiseGrowth` in ValidationTests.

15. **Silence / tail**  
    Influence 0% identity retained; multi-tap has no feedback ring after silence (by design vs SpectralTail).

16. **CPU**  
    Not formally re-profiled this pass; 2048/4× should be lighter than 4096/8× + SpectralTail.

17. **Latency**  
    Host report = STFT only = **2048** samples when aux off (was 5120 with Tune). Impulse test matches report.

18. **Debug build**  
    `build-debug` AFTERIMAGE_Tests built; `ctest --test-dir build-debug` **PASSED** (~10.6 s).

19. **Release build**  
    Standalone + VST3 + AU rebuilt; VST3/AU ditto-installed; artefact/install mtimes match (2026-08-08 ~19:54–19:55).

20. **CTest**  
    Release `ctest --test-dir build` **PASSED** (~1.7 s). Debug **PASSED**.

21. **Sanitizers**  
    Not run (ASan/UBSan not configured in this pass).

22. **DAW results**  
    None (not auditioned).

23. **Remaining limitations**  
    No formal ear A/B; Formant/DeEsser/Tune/Reverb/EQ unproven; Merge remains legacy DSP (not in product UI); Soft Shadow defaults conservative vs flashy demo.

24. **Release blockers cleared by recovery**  
    Feedback SpectralTail, production phase scatter, 4096-unproven STFT, 90 ms memory, always-on Tune latency, aux path in production.

25. **Release blockers remaining**  
    Human listening sign-off; optional aux modules if product requires them for v1.0 marketing.

26. **Preferred v1.0 shape**  
    CORE Shadow/Erase + Memory/Recall/Freeze/Blur/Forget/Transient/Random/Mix + Gain Match + Output. Aux UI hidden until proven.

27. **Gain stages**  
    Shadow influence mix → energy policy / contrast → STFT WOLA → Mix → (aux off) → Gain Match → Bypass → Output Gain. Emergency ceiling only after OLA.

28. **Defaults**  
    Soft Shadow: Memory 3 s, Recall 40%, Influence 38%, Forget 30%, Blur 20%, Transient 35%, Mix 100%, aux off.

---

## Stage checklist

| Stage | Status |
|-------|--------|
| 0 Freeze + known-good diff | Done (`docs/SOUND_RECOVERY_DIFF.md`) |
| 1 Aux hard-off | Done (`AFTERIMAGE_ENABLE_AUX_DSP=0`) |
| 2 Shadow restore / Tail+diffusion off | Done |
| 3 STFT 2048/4× identity | Done (maxErr ~1e-7) |
| 4 ~200 ms memory + Freeze | Done |
| 5 Numerical safety + spike/noise tests | Done |
| 6 Erase/Merge | Restored fede paths; Merge legacy UI; kept for tests |
| 7 Formant | Disabled / UI hidden until proven |
| 8 De-Esser | Disabled / UI hidden until proven |
| 9 One-at-a-time reintro | Deferred (none reintroduced) |
| 10 Full regression | Release+Debug ctest PASS; plugins installed; no sanitizer/DAW |
