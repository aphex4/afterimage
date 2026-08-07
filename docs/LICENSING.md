# AFTERIMAGE Licensing Architecture

Offline signed-license system for commercial VST3 distribution.

## Goals

- Trial mode (14 days, full DSP)
- Licensed mode via signed `.afterimage-license` files
- Persistent local activation (not in DAW session state)
- Clear UI status; graceful failure
- No audio-thread licensing work
- Future online activation can plug into `LicenseManager` without DSP changes

## Format: `AFTERIMAGE-LICENSE-1`

```text
AFTERIMAGE-LICENSE-1
<base64 JSON payload>
<base64 Ed25519 signature (64 bytes)>
```

Payload fields (deterministic JSON):

| Field | Notes |
|-------|--------|
| product | must be `AFTERIMAGE` |
| licenseVersion | `1` |
| licenseId | unique id |
| customerName / customerEmail | display / support |
| licenseType | e.g. `perpetual` |
| issuedAt / expiresAt | ISO-8601; `expiresAt` null/empty = perpetual |
| maxMachines | `0` = not enforced |
| features | e.g. `["full"]` |
| machineIds | optional allow-list of hashed machine ids |

## Cryptography

- **Algorithm:** Ed25519 (SHA-512 + Edwards25519) via **Monocypher 4.0.2** (audited, BSD-2-Clause / CC0)
- **Abstraction:** `LicenseVerifier` — only public-key verify in the plugin
- **Private key:** never shipped in the plugin; only in `AfterimageLicenseTool` / `Tests/Fixtures` (test seed/secret)
- **Production public key:** installed in `kProductionPublicKey` in `LicenseVerifier.cpp` (public material only)
- **Production private key:** offline only — never under `Source/`, never committed (local path e.g. `../AFTERIMAGE-secrets/`)
- **Test builds:** `-DAFTERIMAGE_USE_TEST_LICENSE_KEY=ON` embeds the fixture public key

Production readiness: verification of correctly issued Ed25519 licenses is production-ready once `kProductionPublicKey` is non-zero and commercial configure succeeds. Local DRM is deterrence, not unbreakable protection.

## Components (`Source/Licensing/`)

| Class | Role |
|-------|------|
| `LicenseManager` | Facade; caches `std::atomic<EntitlementState>` |
| `LicenseDocument` | Parse / serialize `AFTERIMAGE-LICENSE-1` |
| `LicenseVerifier` | Ed25519 verify (+ sign in tool builds) |
| `LicenseStorage` | App-data JSON; atomic write via temp+replace |
| `MachineIdentity` | Salted BLAKE2b hash of stable OS hints |

## Storage paths

| Platform | Path |
|----------|------|
| macOS | `~/Library/Application Support/AdamAudio/AFTERIMAGE/license-state.json` |
| Windows | `%APPDATA%/AdamAudio/AFTERIMAGE/license-state.json` |
| Linux | `~/.config/AdamAudio/AFTERIMAGE/license-state.json` (JUCE userApplicationDataDirectory) |

Schema stores: signed license text, customer name, trial start, last observed wall-clock, activation time. Never in plugin presets/sessions.

## Trial

- 14 days from first initialise
- Full DSP (no dropouts, noise, or muting)
- After expiry: **latency-compensated dry pass-through** + non-modal header message
- Entitlement crossfades (~50 ms) when status changes while the plugin is open

## Clock rollback

Stores `lastObservedUnix`. If wall clock is more than 1 hour earlier than last observed time → `ClockRollback` status and dry pass-through. Small NTP/timezone corrections are tolerated. This is deterrence only.

## Machine identity

Salted BLAKE2b over computer name + logon name + OS name + platform tag. Raw IDs never shown; UI may show a short fingerprint. Machine locking is optional (`machineIds` empty → allow all).

## CMake flags

