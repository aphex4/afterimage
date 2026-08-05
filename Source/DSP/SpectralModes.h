#pragma once

#include "SpectralFrame.h"

namespace afterimage
{

enum class SpectralMode
{
    Shadow = 0,
    Erase,
    Merge
    // Phase 5+: Recall, Smear — extend without rewriting the engine core.
};

struct ModeParams
{
    float influence = 0.5f;          // 0..1
    float recallPosition = 0.45f;    // 0..1 (0 = newest)
    float forget = 0.35f;            // 0..1
    float blur = 0.15f;              // 0..1
    float transientPreserve = 0.5f;  // 0..1
    float randomRecall = 0.0f;       // 0..1
    float transientStrength = 0.0f;  // 0..1, from DSP
    bool  freeze = false;
};

/**
    Mode algorithms operate on magnitude/phase of a single SpectralFrame,
    reading historical frames via a callback/provider supplied by SpectralEngine.

    Phase 1: stubs that copy the current frame unchanged.
    Phase 4–5: real Shadow / Erase / Merge implementations.
*/
class SpectralModeProcessor
{
public:
    void prepare (int numBins);
    void reset();

    void process (SpectralMode mode,
                  SpectralFrame& frame,
                  const ModeParams& params,
                  const SpectralFrame* historyFrame,
                  float historyWeight);

    SpectralMode getLastMode() const noexcept { return lastMode_; }

private:
    void processShadow (SpectralFrame& frame, const ModeParams& params,
                        const SpectralFrame* historyFrame, float historyWeight);
    void processErase  (SpectralFrame& frame, const ModeParams& params,
                        const SpectralFrame* historyFrame, float historyWeight);
    void processMerge  (SpectralFrame& frame, const ModeParams& params,
                        const SpectralFrame* historyFrame, float historyWeight);

    void applyBlur (std::vector<float>& magnitudes, float blur01);

    int numBins_ = 0;
    SpectralMode lastMode_ = SpectralMode::Shadow;
    std::vector<float> blurScratch_;
    std::vector<float> eraseEnvelope_;
};

[[nodiscard]] inline const char* spectralModeName (SpectralMode mode) noexcept
{
    switch (mode)
    {
        case SpectralMode::Shadow: return "SHADOW";
        case SpectralMode::Erase:  return "ERASE";
        case SpectralMode::Merge:  return "MERGE";
    }
    return "SHADOW";
}

} // namespace afterimage
