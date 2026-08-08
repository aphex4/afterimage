# AFTERIMAGE — Handoff for External Collaboration

**Paste this entire document into ChatGPT (or similar) as project context.**  
Repo: https://github.com/aphex4/afterimage  
Branch: `cursor/phase-1-project-foundation`  
Local path (author machine): `~/dev/AFTERIMAGE`  
Marketing version: **1.0.0-rc.1** (CMake/JUCE VersionCode stays `1.0.0` / `0x10000` — RC string cannot be fed into JUCE’s integer version parser)

---

## 1. One-paragraph product summary

**AFTERIMAGE** is a real-time creative audio effect (**VST3 + AU + Standalone** on macOS) built with **JUCE + CMake + C++17** (universal arm64/x86_64). Tagline: *Every sound leaves a ghost.*

It does **not** use a normal delay or reverb as the core. It runs an **overlap-add STFT**, stores a circular buffer of **spectral frames**, builds a stabilized **SpectralMemoryProfile** (~200 ms Gaussian window) for recall, and lets the live spectrum interact with that memory.

Modes: **Shadow**, **Erase** (Merge removed from product; legacy sessions → Shadow). Post-chain reverb/formant/de-esser, exclusive Scale Snap / Auto-Tune, 8-band parametric EQ, three editor views (Memory / Scale / EQ), MATCH in global top bar. Factory presets grouped by Shadow / Erase. Stereo Link deferred. Status: **release candidate** — do not claim READY FOR V1.0 without ear audition.

---

## 2. Development status (phased)

| Phase | Status | What it delivered |
|------|--------|-------------------|
| 1 Foundation | ✅ | CMake JUCE plugin, APVTS, dark UI, pass-through, state save/restore |
| 2 STFT | ✅ | FFT 2048 / hop 512 / Hann / WOLA, latency reporting, latency-aligned dry/wet |
| 3 History | ✅ | Per-channel spectral history, Freeze, Memory Length window, fill UI indicator |
| 4 Shadow | ✅ | Multi-age additive spectral tail + diffusion |
| 5 Erase + Merge | ✅ | Familiarity carve + dual-profile envelope morph + mode crossfade |
| 6 Blur / Forget / Transients / Random | ✅ | Mode-specific Blur; Forget + transient; Random = slow wander |
| 7 Memory Well viz | ✅ | DSP-seeded circular particles + Recall ring |
| 8 Presets / polish | ✅ | Mode-bank factory presets, dynamic tooltips; Stereo Link deferred |
| Spectral memory RC | ✅ | SpectralMemoryProfile, Freeze capture, retuned engines (ear sign-off pending) |

Current milestone: **1.0.0-rc.1**.

---

## 3. DSP architecture (must preserve)

```
Input
  → copy undelayed input (meters / dry delay source)
  → SpectralEngine / STFTProcessor (in-place wet)
       per channel, every hop:
         window → FFT
         capture SpectralFrame (mag/phase/rms/centroid/flux)
         READ history / build SpectralMemoryProfile at Recall BEFORE pushing current
         apply SpectralModeProcessor (Shadow / Erase / Merge)
         write magnitudes back to FFT (keep current phase; Hermitian mirror)
         PUSH unmodified analysis frame into history (unless Freeze)
         IFFT → synthesis window → WOLA
  → delay dry by STFT latency
  → equal-power dry/wet mix
  → Gain Match (optional broadband scalar on completed mix vs dry)
  → smoothed bypass → entitlement dry (if licensing) → output gain
  → Output
```

### STFT constants (`Source/Utilities/Constants.h`)

- `fftSize = 2048`, `hopSize = 512` (4× overlap), Hann analysis **and** synthesis  
- WOLA scale measured from Σ window² (same approach as author’s CircleEQ plugin)  
- Host latency = **`fftSize` (2048 samples)**  
- Dry delayed by same amount to avoid comb filtering on Mix  

### Spectral frame

```cpp
struct SpectralFrame {
  std::vector<float> magnitudes;  // sized in prepare, never resized on audio thread
  std::vector<float> phases;
  float rms, spectralCentroid, transientStrength;
  uint64_t frameIndex;
};
```

### History + SpectralMemoryProfile

