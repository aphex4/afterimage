#pragma once

#include "LicenseTypes.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace afterimage
{
namespace licensing
{

/**
    Parsed AFTERIMAGE-LICENSE-1 document.

    Text format:
      AFTERIMAGE-LICENSE-1
      <base64 payload>
      <base64 signature>

    Payload is deterministic UTF-8 JSON (sorted keys recommended for tooling).
*/
struct LicenseDocument
{
    juce::String product;
    int licenseVersion = 0;
    juce::String licenseId;
    juce::String customerName;
    juce::String customerEmail;
    juce::String licenseType; // perpetual | trial | subscription
    juce::String issuedAt;    // ISO-8601
    juce::String expiresAt;   // empty / null → no expiry
    int maxMachines = 0;      // 0 → unlimited / not enforced
    juce::StringArray features;
    juce::StringArray machineIds; // optional allow-list

    juce::String payloadJson;           // exact signed bytes as UTF-8 string
    std::vector<std::uint8_t> signature; // 64 bytes

    [[nodiscard]] bool hasExpiration() const noexcept;
    [[nodiscard]] bool isExpiredAt (juce::int64 unixSeconds) const;
};

struct ParseResult
{
    LicenseStatus status = LicenseStatus::Corrupt;
    LicenseDocument document;
    juce::String message;
};

[[nodiscard]] ParseResult parseLicenseText (const juce::String& text);
[[nodiscard]] juce::String serializeLicenseFile (const LicenseDocument& doc,
                                                 const std::vector<std::uint8_t>& signature64);

[[nodiscard]] juce::String buildCanonicalPayloadJson (const LicenseDocument& fields);

} // namespace licensing
} // namespace afterimage
