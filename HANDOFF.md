# AFTERIMAGE — Handoff for External Collaboration

**Paste this entire document into ChatGPT (or similar) as project context.**  
Repo: https://github.com/aphex4/afterimage  
Branch: `cursor/phase-1-project-foundation`  
Local path (author machine): `~/dev/AFTERIMAGE`

---

## 1. One-paragraph product summary

**AFTERIMAGE** is a real-time creative audio effect (VST3 + Standalone) built with **JUCE + CMake + C++17** on macOS (universal arm64/x86_64). Tagline: *Every sound leaves a ghost.*

It does **not** use a normal delay or reverb as the core. It runs an **overlap-add STFT**, stores a circular buffer of **spectral frames** (magnitude/phase + metadata), and lets the live spectrum interact with its recent past.

Planned modes: **Shadow**, **Erase**, **Merge** (later: Recall, Smear).  
**Shadow, Erase, and Merge are implemented and audible.**

---

## 2. Development status (phased)

| Phase | Status | What it delivered |
|------|--------|-------------------|
| 1 Foundation | ✅ | CMake JUCE plugin, APVTS, dark UI, pass-through, state save/restore |
| 2 STFT | ✅ | FFT 2048 / hop 512 / Hann / WOLA, latency reporting, latency-aligned dry/wet |
| 3 History | ✅ | Per-channel spectral history, Freeze, Memory Length window, fill UI indicator |
| 4 Shadow | ✅ | Additive magnitude blend with interpolated history |
| 5 Erase + Merge | ✅ | Suppression + morph + any-mode crossfade |
| 6 Blur / Forget / Transients polish | Partial | Blur on history; Forget + transient used; Random unused |
| 7 Memory Well viz | ✅ | DSP-seeded circular particles + Recall ring |
| 8 Presets / polish | ❌ | Factory presets, Stereo Link |

Current milestone line in README: **Phase 5 — Erase + Merge**.

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
         apply SpectralModeProcessor (Shadow now)
         write magnitudes back to FFT (keep current phase; Hermitian mirror)
         PUSH unmodified analysis frame into history (unless Freeze)
         IFFT → synthesis window → WOLA
  → mild tanh soft-clip on wet
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

### Shadow formula (implemented)

```text
effectiveInfluence = influence * (1 - transientStrength * transientPreserve)
decayWeight        = exp(-recallAge01 * decayCoeff(forget))   // forget↑ → faster fade
outMag             = currentMag * (1 - effectiveInfluence)
                   + historyMag * (effectiveInfluence * decayWeight)
```

Then ±6 dB energy match vs pre-Shadow magnitude energy. Current **phase kept**. Random Recall = slow smoothed wander around Recall Position (not per-hop chaos).

Erase / Merge: implemented (see §3b).

### Erase / Merge (Phase 5)

**Erase:**
```
overlap = hist / (hist + cur + eps)
out = cur * (1 - min(influence * forgetWeight * overlap, 0.92))
```

**Merge:**
```
out = lerp(cur, hist, influence * forgetWeight)
```

**Mode switch:** ~80 ms dual-pass crossfade between previous and target mode outputs.

---

## 4. Key source files

```
AFTERIMAGE/
├── CMakeLists.txt
├── README.md
└── Source/
    ├── PluginProcessor.*          # APVTS, processBlock, dry delay, soft clip, meters
    ├── PluginEditor.*             # dark UI, knobs, mode selector, timer → Memory Pool
    ├── DSP/
    │   ├── STFTProcessor.*        # rings, Hann, FFT/IFFT, WOLA, spectrum callback
    │   ├── SpectralEngine.*       # history I/O, random recall, Shadow write-back
    │   ├── SpectralHistoryBuffer.*
    │   ├── SpectralFrame.h
    │   ├── SpectralModes.*        # Shadow / Erase / Merge
    │   ├── DryWetMixer.h          # latency delay + equal-power mix helper
    │   └── ParameterSmoother.h
    ├── UI/                        # LookAndFeel, MemoryPool, ModeSelector, SpectrumDisplay
    └── Utilities/Constants.h
```

