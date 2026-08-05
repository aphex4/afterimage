#include "SpectralModes.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace afterimage
{

void SpectralModeProcessor::prepare (int numBins)
{
    numBins_ = numBins;
    blurScratch_.assign (static_cast<std::size_t> (numBins), 0.0f);
    eraseEnvelope_.assign (static_cast<std::size_t> (numBins), 0.0f);
    lastMode_ = SpectralMode::Shadow;
}

void SpectralModeProcessor::reset()
{
    std::fill (blurScratch_.begin(), blurScratch_.end(), 0.0f);
    std::fill (eraseEnvelope_.begin(), eraseEnvelope_.end(), 0.0f);
}

void SpectralModeProcessor::process (SpectralMode mode,
                                     SpectralFrame& frame,
                                     const ModeParams& params,
                                     const SpectralFrame* historyFrame,
                                     float historyWeight)
{
    lastMode_ = mode;

    // Phase 1: no spectral modification — modes are wired for Phase 4+.
    // Keeping the dispatch here avoids rewriting the engine later.
    switch (mode)
    {
        case SpectralMode::Shadow:
            processShadow (frame, params, historyFrame, historyWeight);
            break;
        case SpectralMode::Erase:
            processErase (frame, params, historyFrame, historyWeight);
            break;
        case SpectralMode::Merge:
            processMerge (frame, params, historyFrame, historyWeight);
            break;
    }

    if (params.blur > 0.0f)
        applyBlur (frame.magnitudes, params.blur);

    juce::ignoreUnused (historyFrame, historyWeight);
}

void SpectralModeProcessor::processShadow (SpectralFrame& /*frame*/,
                                           const ModeParams& /*params*/,
                                           const SpectralFrame* /*historyFrame*/,
                                           float /*historyWeight*/)
{
    // TODO(Phase 4): blend current magnitudes with interpolated historical frame.
}

void SpectralModeProcessor::processErase (SpectralFrame& /*frame*/,
                                          const ModeParams& /*params*/,
                                          const SpectralFrame* /*historyFrame*/,
                                          float /*historyWeight*/)
{
    // TODO(Phase 5): suppress bins that overlap the spectral memory envelope.
}

void SpectralModeProcessor::processMerge (SpectralFrame& /*frame*/,
                                          const ModeParams& /*params*/,
                                          const SpectralFrame* /*historyFrame*/,
                                          float /*historyWeight*/)
{
    // TODO(Phase 5): morph current magnitudes toward a selected past frame.
}

void SpectralModeProcessor::applyBlur (std::vector<float>& magnitudes, float blur01)
{
    // TODO(Phase 6): prefix-sum / box blur across neighbouring bins.
    juce::ignoreUnused (magnitudes, blur01);
}

} // namespace afterimage
