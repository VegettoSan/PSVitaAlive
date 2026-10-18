#pragma once
#include "ui/ui_types.hpp"
#include <string>
#include <vector>
namespace psvitaalive { namespace ui {
enum class WebQrGuideKind { Start, Vpk, GameFiles, DataFiles, Zip, PspIso, Ps1Pbp, Pkg, Other };
struct WebQrGuideStep {
    WebQrGuideKind kind;
    // Optional destination is copied from this specific catalog link's
    // extract_path. An empty destination deliberately carries no guessed path.
    std::string destination;
};
std::vector<WebQrGuideStep> makeWebQrGuide(CatalogType catalog, const CatalogItem& item);
}} // namespace psvitaalive::ui
