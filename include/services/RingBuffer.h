#ifndef COSMO_SOFT_RINGBUFFER_H
#define COSMO_SOFT_RINGBUFFER_H
#include <cstdint>

//TODO use atomic bool to make thread-safe, or consider using mutexes for better control over concurrent access;
// also consider making it lock-free (by reserving single space for empty
// but need to be careful with memory management and edge cases (full vs empty)
class RingBuffer {
public:
    RingBuffer(uint8_t* buffer, std::size_t capacity);
    ~RingBuffer();

    // Prevent copying (dangerous with raw pointer management)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Allow move semantics if desired
    RingBuffer(RingBuffer&& other) noexcept;
    RingBuffer& operator=(RingBuffer&& other) noexcept;

    void free(); // Free the buffer memory if owned by this class; otherwise, just reset pointers and state
    void reset(); // Reset the buffer state without freeing memory, useful if the buffer is managed externally

    std::size_t put(uint8_t* data, std::size_t size); // writes data to the buffer, returns the number of bytes actually written (may be less than size if buffer is full)
    std::size_t get(uint8_t* buffer, std::size_t size); // reads data from the buffer into the provided buffer, returns the number of bytes actually read (may be less than size if buffer is empty)

    bool isFull();
    bool isEmpty();

    std::size_t getSize();
    std::size_t getCapacity();

private:
    uint8_t *m_buffer;
    std::size_t m_capacity;
    std::size_t m_size;
    std::size_t m_head;
    std::size_t m_tail;
};

#endif //COSMO_SOFT_RINGBUFFER_H