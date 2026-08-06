#pragma once

#include "LicenseDocument.h"
#include "LicenseStorage.h"
#include "LicenseTypes.h"
#include "LicenseVerifier.h"
#include "MachineIdentity.h"

#include <atomic>
#include <mutex>

namespace afterimage
{
namespace licensing
{

/**
    High-level licensing facade.

    All file IO / crypto happens on the message thread.
    Audio thread reads only entitlement atomic.
*/
class LicenseManager
{
public:
    LicenseManager();
    ~LicenseManager() = default;

    void initialise();

    [[nodiscard]] LicenseStatus getStatus() const noexcept;
    [[nodiscard]] bool isLicensed() const noexcept;
    [[nodiscard]] bool isTrialActive() const noexcept;
    [[nodiscard]] int getTrialDaysRemaining() const noexcept;

    [[nodiscard]] EntitlementState getEntitlement() const noexcept;

    LicenseResult activateFromKey (const juce::String& keyOrDocumentText);
    LicenseResult importLicenseFile (const juce::File& file);
    LicenseResult deactivateLocalLicense();

    [[nodiscard]] juce::String getLicensedCustomerName() const;
    [[nodiscard]] juce::String getStatusMessage() const;
    [[nodiscard]] juce::String getMachineFingerprint() const;

    /** Refresh wall-clock / trial bookkeeping (message thread / UI timer). */
    void refresh();
    void refreshFromWallClock() { refresh(); }
    [[nodiscard]] juce::String getMachineDisplayId() const { return getMachineFingerprint(); }

private:
    void recomputeStatusUnlocked();
    LicenseResult validateAndStore (const juce::String& licenseText);

    mutable std::mutex mutex_;
    LicenseVerifier verifier_;
    LicenseStorage storage_;
    MachineIdentity machine_;
    StoredActivation state_;
    LicenseStatus status_ = LicenseStatus::Trial;
    juce::String statusMessage_;
    juce::String customerName_;
    int trialDaysRemaining_ = kTrialDays;
    std::atomic<EntitlementState> entitlement_ { EntitlementState::FullProcessing };
    bool initialised_ = false;
};

} // namespace licensing
} // namespace afterimage
