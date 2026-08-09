#include "SpectralEngine.h"
#include "XorshiftRandom.h"

#include <cmath>

namespace afterimage
{

float computeRandomRecallAge (float& wanderOffset,
                              std::uint32_t& rng,
                              float recallPosition,
                              float randomAmount,
                              float hopSeconds) noexcept
{
    const float noise = nextUnitRandom (rng) * 2.0f - 1.0f;
    const float cutoff = constants::randomRecallCutoffHz;
    const float coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                         * cutoff * juce::jmax (1.0e-6f, hopSeconds));
    wanderOffset += (noise - wanderOffset) * coeff;
    wanderOffset = juce::jlimit (-1.0f, 1.0f, wanderOffset);

    const float depth = juce::jlimit (0.0f, 1.0f, randomAmount) * constants::randomRecallMaxDepth;
    return juce::jlimit (0.0f, 1.0f, recallPosition + wanderOffset * depth);
}

void SpectralEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = std::max (1, numChannels);

    stft_.prepare (sampleRate, maxBlockSize, numChannels_);
    modes_.prepare (constants::numBins, sampleRate, numChannels_);
    workingFrame_.prepare (constants::numBins);
    analysisForHistory_.prepare (constants::numBins);
    frameSmoothers_.prepareFrameSmoothers (sampleRate);
    tapProfileScratch_.prepare (constants::numBins, sampleRate, stft_.getHopSize());

    histories_.clear();
    histories_.reserve (static_cast<std::size_t> (numChannels_));
    liveProfiles_.assign (static_cast<std::size_t> (numChannels_), {});
    frozenProfiles_.assign (static_cast<std::size_t> (numChannels_), {});
    effectiveProfiles_.assign (static_cast<std::size_t> (numChannels_), {});
    previousMagnitudes_.assign (static_cast<std::size_t> (numChannels_), {});
    hasPreviousFrame_.assign (static_cast<std::size_t> (numChannels_), false);
    frameCounters_.assign (static_cast<std::size_t> (numChannels_), 0);

    for (int c = 0; c < numChannels_; ++c)
    {
        auto hist = std::make_unique<SpectralHistoryBuffer>();
        hist->prepare (sampleRate, stft_.getHopSize(), constants::memoryLengthMaxSec);
        histories_.push_back (std::move (hist));

        liveProfiles_[static_cast<std::size_t> (c)].prepare (
            constants::numBins, sampleRate, stft_.getHopSize());
        frozenProfiles_[static_cast<std::size_t> (c)].prepare (
            constants::numBins, sampleRate, stft_.getHopSize());
        effectiveProfiles_[static_cast<std::size_t> (c)].prepare (
            constants::numBins, sampleRate, stft_.getHopSize());
        previousMagnitudes_[static_cast<std::size_t> (c)].assign (
            static_cast<std::size_t> (constants::numBins), 0.0f);
    }

    modes_.setTapProfileScratch (&tapProfileScratch_);
    stft_.setSpectrumCallback (&SpectralEngine::spectrumCallback, this);
    clearHistoryRequested_.store (false, std::memory_order_relaxed);
    prepared_ = true;
    reset();
}

void SpectralEngine::reset()
{
    stft_.reset();
    modes_.reset();
    modes_.setMode (currentMode_);
    workingFrame_.clear();
    analysisForHistory_.clear();
    std::fill (frameCounters_.begin(), frameCounters_.end(), 0);

    for (auto& hist : histories_)
        hist->reset();

    for (auto& p : liveProfiles_)
        p.clear();
    for (auto& p : frozenProfiles_)
        p.clear();
    for (auto& p : effectiveProfiles_)
        p.clear();

    for (auto& prev : previousMagnitudes_)
        std::fill (prev.begin(), prev.end(), 0.0f);

    std::fill (hasPreviousFrame_.begin(), hasPreviousFrame_.end(), false);

    wanderOffset_ = 0.0f;
    wanderRng_ = 0xA5F1C3E9u;
    freezeArmed_ = false;
    freezeEngaged_ = false;
    freezeCrossfade_ = 1.0f;
}

