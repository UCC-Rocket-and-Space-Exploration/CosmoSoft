#include "gateway/comms/Windows/SerialCommsWindows.h"

#include "gateway/comms/detail/SerialPortScannerUtils.h"

#include <limits>
#include <windows.h>

namespace {

constexpr DWORD kReadWaitMs = 100;

class ScopedHandle {
public:
    explicit ScopedHandle(const HANDLE handle) : m_handle(handle) {}
    ~ScopedHandle() {
        if (m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }

    ScopedHandle(const ScopedHandle &) = delete;
    ScopedHandle &operator=(const ScopedHandle &) = delete;

    [[nodiscard]] HANDLE get() const {
        return m_handle;
    }

    [[nodiscard]] HANDLE release() {
        const HANDLE handle = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return handle;
    }

private:
    HANDLE m_handle;
};

[[nodiscard]] DWORD boundedTransferSize(const std::size_t size) {
    const DWORD maximum = (std::numeric_limits<DWORD>::max)();
    return size > static_cast<std::size_t>(maximum) ? maximum : static_cast<DWORD>(size);
}

} // namespace

struct SerialCommsWindows::HandleState {
    explicit HandleState(const HANDLE value) : handle(value) {}
    ~HandleState() {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }

    HandleState(const HandleState &) = delete;
    HandleState &operator=(const HandleState &) = delete;

    HANDLE handle;
};

SerialCommsWindows::SerialCommsWindows(const std::string &device, const int baud)
    : m_device(device), m_baud(baud) {}

SerialCommsWindows::SerialCommsWindows(std::string &device, const int baud)
    : SerialCommsWindows(static_cast<const std::string &>(device), baud) {}

SerialCommsWindows::~SerialCommsWindows() {
    close();
}

bool SerialCommsWindows::open() {
    std::scoped_lock lock(m_handleMutex);

    if (m_handle) {
        std::shared_ptr<HandleState> oldHandle = std::move(m_handle);
        CancelIoEx(oldHandle->handle, nullptr);
        oldHandle.reset();
    }

    if (m_baud <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    const std::string devicePath =
        cosmo::serial::detail::normalize_windows_port_path(m_device);
    ScopedHandle handle(CreateFileA(
        devicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) {
        return false;
    }

    DCB state{};
    state.DCBlength = sizeof(state);
    if (!GetCommState(handle.get(), &state)) {
        return false;
    }
    state.BaudRate = static_cast<DWORD>(m_baud);
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    state.fBinary = TRUE;
    state.fParity = FALSE;
    state.fOutxCtsFlow = FALSE;
    state.fOutxDsrFlow = FALSE;
    state.fDtrControl = DTR_CONTROL_ENABLE;
    state.fDsrSensitivity = FALSE;
    state.fTXContinueOnXoff = TRUE;
    state.fOutX = FALSE;
    state.fInX = FALSE;
    state.fErrorChar = FALSE;
    state.fNull = FALSE;
    state.fRtsControl = RTS_CONTROL_ENABLE;
    state.fAbortOnError = FALSE;
    if (!SetCommState(handle.get(), &state)) {
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 20;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutConstant = 1000;
    if (!SetCommTimeouts(handle.get(), &timeouts)) {
        return false;
    }

    PurgeComm(
        handle.get(),
        PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    m_handle = std::make_shared<HandleState>(handle.get());
    (void) handle.release();
    return true;
}

void SerialCommsWindows::close() {
    std::shared_ptr<HandleState> handle;
    {
        std::scoped_lock lock(m_handleMutex);
        handle = std::move(m_handle);
    }
    if (handle) {
        CancelIoEx(handle->handle, nullptr);
    }
}

bool SerialCommsWindows::isOpen() const {
    std::scoped_lock lock(m_handleMutex);
    return static_cast<bool>(m_handle);
}

ssize_t SerialCommsWindows::write(const uint8_t *data, const size_t size) {
    if (data == nullptr && size > 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (event.get() == nullptr) {
        return -1;
    }

    OVERLAPPED operation{};
    operation.hEvent = event.get();
    std::shared_ptr<HandleState> handle;
    DWORD bytesWritten = 0;
    DWORD error = ERROR_SUCCESS;
    {
        std::scoped_lock lock(m_handleMutex);
        handle = m_handle;
        if (!handle) {
            SetLastError(ERROR_INVALID_HANDLE);
            return -1;
        }
        if (WriteFile(
                handle->handle,
                data,
                boundedTransferSize(size),
                &bytesWritten,
                &operation)) {
            return static_cast<ssize_t>(bytesWritten);
        }
        error = GetLastError();
    }
    if (error != ERROR_IO_PENDING) {
        SetLastError(error);
        return -1;
    }

    if (!GetOverlappedResult(handle->handle, &operation, &bytesWritten, TRUE)) {
        return -1;
    }
    return static_cast<ssize_t>(bytesWritten);
}

ssize_t SerialCommsWindows::read(uint8_t *buffer, const size_t maxSize) {
    if (buffer == nullptr && maxSize > 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    if (maxSize == 0) {
        return 0;
    }

    ScopedHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (event.get() == nullptr) {
        return -1;
    }

    OVERLAPPED operation{};
    operation.hEvent = event.get();
    std::shared_ptr<HandleState> handle;
    DWORD bytesRead = 0;
    DWORD error = ERROR_SUCCESS;
    {
        std::scoped_lock lock(m_handleMutex);
        handle = m_handle;
        if (!handle) {
            SetLastError(ERROR_INVALID_HANDLE);
            return -1;
        }
        if (ReadFile(
                handle->handle,
                buffer,
                boundedTransferSize(maxSize),
                &bytesRead,
                &operation)) {
            return static_cast<ssize_t>(bytesRead);
        }
        error = GetLastError();
    }
    if (error != ERROR_IO_PENDING) {
        SetLastError(error);
        return -1;
    }

    const DWORD waitResult = WaitForSingleObject(event.get(), kReadWaitMs);
    if (waitResult == WAIT_TIMEOUT) {
        CancelIoEx(handle->handle, &operation);
        // Keep the buffer, OVERLAPPED structure, event, and file handle alive
        // until the driver acknowledges cancellation or completes normally.
        if (GetOverlappedResult(handle->handle, &operation, &bytesRead, TRUE)) {
            return static_cast<ssize_t>(bytesRead);
        }
        if (GetLastError() == ERROR_OPERATION_ABORTED) {
            return 0;
        }
        return -1;
    }
    if (waitResult != WAIT_OBJECT_0) {
        const DWORD waitError = GetLastError();
        CancelIoEx(handle->handle, &operation);
        GetOverlappedResult(handle->handle, &operation, &bytesRead, TRUE);
        SetLastError(waitError);
        return -1;
    }

    if (!GetOverlappedResult(handle->handle, &operation, &bytesRead, FALSE)) {
        return -1;
    }
    return static_cast<ssize_t>(bytesRead);
}

std::string SerialCommsWindows::getDeviceName() const {
    return m_device;
}