- History preallocated for **max 10 s** of frames at prepare time  
- Memory Length (0.1–10 s, default 3 s) only changes **search window**, not allocation  
- Recall uses a **SpectralMemoryProfile** (Gaussian-weighted ~200 ms window + variance stability), not a single raw frame  
- **Freeze**: capture stabilized recent window; crossfade ~100 ms; stop writes  
- State save/restore: **parameters only — never serialize live history**; clear on load  

### Shadow / Erase / Merge

**Influence mapping** (`mapInfluenceForMode`):
```
mapped = 1 - (1 - x)^exponent   // exact 0 at 0, exact 1 at 1
Shadow exp≈1.70, Erase≈2.05, Merge≈1.45
```

**Forget retention floor** (`remappedHistoryWeight`):
```
historyWeight = floor + (1 - floor) * ageWeightFromForget(...)
Shadow floor≈0.22, Erase=0.15, Merge=0.25
```

**Transient Preserve**: max reduction 0.65. Flux calibration `*3.2`.

**Shadow:** multi-age taps from Recall toward older memory + spectral diffusion; `out = current + tail * mixAmount`; allow up to ~`mappedInfluence * 3.5 dB` energy rise.

**Erase:** relative-prominence familiarity map (asymmetric attack/release). Contrast-sensitive dB carve; novel content passes. Freeze holds the stencil. Blur widens the mask.

**Merge:** dual-profile envelope morph (short current EMA vs memory profile). Blur widens envelopes / damps fine structure. Soft energy match (~94% toward unity, residual ≤ ±1.25 dB) so mid Influence does not amp ~4–6 dB before Gain Match. Current phase only.

**Blur:** mode-specific (Shadow diffusion / Erase mask / Merge envelope) — see tooltips / `docs/EFFECT_ENGINE.md`.

Current **phase kept** (no historical phase blending in production).

---

## 4. Key source files

```
AFTERIMAGE/
├── CMakeLists.txt
├── README.md
├── docs/EFFECT_ENGINE.md
├── docs/LICENSING.md
├── docs/PACKAGING.md          # codesign / notarization notes (no certs in repo)
└── Source/
    ├── PluginProcessor.*          # APVTS, processBlock, entitlement dry, factory programs
    ├── PluginEditor.*             # dark UI, knobs, mode-grouped presets, license chip
    ├── DSP/                       # STFT, history, SpectralMemoryProfile, modes
    ├── Licensing/                 # Ed25519 offline licenses (message-thread only)
    ├── UI/                        # LookAndFeel, MemoryWell, LicensePanel, tooltips
    └── Utilities/
        ├── Constants.h
        └── FactoryPresets.h       # Shadow / Erase / Merge banks
```

Company / codes: `AdamAudio`, manufacturer `Adam`, plugin code `AfIm`.  
JUCE: local checkout preferred at `~/dev/Spawnclone/JUCE`, else FetchContent 8.0.6.  
VST3 categories: `Fx|Filter|Modulation`.  
`COPY_PLUGIN_AFTER_BUILD`: **Release/commercial only** — Debug stays in the build tree.

**Default APVTS values** (new instances only; saved sessions keep stored values):  
Recall 40%, Influence 40%, Forget 25%, Blur 12%, Transient Preserve 35% — matches Soft Shadow.

---

## 5. Parameters (APVTS IDs — stable)

| ID | UI | Notes |
|----|-----|------|
| `mode` | Shadow / Erase / Merge | All three transform |
| `memoryLength` | Memory | 0.1–10 s, skewed short |
| `recallPosition` | Recall | 0–100%, default **40%** |
| `influence` | Influence | 0–100%, default **40%** (perceptual mid curve) |
| `forget` | Forget | 0–100%, default **25%** (retention floor) |
| `blur` | Blur | mode-specific; default **12%** |
| `transientPreserve` | Transients | default **35%**; max reduction 0.65 |
| `freeze` | Freeze | bool; stabilized profile capture |
| `randomRecall` | Random | slow wander around Recall Position |
| `outputGain` | Output | −24…+12 dB |
| `mix` | Mix | equal-power, latency-aligned |
| `bypass` | Bypass | smoothed |
| `gainMatch` | Gain Match | bool; broadband loudness trim of mix vs dry (default off) |

Smoothing lives in `ParameterSmoother` / processBlock. Mode crossfade ~80 ms in `SpectralModeProcessor`.

Factory presets are **Shadow / Erase / Merge** banks (ComboBox section headings). Loading a preset clears live history and resets Gain Match adaptive state.

---

## 6. Hard constraints (do not violate)

