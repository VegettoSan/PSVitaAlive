#include "catalog/catalog_parser.hpp"
#include "catalog/catalog_trace.hpp"
#include "catalog/catalog_parse_policy.hpp"

#include <psp2/json.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>

#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

namespace psvitaalive {

namespace {

constexpr const char* IMAGE_CACHE_TAG = "psva_cache=";

class VitaJsonAllocator : public sce::Json::MemAllocator {
public:
    void* allocateMemory(SceSize size, void* userData) override {
        (void)userData;
        return std::malloc(size);
    }

    void freeMemory(void* ptr, void* userData) override {
        (void)userData;
        std::free(ptr);
    }
};

// V1/V2 compatibility: lookup legacy field first, then its explicit compact alias.
const sce::Json::Value& getCatalogField(const sce::Json::Value& object, const char* key,
                                       const char* compactKey = nullptr) {
    return catalog_parse::field(object, key, compactKey);
}

std::string getString(const sce::Json::Value& object, const char* key,
                      const char* compactKey = nullptr) {
    const sce::Json::Value& value = getCatalogField(object, key, compactKey);
    if (!value) return {};
    return value.getString().c_str();
}

uint64_t getUnsigned(const sce::Json::Value& object, const char* key,
                     const char* compactKey = nullptr) {
    const sce::Json::Value& value = getCatalogField(object, key, compactKey);
    if (!value) return 0;
    return value.getUInteger();
}

std::string formatSize(uint64_t bytes) {
    if (bytes == 0) return {};
    char buffer[64];
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        sceClibSnprintf(buffer, sizeof(buffer), "%.2f GB", gb);
        return buffer;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        sceClibSnprintf(buffer, sizeof(buffer), "%.1f MB", mb);
        return buffer;
    }
    if (bytes >= 1024ULL) {
        sceClibSnprintf(buffer, sizeof(buffer), "%llu KB", static_cast<unsigned long long>(bytes / 1024ULL));
        return buffer;
    }
    sceClibSnprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    return buffer;
}

std::string firstArrayString(const sce::Json::Value& object, const char* key,
                             const char* compactKey = nullptr) {
    const sce::Json::Value& value = getCatalogField(object, key, compactKey);
    if (!value) return {};
    const sce::Json::Array& array = value.getArray();
    if (array.empty()) return {};
    return value[static_cast<SceSize>(0)].getString().c_str();
}

void parseStringArray(const sce::Json::Value& object, const char* key,
                      std::vector<std::string>& out, const char* compactKey = nullptr,
                      size_t limit = std::numeric_limits<size_t>::max()) {
    catalog_parse::appendStrings(getCatalogField(object, key, compactKey), out, limit);
}

const char* imageCatalogPrefixForPath(const std::string& path) {
    if (path.find("catalog_psvita_games.json") != std::string::npos) return "PV";
    if (path.find("catalog_psp_games.json") != std::string::npos) return "PSP";
    if (path.find("catalog_ps1_games.json") != std::string::npos) return "PS1";
    return "H";
}

std::string hex64(uint64_t value) {
    char buffer[24];
    sceClibSnprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(value));
    return buffer;
}

std::string imageResourceKey(const ui::CatalogItem& item, const char* catalogPrefix,
                             std::string_view role) {
    const std::string& owner = item.titleId.empty() ? item.id : item.titleId;
    return hex64(catalog_parse::imageResourceHash(catalogPrefix ? catalogPrefix : "U", owner, role));
}

void tagCatalogImageUrl(
    std::string& url,
    const char* catalogPrefix,
    const std::string& resourceKey
) {
    if (url.empty() || !catalogPrefix || !*catalogPrefix || resourceKey.empty()) return;

    url += (url.find('#') == std::string::npos) ? '#' : '&';
    url += IMAGE_CACHE_TAG;
    url += catalogPrefix;
    url.push_back(':');
    url += resourceKey;
}

std::string makeDownloadFileName(const std::string& url, const std::string& id) {
    if (url.empty()) return {};
    std::string clean = url;
    const std::size_t query = clean.find('?');
    if (query != std::string::npos) clean.erase(query);
    const std::size_t fragment = clean.find('#');
    if (fragment != std::string::npos) clean.erase(fragment);
    const std::size_t slash = clean.find_last_of('/');
    std::string fileName = slash != std::string::npos ? clean.substr(slash + 1) : clean;

    // VitaDB / similar redirectors expose get_hb_url.php — that is not a payload name.
    // Force a .vpk so FormatDetector and InstallDispatcher treat the file as homebrew.
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string low = lower(fileName);
    const bool badName =
        fileName.empty() ||
        low == "get_hb_url.php" ||
        low.find("get_hb_url") != std::string::npos ||
        (low.size() >= 4 && (low.rfind(".php") == low.size() - 4 ||
                             low.rfind(".asp") == low.size() - 4 ||
                             low.rfind(".aspx") == low.size() - 5 ||
                             low.rfind(".html") == low.size() - 5 ||
                             low.rfind(".htm") == low.size() - 4));
    if (badName) {
        const std::string base = id.empty() ? "download" : id;
        return base + ".vpk";
    }
    return fileName;
}

