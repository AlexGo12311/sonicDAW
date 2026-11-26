#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace sonic::engine {

// Lock-free SPSC: один писатель (message thread), один читатель (audio thread).
// Ни аллокаций, ни блокировок, ни ожиданий.
template <typename T, std::size_t Capacity>
class CommandQueue
{
    static_assert (std::is_trivially_copyable<T>::value, "T must be POD");
    static_assert (Capacity >= 2, "Capacity too small");

public:
    bool push (const T& item) noexcept
    {
        const auto w = write_.load (std::memory_order_relaxed);
        const auto next = increment (w);

        if (next == read_.load (std::memory_order_acquire))
            return false;                       // переполнение: команду丢掉, UI узнает

        slots_[w] = item;
        write_.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto r = read_.load (std::memory_order_relaxed);

        if (r == write_.load (std::memory_order_acquire))
            return false;                       // пусто

        item = slots_[r];
        read_.store (increment (r), std::memory_order_release);
        return true;
    }

private:
    static std::size_t increment (std::size_t i) noexcept { return (i + 1) % Capacity; }

    std::array<T, Capacity> slots_ {};
    std::atomic<std::size_t> write_ { 0 };
    std::atomic<std::size_t> read_  { 0 };
};

} // namespace sonic::engine