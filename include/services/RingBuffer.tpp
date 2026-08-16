#pragma once //works fine on linux?
#include <optional>

#include "domain/Frame.h"
#include "services/RingBuffer.h"

template <typename T>
RingBuffer<T>::RingBuffer(int size) {
     m_buffer = new T[size]{};
     m_size = size;
}

template <typename T>
RingBuffer<T>::RingBuffer(RingBuffer&& other) noexcept {
     m_buffer = other.m_buffer;
     other.m_buffer = nullptr;
     m_size = other.m_size;
     m_head = other.m_head;
     m_tail = other.m_tail;
     m_occupancy = other.m_occupancy;
 }

template <typename T>
RingBuffer<T>::~RingBuffer() {
    delete[] m_buffer;
}
// //for tests
// template <typename T>
// void RingBuffer<T>::show() {
//     std::cout << "occupacy: " << m_occupancy << std::endl;
//     // if (std::is_same<T, Frame>::value) {
//     //     // std::cout << "1 elem data: " << m_buffer[0].data[0] << std::endl;
//     //     // std::cout << "1 elem size: " << m_buffer[0].size << std::endl;
//     // }
//     // this->
// }
/// overwrite slot once buffer is full. Defined by policy
template <typename T>
void RingBuffer<T>::put(T item) {
    std::lock_guard<std::mutex> lk(m_locker);
     if (std::is_same<T, Frame>::value) {
         std::cout << "PUT: Size: " << item.data.size() << std::endl;
     }

    m_buffer[m_head] = item;

    m_head = (m_head + 1) % m_size;
    if (!is_full()) {
        m_occupancy++;
    }
    else {
        m_tail = (m_tail + 1) % m_size;
    }
}
// //for tests only
// template <typename T>
// void RingBuffer<T>::show() {
//     std::cout << "Show: " << this->m_occupancy << std::endl;
//
//     for (int i = 0; i < this->m_occupancy; i++) {
//         for (auto raw_frame : m_buffer[i].data) {
//             std::cout << raw_frame;
//         }
//         std::cout << std::endl;
//     }
// }

/// if empty - return last nullopt
template <typename T>
std::optional<T> RingBuffer<T>::get() {
    std::lock_guard<std::mutex> lk(m_locker);

    if (is_empty()) {
        return std::nullopt;
    }

    const T item = m_buffer[m_tail];

    m_tail = (m_tail + 1) % m_size;
    m_occupancy--;
    return item;
}

template <typename T>
bool RingBuffer<T>::is_full() const {
    return m_occupancy == m_size;
}

template <typename T>
bool RingBuffer<T>::is_empty() const {
    return m_occupancy == 0;
}
