#pragma once

#include "ui/ui_types.hpp"

#include <atomic>
#include <string>
#include <vector>

#include <psp2/kernel/threadmgr.h>

namespace psvitaalive {

class CatalogManager {
public:
    enum class State {
        Idle,
        Loading,
        Ready,
        Failed
    };

    struct Status {
        State state = State::Idle;
        ui::CatalogType catalog = ui::CatalogType::Homebrew;
        uint64_t current = 0;
        uint64_t total = 0;
        std::string label;
        std::string message;
        std::string error;
    };

    CatalogManager();
    ~CatalogManager();

    bool init();
    void shutdown();

    bool request(ui::CatalogType catalog);
    Status status() const;

    // Called from the UI/main thread. Moves the completed catalog into outItems.
    bool takeReady(std::vector<ui::CatalogItem>& outItems, ui::CatalogType& catalog);

private:
    SceUID mutex_ = -1;
    SceUID workerThread_ = -1;
    volatile bool stopping_ = false;

    ui::CatalogType requestedCatalog_ = ui::CatalogType::Homebrew;
    bool requestPending_ = false;

    Status status_;
    std::vector<ui::CatalogItem> readyItems_;
    bool readyPending_ = false;
    ui::CatalogType readyCatalog_ = ui::CatalogType::Homebrew;

    static int workerEntry(SceSize args, void* argp);
    int workerMain();
    bool loadCatalog(ui::CatalogType catalog, std::vector<ui::CatalogItem>& outItems);
    const char* fileName(ui::CatalogType catalog) const;
    const char* label(ui::CatalogType catalog) const;
    std::string cachePath(ui::CatalogType catalog) const;
    void setStatus(State state, ui::CatalogType catalog, const char* message, const char* error = nullptr);
};

} // namespace psvitaalive
