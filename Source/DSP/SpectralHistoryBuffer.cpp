#include "SpectralHistoryBuffer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace afterimage
{

void SpectralHistoryBuffer::prepare (double sampleRate, int hopSize, float maxMemorySeconds)
{
    sampleRate_ = sampleRate;
    hopSize_ = std::max (1, hopSize);

    const double framesPerSecond = sampleRate_ / static_cast<double> (hopSize_);
    capacity_ = std::max (1, static_cast<int> (std::ceil (framesPerSecond * static_cast<double> (maxMemorySeconds))));

    frames_.resize (static_cast<std::size_t> (capacity_));
    for (auto& f : frames_)
        f.prepare (constants::numBins);

    emptyFrame_.prepare (constants::numBins);
    reset();
}

void SpectralHistoryBuffer::reset()
{
    writeIndex_ = 0;
    availableFrames_ = 0;
    frozen_ = false;

    for (auto& f : frames_)
        f.clear();

    emptyFrame_.clear();
}

void SpectralHistoryBuffer::clear()
{
    writeIndex_ = 0;
    availableFrames_ = 0;

    for (auto& f : frames_)
        f.clear();
}

void SpectralHistoryBuffer::setActiveMemoryLengthSeconds (float seconds) noexcept
{
    activeMemorySeconds_ = juce::jlimit (constants::memoryLengthMinSec,
                                         constants::memoryLengthMaxSec,
                                         seconds);
}

int SpectralHistoryBuffer::getActiveFrameCount() const noexcept
{
    const double framesPerSecond = sampleRate_ / static_cast<double> (hopSize_);
    const int window = std::max (1, static_cast<int> (std::ceil (framesPerSecond * activeMemorySeconds_)));
    return std::min (availableFrames_, window);
}

void SpectralHistoryBuffer::pushFrame (const SpectralFrame& frame) noexcept
{
    SpectralFrame* slot = beginWriteFrame();
    if (slot == nullptr)
        return;

    slot->copyFrom (frame);
    commitWriteFrame();
}

SpectralFrame* SpectralHistoryBuffer::beginWriteFrame() noexcept
{
    if (frozen_ || capacity_ <= 0)
        return nullptr;

    return &frames_[static_cast<std::size_t> (writeIndex_)];
}

void SpectralHistoryBuffer::commitWriteFrame() noexcept
{
    if (frozen_ || capacity_ <= 0)
        return;

    writeIndex_ = (writeIndex_ + 1) % capacity_;

    if (availableFrames_ < capacity_)
        ++availableFrames_;
}

const SpectralFrame& SpectralHistoryBuffer::getFrameByAgeFrames (int age) const noexcept
{
    if (availableFrames_ <= 0 || capacity_ <= 0)
        return emptyFrame_;

    const int clampedAge = juce::jlimit (0, availableFrames_ - 1, age);
    const int newest = (writeIndex_ + capacity_ - 1) % capacity_;
    const int index = (newest + capacity_ - clampedAge) % capacity_;
    return frames_[static_cast<std::size_t> (index)];
}

const SpectralFrame& SpectralHistoryBuffer::getFrameByNormalizedAge (float age01) const noexcept
{
    const int active = getActiveFrameCount();
    if (active <= 0)
        return emptyFrame_;

    const float clamped = juce::jlimit (0.0f, 1.0f, age01);
    const int age = static_cast<int> (std::round (clamped * static_cast<float> (active - 1)));
    return getFrameByAgeFrames (age);
}

void SpectralHistoryBuffer::getInterpolatedMagnitudes (float age01,
                                                       float* destMagnitudes,
                                                       int numBins) const noexcept
{
    const int active = getActiveFrameCount();
    if (destMagnitudes == nullptr || numBins <= 0)
        return;

    if (active <= 0)
    {
        std::fill (destMagnitudes, destMagnitudes + numBins, 0.0f);
        return;
    }

    if (active == 1)
    {
        const auto& f = getFrameByAgeFrames (0);
        const int n = std::min (numBins, static_cast<int> (f.magnitudes.size()));
        for (int i = 0; i < n; ++i)
            destMagnitudes[i] = f.magnitudes[static_cast<std::size_t> (i)];
        for (int i = n; i < numBins; ++i)
            destMagnitudes[i] = 0.0f;
        return;
    }

    const float clamped = juce::jlimit (0.0f, 1.0f, age01);
    const float exactAge = clamped * static_cast<float> (active - 1);
    const int age0 = static_cast<int> (exactAge);
    const int age1 = std::min (age0 + 1, active - 1);
    const float frac = exactAge - static_cast<float> (age0);

    const auto& a = getFrameByAgeFrames (age0);
    const auto& b = getFrameByAgeFrames (age1);
    const int n = std::min (numBins, static_cast<int> (a.magnitudes.size()));

    for (int i = 0; i < n; ++i)
    {
        const float ma = a.magnitudes[static_cast<std::size_t> (i)];
        const float mb = b.magnitudes[static_cast<std::size_t> (i)];
        destMagnitudes[i] = ma + frac * (mb - ma);
    }

    for (int i = n; i < numBins; ++i)
        destMagnitudes[i] = 0.0f;
}

} // namespace afterimage
