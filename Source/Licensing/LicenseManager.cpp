#include "LicenseManager.h"

namespace afterimage
{
namespace licensing
{

LicenseManager::LicenseManager() = default;

void LicenseManager::initialise()
{
    std::lock_guard lock (mutex_);
    if (initialised_)
        return;

#if defined (AFTERIMAGE_USE_TEST_LICENSE_KEY)
    verifier_.setPublicKey (getTestPublicKey());
#else
    verifier_.setPublicKey (getProductionPublicKey());
#endif

    auto loadResult = storage_.load (state_);
    if (! loadResult.ok && loadResult.status == LicenseStatus::Corrupt)
    {
        status_ = LicenseStatus::Corrupt;
        statusMessage_ = loadResult.message;
        entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
        initialised_ = true;
        return;
    }

    const auto now = juce::Time::currentTimeMillis() / 1000;

    if (state_.trialStartUnix <= 0)
    {
        state_.trialStartUnix = now;
        state_.lastObservedUnix = now;
        juce::ignoreUnused (storage_.save (state_));
    }

    recomputeStatusUnlocked();
    initialised_ = true;
}

void LicenseManager::refresh()
{
    std::lock_guard lock (mutex_);
    if (! initialised_)
        return;
    recomputeStatusUnlocked();
}

LicenseStatus LicenseManager::getStatus() const noexcept
{
    std::lock_guard lock (mutex_);
    return status_;
}

bool LicenseManager::isLicensed() const noexcept
{
    return getStatus() == LicenseStatus::Licensed;
}

bool LicenseManager::isTrialActive() const noexcept
{
    return getStatus() == LicenseStatus::Trial;
}

int LicenseManager::getTrialDaysRemaining() const noexcept
{
    std::lock_guard lock (mutex_);
    return trialDaysRemaining_;
}

EntitlementState LicenseManager::getEntitlement() const noexcept
{
    return entitlement_.load (std::memory_order_acquire);
}

juce::String LicenseManager::getLicensedCustomerName() const
{
    std::lock_guard lock (mutex_);
    return customerName_;
}

juce::String LicenseManager::getStatusMessage() const
{
    std::lock_guard lock (mutex_);
    return statusMessage_;
}

juce::String LicenseManager::getMachineFingerprint() const
{
    return machine_.getDisplayFingerprint();
}

LicenseResult LicenseManager::activateFromKey (const juce::String& keyOrDocumentText)
{
    return validateAndStore (keyOrDocumentText);
}

LicenseResult LicenseManager::importLicenseFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return { false, LicenseStatus::Corrupt, "License file not found." };

