/**
    Licensing unit tests — uses AFTERIMAGE_USE_TEST_LICENSE_KEY fixtures.
*/
#include <juce_core/juce_core.h>

#include "Licensing/LicenseDocument.h"
#include "Licensing/LicenseManager.h"
#include "Licensing/LicenseStorage.h"
#include "Licensing/LicenseVerifier.h"
#include "Licensing/MachineIdentity.h"

#include <cstring>
#include <iostream>
#include <vector>

using namespace afterimage::licensing;

extern int gFailures;

#define LIC_CHECK(cond) \
    do { \
        if (! (cond)) { \
            std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++gFailures; \
        } \
    } while (0)

namespace
{
juce::File fixturesDir()
{
#if defined (AFTERIMAGE_FIXTURES_DIR)
    return juce::File (AFTERIMAGE_FIXTURES_DIR);
#else
    return juce::File::getCurrentWorkingDirectory().getChildFile ("Tests/Fixtures");
#endif
}

bool loadKey (const char* name, juce::MemoryBlock& out, size_t expect)
{
    const auto f = fixturesDir().getChildFile (name);
    if (! f.existsAsFile() || ! f.loadFileAsData (out))
        return false;
    return out.getSize() == expect;
}

LicenseDocument makeFields (const juce::String& name = "Test User",
                            const juce::String& expires = {})
{
    LicenseDocument doc;
    doc.product = kProductName;
    doc.licenseVersion = kLicenseFormatVersion;
    doc.licenseId = "test-license-001";
    doc.customerName = name;
    doc.customerEmail = "test@example.com";
    doc.licenseType = "perpetual";
    doc.issuedAt = "2026-08-06T00:00:00Z";
    doc.expiresAt = expires;
    doc.maxMachines = 0;
    doc.features.add ("full");
    return doc;
}

juce::String signDocument (LicenseDocument fields)
{
    juce::MemoryBlock secret;
    LIC_CHECK (loadKey ("test_ed25519_secret.bin", secret, 64));

    fields.payloadJson = buildCanonicalPayloadJson (fields);
    std::uint8_t sig[64];
    LicenseVerifier::sign (reinterpret_cast<const std::uint8_t*> (fields.payloadJson.toRawUTF8()),
                           (size_t) fields.payloadJson.getNumBytesAsUTF8(),
                           static_cast<const std::uint8_t*> (secret.getData()),
                           sig);
    std::vector<std::uint8_t> sigVec (sig, sig + 64);
    return serializeLicenseFile (fields, sigVec);
}

void checkNoPrivateKeyInProductionSources()
{
    const juce::File root = juce::File (AFTERIMAGE_FIXTURES_DIR).getParentDirectory().getParentDirectory();
    const juce::File licensing = root.getChildFile ("Source/Licensing");
    for (const auto& entry : juce::RangedDirectoryIterator (licensing, true, "*.h;*.cpp;*.inc"))
    {
        const auto f = entry.getFile();
        if (f.getFullPathName().contains ("ThirdParty"))
            continue;
        const auto text = f.loadFileAsString();
        LIC_CHECK (! text.containsIgnoreCase ("BEGIN PRIVATE KEY"));
        LIC_CHECK (! text.containsIgnoreCase (
            "4242424242424242424242424242424242424242424242424242424242424242"));
    }
}
} // namespace