void SpectralEngine::releaseResources()
{
    stft_.setSpectrumCallback (nullptr, nullptr);
    stft_.releaseResources();
    prepared_ = false;
}

void SpectralEngine::setSpectralParameterTargets (float influence,
                                                  float recall,
                                                  float forget,
                                                  float blur,
                                                  float transientPreserve,
                                                  float randomRecall,
                                                  bool freeze) noexcept
{
    frameSmoothers_.setSpectralTargets (influence, recall, forget, blur, transientPreserve, randomRecall);
    freezeTarget_ = freeze;

    if (! freeze)
    {
        freezeArmed_ = false;
        freezeEngaged_ = false;
        freezeCrossfade_ = 0.0f;
    }
    else if (! freezeEngaged_)
    {
        freezeArmed_ = true;
    }

    syncHistoryFrozenFlag();
}

void SpectralEngine::setMode (SpectralMode mode) noexcept
{
    currentMode_ = mode;
    modes_.setMode (mode);
}

void SpectralEngine::setActiveMemoryLengthSeconds (float seconds) noexcept
{
    memoryLengthSeconds_ = seconds;
    for (auto& hist : histories_)
        hist->setActiveMemoryLengthSeconds (seconds);
}

void SpectralEngine::clearHistoryOnAudioThread() noexcept
{
    for (auto& hist : histories_)
        hist->clear();

    for (auto& prev : previousMagnitudes_)
        std::fill (prev.begin(), prev.end(), 0.0f);

    std::fill (hasPreviousFrame_.begin(), hasPreviousFrame_.end(), false);
    std::fill (frameCounters_.begin(), frameCounters_.end(), 0);

    for (auto& p : liveProfiles_)
        p.clear();
    for (auto& p : frozenProfiles_)
        p.clear();
    for (auto& p : effectiveProfiles_)
        p.clear();

    // Preset load / clear with Freeze still on: re-arm so we can capture after audio arrives.
    freezeEngaged_ = false;
    freezeArmed_ = freezeTarget_;
    freezeCrossfade_ = 0.0f;
    syncHistoryFrozenFlag();

    modes_.clearEraseMemory();
    modes_.clearShadowTail();
    viz_.storeHistoryFill (0.0f);
}

SpectralHistoryBuffer& SpectralEngine::getHistory (int channel) noexcept
{
    jassert (! histories_.empty());
    const int idx = juce::jlimit (0, static_cast<int> (histories_.size()) - 1, channel);
    return *histories_[static_cast<std::size_t> (idx)];
}

const SpectralHistoryBuffer& SpectralEngine::getHistory (int channel) const noexcept
{
    jassert (! histories_.empty());
    const int idx = juce::jlimit (0, static_cast<int> (histories_.size()) - 1, channel);
    return *histories_[static_cast<std::size_t> (idx)];
}

const SpectralMemoryProfile& SpectralEngine::getEffectiveMemoryProfile (int channel) const noexcept
{
    jassert (! effectiveProfiles_.empty());
    const int idx = juce::jlimit (0, static_cast<int> (effectiveProfiles_.size()) - 1, channel);
    return effectiveProfiles_[static_cast<std::size_t> (idx)];
}

void SpectralEngine::syncHistoryFrozenFlag() noexcept
{
    // Only lock history writes after a real freeze capture — never while armed.
    const bool lock = freezeEngaged_;
    for (auto& hist : histories_)
        hist->setFrozen (lock);
}