    return validateAndStore (file.loadFileAsString());
}

LicenseResult LicenseManager::deactivateLocalLicense()
{
    std::lock_guard lock (mutex_);
    auto result = storage_.clearLicenseKeepTrial();
    if (! result.ok)
        return result;

    state_.hasLicense = false;
    state_.licenseText.clear();
    state_.customerName.clear();
    state_.activatedAtUnix = 0;
    recomputeStatusUnlocked();
    return { true, status_, "License deactivated on this device." };
}

LicenseResult LicenseManager::validateAndStore (const juce::String& licenseText)
{
    std::lock_guard lock (mutex_);

    auto parsed = parseLicenseText (licenseText);
    if (parsed.status == LicenseStatus::WrongProduct
        || parsed.status == LicenseStatus::UnsupportedVersion
        || parsed.status == LicenseStatus::Corrupt)
    {
        status_ = parsed.status;
        statusMessage_ = parsed.message;
        return { false, parsed.status, parsed.message };
    }

    const auto& doc = parsed.document;
    if (! verifier_.verify (reinterpret_cast<const std::uint8_t*> (doc.payloadJson.toRawUTF8()),
                            doc.payloadJson.getNumBytesAsUTF8(),
                            doc.signature.data()))
    {
        status_ = LicenseStatus::InvalidSignature;
        statusMessage_ = "License signature is invalid.";
        return { false, LicenseStatus::InvalidSignature, statusMessage_ };
    }

    if (! licenseAllowsMachine (doc.machineIds, machine_))
    {
        status_ = LicenseStatus::Corrupt;
        statusMessage_ = "License is not valid for this machine.";
        return { false, LicenseStatus::Corrupt, statusMessage_ };
    }

    const auto now = juce::Time::currentTimeMillis() / 1000;
    if (doc.isExpiredAt (now))
    {
        status_ = LicenseStatus::Expired;
        statusMessage_ = "License has expired.";
        entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
        return { false, LicenseStatus::Expired, statusMessage_ };
    }

    state_.hasLicense = true;
    state_.licenseText = serializeLicenseFile (doc, doc.signature);
    state_.customerName = doc.customerName;
    state_.activatedAtUnix = now;
    state_.lastObservedUnix = now;

    auto saveResult = storage_.save (state_);
    if (! saveResult.ok)
    {
        status_ = LicenseStatus::StorageError;
        statusMessage_ = saveResult.message;
        return saveResult;
    }

    customerName_ = doc.customerName;
    status_ = LicenseStatus::Licensed;
    statusMessage_ = "Licensed to " + customerName_;
    entitlement_.store (EntitlementState::FullProcessing, std::memory_order_release);
    return { true, LicenseStatus::Licensed, statusMessage_ };
}

void LicenseManager::recomputeStatusUnlocked()
{
    const auto now = juce::Time::currentTimeMillis() / 1000;

    // Clock rollback detection (deterrence, not perfect security).
    if (state_.lastObservedUnix > 0
        && now + kClockRollbackGraceSeconds < state_.lastObservedUnix)
    {
        status_ = LicenseStatus::ClockRollback;
        statusMessage_ = "System clock appears to have moved backwards. Check date/time settings.";
        entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
        return;
    }

    // Advance persisted lastObserved only when enough wall time has elapsed so a
    // long-lived open editor does not rewrite activation state on every refresh.
    // In-memory status still recomputes from `now` every refresh (trial/expiry).
    if (now > state_.lastObservedUnix)
    {
        constexpr juce::int64 kPersistIntervalSec = 30;
        if ((now - state_.lastObservedUnix) >= kPersistIntervalSec)
        {
            state_.lastObservedUnix = now;
            juce::ignoreUnused (storage_.save (state_));
        }
    }

    if (state_.hasLicense && state_.licenseText.isNotEmpty())
    {
        auto parsed = parseLicenseText (state_.licenseText);
        if (parsed.status == LicenseStatus::Corrupt
            || parsed.status == LicenseStatus::WrongProduct
            || parsed.status == LicenseStatus::UnsupportedVersion)
        {
            status_ = parsed.status;
            statusMessage_ = parsed.message;
            entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
            return;
        }

        const auto& doc = parsed.document;
        if (! verifier_.verify (reinterpret_cast<const std::uint8_t*> (doc.payloadJson.toRawUTF8()),
                                doc.payloadJson.getNumBytesAsUTF8(),
                                doc.signature.data()))
        {
            status_ = LicenseStatus::InvalidSignature;
            statusMessage_ = "Stored license signature is invalid.";
            entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
            return;
        }

        if (doc.isExpiredAt (now))
        {
            status_ = LicenseStatus::Expired;
            statusMessage_ = "License has expired.";
            entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
            return;
        }

        if (! licenseAllowsMachine (doc.machineIds, machine_))
        {
            status_ = LicenseStatus::Corrupt;
            statusMessage_ = "License is not valid for this machine.";
            entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
            return;
        }

        customerName_ = doc.customerName.isNotEmpty() ? doc.customerName : state_.customerName;
        status_ = LicenseStatus::Licensed;
        statusMessage_ = customerName_.isNotEmpty()
                             ? ("Licensed to " + customerName_)
                             : juce::String ("Licensed");
        trialDaysRemaining_ = 0;
        entitlement_.store (EntitlementState::FullProcessing, std::memory_order_release);
        return;
    }

    // Trial path
    const auto elapsed = juce::jmax ((juce::int64) 0, now - state_.trialStartUnix);
    const int daysUsed = static_cast<int> (elapsed / 86400);
    trialDaysRemaining_ = juce::jmax (0, kTrialDays - daysUsed);

    if (trialDaysRemaining_ <= 0)
    {
        status_ = LicenseStatus::Expired;
        statusMessage_ = "Trial expired. Activate a license to continue processing.";
        entitlement_.store (EntitlementState::DryPassThrough, std::memory_order_release);
        return;
    }

    status_ = LicenseStatus::Trial;
    // ASCII hyphen only (em/en dashes mojibake in some hosts / font paths).
    statusMessage_ = "Trial - " + juce::String (trialDaysRemaining_) + " day"
                     + (trialDaysRemaining_ == 1 ? "" : "s") + " left";
    entitlement_.store (EntitlementState::FullProcessing, std::memory_order_release);
}

} // namespace licensing
} // namespace afterimage