void runLicensingTests()
{
    std::cout << "Licensing tests...\n";

    juce::MemoryBlock pub, secret;
    if (! loadKey ("test_ed25519_public.bin", pub, 32)
        || ! loadKey ("test_ed25519_secret.bin", secret, 64))
    {
        std::cerr << "FAIL: missing test key fixtures in " << fixturesDir().getFullPathName() << "\n";
        ++gFailures;
        return;
    }

    // Valid signature
    {
        const auto text = signDocument (makeFields());
        auto parsed = parseLicenseText (text);
        LIC_CHECK (parsed.status == LicenseStatus::Licensed
                   || parsed.document.product == kProductName);
        LicenseVerifier v;
        v.setPublicKey (static_cast<const std::uint8_t*> (pub.getData()));
        LIC_CHECK (v.verify (reinterpret_cast<const std::uint8_t*> (parsed.document.payloadJson.toRawUTF8()),
                             parsed.document.payloadJson.getNumBytesAsUTF8(),
                             parsed.document.signature.data()));
        LIC_CHECK (! parsed.document.isExpiredAt (juce::Time::currentTimeMillis() / 1000));
    }

    // Invalid signature
    {
        auto parsed = parseLicenseText (signDocument (makeFields()));
        parsed.document.signature[0] ^= 0xFF;
        LicenseVerifier v;
        v.setPublicKey (static_cast<const std::uint8_t*> (pub.getData()));
        LIC_CHECK (! v.verify (reinterpret_cast<const std::uint8_t*> (parsed.document.payloadJson.toRawUTF8()),
                               parsed.document.payloadJson.getNumBytesAsUTF8(),
                               parsed.document.signature.data()));
    }

    // Modified payload
    {
        auto parsed = parseLicenseText (signDocument (makeFields()));
        if (parsed.document.payloadJson.length() > 10)
        {
            auto mutated = parsed.document.payloadJson;
            mutated = mutated.replaceSection (10, 1, "X");
            LicenseVerifier v;
            v.setPublicKey (static_cast<const std::uint8_t*> (pub.getData()));
            LIC_CHECK (! v.verify (reinterpret_cast<const std::uint8_t*> (mutated.toRawUTF8()),
                                   mutated.getNumBytesAsUTF8(),
                                   parsed.document.signature.data()));
        }
    }

    // Corrupt Base64
    {
        auto r = parseLicenseText ("AFTERIMAGE-LICENSE-1\n%%%\n@@@\n");
        LIC_CHECK (r.status == LicenseStatus::Corrupt);
    }

    // Wrong product
    {
        auto fields = makeFields();
        fields.product = "OTHER";
        fields.payloadJson = buildCanonicalPayloadJson (fields);
        std::uint8_t sig[64];
        LicenseVerifier::sign (reinterpret_cast<const std::uint8_t*> (fields.payloadJson.toRawUTF8()),
                               (size_t) fields.payloadJson.getNumBytesAsUTF8(),
                               static_cast<const std::uint8_t*> (secret.getData()),
                               sig);
        auto text = serializeLicenseFile (fields, std::vector<std::uint8_t> (sig, sig + 64));
        auto parsed = parseLicenseText (text);
        LIC_CHECK (parsed.status == LicenseStatus::WrongProduct);
    }

    // Unsupported format
    {
        const juce::String bogusSig = juce::String::repeatedString ("A", 88);
        auto r = parseLicenseText (juce::String ("AFTERIMAGE-LICENSE-99\naGVsbG8=\n")
                                   + bogusSig + "\n");
        LIC_CHECK (r.status == LicenseStatus::UnsupportedVersion);
    }

    // Expired
    {
        auto fields = makeFields ("Expired User", "2020-01-01T00:00:00Z");
        auto parsed = parseLicenseText (signDocument (fields));
        LIC_CHECK (parsed.document.isExpiredAt (juce::Time::currentTimeMillis() / 1000));
    }

    // Perpetual
    {
        auto fields = makeFields();
        fields.expiresAt = {};
        LIC_CHECK (! fields.isExpiredAt (juce::Time::currentTimeMillis() / 1000));
    }

    // Machine mismatch
    {
        MachineIdentity id;
        juce::StringArray allowed;
        allowed.add ("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
        LIC_CHECK (! licenseAllowsMachine (allowed, id));
        LIC_CHECK (licenseAllowsMachine ({}, id));
    }

    // Storage round-trip (isolated temp directory)
    {
        const auto tmpRoot = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile ("build-debug")
                                 .getChildFile ("license-test-" + juce::Uuid().toString());
        tmpRoot.createDirectory();
        LicenseStorage::setStorageRootOverrideForTests (tmpRoot);

        LicenseStorage storage;
        StoredActivation data;
        data.schemaVersion = 1;
        data.licenseText = signDocument (makeFields ("Storage User"));
        data.customerName = "Storage User";
        data.trialStartUnix = 1000;
        data.lastObservedUnix = 2000;
        data.activatedAtUnix = 1500;
        data.hasLicense = true;
        auto saveResult = storage.save (data);
        LIC_CHECK (saveResult.ok);

        StoredActivation loaded;
        auto loadResult = storage.load (loaded);
        LIC_CHECK (loadResult.ok);
        LIC_CHECK (loaded.customerName == "Storage User");
        LIC_CHECK (loaded.licenseText.contains (kLicenseFormatTag));

        // Truncated storage
        const auto f = storage.getStorageFile();
        f.replaceWithText ("{ \"schemaVersion\": ");
        StoredActivation out;
        auto r = storage.load (out);
        LIC_CHECK (! r.ok || r.status == LicenseStatus::Corrupt);

        LicenseStorage::setStorageRootOverrideForTests ({});
        tmpRoot.deleteRecursively();
    }

    checkNoPrivateKeyInProductionSources();

    LIC_CHECK (juce::String (licenseStatusName (LicenseStatus::Trial)) == "Trial");
    LIC_CHECK (juce::String (licenseStatusName (LicenseStatus::Licensed)) == "Licensed");

    std::cout << "  licensing checks done\n";
}
