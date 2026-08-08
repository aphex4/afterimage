# AFTERIMAGE

**Every sound leaves a ghost.**

AFTERIMAGE is a real-time spectral memory processor. It continuously analyzes and stores a short history of the signal’s spectral content so the present can interact with its own recent past — producing evolving spectral echoes, ghost harmonics, frequency suppression, and morphing textures.

> **Current milestone: 1.0.0-rc.1 (Release Candidate)**  
> Not ear-signed-off for final v1.0. Product modes are **Shadow / Erase** (Merge removed; legacy sessions map to Shadow). Post-chain reverb/formant/de-esser, exclusive Scale Snap / Auto-Tune, 8-band parametric EQ, Gain Match, three editor views (Memory / Scale / EQ), Memory Well, licensing, and validation tests.

### Versioning choice

| Field | Value | Why |
|-------|-------|-----|
| CMake `PROJECT_VERSION` / JUCE `VersionCode` + `JucePlugin_VersionString` | `1.0.0` → `0x10000` | JUCE’s `_juce_version_code` only accepts integer `MAJOR.MINOR.PATCH` tokens; feeding `1.0.0-rc.1` breaks the hex math |
| Marketing / docs / `AFTERIMAGE_VERSION_STRING` | `1.0.0-rc.1` | Honest RC label until ear audition sign-off |

Do **not** claim READY FOR V1.0 without DAW ear A/B.

---

## Features

- Overlap-add STFT with host latency + latency-compensated dry/wet
- Per-channel spectral history; Freeze captures a stabilized recent memory profile
- **Shadow** — multi-age spectral tail / ghost from diffused memory profiles
- **Erase** — relative-prominence familiarity carve (repeated content hollows out)
- **Post-chain** — Formant, De-esser, conventional Reverb (Spring/Hall/Room + Pre/Post EQ)
- **Scale view** — exclusive Scale Snap or Auto-Tune (Retune Speed + Humanize); MIDI root/chord
- **EQ view** — 8-band parametric EQ (last creative stage): Bell/Shelf/Cut/Notch, ×4 slopes, Solo, Stereo/LR/MS
- **Gain Match** — broadband loudness trim of the completed chain vs latency-aligned dry (MATCH in global top bar)
- Three editor views: **Memory | Scale | EQ**
- Factory presets grouped by Shadow / Erase
- Offline licensing: 14-day trial, signed `.afterimage-license`, compact activation UI
- Memory Well particle visualization (DSP-seeded)
- Formats: **VST3**, **AU** (macOS), **Standalone**

---

## DSP overview

| Stage | Role |
|-------|------|
| STFT (FFT 4096 / hop 512 / Hann) | Analysis & resynthesis |
| Spectral history + SpectralMemoryProfile | Recall window |
| Modes | Shadow, Erase (see [docs/EFFECT_ENGINE.md](docs/EFFECT_ENGINE.md)) |
| Mix | Latency-compensated equal-power dry/wet |
| Formant → De-esser → Reverb | Post-spectral colour / smooth |
| Pitch path | Off \| Scale Snap \| Auto-Tune (exclusive) |
| Parametric EQ | 8-band, last creative stage |
| Gain Match → Bypass → Output | Level match + utility |

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

# Configure (Release) — copies VST3/AU into ~/Library/Audio/Plug-Ins/…
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
Packaging / notarization (optional, needs your Developer ID): [`docs/PACKAGING.md`](docs/PACKAGING.md).

### CMake options

| Flag | Default | Purpose |
|------|---------|---------|
| `AFTERIMAGE_ENABLE_LICENSING` | ON | Offline signed-license system |
| `AFTERIMAGE_BUILD_LICENSE_TOOL` | OFF | Build `AfterimageLicenseTool` |
| `AFTERIMAGE_USE_TEST_LICENSE_KEY` | OFF | Embed test public key (tests only; never ship) |
| `AFTERIMAGE_COMMERCIAL_RELEASE` | OFF | Fail if production key missing; enables Plug-Ins copy |

