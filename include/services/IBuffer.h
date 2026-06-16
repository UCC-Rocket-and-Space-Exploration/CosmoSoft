#ifndef COSMO_SOFT_IBUFFER_H
#define COSMO_SOFT_IBUFFER_H
class IBuffer {
    virtual void put(T item) = 0;
    virtual std::optional<T> get() = 0;
}
#endif //COSMO_SOFT_IBUFFER_H