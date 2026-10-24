#pragma once

#include <string>

namespace psvitaalive {

enum class StartupJobAction { Remove, PreserveKept, PreserveLegacyZip };

inline bool isLegacyZipName(const std::string& name) {
    if (name.size() < 4) return false;
    const size_t i = name.size() - 4;
    const char extension[] = {'.', 'z', 'i', 'p'};
    for (size_t n = 0; n < 4; ++n) {
        char c = name[i + n];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != extension[n]) return false;
    }
    return true;
}

// Before cleanup_protocol=2, a Completed ZIP could have been deliberately kept
// inside jobs/ after recovery relocation failed. No reliable decision marker
// existed, so never silently delete that legacy-only ambiguous case.
inline StartupJobAction startupJobAction(bool explicitKept, bool currentProtocol,
                                        bool completed, const std::string& fileName) {
    if (explicitKept) return StartupJobAction::PreserveKept;
    if (!currentProtocol && completed && isLegacyZipName(fileName))
        return StartupJobAction::PreserveLegacyZip;
    return StartupJobAction::Remove;
}

} // namespace psvitaalive