1. **No audio-thread heap allocation** (no vector resize, no `new`, no locks, no logging spam).  
2. **Genuine spectral memory** — not a delay line dressed up as “spectral.”  
3. History stores **frames**, not raw audio; modes consume **SpectralMemoryProfile** magnitudes.  
4. Don’t naïvely lerp wrapped phase; Shadow correctly keeps current phase.  
5. Don’t introduce unbounded feedback.  
6. Don’t save spectral history in presets/session state.  
7. Prefer extending `SpectralModeProcessor` over stuffing algorithms into `processBlock`.  
8. Keep STFT transparent when Influence = 0 / empty history.  
9. Match existing style: `afterimage::` namespace, RAII, JUCE module includes (not giant JuceHeader soup).

---

## 7. Build / test

```bash
cd ~/dev/AFTERIMAGE   # or clone the GitHub repo
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=$HOME/dev/Spawnclone/JUCE
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

Artefacts:

- Standalone: `build/AFTERIMAGE_artefacts/Release/Standalone/AFTERIMAGE.app`  
- VST3 (build): `build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3`  
- AU (build): `build/AFTERIMAGE_artefacts/Release/AU/AFTERIMAGE.component`  
- VST3/AU (installed, Release only): `~/Library/Audio/Plug-Ins/…`  

Manual check in Ableton Live 12: quit fully after rebuild so the binary reloads. Shadow: sustained pads/chords, Memory ~3 s, Influence 50–70%, Recall 40–60%, Mix 100%. Random: raise Random and listen for slow recall drift (not stutter).

---

## 8. What to work on next (recommended)

### Before calling it v1.0

- Ear A/B matrix (Shadow / Erase / Merge at 40–60% Influence, with/without Gain Match)
- Optional notarized packaging with local Developer ID (`docs/PACKAGING.md`)

### Deferred — Stereo Link

Independent L/R histories remain. Shared/averaged memory would need careful RT-safe design so it does not destabilize isolation or Influence≈0 identity. Prefer not shipping a half-baked link.

### Optional polish

- User preset save slots (beyond factory programs)
- Online activation / machine deactivation server
- Recall / Smear modes (product backlog)

Production public key is installed in `LicenseVerifier.cpp`. Keep the issuer secret offline in `../AFTERIMAGE-secrets/` (never commit). See `docs/LICENSING.md`.

---

## 9. Known pitfalls / design notes

- **Push vs read order matters:** history is read *before* committing the current frame so age 0 is the previous hop, not “self.” History stores **pre-mode analysis**, not the wet ghost.  
- **Stereo:** independent per-channel histories; Stereo Link deferred.  
- **JUCE real-only FFT layout:** bins use interleaved `re/im` at `2*k`, `2*k+1` for `k = 0 … N/2`, with Hermitian mirror on write-back (same pattern as author’s CircleEQ).  
- Branch name still says `phase-1-…` but contains Phases 1–8 + spectral-memory RC; don’t rename unless asked.  
- Unit tests under `Tests/` are wired via CMake (`AFTERIMAGE_Tests` / ctest).  
- Debug builds must not overwrite the user VST3/AU install.

---

## 10. How ChatGPT should help

When responding:

1. Read this handoff + relevant files from the repo before proposing large rewrites.  
2. Prefer **surgical diffs** aligned with existing classes (`SpectralModeProcessor`, `SpectralEngine::onSpectrum`, `SpectralMemoryProfile`).  
3. Call out RT-safety risks explicitly.  
4. After suggesting code, list **files touched**, **how to build**, and **how to A/B listen** in a DAW.  
5. Do not claim Stereo Link works unless implementing it.  
6. Do not claim READY FOR V1.0 without ear sign-off.  
7. If unsure about JUCE APIs, stick to patterns already in-tree (`juce::dsp::FFT`, APVTS, `SmoothedValue`).

---

## 11. Author / toolchain assumptions

- macOS, Apple Clang, CMake ≥ 3.22, Homebrew CMake available  
- Ableton Live 12 Suite present for manual VST3 testing  
- JUCE 8.x via `~/dev/Spawnclone/JUCE`  
- GitHub remote: `origin` → `https://github.com/aphex4/afterimage.git`

---

*End of handoff. AFTERIMAGE 1.0.0-rc.1: spectral-memory redesign + Gain Match + offline licensing; ear A/B and notarization remain blockers for final v1.0. Stereo Link deferred.*
