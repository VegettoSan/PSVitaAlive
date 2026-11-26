#pragma once
#include <cstddef>
#include <vector>

namespace psvitaalive { namespace catalog_memory {
// No borrowed item pointers may survive eviction. The caller owns the mutex
// and UI/queue payloads are separate value-owned vectors.
template<class T, std::size_t N>
std::size_t releaseEntry(std::vector<T> (&items)[N], bool (&valid)[N], std::size_t index) {
    if (index >= N) return 0;
    const std::size_t count = items[index].size();
    std::vector<T>().swap(items[index]); // release strings and vector capacity
    valid[index] = false;
    return count;
}
template<class T, std::size_t N>
std::size_t releaseExcept(std::vector<T> (&items)[N], bool (&valid)[N], std::size_t keep) {
    std::size_t released = 0;
    for (std::size_t i = 0; i < N; ++i)
        if (i != keep) released += releaseEntry(items, valid, i);
    return released;
}
}} // namespace psvitaalive::catalog_memory
