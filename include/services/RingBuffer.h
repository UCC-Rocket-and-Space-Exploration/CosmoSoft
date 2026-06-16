#ifndef COSMO_SOFT_RINGBUFFER_H
#define COSMO_SOFT_RINGBUFFER_H
#include <cstdint>

//TODO use atomic bool to make thread-safe, or consider using mutexes for better control over concurrent access;
// also consider making it lock-free (by reserving single space for empty
// but need to be careful with memory management and edge cases (full vs empty)
#include <iostream>
#include <mutex>
#include <optional>
#include "IBuffer.h"

template <typename T>
class RingBuffer : public IBuffer{
public:
    explicit RingBuffer(int size);
    explicit RingBuffer(const RingBuffer& other) = delete;
    RingBuffer(RingBuffer&& other) noexcept;

    ~RingBuffer();
    void put(T item) override;
    std::optional<T> get() override;

private:
    [[nodiscard]] bool is_empty() const;
    [[nodiscard]] bool is_full() const;

    std::mutex m_locker;
    T* m_buffer;
    size_t m_size;
    size_t m_occupancy = 0;
    size_t m_head = 0;
    size_t m_tail = 0;
};
#include "RingBuffer.tpp"

#endif //COSMO_SOFT_RINGBUFFER_H