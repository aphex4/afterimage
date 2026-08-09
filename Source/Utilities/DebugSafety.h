#pragma once

#include <atomic>
#include <cstdint>

namespace afterimage
{
namespace debug
{

/**
    Debug-only DSP safety telemetry (relaxed atomics).
    Release builds keep the counters but UI should not depend on them.
*/
struct SafetyCounters
{
    std::atomic<std::uint64_t> nonFiniteBins { 0 };
    std::atomic<std::uint64_t> emergencyCeilingHits { 0 };
    std::atomic<std::uint64_t> frameEnergyClamps { 0 };
    std::atomic<float> lastStagePeak { 0.0f };

    void reset() noexcept
    {
        nonFiniteBins.store (0, std::memory_order_relaxed);
        emergencyCeilingHits.store (0, std::memory_order_relaxed);
        frameEnergyClamps.store (0, std::memory_order_relaxed);
        lastStagePeak.store (0.0f, std::memory_order_relaxed);
    }
};

inline SafetyCounters& safetyCounters() noexcept
{
    static SafetyCounters c;
    return c;
}

} // namespace debug
} // namespace afterimage
