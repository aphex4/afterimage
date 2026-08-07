#pragma once

#include "LicenseTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace afterimage
{
namespace licensing
{

/**
    Abstract asymmetric signature verifier.

    Production uses Monocypher Ed25519 (SHA-512 + Edwards25519).
    The private key never lives in the plugin binary.
*/
class LicenseVerifier
{
public:
    LicenseVerifier();
    ~LicenseVerifier();

    LicenseVerifier (const LicenseVerifier&) = delete;
    LicenseVerifier& operator= (const LicenseVerifier&) = delete;

    /** Embed public key (32 bytes). Called once at initialise. */
    void setPublicKey (const std::uint8_t* publicKey32) noexcept;

    /** Verify Ed25519 signature over message bytes. */
    [[nodiscard]] bool verify (const std::uint8_t* message,
                               std::size_t messageSize,
                               const std::uint8_t* signature64) const noexcept;

    /** Sign — only available when AFTERIMAGE_LICENSE_TOOL is defined (tool builds). */
#if defined (AFTERIMAGE_LICENSE_TOOL)
    static void sign (const std::uint8_t* message,
                      std::size_t messageSize,
                      const std::uint8_t* secretKey64,
                      std::uint8_t* signatureOut64) noexcept;

    static void keyPairFromSeed (const std::uint8_t* seed32,
                                 std::uint8_t* secretKeyOut64,
                                 std::uint8_t* publicKeyOut32) noexcept;
#endif

    [[nodiscard]] bool hasPublicKey() const noexcept { return hasKey_; }

private:
    std::array<std::uint8_t, 32> publicKey_ {};
    bool hasKey_ = false;
};

/** Production Ed25519 public key embedded in the plugin (not a private key). */
[[nodiscard]] const std::uint8_t* getProductionPublicKey() noexcept;

/** Test public key — only linked when AFTERIMAGE_USE_TEST_LICENSE_KEY=1. */
[[nodiscard]] const std::uint8_t* getTestPublicKey() noexcept;

} // namespace licensing
} // namespace afterimage
