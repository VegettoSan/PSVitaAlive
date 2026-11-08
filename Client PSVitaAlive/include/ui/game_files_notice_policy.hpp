#pragma once
// Read-only detail warning: never infer missing Game Files from absent/invalid
// installed_paths metadata. This policy is host-testable.
#include "storage/installed_data_policy.hpp"
#include "ui/ui_types.hpp"
#include <string>

namespace psvitaalive { namespace ui {
template<typename IsGameFilesLink, typename IsDirectory>
inline bool missingInstalledGameFiles(const CatalogItem& item, bool installed,
                                      IsGameFilesLink isGameFilesLink,
                                      IsDirectory isDirectory) {
    if (!installed) return false;
    bool hasKnownRoot = false;
    for (const CatalogLink& link : item.linkDetails) {
        if (!isGameFilesLink(link)) continue;
        if (link.installedPaths.empty()) return false;
        for (const std::string& relative : link.installedPaths) {
            const std::string path = "ux0:data/" + relative;
            if (!::psvitaalive::storage::safeExternalDataPath(path)) return false;
            hasKnownRoot = true;
            // Multiple paths/links may be alternative distributions.
            if (isDirectory(path)) return false;
        }
    }
    return hasKnownRoot;
}
}} // namespace psvitaalive::ui
