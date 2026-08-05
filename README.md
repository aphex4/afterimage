# AFTERIMAGE

**Every sound leaves a ghost.**

AFTERIMAGE is a real-time spectral memory processor. It continuously analyzes and stores a short history of the signal’s spectral content so the present can interact with its own recent past — producing evolving spectral echoes, ghost harmonics, frequency suppression, and morphing textures.

> **Current milestone: Phase 3 — Spectral history**  
> Per-channel spectral frames are captured into a circular history each STFT hop. Freeze stops writing. Spectrum reconstruction is still transparent — audible modes begin in Phase 4.

---

## Current features (Phase 3)

- JUCE CMake project targeting **VST3** and **Standalone**
- Full APVTS parameter layout
- State save / restore (parameters only — spectral history is never serialized)
- Overlap-add STFT (FFT 2048 / hop 512 / Hann / WOLA) with host latency reporting
- Latency-compensated equal-power dry/wet mix
- **Per-channel spectral history** (magnitude, phase, RMS, centroid, transient flux)
- **Freeze** stops history writes without freezing the dry/wet audio path
- **Memory Length** controls the searchable age window (no audio-thread allocation)
- Memory Pool fill indicator reflects history occupancy
- Mode algorithms not yet applied to the spectrum (Phase 4+)

---

## DSP overview (planned)

| Stage | Role |
|-------|------|
| STFT (FFT 2048 / hop 512 / Hann) | Analysis & resynthesis |
| Spectral history buffer | Circular store of magnitude/phase frames (0.1–10 s) |
| Modes | Shadow, Erase, Merge (+ Recall / Smear later) |
| Blur / Forget / Transients | Frequency smoothing, age weighting, attack preservation |
| Dry/wet | Latency-compensated equal-power mix |

Phase 1 prepares these modules but does not insert them into the audio path.

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
```

### Standalone

After a successful build, run:

```bash
open build/AFTERIMAGE_artefacts/Release/Standalone/AFTERIMAGE.app
```

(Exact artefact path may vary slightly with generator; check `build/AFTERIMAGE_artefacts`.)

### VST3 output location

With `COPY_PLUGIN_AFTER_BUILD` enabled, the VST3 is copied to the user plug-in folder:

```text
~/Library/Audio/Plug-Ins/VST3/AFTERIMAGE.vst3
```

Build tree copy:

```text
build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3
```

---

## Controls

| Control | Range | Notes |
|---------|-------|-------|
| Mode | Shadow / Erase / Merge | Algorithms arrive in Phases 4–5 |
| Memory | 0.1–10 s | Searchable history window |
| Recall | 0–100% | Position in history (0 = newest) |
| Influence | 0–100% | Strength of spectral interaction |
| Forget | 0–100% | How quickly older frames lose weight |
| Blur | 0–100% | Inter-bin magnitude smoothing |
| Transients | 0–100% | Attack preservation |
| Random | 0–100% | Controlled recall wander |
| Freeze | on/off | Stop writing new history frames |
| Mix | 0–100% | Equal-power dry/wet |
| Output | −24…+12 dB | Output gain |
| Bypass | on/off | Smoothed host-friendly bypass |

---

## Known limitations (Phase 3)

- Spectral modes do not yet modify the spectrum — wet path is still transparent STFT
- History is captured but not yet read by Shadow / Erase / Merge
- Memory Pool trails are still mostly decorative (fill bar is live)
- Factory presets not yet included
- Unit tests are stubs and are not wired into CMake yet

---

## Development roadmap

1. **Phase 1** — Project foundation ✅
2. **Phase 2** — STFT engine (transparent unity processing + latency) ✅
3. **Phase 3** — Spectral history buffer integration ← *current*
4. **Phase 4** — Shadow mode
5. **Phase 5** — Erase + Merge + mode crossfade
6. **Phase 6** — Blur, Forget, transient preservation
7. **Phase 7** — Memory Pool visualization + metering
8. **Phase 8** — Presets and polish

---

## License

Plugin code: project-local. JUCE is subject to the [JUCE license](https://juce.com/legal/juce-8-licence/).