// Commercial games group multiple regional IDs; normalize once in the parser.
void addCatalogTitleId(ui::CatalogItem& item, std::string tid) {
    if (tid.size() != 9) return;
    for (char& c : tid) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return;
    }
    if (item.titleId.empty()) { item.titleId = tid; return; }
    if (item.titleId == tid || item.alternateTitleIds.find(tid) != std::string::npos) return;
    if (!item.alternateTitleIds.empty()) item.alternateTitleIds.push_back('|');
    item.alternateTitleIds += tid;
}

void parseLinks(const sce::Json::Value& application, ui::CatalogItem& item, SceUID zrifIdxFd, uint32_t* zrifWritten) {
    const sce::Json::Value& linksValue = getCatalogField(application, "links", "l");
    if (!linksValue) return;

    const sce::Json::Array& links = linksValue.getArray();
    std::string fallbackDownloadUrl;
    std::string fallbackDownloadName;

    for (SceSize i = 0; i < links.size(); ++i) {
        const sce::Json::Value& link = linksValue[i];
        const std::string type = getString(link, "type", "t");
        const std::string name = getString(link, "name", "n");
        const std::string url = getString(link, "url", "u");
        // DLC-specific IDs never identify the base game as installed.
        if (type == "Download" || type == "Downloads" || type == "download" ||
            type == "Mirror" || type == "mirror")
            addCatalogTitleId(item, getString(link, "title_id", "tid"));
        if (url.empty()) continue;

        ui::CatalogLink detail;
        detail.type = type;
        detail.name = name;
        detail.url = url;
        // Keep zRIF off the heap: write to sidecar index, leave detail.zrif empty.
        {
            std::string z = getString(link, "zrif", "zr");
            if (z.empty()) z = getString(link, "zRIF", "zR");
            if (z.empty()) z = getString(link, "license", "lic");
            if (!z.empty() && zrifIdxFd >= 0) {
                sceIoWrite(zrifIdxFd, url.c_str(), static_cast<SceSize>(url.size()));
                const char tab = static_cast<char>(9);
                sceIoWrite(zrifIdxFd, &tab, 1);
                sceIoWrite(zrifIdxFd, z.c_str(), static_cast<SceSize>(z.size()));
                const char nl = static_cast<char>(10);
                sceIoWrite(zrifIdxFd, &nl, 1);
                if (zrifWritten) ++(*zrifWritten);
            }
        }
        detail.contentId = getString(link, "content_id", "coid");
        if (detail.contentId.empty()) detail.contentId = getString(link, "contentId", "coi");
        detail.recommended = getCatalogField(link, "recommended", "rec").getBoolean();
        // Optional per-link size (numeric bytes preferred).
        const sce::Json::Value& linkSize = getCatalogField(link, "size", "sz");
        if (linkSize) {
            const uint64_t bytes = linkSize.getUInteger();
            if (bytes > 0) detail.size = formatSize(bytes);
            else {
                detail.size = linkSize.getString().c_str();
            }
        }
        // Optional ZIP extract destination (skip path picker when set).
        {
            std::string ep = getString(link, "extract_path", "xp");
            if (ep.empty()) ep = getString(link, "extractPath");
            while (!ep.empty() && (ep.front() == ' ' || ep.front() == static_cast<char>(9))) ep.erase(ep.begin());
            while (!ep.empty() && (ep.back() == ' ' || ep.back() == static_cast<char>(9))) ep.pop_back();
            if (!ep.empty()) {
                if (ep.back() != '/' && ep.back() != ':') ep.push_back('/');
                detail.extractPath = std::move(ep);
            }
        }
        // Plugin link metadata (taiHEN config append).
        {
            detail.section = getString(link, "section", "sec");
            detail.line = getString(link, "line", "ln");
            while (!detail.section.empty() && (detail.section.front() == ' ' || detail.section.front() == static_cast<char>(9)))
                detail.section.erase(detail.section.begin());
            while (!detail.section.empty() && (detail.section.back() == ' ' || detail.section.back() == static_cast<char>(9)))
                detail.section.pop_back();
            while (!detail.line.empty() && (detail.line.front() == ' ' || detail.line.front() == static_cast<char>(9)))
                detail.line.erase(detail.line.begin());
            while (!detail.line.empty() && (detail.line.back() == ' ' || detail.line.back() == static_cast<char>(9)))
                detail.line.pop_back();
            // Default extract_path for plugins when omitted.
            // type is original case; compare loosely
            auto isPlugin = false;
            {
                std::string tl;
                tl.reserve(type.size());
                for (char c : type) tl.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                isPlugin = (tl == "plugin" || tl == "plugins");
            }
            if (isPlugin && detail.extractPath.empty()) {
                detail.extractPath = "ur0:tai/";
            }
            // Presence matters: false means Optional; missing means Unspecified.
            if (isPlugin) {
                const sce::Json::Value& required = getCatalogField(link, "required", "rq");
                if (required.getType() == sce::Json::ValueType::BoolValue) {
                    detail.pluginRequirement = required.getBoolean()
                        ? ui::CatalogLink::PluginRequirement::Required
                        : ui::CatalogLink::PluginRequirement::Optional;
                }
            }
        }
        // Keep installed_paths metadata for Game/Data Files; no scanning or
        // filesystem access during catalog parsing. Legacy links remain valid.
        if (type == "Game Files" || type == "Data Files")
            parseStringArray(link, "installed_paths", detail.installedPaths, "ips");

        std::string display = type;
        if (!name.empty()) {
            if (!display.empty()) display += ": ";
            display += name;
        }
        if (!display.empty()) item.links.push_back(std::move(display));

        if (type == "Download" || type == "download" || type == "Downloads") {
            const std::string fileName = makeDownloadFileName(url, item.id);
            if (detail.recommended) {
                item.downloadUrl = url;
                item.downloadFileName = fileName;
            } else if (fallbackDownloadUrl.empty()) {
                fallbackDownloadUrl = url;
                fallbackDownloadName = fileName;
            }
        }
        item.linkDetails.push_back(std::move(detail));
    }

    if (item.downloadUrl.empty() && !fallbackDownloadUrl.empty()) {
        item.downloadUrl = std::move(fallbackDownloadUrl);
        item.downloadFileName = std::move(fallbackDownloadName);
    }
}

} // namespace