bool SpectralEngine::historyReadyForFreezeCapture() const noexcept
{
    if (histories_.empty() || sampleRate_ <= 0.0)
        return false;

    // Ready once we have enough frames to start a stable capture; capture still
    // aggregates up to freezeCaptureWindowMs when more history is available.
    const float hopMs = 1000.0f * static_cast<float> (stft_.getHopSize())
                        / static_cast<float> (sampleRate_);
    constexpr float kReadyWindowMs = 100.0f; // arm threshold (capture window remains ~200 ms)
    const int minFrames = std::max (
        1, static_cast<int> (std::lround (kReadyWindowMs / std::max (1.0e-3f, hopMs))));

    constexpr float kMinCaptureRms = 1.0e-5f;
    bool anyEnergy = false;

    for (int c = 0; c < numChannels_; ++c)
    {
        const auto& hist = *histories_[static_cast<std::size_t> (c)];
        const int available = hist.getAvailableFrameCount();
        if (available < minFrames)
            return false;

        const int check = std::min (available, minFrames);
        for (int age = 0; age < check; ++age)
        {
            if (hist.getFrameByAgeFrames (age).rms >= kMinCaptureRms)
            {
                anyEnergy = true;
                break;
            }
        }
    }

    return anyEnergy;
}

void SpectralEngine::captureFreezeProfiles() noexcept
{
    MemoryProfileOptions opts;
    opts.windowMs = constants::freezeCaptureWindowMs;
    opts.applyStability = true;

    for (int c = 0; c < numChannels_; ++c)
    {
        const auto& hist = *histories_[static_cast<std::size_t> (c)];
        frozenProfiles_[static_cast<std::size_t> (c)].captureRecent (
            hist, constants::freezeCaptureWindowMs, opts);
    }

    modes_.onFreezeEngaged();
    freezeEngaged_ = true;
    freezeArmed_ = false;
    freezeCrossfade_ = 0.0f; // start morph live → frozen
    syncHistoryFrozenFlag();
}

void SpectralEngine::updateFreezeState (bool freezeTarget, int hopSamples) noexcept
{
    if (! freezeTarget)
    {
        freezeArmed_ = false;
        freezeEngaged_ = false;
        freezeCrossfade_ = 0.0f;
        syncHistoryFrozenFlag();
        return;
    }

    if (! freezeEngaged_)
    {
        freezeArmed_ = true;
        syncHistoryFrozenFlag();

        if (historyReadyForFreezeCapture())
            captureFreezeProfiles();
        else
            return;
    }

    if (freezeCrossfade_ >= 1.0f - 1.0e-4f)
    {
        freezeCrossfade_ = 1.0f;
        return;
    }

    const float hopSec = static_cast<float> (std::max (1, hopSamples))
                         / static_cast<float> (std::max (1.0, sampleRate_));
    const float fadeSec = constants::freezeCrossfadeMs * 0.001f;
    const float alpha = hopSec / std::max (1.0e-4f, fadeSec);
    freezeCrossfade_ = std::min (1.0f, freezeCrossfade_ + alpha);
}

void SpectralEngine::buildEffectiveProfile (int channelIndex, float recallAge01) noexcept
{
    const auto ch = static_cast<std::size_t> (channelIndex);
    auto& hist = *histories_[ch];
    auto& live = liveProfiles_[ch];
    auto& frozen = frozenProfiles_[ch];
    auto& effective = effectiveProfiles_[ch];

    MemoryProfileOptions opts;
    opts.windowMs = constants::memoryProfileWindowMs;
    opts.applyStability = true;

    if (! freezeEngaged_ || freezeCrossfade_ < 1.0f - 1.0e-4f)
        live.buildFromHistory (hist, recallAge01, opts);

    if (! freezeEngaged_)
    {
        effective.copyFrom (live);
        return;
    }

    if (freezeCrossfade_ >= 1.0f - 1.0e-4f)
    {
        effective.copyFrom (frozen);
        return;
    }

    // Crossfade live recall profile → frozen capture
    effective.lerpFrom (live, frozen, freezeCrossfade_);
}

