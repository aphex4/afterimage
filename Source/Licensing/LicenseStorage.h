#pragma once

#include "LicenseTypes.h"

#include <juce_core/juce_core.h>

namespace afterimage
{
namespace licensing
{

struct StoredActivation
{
    int schemaVersion = 1;
    juce::String licenseText;       // full AFTERIMAGE-LICENSE-1 document
    juce::String customerName;
    juce::int64 trialStartUnix = 0;
    juce::int64 lastObservedUnix = 0;
    juce::int64 activatedAtUnix = 0;
    bool hasLicense = false;
};

/**
    App-data license storage with atomic writes.
    Never stores licensing data in plugin/session state.
*/
class LicenseStorage
{
public:
    LicenseStorage();

    /** Test hook: redirect storage root (empty = default app-data path). */
    static void setStorageRootOverrideForTests (const juce::File& root);

    [[nodiscard]] juce::File getStorageDirectory() const;
    [[nodiscard]] juce::File getStorageFile() const;

    [[nodiscard]] LicenseResult load (StoredActivation& out) const;
    [[nodiscard]] LicenseResult save (const StoredActivation& data) const;
    [[nodiscard]] LicenseResult clearLicenseKeepTrial() const;

private:
    juce::File dir_;
    static juce::File& storageRootOverride();
};

} // namespace licensing
} // namespace afterimage