bool CatalogParser::parseFile(const std::string& path, std::vector<ui::CatalogItem>& outItems) {
    const uint64_t traceStarted=catalog_trace::nowUs();
    PSVA_TRACE_EVENT("parser_begin","path="+path);
    outItems.clear();

    // Sidecar zRIF index next to the catalog JSON. Install looks up by URL so
    // we never keep thousands of license strings in RAM (Vita Games OOM).
    const std::string zrifIndexPath = path + ".zrifidx";
    sceIoRemove(zrifIndexPath.c_str());
    const SceUID zrifIdxFd = sceIoOpen(
        zrifIndexPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    uint32_t zrifWritten = 0;

    const int moduleResult = sceSysmoduleLoadModule(SCE_SYSMODULE_JSON);
    if (moduleResult < 0) {
        sceClibPrintf("[CatalogParser] Failed to load JSON module: 0x%08X\n", moduleResult);
        if (zrifIdxFd >= 0) sceIoClose(zrifIdxFd);
        return false;
    }

    VitaJsonAllocator allocator;
    sce::Json::InitParameter params;
    params.allocator = &allocator;
    params.userData = nullptr;
    params.bufSize = 64 * 1024;

    sce::Json::Initializer initializer;
    const int initResult = initializer.initialize(&params);
    if (initResult < 0) {
        sceClibPrintf("[CatalogParser] JSON initializer failed: 0x%08X\n", initResult);
        if (zrifIdxFd >= 0) sceIoClose(zrifIdxFd);
        return false;
    }

    sce::Json::Value root;
    const uint64_t domStart=catalog_trace::nowUs();
    const int parseResult = sce::Json::Parser::parse(root, path.c_str());
    PSVA_TRACE_DURATION("parser_dom_end",domStart,
        "rc="+std::to_string(parseResult));
    if (parseResult < 0) {
        sceClibPrintf("[CatalogParser] JSON parse failed: 0x%08X\n", parseResult);
        initializer.terminate();
        if (zrifIdxFd >= 0) sceIoClose(zrifIdxFd);
        return false;
    }

    const char* imagePrefix = imageCatalogPrefixForPath(path);
    const uint64_t conversionStart=catalog_trace::nowUs();
    const sce::Json::Array& applications = root.getArray();
    const size_t initialReserve = catalog_parse::initialItemReserve(applications.size());
    if (outItems.capacity() < initialReserve) outItems.reserve(initialReserve);
    PSVA_TRACE_EVENT("parser_conversion_begin","entries="+std::to_string(applications.size()));
    for (SceSize i = 0; i < applications.size(); ++i) {
        const sce::Json::Value& app = root[i];
        ui::CatalogItem item;

        item.id = getString(app, "id");
        item.titleId = getString(app, "title_id", "tid");
        // Regional game IDs are grouped in title_ids rather than title_id.
        const sce::Json::Value& regionIds = getCatalogField(app, "title_ids", "tids");
        if (regionIds) {
            static const char* regions[] = {
                "US", "EU", "JP", "AS", "ASIA", "AU", "KR", "CA", "CN", "TW",
                "HK", "INT", "Other", "other", "World", "WORLD"
            };
            for (const char* region : regions)
                addCatalogTitleId(item, getString(regionIds, region));
        }
        item.name = getString(app, "name", "n");
        item.description = getString(app, "description", "d");
        // long_description skipped — was a major RAM cost on Vita Games catalog.
        if (item.description.size() > 480) item.description.resize(480);
        item.longDescription.clear();
        item.version = getString(app, "version", "v");
        const uint64_t dr = getUnsigned(app, "dr");
        item.dataRevision = (dr >= 1 && dr <= 65535) ? static_cast<uint32_t>(dr) : 0;
        item.versionDate = getString(app, "version_date", "vd");
        if (item.versionDate.empty()) item.versionDate = getString(app, "date", "dt");
        item.requirements = getString(app, "requirements", "req");
        item.status = getString(app, "status", "st");
        item.category = getString(app, "category_id", "cid");
        if (item.category.empty()) item.category = getString(app, "category");
        item.subcategory = firstArrayString(app, "subcategory_ids", "sids");
        item.changelog.clear(); // skipped for RAM (detail can show description only)
        item.size = formatSize(getUnsigned(app, "size", "sz"));
        item.icon = getString(app, "icon", "ic");
        item.cover = getString(app, "cover", "cv");
        parseStringArray(app, "screenshots", item.screenshots, "ss", 4);
        item.author = firstArrayString(app, "author_ids", "aids");
        if (item.author.empty()) item.author = getString(app, "author");

        parseLinks(app, item, zrifIdxFd, &zrifWritten);

        if (item.status.empty()) item.status = "Available";

        if (item.id.empty() || item.name.empty()) {
            sceClibPrintf("[CatalogParser] Skipping invalid application at index %u\n", static_cast<unsigned>(i));
            continue;
        }

        // Internal-only cache metadata. ImageCache strips it before libcurl sees
        // the URL. The stable resource key lets one changed image replace only
        // its own previous cached version instead of scanning a whole catalog.
        if (!item.cover.empty()) {
            tagCatalogImageUrl(
                item.cover,
                imagePrefix,
                imageResourceKey(item, imagePrefix, "cover"));
        }

        if (!item.icon.empty()) {
            tagCatalogImageUrl(
                item.icon,
                imagePrefix,
                imageResourceKey(item, imagePrefix, "icon"));
        } else {
            // Game catalogs may expose only cover. Reuse the exact same tagged
            // resource so icon + cover do not create duplicate cache files.
            item.icon = item.cover;
        }

        for (size_t shotIndex = 0; shotIndex < item.screenshots.size(); ++shotIndex) {
            char role[24];
            sceClibSnprintf(role, sizeof(role), "shot%u", static_cast<unsigned>(shotIndex));
            tagCatalogImageUrl(
                item.screenshots[shotIndex],
                imagePrefix,
                imageResourceKey(item, imagePrefix, role));
        }

        outItems.push_back(std::move(item));
    }

    PSVA_TRACE_DURATION("parser_conversion_end",conversionStart,
        "loaded="+std::to_string(outItems.size())+
        " entries="+std::to_string(applications.size()));
    initializer.terminate();
    if (zrifIdxFd >= 0) sceIoClose(zrifIdxFd);
    PSVA_TRACE_DURATION("parser_complete",traceStarted,
        "loaded="+std::to_string(outItems.size()));
    sceClibPrintf("[CatalogParser] Loaded %u applications (zrif index entries=%u)\n",
                  static_cast<unsigned>(outItems.size()),
                  static_cast<unsigned>(zrifWritten));
    return !outItems.empty();
}

} // namespace psvitaalive
