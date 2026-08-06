# AFTERIMAGE

**Every sound leaves a ghost.**

AFTERIMAGE is a real-time spectral memory processor. It continuously analyzes and stores a short history of the signal’s spectral content so the present can interact with its own recent past — producing evolving spectral echoes, ghost harmonics, frequency suppression, and morphing textures.

> **Current milestone: Phase 8 polish — first solid release**  
> Shadow / Erase / Merge, Random Recall wander, factory presets, Memory Well, and validation tests.

---

## Features

- Overlap-add STFT with host latency + latency-compensated dry/wet
- Per-channel spectral history with Freeze and Memory Length
- **Shadow** — additive spectral ghost of recalled memory
- **Erase** — carve holes where memory overlaps the present
- **Merge** — morph the present toward recalled memory
- **Random Recall** — slow smoothed wander around Recall Position
- Factory presets (parameter values only; history cleared on load)
- Shared controls: Recall (ring), Forget, Blur (history only), Transient Preserve, Influence, Mix, Output
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
```

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

## Controls

| Control | Range | Notes |
|---------|-------|-------|
| Mode | Shadow / Erase / Merge | All three transform magnitudes |
| Memory | 0.1–10 s | Searchable history window |
| Recall | 0–100% | Position in history (0 = newest); scrub via Memory Well ring |
| Influence | 0–100% | Strength of spectral interaction |
| Forget | 0–100% | How quickly older frames lose weight |
| Blur | 0–100% | Inter-bin smoothing of **history** magnitudes |
| Transients | 0–100% | Attack preservation |
| Random | 0–100% | Slow smoothed wander around Recall Position |
| Freeze | on/off | Stop writing new history frames |
| Mix | 0–100% | Equal-power dry/wet |
| Output | −24…+12 dB | Output gain |
| Bypass | on/off | Smoothed host-friendly bypass |
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

---

## Development roadmap

1. **Phase 1** — Project foundation ✅
2. **Phase 2** — STFT engine ✅
3. **Phase 3** — Spectral history buffer integration ✅
4. **Phase 4** — Shadow mode ✅
5. **Phase 5** — Erase + Merge + mode crossfade ✅
6. **Phase 6** — Random Recall ✅
7. **Phase 7** — Memory Well ✅
8. **Phase 8** — Factory presets + polish ✅ (Stereo Link deferred)

---

## License

Plugin code: project-local. JUCE is subject to the [JUCE license](https://juce.com/legal/juce-8-licence/).
