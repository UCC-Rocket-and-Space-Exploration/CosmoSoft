#include <catch2/catch_test_macros.hpp>

#include "gateway/comms/IComms.h"
#include "gateway/comms/SerialWorker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

class BlockingComms final : public IComms {
public:
    bool open() override {
        std::scoped_lock lock(m_mutex);
        m_open = true;
        m_readStarted = false;
        return true;
    }

    void close() override {
        {
            std::scoped_lock lock(m_mutex);
            m_open = false;
        }
        m_stateChanged.notify_all();
    }

    [[nodiscard]] bool isOpen() const override {
        std::scoped_lock lock(m_mutex);
        return m_open;
    }

    ssize_t write(const uint8_t *, const size_t size) override {
        std::scoped_lock lock(m_mutex);
        return m_open ? static_cast<ssize_t>(size) : -1;
    }

    ssize_t read(uint8_t *, size_t) override {
        std::unique_lock lock(m_mutex);
        if (!m_open) {
            return -1;
        }

        m_readStarted = true;
        m_stateChanged.notify_all();
        m_stateChanged.wait(lock, [this] { return !m_open; });
        return -1;
    }

    [[nodiscard]] std::string getDeviceName() const override {
        return "blocking-test-port";
    }

    [[nodiscard]] bool waitUntilReadStarts(const std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        return m_stateChanged.wait_for(lock, timeout, [this] { return m_readStarted; });
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_stateChanged;
    bool m_open = false;
    bool m_readStarted = false;
};

class FaultingComms final : public IComms {
public:
    enum class Fault {
        OversizedRead,
        Exception,
        NegativeRead,
    };

    explicit FaultingComms(const Fault fault) : m_fault(fault) {}

    bool open() override {
        m_open.store(true);
        return true;
    }

    void close() override {
        m_open.store(false);
    }

    [[nodiscard]] bool isOpen() const override {
        return m_open.load();
    }

    ssize_t write(const uint8_t *, const size_t size) override {
        return m_open.load() ? static_cast<ssize_t>(size) : -1;
    }

    ssize_t read(uint8_t *, const size_t maxSize) override {
        m_readCount.fetch_add(1);
        if (m_fault == Fault::Exception) {
            throw std::runtime_error("test backend failure");
        }
        if (m_fault == Fault::NegativeRead) {
            return -1;
        }
        return static_cast<ssize_t>(maxSize + 1);
    }

    [[nodiscard]] std::string getDeviceName() const override {
        return "faulting-test-port";
    }

    [[nodiscard]] int readCount() const noexcept {
        return m_readCount.load();
    }

private:
    Fault m_fault;
    std::atomic<bool> m_open = false;
    std::atomic<int> m_readCount = 0;
};

class ThrowingCloseComms final : public IComms {
public:
    bool open() override {
        m_open.store(true);
        return true;
    }

    void close() override {
        m_open.store(false);
        throw std::runtime_error("test close failure");
    }

    [[nodiscard]] bool isOpen() const override { return m_open.load(); }

    ssize_t write(const uint8_t *, const size_t size) override {
        return m_open.load() ? static_cast<ssize_t>(size) : -1;
    }

    ssize_t read(uint8_t *, size_t) override { return -1; }

    [[nodiscard]] std::string getDeviceName() const override {
        return "throwing-close-test-port";
    }

private:
    std::atomic_bool m_open = false;
};

[[nodiscard]] std::string runFaultingWorker(const FaultingComms::Fault fault) {
    FaultingComms comms(fault);
    std::mutex errorMutex;
    std::condition_variable errorReceived;
    std::string errorMessage;
    REQUIRE(comms.open());

    SerialWorker worker(
        &comms,
        [](std::vector<uint8_t>) {},
        [&](const std::string &message) {
            {
                std::scoped_lock lock(errorMutex);
                errorMessage = message;
            }
            errorReceived.notify_one();
        });
    REQUIRE(worker.start());

    {
        std::unique_lock lock(errorMutex);
        REQUIRE(errorReceived.wait_for(lock, 500ms, [&] { return !errorMessage.empty(); }));
    }
    worker.stop();
    return errorMessage;
}

} // namespace

TEST_CASE("SerialWorker stop cancels a waiting read before joining", "[comms][lifecycle]") {
    BlockingComms comms;
    std::atomic<int> errorCount = 0;
    REQUIRE(comms.open());

    SerialWorker worker(
        &comms,
        [](std::vector<uint8_t>) {},
        [&errorCount](const std::string &) { errorCount.fetch_add(1); });
    REQUIRE(worker.start());
    REQUIRE(comms.waitUntilReadStarts(500ms));

    const auto startedAt = std::chrono::steady_clock::now();
    worker.stop();
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;

    REQUIRE(elapsed < 250ms);
    REQUIRE_FALSE(comms.isOpen());
    REQUIRE(errorCount.load() == 0);
}

TEST_CASE("SerialWorker rejects an oversized backend read count", "[comms][lifecycle]") {
    REQUIRE(runFaultingWorker(FaultingComms::Fault::OversizedRead)
            == "Serial backend returned more bytes than requested");
}

TEST_CASE("SerialWorker contains standard exceptions thrown by a backend", "[comms][lifecycle]") {
    REQUIRE(runFaultingWorker(FaultingComms::Fault::Exception)
            == "Serial read threw an exception: test backend failure");
}

TEST_CASE("SerialWorker reports a persistent read failure once and can restart",
          "[comms][lifecycle]") {
    FaultingComms comms(FaultingComms::Fault::NegativeRead);
    std::mutex errorMutex;
    std::condition_variable errorReceived;
    int errorCount = 0;
    SerialWorker worker(
        &comms,
        [](std::vector<uint8_t>) {},
        [&](const std::string &) {
            {
                std::scoped_lock lock(errorMutex);
                ++errorCount;
            }
            errorReceived.notify_one();
        });

    for (int run = 1; run <= 2; ++run) {
        REQUIRE(comms.open());
        REQUIRE(worker.start());
        {
            std::unique_lock lock(errorMutex);
            REQUIRE(errorReceived.wait_for(
                lock,
                500ms,
                [&] { return errorCount == run; }));
        }
        const auto deadline = std::chrono::steady_clock::now() + 500ms;
        while (worker.isRunning() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE_FALSE(worker.isRunning());
        REQUIRE_FALSE(comms.isOpen());
        REQUIRE(comms.readCount() == run);
    }

    worker.stop();
}

TEST_CASE("SerialWorker stop joins when a backend close throws", "[comms][lifecycle]") {
    ThrowingCloseComms comms;
    REQUIRE(comms.open());
    {
        SerialWorker worker(
            &comms,
            [](std::vector<uint8_t>) {},
            [](const std::string &) {});
        REQUIRE(worker.start());
        worker.stop();
        REQUIRE_FALSE(worker.isRunning());
    }
    REQUIRE_FALSE(comms.isOpen());
}
