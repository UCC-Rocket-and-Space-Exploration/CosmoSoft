#ifndef COSMO_SOFT_BLOCKINGQUEUE_H
#define COSMO_SOFT_BLOCKINGQUEUE_H

#include <condition_variable>
#include <chrono>
#include <deque>
#include <mutex>

// e.g.: BlockingQueue<std::vector<uint8_t>> queue(128);
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(const size_t capacity) : m_capacity(capacity) {}

    // drop-oldest
    void push(T item) {
        std::unique_lock lk(m_mtx);
        if (m_q.size() >= m_capacity) {
            m_q.pop_front();
            ++m_dropped;
        }
        m_q.push_back(std::move(item));
        //lk.unlock(); //Might cause spurious wakeups, let unique_lock handle it
        m_cv.notify_one();
    }

    bool pop_for(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock lk(m_mtx);
        if (!m_cv.wait_for(lk, timeout, [&]{ return !m_q.empty(); }))
            return false;
        out = std::move(m_q.front());
        m_q.pop_front();
        return true;
    }

    size_t dropped() const {
        std::scoped_lock lk(m_mtx);
        return m_dropped;
    }

private:
    size_t m_capacity;
    mutable std::mutex m_mtx;
    std::condition_variable m_cv;
    std::deque<T> m_q;
    size_t m_dropped = 0;
};

#endif //COSMO_SOFT_BLOCKINGQUEUE_H