#pragma once

#include "ui/ui_types.hpp"

#include <string>
#include <vector>

namespace psvitaalive {

class CatalogParser {
public:
    static bool parseFile(
        const std::string& path,
        std::vector<ui::CatalogItem>& outItems
    );
};

} // namespace psvitaalive