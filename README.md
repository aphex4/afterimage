# AFTERIMAGE

**Every sound leaves a ghost.**

AFTERIMAGE is a real-time spectral memory processor. It continuously analyzes and stores a short history of the signal’s spectral content so the present can interact with its own recent past — producing evolving spectral echoes, ghost harmonics, frequency suppression, and morphing textures.

> **Current milestone: Audible retune + offline licensing**  
> Shadow / Erase / Merge with perceptual Influence mapping, factory presets (subtle/medium/extreme), Memory Well, Ed25519 license activation, and validation tests.

---

## Features

- Overlap-add STFT with host latency + latency-compensated dry/wet
- Per-channel spectral history with Freeze and Memory Length
- **Shadow** — additive spectral ghost (controlled loudness rise, not forced energy match)
- **Erase** — sensitive overlap carve where memory meets the present
- **Merge** — log-magnitude morph toward recalled memory
- **Random Recall** — slow smoothed wander around Recall Position
- Factory presets in subtle / medium / extreme categories
- Shared controls: Recall (ring), Forget (retention floor), Blur (history only), Transient Preserve, Influence (perceptual curve), Mix, Output
- Offline licensing: 14-day trial, signed `.afterimage-license`, compact activation UI
- ~80 ms click-free crossfade when switching modes
- Memory Well particle visualization (DSP-seeded)

---

## DSP overview

| Stage | Role |
|-------|------|
| STFT (FFT 2048 / hop 512 / Hann) | Analysis & resynthesis |
| Spectral history buffer | Circular store of magnitude/phase frames (0.1–10 s) |
| Modes | Shadow, Erase, Merge |
| Blur / Forget / Transients / Random | History smoothing, age weighting, attack preservation, recall wander |
| Dry/wet | Latency-compensated equal-power mix |

---

## Build prerequisites

- macOS with Xcode command-line tools
- CMake ≥ 3.22
- C++17 compiler (Apple Clang)
- JUCE 8.x

### JUCE setup

This project prefers a local JUCE checkout. By default it looks for:

```text
~/dev/Spawnclone/JUCE
```

Override with:

```bash
cmake -B build -S . -DJUCE_PATH=/path/to/JUCE
```

If no local path is found, CMake fetches **JUCE 8.0.6** via FetchContent (requires network).

---

## Build commands

```bash
cd ~/dev/AFTERIMAGE

# Configure (Release)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Or with an explicit JUCE path:
# cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=$HOME/dev/Spawnclone/JUCE

# Build
cmake --build build --config Release -j

# Validation tests
ctest --test-dir build --output-on-failure

# Optional: print effect-strength diagnostics
AFTERIMAGE_PRINT_MEASUREMENTS=1 ./build/AFTERIMAGE_Tests

# Developer license tool (not linked into the plugin)
cmake -B build -S . -DAFTERIMAGE_BUILD_LICENSE_TOOL=ON -DJUCE_PATH=$HOME/dev/Spawnclone/JUCE
cmake --build build --target AfterimageLicenseTool -j
```

Licensing details: [`docs/LICENSING.md`](docs/LICENSING.md).

### CMake options

| Flag | Default | Purpose |
|------|---------|---------|
| `AFTERIMAGE_ENABLE_LICENSING` | ON | Offline signed-license system |
| `AFTERIMAGE_BUILD_LICENSE_TOOL` | OFF | Build `AfterimageLicenseTool` |
| `AFTERIMAGE_USE_TEST_LICENSE_KEY` | OFF | Embed test public key (tests only; never ship) |

### Debug build

```bash
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DJUCE_PATH=$HOME/dev/Spawnclone/JUCE
cmake --build build-debug --config Debug -j
ctest --test-dir build-debug --output-on-failure
```

### Standalone

After a successful **Release** build:

```bash
open build/AFTERIMAGE_artefacts/Release/Standalone/AFTERIMAGE.app
```

After a **Debug** build:

```bash
open build-debug/AFTERIMAGE_artefacts/Debug/Standalone/AFTERIMAGE.app
```

