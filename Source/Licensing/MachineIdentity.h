#pragma once

#include <juce_core/juce_core.h>

namespace afterimage
{
namespace licensing
{

/**
    Privacy-conscious machine identity.

    Derives a salted hash from stable, minimally sensitive identifiers.
    Raw hardware IDs are never exposed in UI or logs.
*/
class MachineIdentity
{
public:
    /** Compute (or return cached) hashed machine id. Safe on message thread only. */
    [[nodiscard]] juce::String getHashedId() const;

    /** Hex fingerprint suitable for optional display when required for support. */
    [[nodiscard]] juce::String getDisplayFingerprint() const;

private:
    mutable juce::String cached_;
};

[[nodiscard]] bool licenseAllowsMachine (const juce::StringArray& allowedMachineIds,
                                         const MachineIdentity& identity);

} // namespace licensing
} // namespace afterimage
