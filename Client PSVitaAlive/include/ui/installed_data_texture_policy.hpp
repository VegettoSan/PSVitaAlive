#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace psvitaalive::ui {

// Only images actually decoded while the Game/Data Files dialog was open
// belong to this dialog's cleanup scope. Icons already resident in the
// catalog LRU remain governed by the existing catalog cache policy.
//
// A shared icon that is still needed by the restored catalog viewport is
// retained, even if the dialog was the first screen to decode it.
inline std::vector<std::string> installedDataExclusiveTextureKeys(
    const std::unordered_set<std::string>& loadedByDialog,
    const std::unordered_set<std::string>& neededByCatalog) {
    std::vector<std::string> exclusive;
    exclusive.reserve(loadedByDialog.size());
    for (const std::string& key : loadedByDialog)
        if (neededByCatalog.find(key) == neededByCatalog.end())
            exclusive.push_back(key);
    return exclusive;
}

} // namespace psvitaalive::ui
