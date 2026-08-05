#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

namespace afterimage
{

/**
    Lightweight debug helpers. Logging is compile-time gated so release
    builds never pay for string formatting on the audio thread.
*/
namespace debug
{
#if JUCE_DEBUG
    inline void log (const juce::String& message)
    {
        juce::Logger::writeToLog ("[AFTERIMAGE] " + message);
    }
#else
    inline void log (const juce::String&) {}
#endif

    template <typename T>
    inline bool isFiniteValue (T v) noexcept
    {
        return std::isfinite (static_cast<double> (v));
    }
} // namespace debug

} // namespace afterimage
