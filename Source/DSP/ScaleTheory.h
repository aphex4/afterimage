#pragma once

#include <cmath>
#include <cstdint>

namespace afterimage
{

enum class ScaleType : int
{
    Major = 0,
    NaturalMinor,
    Dorian,
    PentatonicMajor,
    PentatonicMinor,
    Chromatic,
    NumTypes
};

/** Pitch-class mask bit i set ⇒ MIDI note % 12 is in-scale. */
[[nodiscard]] inline std::uint16_t scaleMask (ScaleType type, int rootPc) noexcept
{
    rootPc = ((rootPc % 12) + 12) % 12;
    auto rotate = [rootPc] (std::uint16_t mask) noexcept -> std::uint16_t
    {
        return (std::uint16_t) (((mask << rootPc) | (mask >> (12 - rootPc))) & 0x0FFFu);
    };

    // Bit i (LSB=C) set when pitch-class i is in the C-rooted scale, then rotated.
    switch (type)
    {
        case ScaleType::Major:           return rotate (0b101011010101); // 0,2,4,5,7,9,11
        case ScaleType::NaturalMinor:    return rotate (0b010110101101); // 0,2,3,5,7,8,10
        case ScaleType::Dorian:          return rotate (0b011010101101); // 0,2,3,5,7,9,10
        case ScaleType::PentatonicMajor: return rotate (0b001010010101); // 0,2,4,7,9
        case ScaleType::PentatonicMinor: return rotate (0b010010101001); // 0,3,5,7,10
        case ScaleType::Chromatic:
        default:                         return 0x0FFFu;
    }
}

[[nodiscard]] inline bool isPitchClassInScale (std::uint16_t mask, int pc) noexcept
{
    pc = ((pc % 12) + 12) % 12;
    return (mask & (1u << pc)) != 0;
}

/** Nearest in-scale pitch class; prefers downward then upward on ties. */
[[nodiscard]] inline int nearestInScalePc (std::uint16_t mask, int pc) noexcept
{
    pc = ((pc % 12) + 12) % 12;
    if (isPitchClassInScale (mask, pc))
        return pc;

    for (int d = 1; d <= 6; ++d)
    {
        const int down = (pc - d + 12) % 12;
        if (isPitchClassInScale (mask, down))
            return down;
        const int up = (pc + d) % 12;
        if (isPitchClassInScale (mask, up))
            return up;
    }
    return pc;
}

/** Semitone shift (-6..+6) to snap pc into scale. */
[[nodiscard]] inline int snapSemitoneDelta (std::uint16_t mask, int pc) noexcept
{
    pc = ((pc % 12) + 12) % 12;
    const int target = nearestInScalePc (mask, pc);
    int d = target - pc;
    if (d > 6) d -= 12;
    if (d < -6) d += 12;
    return d;
}

[[nodiscard]] inline float midiNoteToHz (float note) noexcept
{
    return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
}

[[nodiscard]] inline float hzToMidiNote (float hz) noexcept
{
    if (hz < 1.0f)
        return 0.0f;
    return 69.0f + 12.0f * std::log2 (hz / 440.0f);
}

} // namespace afterimage
