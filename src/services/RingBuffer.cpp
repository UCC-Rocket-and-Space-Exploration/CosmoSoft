#include "../include/services/RingBuffer.h"

#include <cstdlib>

RingBuffer::RingBuffer(uint8_t *buffer, const std::size_t capacity) {
    //static_assert(buffer && capacity);

    m_buffer = buffer;
    m_size = capacity;
    reset();

    //static_assert(isEmpty());

}

RingBuffer::~RingBuffer() {

}

RingBuffer::RingBuffer(RingBuffer &&other) noexcept {
}

RingBuffer & RingBuffer::operator=(RingBuffer &&other) noexcept {

}

void RingBuffer::free() {
    ::free(m_buffer);
}

void RingBuffer::reset() {
    m_head = 0;
    m_tail = 0;

}

std::size_t RingBuffer::put(uint8_t *data, std::size_t size) {
}

std::size_t RingBuffer::get(uint8_t *buffer, std::size_t size) {
}

bool RingBuffer::isFull() {
}

bool RingBuffer::isEmpty() {
}

std::size_t RingBuffer::getSize() {
}

std::size_t RingBuffer::getCapacity() {
}
