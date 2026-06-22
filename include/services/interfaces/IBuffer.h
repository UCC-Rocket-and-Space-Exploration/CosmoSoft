#ifndef COSMO_SOFT_IBUFFER_H
#define COSMO_SOFT_IBUFFER_H
#include <optional>

template <typename T>
class IBuffer {
public:
    virtual ~IBuffer() = default;
    virtual void put(T item) = 0;
    virtual std::optional<T> get() = 0;
};
#endif //COSMO_SOFT_IBUFFER_H