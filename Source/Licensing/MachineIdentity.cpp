#include "MachineIdentity.h"

#include "ThirdParty/monocypher/monocypher.h"

#include <array>
#include <cstdio>

namespace afterimage
{
namespace licensing
{

namespace
{
constexpr const char* kSalt = "AFTERIMAGE-MACHINE-v1";

juce::String collectStableHints()
{
    juce::String raw;
    raw << juce::SystemStats::getComputerName() << '|';
    raw << juce::SystemStats::getLogonName() << '|';
    raw << juce::SystemStats::getOperatingSystemName() << '|';
#if JUCE_MAC
    raw << "mac";
#elif JUCE_WINDOWS
    raw << "win";
#elif JUCE_LINUX
    raw << "linux";
#else
    raw << "other";
#endif
    return raw;
}

juce::String toHex (const std::uint8_t* data, std::size_t n)
{
    juce::String s;
    s.preallocateBytes (static_cast<size_t> (n * 2 + 1));
    for (std::size_t i = 0; i < n; ++i)
        s << juce::String::toHexString ((int) data[i]).paddedLeft ('0', 2);
    return s.toLowerCase();
}
} // namespace

juce::String MachineIdentity::getHashedId() const
{
    if (cached_.isNotEmpty())
        return cached_;

    const auto material = juce::String (kSalt) + "|" + collectStableHints();
    std::array<std::uint8_t, 32> out {};
    crypto_blake2b (out.data(), out.size(),
                    reinterpret_cast<const std::uint8_t*> (material.toRawUTF8()),
                    material.getNumBytesAsUTF8());
    cached_ = toHex (out.data(), out.size());
    return cached_;
}

juce::String MachineIdentity::getDisplayFingerprint() const
{
    const auto id = getHashedId();
    if (id.length() < 12)
        return id;
    return id.substring (0, 4) + "-" + id.substring (4, 8) + "-" + id.substring (8, 12);
}

bool licenseAllowsMachine (const juce::StringArray& allowedMachineIds,
                           const MachineIdentity& identity)
{
    if (allowedMachineIds.isEmpty())
        return true;

    const auto id = identity.getHashedId();
    for (const auto& allowed : allowedMachineIds)
        if (allowed.equalsIgnoreCase (id))
            return true;
    return false;
}

} // namespace licensing
} // namespace afterimage
