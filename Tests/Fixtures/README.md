# Test license fixtures

TEST ONLY — never use as a production signing key.

| File | Contents |
|------|----------|
| `test_ed25519_seed.bin` | 32-byte seed (all `0x42`) |
| `test_ed25519_secret.bin` | 64-byte Ed25519 secret (seed\|\|pk) |
| `test_ed25519_public.bin` | 32-byte public key |

Matching public key bytes live in `Source/Licensing/LicensePublicKeys.inc` for `-DAFTERIMAGE_USE_TEST_LICENSE_KEY=ON` builds.

Regenerate with:
```bash
./build/AfterimageLicenseTool --gen-test-keys --out-dir Tests/Fixtures
# then copy LicensePublicKeys.inc into Source/Licensing/
```

Production private keys must never appear under `Source/`.
