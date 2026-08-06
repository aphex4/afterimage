#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cmath>

/**
    Thread-safe visualization bridge.

    AUDIO THREAD publishes compact spectral seeds + scalars.
    UI THREAD copies the latest published snapshot (never touches history storage).
*/

namespace afterimage
{

struct VizParticleSeed
{
    float freqNorm = 0.0f;      // 0..1 (log-ish mapping done at publish)
    float magnitude = 0.0f;     // 0..1
    float transient = 0.0f;     // 0..1
    float centroidNorm = 0.0f;  // 0..1
};

struct VisualizationSnapshot
{
    static constexpr int maxSeeds = 64;

    VizParticleSeed seeds[maxSeeds] {};
    int numSeeds = 0;

    float historyFill = 0.0f;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    float transientStrength = 0.0f;
    float spectralCentroidHz = 0.0f;
    float memoryLengthNorm = 0.5f; // for well radius feel
    bool  frozen = false;
    std::uint32_t sequence = 0;
};

struct VisualizationAtomics
{
    std::atomic<float> historyFill        { 0.0f };
    std::atomic<float> inputPeak          { 0.0f };
    std::atomic<float> outputPeak         { 0.0f };
    std::atomic<float> transientStrength  { 0.0f };
    std::atomic<float> spectralCentroidHz { 0.0f };

    void storeInputPeak (float v) noexcept  { inputPeak.store (v, std::memory_order_relaxed); }
    void storeOutputPeak (float v) noexcept { outputPeak.store (v, std::memory_order_relaxed); }

    void storeHistoryFill (float v) noexcept
    {
        historyFill.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }

    void storeTransient (float v) noexcept
    {
        transientStrength.store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }

    void storeCentroid (float hz) noexcept
    {
        spectralCentroidHz.store (std::max (0.0f, hz), std::memory_order_relaxed);
    }

    float loadHistoryFill() const noexcept        { return historyFill.load (std::memory_order_relaxed); }
    float loadInputPeak() const noexcept          { return inputPeak.load (std::memory_order_relaxed); }
    float loadOutputPeak() const noexcept         { return outputPeak.load (std::memory_order_relaxed); }
    float loadTransient() const noexcept          { return transientStrength.load (std::memory_order_relaxed); }
    float loadCentroidHz() const noexcept         { return spectralCentroidHz.load (std::memory_order_relaxed); }
};

/** Double-buffered snapshot: audio writes inactive side, then publishes index. */
class SnapshotPublisher
{
public:
    void publish (const VisualizationSnapshot& snap) noexcept
    {
        const int write = 1 - published_.load (std::memory_order_relaxed);
        buffers_[write] = snap;
        published_.store (write, std::memory_order_release);
    }

    bool copyLatest (VisualizationSnapshot& dest) const noexcept
    {
        const int idx = published_.load (std::memory_order_acquire);
        dest = buffers_[idx];
        return true;
    }

private:
    VisualizationSnapshot buffers_[2] {};
    std::atomic<int> published_ { 0 };
};

} // namespace afterimage
