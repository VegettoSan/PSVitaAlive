#pragma once

#include <string>

namespace psvitaalive {

// Vita directory lookups can become expensive when a ZIP recreates a tree
// containing thousands of preexisting files. Prefer exactly one truncate
// open in existing-file runs; after an ENOENT, switch to exclusive creation.
// A fresh parent starts with an existing-file probe again. This class only
// selects the order: callers MUST preserve O_EXCL ownership and cleanup rules.
class ZipExistingOpenPolicy {
public:
    bool tryExistingFirst(const std::string& parent) {
        if (parent != lastParent_) {
            lastParent_ = parent;
            preferExisting_ = true;
        }
        return preferExisting_;
    }

    void existingOpened() { preferExisting_ = true; }
    void existingMissing() { preferExisting_ = false; }
    void newFileCreated() { preferExisting_ = false; }

private:
    std::string lastParent_;
    bool preferExisting_ = true;
};

} // namespace psvitaalive
