#pragma once

namespace afterimage
{
namespace measure
{

/** Print/compute effect-strength diagnostics. Quiet unless verbose. */
void runEffectStrengthMeasurements (bool verbose);

/** True when AFTERIMAGE_PRINT_MEASUREMENTS is set and non-zero. */
bool envWantsMeasurements();

} // namespace measure
} // namespace afterimage
