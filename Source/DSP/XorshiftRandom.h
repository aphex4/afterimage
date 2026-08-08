#pragma once

#include <cmath>
#include <cstdint>

namespace afterimage
{

/** xorshift32 — RT-safe, no alloc, returns [0, 1). */
[[nodiscard]] inline float nextUnitRandom (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float> (state & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

[[nodiscard]] inline float nextBipolarRandom (std::uint32_t& state) noexcept
{
    return nextUnitRandom (state) * 2.0f - 1.0f;
}

/** Cheap Gaussian approx: sum of three uniforms − 1.5 (mean 0, rough σ≈0.5). */
[[nodiscard]] inline float nextGaussianApprox (std::uint32_t& state) noexcept
{
    return nextUnitRandom (state) + nextUnitRandom (state) + nextUnitRandom (state) - 1.5f;
}

/** Wrap radians to [-π, π]. */
[[nodiscard]] inline float princarg (float x) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float twoPi = 6.28318530717958647692f;
    return x - twoPi * std::floor ((x + pi) / twoPi);
}

} // namespace afterimage
