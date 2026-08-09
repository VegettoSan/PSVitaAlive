#pragma once

#include <string>

class StorageManager {
public:
    static bool initialize();

    static bool createTestFile(
        const std::string& path,
        const std::string& content
    );

    static bool readTestFile(
        const std::string& path,
        std::string& content
    );
};
