#pragma once
#include <cstddef>
#include <vector>
#include <utility>

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

template<class T, std::size_t N>
std::size_t retainForSelection(std::vector<T> (&items)[N], bool (&valid)[N],
                               std::size_t next, std::size_t previous,
                               std::vector<T>* outgoing, bool updating) {
    std::size_t released = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (i != next && (updating || i != previous))
            released += releaseEntry(items, valid, i);
    }
    // During normal browsing, move the outgoing visible payload back into
    // the cache. This also retains a freshly updated catalog on the next
    // user-driven switch without allocating a duplicate after the update.
    if (outgoing) {
        if (!updating && previous < N && previous != next && !outgoing->empty()) {
            items[previous] = std::move(*outgoing);
            valid[previous] = true;
        }
        std::vector<T>().swap(*outgoing);
    }
    return released;
}
}} // namespace psvitaalive::catalog_memory