void SpectralEngine::publishVisualization (int channelIndex) noexcept
{
    if (channelIndex != 0 || histories_.empty())
        return;

    const auto& hist = *histories_.front();
    const double framesPerSecond = sampleRate_ / static_cast<double> (stft_.getHopSize());
    const int window = std::max (
        1, static_cast<int> (std::ceil (framesPerSecond * hist.getActiveMemoryLengthSeconds())));
    const float fill = static_cast<float> (hist.getAvailableFrameCount()) / static_cast<float> (window);

    viz_.storeHistoryFill (fill);
    viz_.storeTransient (workingFrame_.transientStrength);
    viz_.storeCentroid (workingFrame_.spectralCentroid);

    VisualizationSnapshot snap;
    snap.historyFill = juce::jlimit (0.0f, 1.0f, fill);
    snap.inputPeak = viz_.loadInputPeak();
    snap.outputPeak = viz_.loadOutputPeak();
    snap.transientStrength = workingFrame_.transientStrength;
    snap.spectralCentroidHz = workingFrame_.spectralCentroid;
    snap.memoryLengthNorm = juce::jlimit (
        0.0f, 1.0f,
        (memoryLengthSeconds_ - constants::memoryLengthMinSec)
            / (constants::memoryLengthMaxSec - constants::memoryLengthMinSec));
    snap.frozen = freezeTarget_;
    snap.sequence = ++vizSequence_;
    snap.numSeeds = 0;

    const auto& mags = workingFrame_.magnitudes;
    const int numBins = static_cast<int> (mags.size());
    float maxMag = 1.0e-8f;
    for (int k = 1; k < numBins; ++k)
        maxMag = juce::jmax (maxMag, mags[static_cast<std::size_t> (k)]);

    const float nyquist = static_cast<float> (sampleRate_ * 0.5);
    const float centroidNorm = juce::jlimit (
        0.0f, 1.0f, snap.spectralCentroidHz / juce::jmax (1.0f, nyquist));

    const int stride = juce::jmax (1, numBins / (VisualizationSnapshot::maxSeeds * 2));
    for (int k = 1; k < numBins && snap.numSeeds < VisualizationSnapshot::maxSeeds; k += stride)
    {
        int peakBin = k;
        float peakMag = mags[static_cast<std::size_t> (k)];
        const int end = juce::jmin (numBins, k + stride);
        for (int j = k + 1; j < end; ++j)
        {
            const float m = mags[static_cast<std::size_t> (j)];
            if (m > peakMag)
            {
                peakMag = m;
                peakBin = j;
            }
        }

        if (peakMag < maxMag * 0.08f)
            continue;

        auto& seed = snap.seeds[snap.numSeeds++];
        const float freqLin = static_cast<float> (peakBin) / static_cast<float> (juce::jmax (1, numBins - 1));
        seed.freqNorm = std::sqrt (freqLin);
        seed.magnitude = juce::jlimit (0.0f, 1.0f, peakMag / maxMag);
        seed.transient = workingFrame_.transientStrength;
        seed.centroidNorm = centroidNorm;
    }

    snapshots_.publish (snap);
}

void SpectralEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    jassert (prepared_);
    if (! prepared_)
        return;

    if (clearHistoryRequested_.exchange (false, std::memory_order_acq_rel))
        clearHistoryOnAudioThread();

    stft_.process (buffer);
}

void SpectralEngine::spectrumCallback (void* userData,
                                       float* interleavedFftData,
                                       int fftSize,
                                       int channelIndex) noexcept
{
    static_cast<SpectralEngine*> (userData)->onSpectrum (interleavedFftData, fftSize, channelIndex);
}

