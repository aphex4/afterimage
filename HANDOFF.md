# AFTERIMAGE — Handoff for External Collaboration

**Paste this entire document into ChatGPT (or similar) as project context.**  
Repo: https://github.com/aphex4/afterimage  
Branch: `cursor/phase-1-project-foundation`  
Local path (author machine): `~/dev/AFTERIMAGE`

---

## 1. One-paragraph product summary

**AFTERIMAGE** is a real-time creative audio effect (VST3 + Standalone) built with **JUCE + CMake + C++17** on macOS (universal arm64/x86_64). Tagline: *Every sound leaves a ghost.*

It does **not** use a normal delay or reverb as the core. It runs an **overlap-add STFT**, stores a circular buffer of **spectral frames** (magnitude/phase + metadata), and lets the live spectrum interact with its recent past.

Modes: **Shadow**, **Erase**, **Merge** — all implemented and audible. Random Recall wander and factory presets are shipped. Stereo Link is deferred.

---

## 2. Development status (phased)

| Phase | Status | What it delivered |
|------|--------|-------------------|
| 1 Foundation | ✅ | CMake JUCE plugin, APVTS, dark UI, pass-through, state save/restore |
| 2 STFT | ✅ | FFT 2048 / hop 512 / Hann / WOLA, latency reporting, latency-aligned dry/wet |
| 3 History | ✅ | Per-channel spectral history, Freeze, Memory Length window, fill UI indicator |
| 4 Shadow | ✅ | Additive magnitude blend with interpolated history |
| 5 Erase + Merge | ✅ | Suppression + morph + any-mode crossfade |
| 6 Blur / Forget / Transients / Random | ✅ | Blur on history; Forget + transient; Random = slow wander |
| 7 Memory Well viz | ✅ | DSP-seeded circular particles + Recall ring |
| 8 Presets / polish | ✅ | ≥8 factory presets, tooltips; Stereo Link deferred |

Current milestone line in README: **Audible retune + offline licensing**.

---

## 3. DSP architecture (must preserve)

```
Input
  → copy undelayed input (meters / dry delay source)
  → SpectralEngine / STFTProcessor (in-place wet)
       per channel, every hop:
         window → FFT
         capture SpectralFrame (mag/phase/rms/centroid/flux)
         READ history at Recall (interpolated) BEFORE pushing current
         apply SpectralModeProcessor (Shadow / Erase / Merge)
         write magnitudes back to FFT (keep current phase; Hermitian mirror)
         PUSH unmodified analysis frame into history (unless Freeze)
         IFFT → synthesis window → WOLA
  → delay dry by STFT latency
  → equal-power dry/wet mix + output gain + smoothed bypass
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

### History (`SpectralHistoryBuffer`)

- Preallocated for **max 10 s** of frames at prepare time  
- Memory Length (0.1–10 s, default 3 s) only changes **search window**, not allocation  
- `age01 = 0` → newest available; `1` → oldest in active window  
- `getInterpolatedMagnitudes(age01, …)` lerps between neighboring frames  
- **Freeze**: stop writes; keep processing; dry input not frozen  
- State save/restore: **parameters only — never serialize live history**; clear on load  

### Shadow / Erase / Merge (audible retune)

**Influence mapping** (`mapInfluenceForMode`):
```
mapped = 1 - (1 - x)^exponent   // exact 0 at 0, exact 1 at 1
Shadow exp≈1.70, Erase≈2.05, Merge≈1.45
```

**Forget retention floor** (`remappedHistoryWeight`):
```
historyWeight = floor + (1 - floor) * ageWeightFromForget(...)
Shadow floor=0.20, Erase=0.15, Merge=0.25
```

**Transient Preserve**: max reduction 0.65 (not full shut-off). Flux calibration `*3.2` (was `*4.0`).

**Shadow:** `out = current + normalizedHistory * mixAmount` — allow up to ~+3 dB energy rise at high Influence; do not force output=input energy.

**Erase:** `overlap = ratio/(ratio+knee)` with `ratio = hist/(cur+eps)`, knee≈0.32; max atten 0.92; no upward energy restore.

**Merge:** log-magnitude morph with ±18 dB bin delta limit; soft energy stabilisation.

**Blur:** history-only; Blur=0 exact identity; RMS energy preserved after blur.

Recalled spectra get bounded, smoothed energy normalisation (+9 / −6 dB, silence-safe).

Current **phase kept** (no historical phase blending).

---

## 4. Key source files

```
AFTERIMAGE/
├── CMakeLists.txt
├── README.md
├── docs/LICENSING.md
└── Source/
    ├── PluginProcessor.*          # APVTS, processBlock, entitlement dry, factory programs
    ├── PluginEditor.*             # dark UI, knobs, presets, license chip
    ├── DSP/                       # STFT, history, modes (retuned)
    ├── Licensing/                 # Ed25519 offline licenses (message-thread only)
    ├── UI/                        # LookAndFeel, MemoryWell, LicensePanel, …
    └── Utilities/
        ├── Constants.h
        └── FactoryPresets.h       # subtle / medium / extreme banks
