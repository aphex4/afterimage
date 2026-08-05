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

void SpectralHistoryBuffer::pushFrame (const SpectralFrame& frame)
{
    if (frozen_ || capacity_ <= 0)
        return;

    frames_[static_cast<std::size_t> (writeIndex_)] = frame;
    writeIndex_ = (writeIndex_ + 1) % capacity_;

    if (availableFrames_ < capacity_)
        ++availableFrames_;
}

const SpectralFrame& SpectralHistoryBuffer::getFrameByAgeFrames (int age) const
{
    if (availableFrames_ <= 0 || capacity_ <= 0)
        return emptyFrame_;

    const int clampedAge = juce::jlimit (0, availableFrames_ - 1, age);
    const int newest = (writeIndex_ + capacity_ - 1) % capacity_;
    const int index = (newest + capacity_ - clampedAge) % capacity_;
    return frames_[static_cast<std::size_t> (index)];
}

const SpectralFrame& SpectralHistoryBuffer::getFrameByNormalizedAge (float age01) const
{
    const int active = getActiveFrameCount();
    if (active <= 0)
        return emptyFrame_;

    const float clamped = juce::jlimit (0.0f, 1.0f, age01);
    const int age = static_cast<int> (std::round (clamped * static_cast<float> (active - 1)));
    return getFrameByAgeFrames (age);
}

} // namespace afterimage