void SpectralEngine::onSpectrum (float* interleavedFftData, int fftSize, int channelIndex) noexcept
{
    jassert (fftSize == constants::fftSize);
    jassert (interleavedFftData != nullptr);

    if (channelIndex < 0 || channelIndex >= static_cast<int> (histories_.size()))
        return;

    // Advance spectral smoothers once per hop (channel 0 only); share params for L/R.
    if (channelIndex == 0)
    {
        updateFreezeState (freezeTarget_, stft_.getHopSize());

        // Mode freeze (tail / blur EMA / erase) only after capture — not while armed.
        hopParams_ = frameSmoothers_.snapSpectralParamsForHop (stft_.getHopSize(), freezeEngaged_);
        hopParams_.memoryLengthSeconds = memoryLengthSeconds_;

        const float hopSec = static_cast<float> (stft_.getHopSize())
                             / static_cast<float> (juce::jmax (1.0, sampleRate_));
        hopParams_.recallAge01 = computeRandomRecallAge (wanderOffset_,
                                                         wanderRng_,
                                                         hopParams_.recallPosition,
                                                         hopParams_.randomRecall,
                                                         hopSec);
    }

    auto& hist = *histories_[static_cast<std::size_t> (channelIndex)];

    workingFrame_.fillFromInterleavedFFT (interleavedFftData, fftSize);
    workingFrame_.frameIndex = frameCounters_[static_cast<std::size_t> (channelIndex)];
    workingFrame_.computeRmsAndCentroid (sampleRate_, fftSize);

    auto& prev = previousMagnitudes_[static_cast<std::size_t> (channelIndex)];
    const bool hadPrev = hasPreviousFrame_[static_cast<std::size_t> (channelIndex)];
    double flux = 0.0;
    double denom = 0.0;
    const int numBins = static_cast<int> (workingFrame_.magnitudes.size());
    jassert (numBins == constants::numBins);

    for (int k = 0; k < numBins; ++k)
    {
        const float mag = workingFrame_.magnitudes[static_cast<std::size_t> (k)];
        jassert (std::isfinite (mag));
        const float previous = hadPrev ? prev[static_cast<std::size_t> (k)] : mag;
        const float diff = mag - previous;
        if (diff > 0.0f)
            flux += static_cast<double> (diff);
        denom += static_cast<double> (mag);
        prev[static_cast<std::size_t> (k)] = mag;
    }

    hasPreviousFrame_[static_cast<std::size_t> (channelIndex)] = true;
    const float raw = (denom > 1.0e-9) ? static_cast<float> (flux / denom) : 0.0f;
    workingFrame_.transientStrength = juce::jlimit (0.0f, 1.0f, raw * kTransientFluxCalibration);

    analysisForHistory_.copyFrom (workingFrame_);

    ModeParams params = hopParams_;
    params.transientStrength = workingFrame_.transientStrength;

    const bool hasHistory = hist.getAvailableFrameCount() > 0;

    if (hasHistory)
    {
        // READ / profile BEFORE push — age 0 is newest committed frame, not this write.
        buildEffectiveProfile (channelIndex, params.recallAge01);

        const auto& profile = effectiveProfiles_[static_cast<std::size_t> (channelIndex)];

        const bool wroteSpectrum = modes_.process (currentMode_,
                                                   workingFrame_,
                                                   params,
                                                   profile.getMagnitudes(),
                                                   &hist,
                                                   channelIndex,
                                                   stft_.getHopSize());

        if (wroteSpectrum)
        {
            // Production Shadow uses current-phase magnitude writeback.
            // PropagatedGhostPhase remains available for experiments only (default OFF).
            if (currentMode_ == SpectralMode::Shadow
                && modes_.getShadowPhaseMode() == ShadowPhaseMode::PropagatedGhostPhase)
            {
                writeInterleavedAdditiveGhost (interleavedFftData,
                                               fftSize,
                                               workingFrame_.magnitudes.data(),
                                               workingFrame_.phases.data(),
                                               modes_.getLastShadowTail(),
                                               modes_.getGhostPhases (channelIndex),
                                               numBins);
            }
            else
            {
                writeInterleavedFromMagnitudePhase (interleavedFftData,
                                                    fftSize,
                                                    workingFrame_.magnitudes.data(),
                                                    workingFrame_.phases.data(),
                                                    numBins);
            }
        }
    }
    else if (channelIndex == 0)
    {
        modes_.setMode (currentMode_);
        modes_.tickModeCrossfade (stft_.getHopSize());
    }

    // Push unmodified analysis (Freeze skips via beginWriteFrame).
    if (SpectralFrame* slot = hist.beginWriteFrame())
    {
        slot->copyFrom (analysisForHistory_);
        slot->frameIndex = frameCounters_[static_cast<std::size_t> (channelIndex)]++;
        hist.commitWriteFrame();
    }

    publishVisualization (channelIndex);
}

} // namespace afterimage
