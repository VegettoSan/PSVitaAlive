#pragma once
#include <string>
namespace psvitaalive {
// Saved .zip.001 files contain ordered raw HTTP ranges, not ZIP subarchives.
inline bool savedZipIsFirstPart(const std::string& path) {
    return path.size() >= 8 && path.compare(path.size()-4, 4, ".001") == 0;
}
inline std::string savedZipPartPath(const std::string& first, unsigned index) {
    if (!savedZipIsFirstPart(first) || index >= 4) return {};
    return first.substr(0, first.size()-4) + ".00" + std::to_string(index+1);
}
} // namespace psvitaalive
