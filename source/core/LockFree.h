#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace digga
{

/** Single-producer / single-consumer queue of trivially copyable values,
    lock-free and allocation-free. */
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 <= 0)
            return false;
        items[(size_t) scope.startIndex1] = item;
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto scope = fifo.read (1);
        if (scope.blockSize1 <= 0)
            return false;
        item = items[(size_t) scope.startIndex1];
        return true;
    }

    int getFreeSpace() const noexcept { return fifo.getFreeSpace(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> items {};
};

/** Hands an object from a non-audio thread to the audio thread without locks.
    The audio thread adopts the newest object in acquire(); the one it replaces
    goes to a trash queue that collectGarbage() empties on the message thread. */
template <typename T>
class LockFreeHandoff
{
public:
    ~LockFreeHandoff()
    {
        collectGarbage();
        delete pending.exchange (nullptr);
        delete active;
    }

    /** Any non-audio thread. */
    void publish (std::unique_ptr<T> object)
    {
        delete pending.exchange (object.release());
    }

    /** Message thread. */
    void collectGarbage()
    {
        T* item = nullptr;
        while (trash.pop (item))
            delete item;
    }

    /** Audio thread: returns true if a new object was adopted. */
    bool acquire() noexcept
    {
        if (pending.load() == nullptr || trash.getFreeSpace() == 0)
            return false;

        auto* incoming = pending.exchange (nullptr);
        if (incoming == nullptr)
            return false;

        if (active != nullptr)
            trash.push (active);

        active = incoming;
        return true;
    }

    /** Audio thread only. */
    T* get() const noexcept { return active; }

private:
    std::atomic<T*> pending { nullptr };
    T* active = nullptr;
    SpscQueue<T*, 32> trash;
};

} // namespace digga
