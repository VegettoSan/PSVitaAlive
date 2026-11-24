#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace psvitaalive::catalog_parse {

// Preserve the original Value truthiness and V1-before-V2 precedence.
template<class Object>
const auto& field(const Object& object, const char* legacyKey,
                  const char* compactKey = nullptr) {
    const auto& legacy = object[legacyKey];
    if (legacy || !compactKey) return legacy;
    return object[compactKey];
}

// Limit accepted, nonempty strings, not raw array positions.
template<class Value>
void appendStrings(const Value& value, std::vector<std::string>& out,
                   std::size_t limit = std::numeric_limits<std::size_t>::max()) {
    if (!value) return;
    const auto& array = value.getArray();
    const auto count = array.size();
    for (decltype(array.size()) i = 0; i < count && out.size() < limit; ++i) {
        const auto& entry = value[i];
        if (entry) {
            std::string text = entry.getString().c_str();
            if (!text.empty()) out.push_back(std::move(text));
        }
    }
}

// Bound eager reservation even for large or malformed remote arrays.
inline constexpr std::size_t initialItemReserve(std::size_t count) {
    return count < 512 ? count : 512;
}

// Same historical FNV seed and byte order as the old concatenated key.
inline std::uint64_t imageResourceHash(std::string_view prefix,
                                      std::string_view owner,
                                      std::string_view role) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append = [&hash](std::string_view part) {
        for (unsigned char c : part) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
    };
    append(prefix);
    append("|");
    append(owner);
    append("|");
    append(role);
    return hash;
}

} // namespace psvitaalive::catalog_parse
