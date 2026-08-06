#include "LicenseStorage.h"

#include <juce_data_structures/juce_data_structures.h>

namespace afterimage
{
namespace licensing
{

LicenseStorage::LicenseStorage()
{
    const auto& overrideRoot = storageRootOverride();
    if (overrideRoot != juce::File())
        dir_ = overrideRoot;
    else
        dir_ = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("AdamAudio")
                   .getChildFile ("AFTERIMAGE");
}

juce::File& LicenseStorage::storageRootOverride()
{
    static juce::File root;
    return root;
}

void LicenseStorage::setStorageRootOverrideForTests (const juce::File& root)
{
    storageRootOverride() = root;
}

juce::File LicenseStorage::getStorageDirectory() const
{
    return dir_;
}

juce::File LicenseStorage::getStorageFile() const
{
    return dir_.getChildFile ("license-state.json");
}

LicenseResult LicenseStorage::load (StoredActivation& out) const
{
    out = {};
    const auto file = getStorageFile();
    if (! file.existsAsFile())
        return { true, LicenseStatus::Trial, "No stored activation." };

    const auto text = file.loadFileAsString();
    if (text.isEmpty())
        return { false, LicenseStatus::Corrupt, "Activation file is empty." };

    auto parsed = juce::JSON::parse (text);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return { false, LicenseStatus::Corrupt, "Activation file is corrupt." };

    out.schemaVersion = static_cast<int> (obj->getProperty ("schemaVersion"));
    out.licenseText = obj->getProperty ("licenseText").toString();
    out.customerName = obj->getProperty ("customerName").toString();
    out.trialStartUnix = static_cast<juce::int64> (obj->getProperty ("trialStartUnix"));
    out.lastObservedUnix = static_cast<juce::int64> (obj->getProperty ("lastObservedUnix"));
    out.activatedAtUnix = static_cast<juce::int64> (obj->getProperty ("activatedAtUnix"));
    out.hasLicense = static_cast<bool> (obj->getProperty ("hasLicense"));

    if (out.schemaVersion < 1 || out.schemaVersion > 1)
        return { false, LicenseStatus::UnsupportedVersion, "Unsupported storage schema." };

    return { true, LicenseStatus::Trial, "Loaded." };
}

LicenseResult LicenseStorage::save (const StoredActivation& data) const
{
    if (! dir_.exists())
    {
        const auto ok = dir_.createDirectory();
        if (! ok)
            return { false, LicenseStatus::StorageError, "Cannot create license storage directory." };
    }

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("schemaVersion", data.schemaVersion);
    obj->setProperty ("licenseText", data.licenseText);
    obj->setProperty ("customerName", data.customerName);
    obj->setProperty ("trialStartUnix", data.trialStartUnix);
    obj->setProperty ("lastObservedUnix", data.lastObservedUnix);
    obj->setProperty ("activatedAtUnix", data.activatedAtUnix);
    obj->setProperty ("hasLicense", data.hasLicense);

    const auto json = juce::JSON::toString (juce::var (obj), true);
    const auto target = getStorageFile();
    const auto tmp = target.getSiblingFile (target.getFileName() + ".tmp");

    if (! tmp.replaceWithText (json))
        return { false, LicenseStatus::StorageError, "Cannot write temporary activation file." };

    if (target.existsAsFile())
        target.deleteFile();

    if (! tmp.moveFileTo (target))
    {
        // Fallback: copy then delete temp
        if (! tmp.copyFileTo (target))
            return { false, LicenseStatus::StorageError, "Atomic replace failed." };
        tmp.deleteFile();
    }

    return { true, LicenseStatus::Licensed, "Saved." };
}

LicenseResult LicenseStorage::clearLicenseKeepTrial() const
{
    StoredActivation data;
    auto loadResult = load (data);
    juce::ignoreUnused (loadResult);
    data.hasLicense = false;
    data.licenseText.clear();
    data.customerName.clear();
    data.activatedAtUnix = 0;
    return save (data);
}

} // namespace licensing
} // namespace afterimage
