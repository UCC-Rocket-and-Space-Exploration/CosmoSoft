#ifndef COSMO_SOFT_RINGBUFFER_H
#define COSMO_SOFT_RINGBUFFER_H
#include <iostream>
#include <mutex>
#include <optional>
#include "interfaces/IBuffer.h"

template <typename T>
class RingBuffer : public IBuffer<T>{
public:
    explicit RingBuffer(int size);
    explicit RingBuffer(const RingBuffer& other) = delete;
    RingBuffer(RingBuffer&& other) noexcept;

    ~RingBuffer() override;
    void put(T item) override;
    void show();
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
#include "services/RingBuffer.tpp"

#endif //COSMO_SOFT_RINGBUFFER_H