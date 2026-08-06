#include "LicenseDocument.h"

#include <juce_data_structures/juce_data_structures.h>

namespace afterimage
{
namespace licensing
{

namespace
{
juce::String stripWhitespace (juce::String s)
{
    return s.trim().removeCharacters (" \t\r\n");
}

std::optional<juce::int64> parseIso8601ToUnix (const juce::String& iso)
{
    if (iso.isEmpty() || iso == "null")
        return std::nullopt;

    // Accept "YYYY-MM-DDTHH:MM:SSZ" via Time::fromISO8601 when available;
    // fall back to juce::Time parsing.
    auto t = juce::Time::fromISO8601 (iso);
    if (t.toMilliseconds() == 0 && ! iso.startsWith ("1970"))
    {
        // Some JUCE builds are strict; try without fractional seconds.
        t = juce::Time::fromISO8601 (iso.upToFirstOccurrenceOf (".", false, false)
                                         .retainCharacters ("0123456789T:-Z")
                                     + (iso.endsWithIgnoreCase ("Z") ? "" : "Z"));
    }
    if (t.toMilliseconds() == 0 && iso != "1970-01-01T00:00:00Z")
        return std::nullopt;
    return t.toMilliseconds() / 1000;
}
} // namespace

bool LicenseDocument::hasExpiration() const noexcept
{
    return expiresAt.isNotEmpty() && expiresAt != "null";
}

bool LicenseDocument::isExpiredAt (juce::int64 unixSeconds) const
{
    if (! hasExpiration())
        return false;
    auto exp = parseIso8601ToUnix (expiresAt);
    if (! exp.has_value())
        return true; // unparseable expiry → treat as expired/invalid
    return unixSeconds >= *exp;
}

juce::String buildCanonicalPayloadJson (const LicenseDocument& fields)
{
    // Deterministic key order for stable signatures.
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("product", fields.product);
    obj->setProperty ("licenseVersion", fields.licenseVersion);
    obj->setProperty ("licenseId", fields.licenseId);
    obj->setProperty ("customerName", fields.customerName);
    obj->setProperty ("customerEmail", fields.customerEmail);
    obj->setProperty ("licenseType", fields.licenseType);
    obj->setProperty ("issuedAt", fields.issuedAt);
    if (fields.expiresAt.isEmpty() || fields.expiresAt == "null")
        obj->setProperty ("expiresAt", juce::var());
    else
        obj->setProperty ("expiresAt", fields.expiresAt);
    obj->setProperty ("maxMachines", fields.maxMachines);

    juce::Array<juce::var> feats;
    for (const auto& f : fields.features)
        feats.add (f);
    obj->setProperty ("features", feats);

    if (! fields.machineIds.isEmpty())
    {
        juce::Array<juce::var> mids;
        for (const auto& m : fields.machineIds)
            mids.add (m);
        obj->setProperty ("machineIds", mids);
    }

    return juce::JSON::toString (juce::var (obj), true);
}

ParseResult parseLicenseText (const juce::String& text)
{
    ParseResult result;
    auto lines = juce::StringArray::fromLines (text.trim());
    lines.removeEmptyStrings (true);

    if (lines.size() < 3)
    {
        result.status = LicenseStatus::Corrupt;
        result.message = "License file is truncated or incomplete.";
        return result;
    }

    if (lines[0].trim() != kLicenseFormatTag)
    {
        // Also accept JSON container
        auto parsed = juce::JSON::parse (text);
        if (auto* obj = parsed.getDynamicObject())
        {
            const auto format = obj->getProperty ("format").toString();
            if (format != kLicenseFormatTag)
            {
                result.status = LicenseStatus::UnsupportedVersion;
                result.message = "Unsupported license format.";
                return result;
            }
            const auto payloadB64 = obj->getProperty ("payload").toString();
            const auto sigB64 = obj->getProperty ("signature").toString();
            lines.clear();
            lines.add (kLicenseFormatTag);
            lines.add (payloadB64);
            lines.add (sigB64);
        }
        else
        {
            result.status = LicenseStatus::UnsupportedVersion;
            result.message = "Unsupported license format.";
            return result;
        }
    }

    juce::MemoryOutputStream payloadBin;
    if (! juce::Base64::convertFromBase64 (payloadBin, stripWhitespace (lines[1])))
    {
        result.status = LicenseStatus::Corrupt;
        result.message = "Payload Base64 is corrupt.";
        return result;
    }

    juce::MemoryOutputStream sigBin;
    if (! juce::Base64::convertFromBase64 (sigBin, stripWhitespace (lines[2])))
    {
        result.status = LicenseStatus::Corrupt;
        result.message = "Signature Base64 is corrupt.";
        return result;
    }

    if (sigBin.getDataSize() != 64)
    {
        result.status = LicenseStatus::Corrupt;
        result.message = "Signature length is invalid.";
        return result;
    }

    const auto payloadStr = juce::String::fromUTF8 (
        static_cast<const char*> (payloadBin.getData()),
        static_cast<int> (payloadBin.getDataSize()));

    auto json = juce::JSON::parse (payloadStr);
    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
    {
        result.status = LicenseStatus::Corrupt;
        result.message = "Payload JSON is corrupt.";
        return result;
    }

    LicenseDocument doc;
    doc.payloadJson = payloadStr;
    doc.signature.assign (static_cast<const std::uint8_t*> (sigBin.getData()),
                          static_cast<const std::uint8_t*> (sigBin.getData()) + 64);

    doc.product = obj->getProperty ("product").toString();
    doc.licenseVersion = static_cast<int> (obj->getProperty ("licenseVersion"));
    doc.licenseId = obj->getProperty ("licenseId").toString();
    doc.customerName = obj->getProperty ("customerName").toString();
    doc.customerEmail = obj->getProperty ("customerEmail").toString();
    doc.licenseType = obj->getProperty ("licenseType").toString();
    doc.issuedAt = obj->getProperty ("issuedAt").toString();
    {
        const auto exp = obj->getProperty ("expiresAt");
        if (exp.isVoid() || exp.isUndefined())
            doc.expiresAt = {};
        else
            doc.expiresAt = exp.toString();
    }
    doc.maxMachines = static_cast<int> (obj->getProperty ("maxMachines"));

    if (auto* feats = obj->getProperty ("features").getArray())
        for (const auto& v : *feats)
            doc.features.add (v.toString());

    if (auto* mids = obj->getProperty ("machineIds").getArray())
        for (const auto& v : *mids)
            doc.machineIds.add (v.toString());

    if (doc.product != kProductName)
    {
        result.status = LicenseStatus::WrongProduct;
        result.message = "License is for a different product.";
        result.document = doc;
        return result;
    }

    if (doc.licenseVersion != kLicenseFormatVersion)
    {
        result.status = LicenseStatus::UnsupportedVersion;
        result.message = "Unsupported license version.";
        result.document = doc;
        return result;
    }

    result.status = LicenseStatus::Licensed; // signature checked by manager
    result.document = std::move (doc);
    result.message = "Parsed.";
    return result;
}

juce::String serializeLicenseFile (const LicenseDocument& doc,
                                   const std::vector<std::uint8_t>& signature64)
{
    juce::ignoreUnused (doc);
    const auto payloadB64 = juce::Base64::toBase64 (doc.payloadJson.toRawUTF8(),
                                                    static_cast<size_t> (doc.payloadJson.getNumBytesAsUTF8()));
    const auto sigB64 = juce::Base64::toBase64 (signature64.data(), signature64.size());
    return juce::String (kLicenseFormatTag) + "\n" + payloadB64 + "\n" + sigB64 + "\n";
}

} // namespace licensing
} // namespace afterimage
