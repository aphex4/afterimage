#include "LicenseVerifier.h"

#include "ThirdParty/monocypher/monocypher.h"
#include "ThirdParty/monocypher/monocypher-ed25519.h"

#include <cstring>

namespace afterimage
{
namespace licensing
{

namespace
{
// Production Ed25519 public key (issuer secret lives outside this repo —
// see docs/LICENSING.md). Not a private key. All-zeros was the pre-install
// placeholder; AFTERIMAGE_COMMERCIAL_RELEASE rejects that state.
// Builds with AFTERIMAGE_USE_TEST_LICENSE_KEY use the test key instead.
constexpr std::uint8_t kProductionPublicKey[32] = {
    0x91, 0x84, 0x71, 0xff, 0x62, 0xd7, 0xf0, 0x41,
    0x3d, 0xc9, 0x8e, 0x2d, 0x4f, 0x2b, 0x71, 0xcb,
    0xbe, 0x18, 0x59, 0x12, 0x6c, 0xdc, 0x70, 0x01,
    0x89, 0x67, 0x2d, 0x55, 0xd7, 0x46, 0x9b, 0x6b
};

constexpr bool isAllZeroKey (const std::uint8_t* key) noexcept
{
    for (int i = 0; i < 32; ++i)
        if (key[i] != 0)
            return false;
    return true;
}

[[maybe_unused]] constexpr bool kProductionKeyIsPlaceholder = isAllZeroKey (kProductionPublicKey);

#if defined (AFTERIMAGE_COMMERCIAL_RELEASE)
  #if defined (AFTERIMAGE_USE_TEST_LICENSE_KEY)
    #error "AFTERIMAGE_USE_TEST_LICENSE_KEY cannot be enabled in AFTERIMAGE_COMMERCIAL_RELEASE builds."
  #endif
    static_assert (! kProductionKeyIsPlaceholder,
                   "Production licensing public key has not been installed. "
                   "Replace kProductionPublicKey before AFTERIMAGE_COMMERCIAL_RELEASE.");
#endif

#if defined (AFTERIMAGE_USE_TEST_LICENSE_KEY)
// Deterministic test public key matching Tests/Fixtures/test_ed25519_seed.bin
// Generated via AfterimageLicenseTool --gen-test-keys (seed = 32 × 0x42).
constexpr std::uint8_t kTestPublicKey[32] = {
    // Filled at first successful tool run; see LicensePublicKeys.inc if present.
    #include "LicensePublicKeys.inc"
};
#else
constexpr std::uint8_t kTestPublicKey[32] = {};
#endif
} // namespace

LicenseVerifier::LicenseVerifier() = default;

LicenseVerifier::~LicenseVerifier()
{
    crypto_wipe (publicKey_.data(), publicKey_.size());
}

void LicenseVerifier::setPublicKey (const std::uint8_t* publicKey32) noexcept
{
    if (publicKey32 == nullptr)
    {
        hasKey_ = false;
        publicKey_.fill (0);
        return;
    }
    std::memcpy (publicKey_.data(), publicKey32, 32);
    hasKey_ = true;
}

bool LicenseVerifier::verify (const std::uint8_t* message,
                              std::size_t messageSize,
                              const std::uint8_t* signature64) const noexcept
{
    if (! hasKey_ || message == nullptr || signature64 == nullptr)
        return false;

    return crypto_ed25519_check (signature64, publicKey_.data(), message, messageSize) == 0;
}

#if defined (AFTERIMAGE_LICENSE_TOOL)
void LicenseVerifier::sign (const std::uint8_t* message,
                            std::size_t messageSize,
                            const std::uint8_t* secretKey64,
                            std::uint8_t* signatureOut64) noexcept
{
    crypto_ed25519_sign (signatureOut64, secretKey64, message, messageSize);
}

void LicenseVerifier::keyPairFromSeed (const std::uint8_t* seed32,
                                       std::uint8_t* secretKeyOut64,
                                       std::uint8_t* publicKeyOut32) noexcept
{
    std::uint8_t seedCopy[32];
    std::memcpy (seedCopy, seed32, 32);
    crypto_ed25519_key_pair (secretKeyOut64, publicKeyOut32, seedCopy);
}
#endif

const std::uint8_t* getProductionPublicKey() noexcept
{
    return kProductionPublicKey;
}

const std::uint8_t* getTestPublicKey() noexcept
{
    return kTestPublicKey;
}

} // namespace licensing
} // namespace afterimage
