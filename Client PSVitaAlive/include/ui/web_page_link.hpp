#pragma once
#include "ui/ui_types.hpp"
#include <string>
namespace psvitaalive { namespace ui {
// Public page URL derived from the catalog identity, never stored in public JSON.
std::string webPageUrl(CatalogType catalog, const CatalogItem& item);
}} // namespace psvitaalive::ui