Company / codes: `AdamAudio`, manufacturer `Adam`, plugin code `AfIm`.  
JUCE: local checkout preferred at `~/dev/Spawnclone/JUCE`, else FetchContent 8.0.6.

---

## 5. Parameters (APVTS IDs — stable)

| ID | UI | Notes |
|----|-----|------|
| `mode` | Shadow / Erase / Merge | All three transform |
| `memoryLength` | Memory | 0.1–10 s, skewed short |
| `recallPosition` | Recall | 0–100%, default 45% (Memory Well ring) |
| `influence` | Influence | 0–100%, default 50% |
| `forget` | Forget | 0–100%, default 35% |
| `blur` | Blur | applied to history magnitudes |
| `transientPreserve` | Transients | used in all modes |
| `freeze` | Freeze | bool |
| `randomRecall` | Random | unused |
| `outputGain` | Output | −24…+12 dB |
| `mix` | Mix | equal-power, latency-aligned |
| `bypass` | Bypass | smoothed |

Smoothing lives in `ParameterSmoother` / processBlock. Mode crossfade ~80 ms in `SpectralModeProcessor`.

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
```

Artefacts:

- Standalone: `build/AFTERIMAGE_artefacts/Release/Standalone/AFTERIMAGE.app`  
- VST3 (build): `build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3`  
- VST3 (installed): `~/Library/Audio/Plug-Ins/VST3/AFTERIMAGE.vST3`  

Manual check in Ableton Live 12: quit fully after rebuild so the binary reloads. Shadow: sustained pads/chords, Memory ~3 s, Influence 50–70%, Recall 40–60%, Mix 100%.

---

## 8. What to work on next (recommended)

### Phase 6 — Random Recall + polish

Wire Random Recall (slow smoothed wander around Recall Position). Polish Forget/transient mapping if needed.

### Phase 8

≥8 factory presets (parameter values only), tooltips, Stereo Link, README polish.

---

## 9. Known pitfalls / design notes

- **Push vs read order matters:** history is read *before* committing the current frame so age 0 is the previous hop, not “self.” History stores **pre-Shadow analysis**, not the wet ghost.  
- **Stereo:** independent per-channel histories for now; Stereo Link parameter not exposed yet (architecture should allow shared/averaged memory later).  
- **JUCE real-only FFT layout:** bins use interleaved `re/im` at `2*k`, `2*k+1` for `k = 0 … N/2`, with Hermitian mirror on write-back (same pattern as author’s CircleEQ).  
- Branch name still says `phase-1-…` but contains Phases 1–4; don’t rename unless asked.  
- Unit tests under `Tests/` are stubs and **not** in CMake yet.

---

## 10. How ChatGPT should help

When responding:

1. Read this handoff + relevant files from the repo before proposing large rewrites.  
2. Prefer **surgical diffs** aligned with existing classes (`SpectralModeProcessor`, `SpectralEngine::onSpectrum`).  
3. Call out RT-safety risks explicitly.  
4. After suggesting code, list **files touched**, **how to build**, and **how to A/B listen** in a DAW.  
5. Do not claim Random Recall / Stereo Link work unless implementing them.  
6. If unsure about JUCE APIs, stick to patterns already in-tree (`juce::dsp::FFT`, APVTS, `SmoothedValue`).

### Good first prompts for ChatGPT

- “Wire Random Recall as a slow wander around Recall Position.”  
- “Add factory presets for Shadow / Erase / Merge starting points.”  
- “Review Erase attenuation for pumping on percussion.”

---

## 11. Author / toolchain assumptions

- macOS, Apple Clang, CMake ≥ 3.22, Homebrew CMake available  
- Ableton Live 12 Suite present for manual VST3 testing  
- JUCE 8.x via `~/dev/Spawnclone/JUCE`  
- GitHub remote: `origin` → `https://github.com/aphex4/afterimage.git`

---

*End of handoff. Generated for AFTERIMAGE Phase 5 (Erase + Merge).*
