#include "AfterimageFonts.h"

namespace AfterimageFonts
{

juce::String preferredTypefaceName()
{
   #if JUCE_MAC || JUCE_IOS
    return "Avenir Next";
   #elif JUCE_WINDOWS
    return "Segoe UI";
   #else
    return "Sans-Serif";
   #endif
}

float height (AfterimageFontRole role) noexcept
{
    switch (role)
    {
        case AfterimageFontRole::Wordmark:       return 28.0f;
        case AfterimageFontRole::Mode:           return 11.0f;
        case AfterimageFontRole::ControlLabel:   return 9.5f;
        case AfterimageFontRole::ParameterValue: return 10.5f;
        case AfterimageFontRole::TooltipTitle:   return 11.0f;
        case AfterimageFontRole::TooltipBody:    return 11.5f;
        case AfterimageFontRole::Status:         return 10.0f;
        case AfterimageFontRole::Caption:        return 10.0f;
    }
    return 11.0f;
}

juce::Font get (AfterimageFontRole role)
{
    const float h = height (role);
    juce::FontOptions opts (h);
    opts = opts.withName (preferredTypefaceName());

    switch (role)
    {
        case AfterimageFontRole::Wordmark:
            opts = opts.withStyle ("Demi Bold");
            break;
        case AfterimageFontRole::Mode:
        case AfterimageFontRole::ControlLabel:
        case AfterimageFontRole::TooltipTitle:
            opts = opts.withStyle ("Medium");
            break;
        case AfterimageFontRole::ParameterValue:
            opts = opts.withStyle ("Regular");
            break;
        case AfterimageFontRole::TooltipBody:
        case AfterimageFontRole::Status:
        case AfterimageFontRole::Caption:
            opts = opts.withStyle ("Regular");
            break;
    }

    return juce::Font (opts);
}

} // namespace AfterimageFonts
