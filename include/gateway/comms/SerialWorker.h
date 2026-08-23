#ifndef COSMO_SOFT_SERIALWORKER_H
#define COSMO_SOFT_SERIALWORKER_H
#include <atomic>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// #include "gateway/comms/IComms.h"
#include "interfaces/IComms.h"

/**
 * @brief Reads byte chunks from an IComms connection on a background thread.
 *
 * The IComms object is non-owning and must outlive this worker. stop() closes
 * the connection to cancel a pending read before joining the worker thread.
 * Callbacks run on the worker thread and must not destroy this worker.
 */
class SerialWorker {
    using DataCallback = std::function<void(std::vector<uint8_t>)>;
    using ErrorCallback = std::function<void(const std::string&)>;

public:
    /**
     * @brief Construct a reader for an externally owned communications object.
     * @param comms Connection that remains alive until this worker is destroyed.
     * @param onData Callback for each non-empty byte chunk.
     * @param onError Callback for backend and callback failures.
     */
    explicit SerialWorker(IComms* comms, DataCallback onData, ErrorCallback onError)
        : m_connectedPort(comms),
          m_onData(std::move(onData)),
          m_onError(std::move(onError)) {}

    /** @brief Stop and join the reader before releasing worker state. */
    ~SerialWorker();

    /** @brief Start reading from an already-open connection. */
    bool start();

    /** @brief Return true while the reader thread is accepting work. */
    [[nodiscard]] bool isRunning() const noexcept;

    /** @brief Request cancellation, close the connection, and join the reader. */
    void stop();

private:
    void run(std::stop_token stopToken);

    IComms* m_connectedPort;
    DataCallback m_onData;
    ErrorCallback m_onError;
    std::jthread m_workerThread;
    std::atomic<bool> m_running{false};
    std::mutex m_lifecycleMutex;
};

#endif // COSMO_SOFT_SERIALWORKER_H
