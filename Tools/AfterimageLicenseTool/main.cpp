/**
    AfterimageLicenseTool — developer-only (NOT linked into the plugin).
*/
#include "Licensing/LicenseDocument.h"
#include "Licensing/LicenseVerifier.h"
#include "Licensing/LicenseTypes.h"

#include <juce_core/juce_core.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

using namespace afterimage::licensing;

static bool readFileBytes (const juce::File& f, juce::MemoryBlock& out)
{
    return f.existsAsFile() && f.loadFileAsData (out);
}

static int genTestKeys (const juce::File& outDir)
{
    outDir.createDirectory();
    std::uint8_t seed[32];
    std::memset (seed, 0x42, 32);
    std::uint8_t sk[64], pk[32];
    LicenseVerifier::keyPairFromSeed (seed, sk, pk);

    outDir.getChildFile ("test_ed25519_seed.bin").replaceWithData (seed, 32);
    outDir.getChildFile ("test_ed25519_secret.bin").replaceWithData (sk, 64);
    outDir.getChildFile ("test_ed25519_public.bin").replaceWithData (pk, 32);

    juce::String inc;
    for (int i = 0; i < 32; ++i)
    {
        if (i % 8 == 0 && i > 0) inc << '\n';
        inc << "0x" << juce::String::toHexString ((int) pk[i]).paddedLeft ('0', 2);
        if (i < 31) inc << ", ";
    }
    inc << '\n';
    outDir.getChildFile ("LicensePublicKeys.inc").replaceWithText (inc);
    std::cout << "Wrote test keys to " << outDir.getFullPathName() << "\n";
    return 0;
}

static int signLicense (const juce::File& secretFile, const juce::File& outFile, LicenseDocument fields)
{
    juce::MemoryBlock secret;
    if (! readFileBytes (secretFile, secret) || secret.getSize() != 64)
    {
        std::cerr << "Secret key must be a 64-byte file.\n";
        return 1;
    }

    fields.payloadJson = buildCanonicalPayloadJson (fields);
    std::uint8_t sig[64];
    LicenseVerifier::sign (reinterpret_cast<const std::uint8_t*> (fields.payloadJson.toRawUTF8()),
                           (size_t) fields.payloadJson.getNumBytesAsUTF8(),
                           static_cast<const std::uint8_t*> (secret.getData()),
                           sig);
    const auto text = serializeLicenseFile (fields, std::vector<std::uint8_t> (sig, sig + 64));
    if (! outFile.replaceWithText (text))
    {
        std::cerr << "Failed to write " << outFile.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "Wrote " << outFile.getFullPathName() << "\n";
    return 0;
}

static int verifyLicense (const juce::File& publicFile, const juce::File& licenseFile)
{
    juce::MemoryBlock pub;
    if (! readFileBytes (publicFile, pub) || pub.getSize() != 32)
    {
        std::cerr << "Public key must be a 32-byte file.\n";
        return 1;
    }

    auto parsed = parseLicenseText (licenseFile.loadFileAsString());
    if (parsed.status == LicenseStatus::Corrupt
        || parsed.status == LicenseStatus::UnsupportedVersion)
    {
        std::cerr << "Parse failed: " << parsed.message << "\n";
        return 1;
    }

    LicenseVerifier v;
    v.setPublicKey (static_cast<const std::uint8_t*> (pub.getData()));
    const bool ok = v.verify (reinterpret_cast<const std::uint8_t*> (parsed.document.payloadJson.toRawUTF8()),
                              parsed.document.payloadJson.getNumBytesAsUTF8(),
                              parsed.document.signature.data());
    if (! ok)
    {
        std::cerr << "Signature INVALID\n";
        return 1;
    }

    const auto now = juce::Time::currentTimeMillis() / 1000;
    const bool expired = parsed.document.isExpiredAt (now);
    std::cout << "Signature OK. expired=" << (expired ? "yes" : "no")
              << " product=" << parsed.document.product
              << " customer=" << parsed.document.customerName << "\n";
    return expired ? 2 : 0;
}

int main (int argc, char* argv[])
{
    juce::StringArray tokens;
    for (int i = 0; i < argc; ++i)
        tokens.add (argv[i]);

    auto hasOpt = [&] (const char* name) { return tokens.contains (name); };
    auto optVal = [&] (const char* name) -> juce::String
    {
        const int idx = tokens.indexOf (name);
        if (idx >= 0 && idx + 1 < tokens.size())
            return tokens[idx + 1];
        return {};
    };

    if (hasOpt ("--gen-test-keys"))
    {
        const auto out = optVal ("--out-dir");
        return genTestKeys (juce::File (out.isNotEmpty() ? out : "Tests/Fixtures"));
    }

    if (hasOpt ("--sign"))
    {
        LicenseDocument doc;
        doc.product = kProductName;
        doc.licenseVersion = kLicenseFormatVersion;
        doc.licenseId = hasOpt ("--id") ? optVal ("--id") : juce::Uuid().toDashedString();
        doc.customerName = hasOpt ("--name") ? optVal ("--name") : "Test User";
        doc.customerEmail = hasOpt ("--email") ? optVal ("--email") : "test@example.com";
        doc.licenseType = "perpetual";
        doc.issuedAt = juce::Time::getCurrentTime().toISO8601 (true);
        if (hasOpt ("--expires"))
            doc.expiresAt = optVal ("--expires");
        doc.maxMachines = hasOpt ("--max-machines") ? optVal ("--max-machines").getIntValue() : 0;
        doc.features.add ("full");

        const auto secret = optVal ("--secret");
        const auto out = optVal ("--out");
        if (secret.isEmpty() || out.isEmpty())
        {
            std::cerr << "Need --secret and --out\n";
            return 1;
        }
        return signLicense (juce::File (secret), juce::File (out), doc);
    }

    if (hasOpt ("--verify"))
    {
        return verifyLicense (juce::File (optVal ("--public")),
                              juce::File (optVal ("--license")));
    }

    std::cout <<
        "AfterimageLicenseTool\n"
        "  --gen-test-keys --out-dir <dir>\n"
        "  --sign --secret <64B> --out <file> [--name ...] [--email ...] [--id ...] [--expires ISO]\n"
        "  --verify --public <32B> --license <file>\n";
    return 0;
}