### VST3 output location

With `COPY_PLUGIN_AFTER_BUILD` enabled, the VST3 is copied to the user plug-in folder:

```text
~/Library/Audio/Plug-Ins/VST3/AFTERIMAGE.vst3
```

Build tree copy (Release):

```text
build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3
```

---

## Licensing

Commercial builds include an offline signed-license system (Ed25519 via Monocypher).

- 14-day trial with full DSP; after expiry → latency-compensated dry pass-through
- Import `.afterimage-license` or paste from the header license chip
- See [Docs/LICENSING.md](Docs/LICENSING.md) for architecture, tool usage, and security limitations
- Effect formula notes: [Docs/EFFECT_ENGINE.md](Docs/EFFECT_ENGINE.md)

```bash
# Optional license tool (not in the plugin binary)
cmake -B build -S . -DAFTERIMAGE_BUILD_LICENSE_TOOL=ON -DJUCE_PATH=$HOME/dev/Spawnclone/JUCE
cmake --build build --target AfterimageLicenseTool -j
```

CMake options: `AFTERIMAGE_ENABLE_LICENSING` (default ON), `AFTERIMAGE_BUILD_LICENSE_TOOL` (OFF), `AFTERIMAGE_USE_TEST_LICENSE_KEY` (OFF — do not ship Release with this ON).

---

## Controls

| Control | Range | Notes |
|---------|-------|-------|
| Mode | Shadow / Erase / Merge | All three transform magnitudes |
| Memory | 0.1–10 s | Searchable history window |
| Recall | 0–100% | Position in history (0 = newest); scrub via Memory Well ring |
| Influence | 0–100% | Perceptual curve (exact 0/1); mid range more useful |
| Forget | 0–100% | Age weighting with mode-specific retention floor |
| Blur | 0–100% | Inter-bin smoothing of **history** magnitudes (RMS preserved) |
| Transients | 0–100% | Attack preservation (max ~65% influence reduction) |
| Random | 0–100% | Slow smoothed wander around Recall Position |
| Freeze | on/off | Stop writing new history frames |
| Mix | 0–100% | Equal-power dry/wet |
| Output | −24…+12 dB | Output gain |
| Bypass | on/off | Smoothed host-friendly bypass |
| License | header chip | Trial / licensed / invalid — click to activate |
| Preset | factory list | Applies parameter values only; clears live history |

---

## UI notes

- Typography uses host system geometric sans (`Avenir Next` on macOS). See `Assets/Fonts/README.md`.
- Custom tooltips (dark elevated cards, ~550 ms delay) replace default JUCE bars.
- Dock groups: Memory | Spectral processing | Output.
- `EDITOR_WANTS_KEYBOARD_FOCUS` is **FALSE** so DAW hosts keep primary keyboard focus. In Standalone, the mode selector accepts arrow keys when focused.
- Recall Ring: click/drag inside the Memory Well only; clicks outside do not change Recall.

---

## Known limitations

- **Stereo Link** deferred — L/R keep independent spectral histories
- Session/preset state stores parameters only (never live spectral history)
- Offline licensing is commercial deterrence, not unbreakable DRM (see `docs/LICENSING.md`)
- Musical calibration of Influence curves should be confirmed by DAW audition

---

## Development roadmap

1. **Phase 1–8** — Foundation through factory presets + polish ✅ (Stereo Link deferred)
2. **Audible retune** — perceptual Influence, retention floors, mode formulas ✅
3. **Licensing** — offline Ed25519 licenses + trial + activation UI ✅
4. **Next** — install production public key; optional online activation; Stereo Link

---

## License

Plugin code: project-local commercial product with offline activation (see Docs/LICENSING.md).  
Vendored Monocypher: BSD-2-Clause OR CC0-1.0.  
JUCE is subject to the [JUCE license](https://juce.com/legal/juce-8-licence/).
Monocypher (Ed25519): BSD-2-Clause OR CC0-1.0.
