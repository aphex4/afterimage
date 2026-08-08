#include "DSP/ScaleTheory.h"

#include <iostream>

namespace
{
int failures = 0;

void check (bool cond, const char* msg)
{
    if (! cond)
    {
        std::cout << "FAIL: " << msg << "\n";
        ++failures;
    }
}
} // namespace

int runScaleTheoryTests()
{
    failures = 0;
    std::cout << "ScaleTheory: masks / snap / MIDI mapping...\n";
    using namespace afterimage;

    const auto cMajor = scaleMask (ScaleType::Major, 0);
    check (isPitchClassInScale (cMajor, 0), "C in C major");
    check (isPitchClassInScale (cMajor, 4), "E in C major");
    check (! isPitchClassInScale (cMajor, 1), "C# not in C major");
    check (snapSemitoneDelta (cMajor, 1) == -1 || snapSemitoneDelta (cMajor, 1) == 1,
           "C# snaps by 1 semitone");
    check (nearestInScalePc (cMajor, 1) == 0 || nearestInScalePc (cMajor, 1) == 2,
           "nearest in-scale for C#");

    const auto aMinor = scaleMask (ScaleType::NaturalMinor, 9); // A
    check (isPitchClassInScale (aMinor, 9), "A in A minor");
    check (isPitchClassInScale (aMinor, 0), "C in A minor");

    check (std::abs (midiNoteToHz (69.0f) - 440.0f) < 0.01f, "A4=440");
    check (std::abs (hzToMidiNote (440.0f) - 69.0f) < 0.01f, "440→69");

    if (failures == 0)
        std::cout << "  ScaleTheory OK\n";
    return failures;
}
