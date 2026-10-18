#include "ui/web_qr_guide.hpp"
#include <algorithm>
#include <cctype>
namespace psvitaalive { namespace ui {
namespace {
std::string lower(const std::string& in) {
    std::string s = in;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool includes(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}
bool typeIs(const std::string& type, const char* a, const char* b = "") {
    return type == a || (!*b ? false : type == b);
}
bool named(const std::string& name, const char* pair) {
    return includes(name, pair);
}
bool likelyDownload(const std::string& type) {
    return typeIs(type, "download", "downloads") || type == "mirror";
}
bool gameFilesLink(const CatalogLink& link) {
    const std::string type = lower(link.type), name = lower(link.name);
    return typeIs(type,"game files","game file") || type == "gamefiles"
        || (likelyDownload(type) && (named(name, "game files") || named(name, "game file") || name == "gamefiles"));
}
bool dataFilesLink(const CatalogLink& link) {
    const std::string type = lower(link.type), name = lower(link.name);
    return typeIs(type,"data files","data file") || typeIs(type,"datafiles","data")
        || (likelyDownload(type) && (named(name,"data files") || named(name,"data file") || name == "datafiles"));
}
bool pathUsable(const std::string& p) {
    if (p.size() < 5 || p.size() > 180 || p.find("..") != std::string::npos) return false;
    // Show device paths only. Catalog authors are not allowed to insert text
    // controls or untrusted prose into the modal via extract_path.
    const size_t colon = p.find(':');
    if (colon < 2 || colon > 5 || colon + 1 >= p.size() || p[colon+1] != '/') return false;
    for (unsigned char c : p)
        if (c < 0x21 || c > 0x7e) return false;
    return true;
}
bool hasStep(const std::vector<WebQrGuideStep>& out, WebQrGuideKind kind, const std::string& path) {
    for (const auto& step : out)
        if (step.kind == kind && step.destination == path) return true;
    return false;
}
void append(std::vector<WebQrGuideStep>& out, WebQrGuideKind kind, const std::string& path = {}) {
    if (!hasStep(out, kind, path)) out.push_back({kind, path});
}
}
std::vector<WebQrGuideStep> makeWebQrGuide(CatalogType catalog, const CatalogItem& item) {
    std::vector<WebQrGuideStep> steps;
    steps.reserve(8);
    append(steps, WebQrGuideKind::Start);
    bool vpk = false, zip = false, iso = false, pbp = false, pkg = false;
    bool game = false, data = false;
    std::vector<WebQrGuideStep> dataPaths, gamePaths;
    for (const auto& link : item.linkDetails) {
        const std::string url = lower(link.url), type = lower(link.type), name = lower(link.name);
        if (gameFilesLink(link)) {
            game = true;
            const std::string path = pathUsable(link.extractPath) ? link.extractPath : std::string();
            append(gamePaths, WebQrGuideKind::GameFiles, path);
            continue;
        }
        if (dataFilesLink(link)) {
            data = true;
            const std::string path = pathUsable(link.extractPath) ? link.extractPath : std::string();
            append(dataPaths, WebQrGuideKind::DataFiles, path);
            continue;
        }
        if (includes(url, ".vpk") || (likelyDownload(type) && includes(name, "vpk"))) vpk = true;
        if (includes(url, ".zip")) zip = true;
        if (includes(url, ".iso") || includes(url, ".cso")) iso = true;
        if (includes(url, ".pbp")) pbp = true;
        if (type == "pkg" || includes(url, ".pkg")) pkg = true;
    }
    if (catalog == CatalogType::Homebrew && !vpk) {
        // Direct downloads are often hosted behind redirects (e.g. MediaFire).
        // The instruction is conditional: "IF you download a VPK".
        for (const auto& link : item.linkDetails)
            if (likelyDownload(lower(link.type)) && !gameFilesLink(link) && !dataFilesLink(link))
                { vpk = true; break; }
    }
    if (vpk) append(steps, WebQrGuideKind::Vpk);
    for (const auto& step : gamePaths) append(steps, step.kind, step.destination);
    for (const auto& step : dataPaths) append(steps, step.kind, step.destination);
    if (zip && !game && !data) append(steps, WebQrGuideKind::Zip);
    if (iso && catalog == CatalogType::PspGames) append(steps, WebQrGuideKind::PspIso);
    if (pbp && (catalog == CatalogType::Ps1Games || catalog == CatalogType::PspGames)) append(steps, WebQrGuideKind::Ps1Pbp);
    if (pkg && catalog != CatalogType::Homebrew) append(steps, WebQrGuideKind::Pkg);
    if (steps.size() == 1) append(steps, WebQrGuideKind::Other);
    return steps;
}
}} // namespace psvitaalive::ui
