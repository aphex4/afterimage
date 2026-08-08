# AFTERIMAGE packaging & notarization (macOS)

Optional steps for distributing a signed, notarized build. **This repo does not ship certificates, private keys, or Apple credentials.** Skip signing/notarization if you do not have a Developer ID.

Marketing version: **1.0.0-rc.1** (CMake/JUCE `VersionCode` remains `1.0.0` / `0x10000` — see README).

## Prerequisites

- Apple Developer Program membership
- Developer ID Application certificate installed in Keychain
- `notarytool` credentials (App Store Connect API key or Apple ID app-specific password)
- Release (or commercial) build of AFTERIMAGE

## Build artefacts to package

From a Release configure/build:

```text
build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3
build/AFTERIMAGE_artefacts/Release/AU/AFTERIMAGE.component
build/AFTERIMAGE_artefacts/Release/Standalone/AFTERIMAGE.app
```

Commercial packaging:

```bash
cmake -B build-commercial -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DAFTERIMAGE_COMMERCIAL_RELEASE=ON \
  -DJUCE_PATH=$HOME/dev/Spawnclone/JUCE
cmake --build build-commercial --config Release -j
ctest --test-dir build-commercial --output-on-failure
```

## Codesign (example)

Replace identity with your Developer ID Application name:

```bash
IDENTITY="Developer ID Application: Your Name (TEAMID)"

codesign --force --deep --options runtime \
  --sign "$IDENTITY" \
  build/AFTERIMAGE_artefacts/Release/VST3/AFTERIMAGE.vst3

codesign --force --deep --options runtime \
  --sign "$IDENTITY" \
  build/AFTERIMAGE_artefacts/Release/AU/AFTERIMAGE.component

codesign --force --deep --options runtime \
  --sign "$IDENTITY" \
  build/AFTERIMAGE_artefacts/Release/Standalone/AFTERIMAGE.app
```

Verify:

```bash
codesign --verify --verbose=2 path/to/AFTERIMAGE.vst3
spctl --assess --verbose=4 --type install path/to/AFTERIMAGE.vst3
```

## Notarize (example)

Zip the product, submit, staple:

```bash
ditto -c -k --keepParent AFTERIMAGE.vst3 AFTERIMAGE-vst3.zip

xcrun notarytool submit AFTERIMAGE-vst3.zip \
  --apple-id "you@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password" \
  --wait

xcrun stapler staple AFTERIMAGE.vst3
```

App Store Connect API keys are preferred over Apple ID passwords for CI. Do not commit `.p8` keys or notarization passwords.

## Installer / DMG

Any outer DMG or `.pkg` should also be signed with Developer ID and notarized. Exact layout (drag-install vs pkg) is a product choice — not prescribed here.

## What this project will not do

- Require or embed your signing identity in CMake
- Fake notarization success
- Claim Gatekeeper readiness without a real staple on your machine
