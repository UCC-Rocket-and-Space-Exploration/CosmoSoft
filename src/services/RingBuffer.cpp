#include "services/RingBuffer.h"


#include <algorithm>
#include <cstdlib>

RingBuffer::RingBuffer(uint8_t *buffer, std::size_t capacity)
    : m_buffer(buffer)
    , m_capacity(capacity)
    , m_size(0)
    , m_head(0)
    , m_tail(0) {}

RingBuffer::~RingBuffer() = default;

RingBuffer::RingBuffer(RingBuffer &&other) noexcept
    : m_buffer(other.m_buffer)
    , m_capacity(other.m_capacity)
    , m_size(other.m_size)
    , m_head(other.m_head)
    , m_tail(other.m_tail) {
    other.m_buffer = nullptr;
    other.m_capacity = 0;
    other.m_size = 0;
    other.m_head = 0;
    other.m_tail = 0;
}

RingBuffer &RingBuffer::operator=(RingBuffer &&other) noexcept {
    if (this != &other) {
        m_buffer = other.m_buffer;
        m_capacity = other.m_capacity;
        m_size = other.m_size;
        m_head = other.m_head;
        m_tail = other.m_tail;
        other.m_buffer = nullptr;
        other.m_capacity = 0;
        other.m_size = 0;
        other.m_head = 0;
        other.m_tail = 0;
    }
    return *this;
}

void RingBuffer::free() {
    std::free(m_buffer);
    m_buffer = nullptr;
    m_capacity = 0;
    reset();
}

void RingBuffer::reset() {
    m_head = 0;
    m_tail = 0;
    m_size = 0;
}

std::size_t RingBuffer::put(uint8_t *data, std::size_t size) {
    if (!m_buffer || m_capacity == 0) {
        return 0;
    }
    const std::size_t avail = m_capacity - m_size;
    const std::size_t n = std::min(size, avail);
    for (std::size_t i = 0; i < n; ++i) {
        m_buffer[m_tail] = data[i];
        m_tail = (m_tail + 1) % m_capacity;
    }
    m_size += n;
    return n;
}

std::size_t RingBuffer::get(uint8_t *buffer, std::size_t size) {
    if (!m_buffer || m_capacity == 0) {
        return 0;
    }
    const std::size_t n = std::min(size, m_size);
    for (std::size_t i = 0; i < n; ++i) {
        buffer[i] = m_buffer[m_head];
        m_head = (m_head + 1) % m_capacity;
    }
    m_size -= n;
    return n;
}

bool RingBuffer::isFull() { return m_capacity > 0 && m_size == m_capacity; }

bool RingBuffer::isEmpty() { return m_size == 0; }

std::size_t RingBuffer::getSize() { return m_size; }

std::size_t RingBuffer::getCapacity() { return m_capacity; }
