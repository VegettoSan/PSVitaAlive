#include "catalog/catalog_parser.hpp"

#include <psp2/json.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/clib.h>

#include <cstdlib>
#include <string>

namespace psvitaalive {

namespace {

class VitaJsonAllocator : public sce::Json::MemAllocator {
public:
    void* allocateMemory(
        SceSize size,
        void* userData
    ) override {
        (void)userData;
        return std::malloc(size);
    }

    void freeMemory(
        void* ptr,
        void* userData
    ) override {
        (void)userData;
        std::free(ptr);
    }
};

std::string getString(
    const sce::Json::Value& object,
    const char* key
) {
    const sce::Json::Value& value = object[key];

    if (!value) {
        return {};
    }

    return value.getString().c_str();
}

uint64_t getUnsigned(
    const sce::Json::Value& object,
    const char* key
) {
    const sce::Json::Value& value = object[key];

    if (!value) {
        return 0;
    }

    return value.getUInteger();
}

std::string formatSize(uint64_t bytes) {
    if (bytes == 0) {
        return {};
    }

    char buffer[64];

    if (bytes >= 1024ULL * 1024ULL) {
        const double mb =
            static_cast<double>(bytes) /
            (1024.0 * 1024.0);

        sceClibSnprintf(
            buffer,
            sizeof(buffer),
            "%.1f MB",
            mb
        );

        return buffer;
    }

    if (bytes >= 1024ULL) {
        const uint64_t kb = bytes / 1024ULL;

        sceClibSnprintf(
            buffer,
            sizeof(buffer),
            "%llu KB",
            static_cast<unsigned long long>(kb)
        );

        return buffer;
    }

    sceClibSnprintf(
        buffer,
        sizeof(buffer),
        "%llu B",
        static_cast<unsigned long long>(bytes)
    );

    return buffer;
}

std::string firstArrayString(
    const sce::Json::Value& object,
    const char* key
) {
    const sce::Json::Value& value = object[key];

    if (!value) {
        return {};
    }

    const sce::Json::Array& array = value.getArray();

    if (array.empty()) {
        return {};
    }

    // SceLibJson exposes indexed access through Value::operator[].
    // Use an explicit SceSize index because literal 0 is ambiguous
    // with the string-key overloads of operator[].
    return value[static_cast<SceSize>(0)].getString().c_str();
}

std::string makeDownloadFileName(
    const std::string& url,
    const std::string& id
) {
    if (url.empty()) {
        return {};
    }

    std::string clean = url;

    const std::size_t query = clean.find('?');
    if (query != std::string::npos) {
        clean.erase(query);
    }

    const std::size_t fragment = clean.find('#');
    if (fragment != std::string::npos) {
        clean.erase(fragment);
    }

    const std::size_t slash = clean.find_last_of('/');

    std::string fileName;

    if (slash != std::string::npos) {
        fileName = clean.substr(slash + 1);
    } else {
        fileName = clean;
    }

    if (fileName.empty()) {
        fileName = id + ".vpk";
    }

    return fileName;
}

void parseLinks(
    const sce::Json::Value& application,
    ui::CatalogItem& item
) {
    const sce::Json::Value& linksValue =
        application["links"];

    if (!linksValue) {
        return;
    }

    const sce::Json::Array& links =
        linksValue.getArray();

    std::string fallbackDownloadUrl;
    std::string fallbackDownloadName;

    for (SceSize i = 0; i < links.size(); ++i) {
        // SceLibJson provides indexed access on Value, not on Array.
        const sce::Json::Value& link = linksValue[i];

        const std::string type =
            getString(link, "type");

        const std::string name =
            getString(link, "name");

        const std::string url =
            getString(link, "url");

        if (url.empty()) {
            continue;
        }

        std::string display;

        if (!type.empty()) {
            display = type;
        }

        if (!name.empty()) {
            if (!display.empty()) {
                display += ": ";
            }

            display += name;
        }

        if (!display.empty()) {
            item.links.push_back(display);
        }

        if (type != "Download") {
            continue;
        }

        const bool recommended =
            link["recommended"].getBoolean();

        const std::string fileName =
            makeDownloadFileName(
                url,
                item.id
            );

        if (recommended) {
            item.downloadUrl = url;
            item.downloadFileName = fileName;
        } else if (fallbackDownloadUrl.empty()) {
            fallbackDownloadUrl = url;
            fallbackDownloadName = fileName;
        }
    }

    if (item.downloadUrl.empty() &&
        !fallbackDownloadUrl.empty()) {
        item.downloadUrl = fallbackDownloadUrl;
        item.downloadFileName = fallbackDownloadName;
    }
}

} // namespace

bool CatalogParser::parseFile(
    const std::string& path,
    std::vector<ui::CatalogItem>& outItems
) {
    outItems.clear();

    const int moduleResult =
        sceSysmoduleLoadModule(
            SCE_SYSMODULE_JSON
        );

    if (moduleResult < 0) {
        sceClibPrintf(
            "[CatalogParser] Failed to load JSON module: 0x%08X\n",
            moduleResult
        );

        return false;
    }

    VitaJsonAllocator allocator;

    sce::Json::InitParameter params;
    params.allocator = &allocator;
    params.userData = nullptr;

    // Buffer utilizado por Parser::parse(path).
    params.bufSize = 64 * 1024;

    sce::Json::Initializer initializer;

    const int initResult =
        initializer.initialize(&params);

    if (initResult < 0) {
        sceClibPrintf(
            "[CatalogParser] JSON initializer failed: 0x%08X\n",
            initResult
        );

        return false;
    }

    sce::Json::Value root;

    const int parseResult =
        sce::Json::Parser::parse(
            root,
            path.c_str()
        );

    if (parseResult < 0) {
        sceClibPrintf(
            "[CatalogParser] JSON parse failed: 0x%08X\n",
            parseResult
        );

        initializer.terminate();
        return false;
    }

    const sce::Json::Array& applications =
        root.getArray();

    for (SceSize i = 0; i < applications.size(); ++i) {
        // SceLibJson provides indexed access on Value, not on Array.
        const sce::Json::Value& app =
            root[i];

        ui::CatalogItem item;

        item.id =
            getString(app, "id");

        item.titleId =
            getString(app, "title_id");

        item.name =
            getString(app, "name");

        item.description =
            getString(app, "description");

        item.longDescription =
            getString(app, "long_description");

        item.version =
            getString(app, "version");

        item.versionDate =
            getString(app, "version_date");

        item.requirements =
            getString(app, "requirements");

        item.status =
            getString(app, "status");

        item.category =
            getString(app, "category_id");

        item.subcategory =
            firstArrayString(
                app,
                "subcategory_ids"
            );

        item.changelog =
            getString(app, "changelog");

        item.size =
            formatSize(
                getUnsigned(app, "size")
            );

        item.author =
            firstArrayString(
                app,
                "author_ids"
            );

        parseLinks(app, item);

        if (item.id.empty() ||
            item.name.empty()) {
            sceClibPrintf(
                "[CatalogParser] Skipping invalid application at index %u\n",
                static_cast<unsigned>(i)
            );

            continue;
        }

        outItems.push_back(
            std::move(item)
        );
    }

    initializer.terminate();

    sceClibPrintf(
        "[CatalogParser] Loaded %u applications\n",
        static_cast<unsigned>(outItems.size())
    );

    return !outItems.empty();
}

} // namespace psvitaalive
