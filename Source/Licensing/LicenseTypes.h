#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>

namespace afterimage
{
namespace licensing
{

enum class LicenseStatus : int
{
    Trial = 0,
    Licensed,
    Expired,
    InvalidSignature,
    WrongProduct,
    UnsupportedVersion,
    Corrupt,
    StorageError,
    ClockRollback
};

/** Cached entitlement read by the audio thread — never do license IO here. */
enum class EntitlementState : std::uint8_t
{
    FullProcessing = 0,   // trial active or licensed
    DryPassThrough = 1    // trial expired / invalid — latency-compensated dry only
};

struct LicenseResult
{
    bool ok = false;
    LicenseStatus status = LicenseStatus::Corrupt;
    juce::String message;
};

[[nodiscard]] inline const char* licenseStatusName (LicenseStatus s) noexcept
{
    switch (s)
    {
        case LicenseStatus::Trial:              return "Trial";
        case LicenseStatus::Licensed:           return "Licensed";
        case LicenseStatus::Expired:            return "Expired";
        case LicenseStatus::InvalidSignature:   return "InvalidSignature";
        case LicenseStatus::WrongProduct:       return "WrongProduct";
        case LicenseStatus::UnsupportedVersion: return "UnsupportedVersion";
        case LicenseStatus::Corrupt:            return "Corrupt";
        case LicenseStatus::StorageError:       return "StorageError";
        case LicenseStatus::ClockRollback:      return "ClockRollback";
    }
    return "Corrupt";
}

inline constexpr int kLicenseFormatVersion = 1;
inline constexpr const char* kLicenseFormatTag = "AFTERIMAGE-LICENSE-1";
inline constexpr const char* kProductName = "AFTERIMAGE";
inline constexpr int kTrialDays = 14;
inline constexpr int kClockRollbackGraceSeconds = 3600; // ignore small corrections

} // namespace licensing
} // namespace afterimage
