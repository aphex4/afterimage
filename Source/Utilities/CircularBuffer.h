#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

namespace afterimage
{

/**
    Simple preallocated circular buffer of T.

    Sized once (typically in prepareToPlay). push/pop never allocate.
    Not thread-safe — intended for single-producer single-consumer or
    audio-thread-only use unless externally synchronized.
*/
template <typename T>
class CircularBuffer
{
public:
    CircularBuffer() = default;

    explicit CircularBuffer (std::size_t capacity)
    {
        setCapacity (capacity);
    }

    void setCapacity (std::size_t capacity)
    {
        data_.assign (capacity, T {});
        writeIndex_ = 0;
        size_ = 0;
    }

    void clear()
    {
        writeIndex_ = 0;
        size_ = 0;

        for (auto& v : data_)
            v = T {};
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == data_.size() && ! data_.empty(); }

    void push (const T& value)
    {
        assert (! data_.empty());
        if (data_.empty())
            return;

        data_[writeIndex_] = value;
        writeIndex_ = (writeIndex_ + 1) % data_.size();

        if (size_ < data_.size())
            ++size_;
    }

    /** Age 0 = newest. Age size()-1 = oldest currently stored. */
    [[nodiscard]] const T& getByAge (std::size_t age) const
    {
        assert (! empty());
        assert (age < size_);

        // Newest sits at writeIndex_ - 1 (modulo).
        const std::size_t newest = (writeIndex_ + data_.size() - 1) % data_.size();
        const std::size_t index  = (newest + data_.size() - age) % data_.size();
        return data_[index];
    }

    [[nodiscard]] T& getByAge (std::size_t age)
    {
        return const_cast<T&> (static_cast<const CircularBuffer*> (this)->getByAge (age));
    }

private:
    std::vector<T> data_;
    std::size_t writeIndex_ = 0;
    std::size_t size_ = 0;
};

} // namespace afterimage
