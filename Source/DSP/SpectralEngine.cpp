#include "SpectralEngine.h"

#include <cmath>

namespace afterimage
{

void SpectralEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    sampleRate_ = sampleRate;
    numChannels_ = std::max (1, numChannels);

    stft_.prepare (sampleRate, maxBlockSize, numChannels_);
    modes_.prepare (constants::numBins);

    histories_.clear();
    histories_.reserve (static_cast<std::size_t> (numChannels_));
    previousMagnitudes_.assign (static_cast<std::size_t> (numChannels_), {});
    hasPreviousFrame_.assign (static_cast<std::size_t> (numChannels_), false);
    frameCounters_.assign (static_cast<std::size_t> (numChannels_), 0);

    for (int c = 0; c < numChannels_; ++c)
    {
        auto hist = std::make_unique<SpectralHistoryBuffer>();
        hist->prepare (sampleRate, stft_.getHopSize(), constants::memoryLengthMaxSec);
        histories_.push_back (std::move (hist));

        previousMagnitudes_[static_cast<std::size_t> (c)].assign (
            static_cast<std::size_t> (constants::numBins), 0.0f);
    }

    stft_.setSpectrumCallback (&SpectralEngine::spectrumCallback, this);

    prepared_ = true;
    reset();
}

void SpectralEngine::reset()
{
    stft_.reset();
    modes_.reset();
    std::fill (frameCounters_.begin(), frameCounters_.end(), 0);

    for (auto& hist : histories_)
        hist->reset();

    for (auto& prev : previousMagnitudes_)
        std::fill (prev.begin(), prev.end(), 0.0f);

    std::fill (hasPreviousFrame_.begin(), hasPreviousFrame_.end(), false);
}

void SpectralEngine::releaseResources()
{
    stft_.setSpectrumCallback (nullptr, nullptr);
    stft_.releaseResources();
    prepared_ = false;
}

void SpectralEngine::setFrozen (bool shouldFreeze) noexcept
{
    for (auto& hist : histories_)
        hist->setFrozen (shouldFreeze);
}

void SpectralEngine::clearHistory()
{
    for (auto& hist : histories_)
        hist->clear();

    for (auto& prev : previousMagnitudes_)
        std::fill (prev.begin(), prev.end(), 0.0f);

    std::fill (hasPreviousFrame_.begin(), hasPreviousFrame_.end(), false);
    std::fill (frameCounters_.begin(), frameCounters_.end(), 0);
}

void SpectralEngine::setActiveMemoryLengthSeconds (float seconds) noexcept
{
    for (auto& hist : histories_)
        hist->setActiveMemoryLengthSeconds (seconds);
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

float SpectralEngine::getHistoryFillAmount (int channel) const noexcept
{
    const auto& hist = getHistory (channel);
    const int activeWindow = [&]
    {
        // Mirror getActiveFrameCount window size at full capacity.
        const double framesPerSecond = sampleRate_ / static_cast<double> (stft_.getHopSize());
        return std::max (1, static_cast<int> (std::ceil (framesPerSecond * hist.getActiveMemoryLengthSeconds())));
    }();

    return juce::jlimit (0.0f, 1.0f,
                         static_cast<float> (hist.getAvailableFrameCount())
                             / static_cast<float> (activeWindow));
}

void SpectralEngine::process (juce::AudioBuffer<float>& buffer,
                              const ModeParams& params,
                              SpectralMode mode)
{
    if (! prepared_)
        return;

    pendingParams_ = params;
    pendingMode_ = mode;

    setFrozen (params.freeze);

    // Phase 3: STFT + history capture via spectrumCallback. Spectrum unchanged.
    stft_.process (buffer);

    juce::ignoreUnused (pendingMode_);
}

void SpectralEngine::spectrumCallback (void* userData,
                                       float* interleavedFftData,
                                       int fftSize,
                                       int channelIndex) noexcept
{
    auto* self = static_cast<SpectralEngine*> (userData);
    self->onSpectrum (interleavedFftData, fftSize, channelIndex);
}

void SpectralEngine::onSpectrum (float* interleavedFftData, int fftSize, int channelIndex) noexcept
{
    if (channelIndex < 0 || channelIndex >= static_cast<int> (histories_.size()))
        return;

    auto& hist = *histories_[static_cast<std::size_t> (channelIndex)];

    // Freeze: keep processing audio, but stop writing new memory.
    SpectralFrame* slot = hist.beginWriteFrame();
    if (slot == nullptr)
        return;

    slot->fillFromInterleavedFFT (interleavedFftData, fftSize);
    slot->frameIndex = frameCounters_[static_cast<std::size_t> (channelIndex)]++;
    slot->computeRmsAndCentroid (sampleRate_, fftSize);

    // Spectral flux vs previous frame → transientStrength in [0,1].
    auto& prev = previousMagnitudes_[static_cast<std::size_t> (channelIndex)];
    const bool hadPrev = hasPreviousFrame_[static_cast<std::size_t> (channelIndex)];
    double flux = 0.0;
    double denom = 0.0;

    const int numBins = static_cast<int> (slot->magnitudes.size());
    for (int k = 0; k < numBins; ++k)
    {
        const float mag = slot->magnitudes[static_cast<std::size_t> (k)];
        const float previous = hadPrev ? prev[static_cast<std::size_t> (k)] : mag;
        const float diff = mag - previous;
        if (diff > 0.0f)
            flux += static_cast<double> (diff);
        denom += static_cast<double> (mag);
        prev[static_cast<std::size_t> (k)] = mag;
    }

    hasPreviousFrame_[static_cast<std::size_t> (channelIndex)] = true;

    const float raw = (denom > 1.0e-9) ? static_cast<float> (flux / denom) : 0.0f;
    slot->transientStrength = juce::jlimit (0.0f, 1.0f, raw * 4.0f); // gentle scale

    hist.commitWriteFrame();

    // Phase 3: leave interleaved FFT data untouched for transparent reconstruction.
    juce::ignoreUnused (pendingParams_);
}

} // namespace afterimage