```

Company / codes: `AdamAudio`, manufacturer `Adam`, plugin code `AfIm`.  
JUCE: local checkout preferred at `~/dev/Spawnclone/JUCE`, else FetchContent 8.0.6.

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
| `blur` | Blur | history magnitudes; default **12%** |
| `transientPreserve` | Transients | default **35%**; max reduction 0.65 |
| `freeze` | Freeze | bool |
| `randomRecall` | Random | slow wander around Recall Position |
| `outputGain` | Output | −24…+12 dB |
| `mix` | Mix | equal-power, latency-aligned |
| `bypass` | Bypass | smoothed |

Smoothing lives in `ParameterSmoother` / processBlock. Mode crossfade ~80 ms in `SpectralModeProcessor`.

Factory presets retuned (Subtle / Medium / Extreme). Loading a preset clears live history.

---

## 6. Hard constraints (do not violate)

1. **No audio-thread heap allocation** (no vector resize, no `new`, no locks, no logging spam).  
2. **Genuine spectral memory** — not a delay line dressed up as “spectral.”  
3. History stores **frames**, not raw audio.  
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
- VST3 (installed): `~/Library/Audio/Plug-Ins/VST3/AFTERIMAGE.vst3`  

Manual check in Ableton Live 12: quit fully after rebuild so the binary reloads. Shadow: sustained pads/chords, Memory ~3 s, Influence 50–70%, Recall 40–60%, Mix 100%. Random: raise Random and listen for slow recall drift (not stutter).

---

## 8. What to work on next (recommended)

### Install production license public key

Replace the zeroed production key slot in `LicenseVerifier.cpp` before commercial release. Keep private key offline. See `docs/LICENSING.md`.

### Deferred — Stereo Link

Independent L/R histories remain. Shared/averaged memory would need careful RT-safe design so it does not destabilize isolation or Influence≈0 identity. Prefer not shipping a half-baked link.

### Optional polish

- User preset save slots (beyond factory programs)
- DAW audition matrix for Influence curve fine-tuning
- Online activation / machine deactivation server
- Recall / Smear modes (product backlog)

---

## 9. Known pitfalls / design notes

- **Push vs read order matters:** history is read *before* committing the current frame so age 0 is the previous hop, not “self.” History stores **pre-Shadow analysis**, not the wet ghost.  
- **Stereo:** independent per-channel histories; Stereo Link deferred.  
- **JUCE real-only FFT layout:** bins use interleaved `re/im` at `2*k`, `2*k+1` for `k = 0 … N/2`, with Hermitian mirror on write-back (same pattern as author’s CircleEQ).  
- Branch name still says `phase-1-…` but contains Phases 1–8 polish; don’t rename unless asked.  
- Unit tests under `Tests/` are wired via CMake (`AFTERIMAGE_Tests` / ctest).

---

## 10. How ChatGPT should help

When responding:

1. Read this handoff + relevant files from the repo before proposing large rewrites.  
2. Prefer **surgical diffs** aligned with existing classes (`SpectralModeProcessor`, `SpectralEngine::onSpectrum`).  
3. Call out RT-safety risks explicitly.  
4. After suggesting code, list **files touched**, **how to build**, and **how to A/B listen** in a DAW.  
5. Do not claim Stereo Link works unless implementing it.  
6. If unsure about JUCE APIs, stick to patterns already in-tree (`juce::dsp::FFT`, APVTS, `SmoothedValue`).

---

## 11. Author / toolchain assumptions

- macOS, Apple Clang, CMake ≥ 3.22, Homebrew CMake available  
- Ableton Live 12 Suite present for manual VST3 testing  
- JUCE 8.x via `~/dev/Spawnclone/JUCE`  
- GitHub remote: `origin` → `https://github.com/aphex4/afterimage.git`

---

*End of handoff. AFTERIMAGE audible retune + offline licensing; Stereo Link deferred.*