### Debug build

Debug **does not** copy into `~/Library/Audio/Plug-Ins/` (avoids overwriting a Release install). Artefacts stay in the build tree.

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

### Plugin output locations

**Release** (and commercial) builds copy into the user plug-in folders:

```text
~/Library/Audio/Plug-Ins/VST3/AFTERIMAGE.vst3
~/Library/Audio/Plug-Ins/Components/AFTERIMAGE.component   # AU
```

Build-tree copies:

```text
build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3
build/AFTERIMAGE_artefacts/Release/AU/AFTERIMAGE.component
build-debug/AFTERIMAGE_artefacts/Debug/VST3/AFTERIMAGE.vst3   # Debug: build tree only
```

VST3 category: `Fx|Filter|Modulation`.

---

## Licensing

Commercial builds include an offline signed-license system (Ed25519 via Monocypher).

- 14-day trial with full DSP; after expiry → latency-compensated dry pass-through
- Import `.afterimage-license` or paste from the header license chip
- See [docs/LICENSING.md](docs/LICENSING.md) for architecture, tool usage, and security limitations
- Effect formula notes: [docs/EFFECT_ENGINE.md](docs/EFFECT_ENGINE.md)

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
| Blur | 0–100% | Mode-specific: Shadow diffusion / Erase mask width / Merge envelope |
| Transients | 0–100% | Attack preservation (max ~65% influence reduction) |
| Random | 0–100% | Slow smoothed wander around Recall Position |
| Freeze | on/off | Hold a stabilized recent memory profile |
| Gain Match | on/off | Match mixed level to latency-aligned dry (broadband scalar) |
| Mix | 0–100% | Equal-power dry/wet |
| Output | −24…+12 dB | Output gain |
| Bypass | on/off | Smoothed host-friendly bypass |
| License | header chip | Trial / licensed / invalid — click to activate |
| Preset | factory list | Shadow / Erase / Merge sections; parameters only; clears live history |

---

## UI notes

- Typography uses host system geometric sans (`Avenir Next` on macOS). See `Assets/Fonts/README.md`.
- Custom tooltips (dark elevated cards, ~550 ms delay) replace default JUCE bars. Influence and Blur tips follow the active mode.
- Dock groups: Memory | Spectral processing | Output.
- `EDITOR_WANTS_KEYBOARD_FOCUS` is **FALSE** so DAW hosts keep primary keyboard focus. In Standalone, the mode selector accepts arrow keys when focused.
- Recall Ring: click/drag inside the Memory Well only; clicks outside do not change Recall.

---

## Known limitations

- **Stereo Link** deferred — L/R keep independent spectral histories
- Session/preset state stores parameters only (never live spectral history)
- Offline licensing is commercial deterrence, not unbreakable DRM (see `docs/LICENSING.md`)
- Musical calibration and final v1.0 claim require DAW ear audition
- Notarized installers need a local Developer ID (see `docs/PACKAGING.md`) — not automated in-repo

---

## Development roadmap

1. **Phase 1–8** — Foundation through factory presets + polish ✅ (Stereo Link deferred)
2. **Audible retune** — perceptual Influence, retention floors, mode formulas ✅
3. **Licensing** — offline Ed25519 licenses + trial + activation UI ✅
4. **Gain Match** — post-mix broadband loudness match ✅
5. **Spectral memory redesign** — SpectralMemoryProfile + multi-age Shadow / Erase / Merge ✅ (RC)
6. **Before v1.0** — ear A/B sign-off; optional notarized packaging
7. **Later** — optional online activation; Stereo Link; user preset slots

---

## License

Plugin code: project-local commercial product with offline activation (see docs/LICENSING.md).  
Vendored Monocypher: BSD-2-Clause OR CC0-1.0.  
JUCE is subject to the [JUCE license](https://juce.com/legal/juce-8-licence/).
Monocypher (Ed25519): BSD-2-Clause OR CC0-1.0.