| Flag | Default | Meaning |
|------|---------|---------|
| `AFTERIMAGE_ENABLE_LICENSING` | ON | Compile licensing into plugin |
| `AFTERIMAGE_BUILD_LICENSE_TOOL` | OFF | Build `AfterimageLicenseTool` |
| `AFTERIMAGE_USE_TEST_LICENSE_KEY` | OFF | Embed test public key (**tests / local debug only — never ship**) |
| `AFTERIMAGE_COMMERCIAL_RELEASE` | OFF | **Release blocker:** compile fails if production public key is still placeholder zeros, or if the test key flag is on |
| `AFTERIMAGE_ENABLE_SANITIZERS` | OFF | ASan + UBSan on the test target (where supported) |

### Production key injection (commercial packaging)

1. Generate an Ed25519 issuer keypair **offline** with the license tool (private key never enters this repo or plugin binaries):

```bash
cmake -B build -S . -DAFTERIMAGE_BUILD_LICENSE_TOOL=ON -DAFTERIMAGE_USE_TEST_LICENSE_KEY=ON
cmake --build build --target AfterimageLicenseTool -j

# OUTSIDE the plugin tree — e.g. sibling folder or $HOME/.afterimage/keys/
./build/AfterimageLicenseTool --gen-issuer-keys --out-dir ../AFTERIMAGE-secrets
```

   Local convention: sibling folder **`../AFTERIMAGE-secrets/`** (never inside the plugin tree). Contains `production_ed25519_secret.bin` / `*_seed.bin` (mode 600) and `production_ed25519_public.bin` / `ProductionPublicKey.inc`.

2. Replace the 32-byte `kProductionPublicKey` array in `Source/Licensing/LicenseVerifier.cpp` with the public key bytes from `ProductionPublicKey.inc` (public material only).
3. Configure the shipping build with:

```bash
cmake -B build-release -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DAFTERIMAGE_COMMERCIAL_RELEASE=ON \
  -DAFTERIMAGE_USE_TEST_LICENSE_KEY=OFF
```

4. Confirm configure/compile succeed. A zeroed production key or `AFTERIMAGE_USE_TEST_LICENSE_KEY=ON` fails the build when `AFTERIMAGE_COMMERCIAL_RELEASE=ON`.

Test builds continue to use `-DAFTERIMAGE_USE_TEST_LICENSE_KEY=ON` with the fixture public key in `LicensePublicKeys.inc`. The AFTERIMAGE_Tests target always embeds the test key.

## License tool

```bash
cmake -B build -S . -DAFTERIMAGE_BUILD_LICENSE_TOOL=ON -DAFTERIMAGE_USE_TEST_LICENSE_KEY=ON
cmake --build build --target AfterimageLicenseTool -j

# Generate test key material into Tests/Fixtures
./build/AfterimageLicenseTool --gen-test-keys --out-dir Tests/Fixtures

# Generate production issuer keys (OUTSIDE repo)
./build/AfterimageLicenseTool --gen-issuer-keys --out-dir ../AFTERIMAGE-secrets

# Issue a license (64-byte secret from fixtures or your issuer key)
./build/AfterimageLicenseTool --sign \
  --secret ../AFTERIMAGE-secrets/production_ed25519_secret.bin \
  --out ~/Desktop/customer.afterimage-license \
  --name "Ada" --email "ada@example.com"

./build/AfterimageLicenseTool --verify \
  --public ../AFTERIMAGE-secrets/production_ed25519_public.bin \
  --license ~/Desktop/customer.afterimage-license
```

Copy the generated test `LicensePublicKeys.inc` into `Source/Licensing/` when rotating the test key. Never copy secret/seed files into `Source/`.

## Security limitations (honest)

- Motivated attackers can patch entitlement checks or replace the public key
- Trial timestamps are local-file based
- Clock rollback detection is imperfect
- Goal: reasonable commercial protection, not impossible DRM
- Online activation / revocation is a later extension

## UI

Compact header chip (`LicensePanel`): trial days / licensed name / error. Click opens a small AFTERIMAGE-styled overlay for paste/import/deactivate — not a giant form in the creative UI.
