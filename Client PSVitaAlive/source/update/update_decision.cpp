#include "update/update_decision.hpp"

#include <cctype>
#include <sstream>
#include <vector>

namespace psvitaalive {
namespace update {
namespace {

std::string toLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

struct ParsedVersion {
    int parts[4] = {0, 0, 0, 0};
    int stage = 5; // dev/test=0, alpha=1, beta=2, pre=3, rc=4, stable=5
    int sequence = 0;
};


std::string trimAscii(std::string s) {
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) ++first;
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) --last;
    return s.substr(first, last - first);
}

bool parseUnsignedStrict(const std::string& s, int& out) {
    if (s.empty()) return false;
    int value = 0;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
        value = value * 10 + (c - '0');
        if (value > 9999) return false;
    }
    out = value;
    return true;
}


bool parseNormalizedVersionStrict(const std::string& input, ParsedVersion& out) {
    const std::string s = trimAscii(input);
    size_t pos = 0;
    if (pos < s.size() && (s[pos] == 'v' || s[pos] == 'V')) ++pos;
    if (pos < s.size() && s[pos] == '.') ++pos;
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;

    for (int index = 0; index < 4; ++index) {
        int num = 0;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
            const int digit = s[pos++] - '0';
            if (num > 100000000) return false; // avoid integer overflow
            num = num * 10 + digit;
        }
        out.parts[index] = num;
        if (pos + 1 < s.size() && s[pos] == '.' &&
            std::isdigit(static_cast<unsigned char>(s[pos + 1]))) {
            if (index == 3) return false;
            ++pos;
        } else {
            break;
        }
    }
    if (pos == s.size()) return true;
    if (s[pos] == '-' || s[pos] == '_' || s[pos] == '.') ++pos;

    const size_t stageStart = pos;
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) ++pos;
    if (stageStart == pos) return false;
    const std::string stage = toLower(s.substr(stageStart, pos - stageStart));
    if (stage == "dev" || stage == "test") out.stage = 0;
    else if (stage == "alpha" || stage == "a") out.stage = 1;
    else if (stage == "beta" || stage == "b") out.stage = 2;
    else if (stage == "pre" || stage == "preview") out.stage = 3;
    else if (stage == "rc") out.stage = 4;
    else return false;

    if (pos == s.size()) return true;
    if (s[pos] == '-' || s[pos] == '_' || s[pos] == '.') ++pos;
    if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return false;
    int number = 0;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        const int digit = s[pos++] - '0';
        if (number > 100000000) return false;
        number = number * 10 + digit;
    }
    if (pos != s.size()) return false;
    out.sequence = number;
    return true;
}

bool tryCompareNormalizedVersions(const std::string& a, const std::string& b, int& cmp) {
    ParsedVersion left, right;
    if (!parseNormalizedVersionStrict(a, left) || !parseNormalizedVersionStrict(b, right)) {
        // An exact opaque version may still be recognised as the installed one,
        // but never guess that an arbitrary tag represents a newer version.
        if (!trimAscii(a).empty() && toLower(trimAscii(a)) == toLower(trimAscii(b))) {
            cmp = 0;
            return true;
        }
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (left.parts[i] != right.parts[i]) {
            cmp = left.parts[i] < right.parts[i] ? -1 : 1;
            return true;
        }
    }
    if (left.stage != right.stage) {
        cmp = left.stage < right.stage ? -1 : 1;
        return true;
    }
    cmp = left.sequence < right.sequence ? -1 : (left.sequence > right.sequence ? 1 : 0);
    return true;
}

// Some Vita projects must squeeze semantic X.Y.Z into SFO APP_VER's XX.YY
// field and encode it as XX.YZ. Example: 1.7.1 -> 01.71.
bool parseSemanticTripleForVita(const std::string& input, int& major, int& minor, int& patch) {
    std::string s = trimAscii(input);
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) s.erase(s.begin());

    const size_t p1 = s.find('.');
    if (p1 == std::string::npos) return false;
    const size_t p2 = s.find('.', p1 + 1);
    if (p2 == std::string::npos || s.find('.', p2 + 1) != std::string::npos) return false;

    if (!parseUnsignedStrict(s.substr(0, p1), major) ||
        !parseUnsignedStrict(s.substr(p1 + 1, p2 - p1 - 1), minor) ||
        !parseUnsignedStrict(s.substr(p2 + 1), patch)) {
        return false;
    }

    // XX.YZ only has one decimal digit available for Y and one for Z.
    return major >= 0 && major <= 99 && minor >= 0 && minor <= 9 && patch >= 0 && patch <= 9;
}

bool parseVitaAppVer(const std::string& input, int& major, int& minorPatch) {
    const std::string s = trimAscii(input);
    const size_t dot = s.find('.');
    if (dot == std::string::npos || s.find('.', dot + 1) != std::string::npos) return false;

    const std::string majorPart = s.substr(0, dot);
    const std::string fractionalPart = s.substr(dot + 1);
    if (majorPart.empty() || majorPart.size() > 2 || fractionalPart.size() != 2) return false;
    if (!parseUnsignedStrict(majorPart, major) || !parseUnsignedStrict(fractionalPart, minorPatch)) return false;
    return major >= 0 && major <= 99 && minorPatch >= 0 && minorPatch <= 99;
}

} // namespace

int compareNormalizedVersions(const std::string& a, const std::string& b) {
    int cmp = 0;
    (void)tryCompareNormalizedVersions(a, b, cmp);
    return cmp;
}

namespace {

// SFO's XX.YY format cannot encode prerelease stage. Do not reinterpret a
// known beta/RC as a stable release just because the Vita stores 01.20.
bool tryCompareSfoToCatalogVersion(const std::string& sfoVersion,
                                   const std::string& catalogVersion, int& cmp) {
    ParsedVersion parsedCatalog;
    int catalogMajor = 0, catalogMinor = 0, catalogPatch = 0;
    int sfoMajor = 0, sfoMinorPatch = 0;
    if (parseNormalizedVersionStrict(catalogVersion, parsedCatalog) &&
        parsedCatalog.stage == 5 &&
        parseSemanticTripleForVita(catalogVersion, catalogMajor, catalogMinor, catalogPatch) &&
        parseVitaAppVer(sfoVersion, sfoMajor, sfoMinorPatch)) {
        const int catalogMinorPatch = catalogMinor * 10 + catalogPatch;
        if (sfoMajor != catalogMajor) cmp = sfoMajor < catalogMajor ? -1 : 1;
        else if (sfoMinorPatch != catalogMinorPatch) cmp = sfoMinorPatch < catalogMinorPatch ? -1 : 1;
        else cmp = 0;
        return true;
    }
    return tryCompareNormalizedVersions(sfoVersion, catalogVersion, cmp);
}

} // namespace

bool isUnreliableSfoVersion(const std::string& appVer) {
    std::string s = toLower(appVer);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.empty()) return true;
    if (s == "00.00" || s == "0.00" || s == "0" || s == "00" || s == "0.0" || s == "00.0")
        return true;
    bool allZero = true;
    bool anyDigit = false;
    for (unsigned char c : s) {
        if (std::isdigit(c)) {
            anyDigit = true;
            if (c != '0') allZero = false;
        }
    }
    return anyDigit && allZero;
}

SfoPolicy parseSfoPolicy(const std::string& s) {
    const std::string l = toLower(s);
    if (l == "trusted") return SfoPolicy::Trusted;
    if (l == "ignore") return SfoPolicy::Ignore;
    return SfoPolicy::Fallback;
}

InstallDetectResult decideInstallState(
    bool titleInstalled,
    const std::string& catalogVersion,
    const UpdateDetectionMeta& meta,
    const ReceiptEvidence& receipt,
    const FingerprintEvidence& fingerprint,
    const SfoEvidence& sfo) {

    InstallDetectResult out;
    if (!titleInstalled) {
        out.state = InstallDetectState::NotInstalled;
        out.source = "none";
        return out;
    }

    const SfoPolicy policy = meta.present ? meta.sfoPolicy : SfoPolicy::Fallback;

    if (receipt.present && receipt.fingerprintMatchesInstalled) {
        out.source = "receipt";
        out.installedVersion = receipt.catalogVersion;
        if (!catalogVersion.empty() && !receipt.catalogVersion.empty()) {
            int cmp = 0;
            if (!tryCompareNormalizedVersions(receipt.catalogVersion, catalogVersion, cmp)) {
                out.state = InstallDetectState::InstalledUnknown;
                return out;
            }
            if (cmp == 0) {
                out.state = InstallDetectState::Installed;
                return out;
            }
            if (cmp < 0) {
                out.state = InstallDetectState::UpdateAvailable;
                return out;
            }
            out.state = InstallDetectState::InstalledUnknown;
            return out;
        }
        if (meta.present && meta.revision > 0 && receipt.releaseRevision > 0) {
            if (receipt.releaseRevision == meta.revision) {
                out.state = InstallDetectState::Installed;
                return out;
            }
            if (receipt.releaseRevision < meta.revision) {
                out.state = InstallDetectState::UpdateAvailable;
                return out;
            }
            out.state = InstallDetectState::InstalledUnknown;
            return out;
        }
        out.state = InstallDetectState::Installed;
        return out;
    }

    // Phase-1 compatibility for PSVitaAlive-managed installs.
    // Current receipts are written with the catalog version but without a live
    // fingerprint digest. queryLocalInstall therefore cannot mark them as a
    // fingerprint match once the catalog version changes. While the catalog has
    // no update_detection metadata (meta.present == false), the receipt is still
    // positive evidence of the last version installed by PSVitaAlive.
    //
    // Once catalog fingerprints are available (meta.present == true), this
    // compatibility path is disabled and the strict verified-receipt/fingerprint
    // rules above/below take over again.
    if (receipt.present && !receipt.fingerprintMatchesInstalled &&
        !meta.present && !receipt.catalogVersion.empty() && !catalogVersion.empty()) {

        // If APP_VER is actually reliable and already says current/newer, prefer
        // that evidence over a potentially stale receipt. Never offer a downgrade.
        // APP_VER may use Vita's XX.YY encoding for semantic X.Y.Z (01.71 = 1.7.1).
        if (sfo.hasAppVer && !sfo.appVer.empty() && !isUnreliableSfoVersion(sfo.appVer)) {
            int sfoCmp = 0;
            const bool comparable = tryCompareSfoToCatalogVersion(sfo.appVer, catalogVersion, sfoCmp);
            if (comparable && sfoCmp == 0) {
                out.state = InstallDetectState::Installed;
                out.source = "sfo";
                out.installedVersion = sfo.appVer;
                return out;
            }
            if (comparable && sfoCmp > 0) {
                out.state = InstallDetectState::InstalledUnknown;
                out.source = "sfo";
                out.installedVersion = sfo.appVer;
                return out;
            }
        }

        out.source = "receipt";
        out.installedVersion = receipt.catalogVersion;
        int cmp = 0;
        if (!tryCompareNormalizedVersions(receipt.catalogVersion, catalogVersion, cmp)) {
            out.state = InstallDetectState::InstalledUnknown;
            return out;
        }
        if (cmp < 0) {
            out.state = InstallDetectState::UpdateAvailable;
            return out;
        }
        if (cmp == 0) {
            out.state = InstallDetectState::Installed;
            return out;
        }

        // Receipt newer than catalog: do not offer an older package.
        out.state = InstallDetectState::InstalledUnknown;
        return out;
    }

    if (fingerprint.matchedCurrent) {
        out.state = InstallDetectState::Installed;
        out.source = "fingerprint";
        out.installedVersion = catalogVersion;
        return out;
    }
    if (fingerprint.matchedHistory) {
        out.state = InstallDetectState::UpdateAvailable;
        out.source = "fingerprint";
        out.installedVersion = fingerprint.historyVersion;
        return out;
    }

    if (policy != SfoPolicy::Ignore && sfo.hasAppVer && !sfo.appVer.empty()) {
        out.source = "sfo";
        out.installedVersion = sfo.appVer;

        int sfoCmp = 0;
        const bool comparable = !catalogVersion.empty() &&
            tryCompareSfoToCatalogVersion(sfo.appVer, catalogVersion, sfoCmp);
        if (comparable && sfoCmp == 0) {
            out.state = InstallDetectState::Installed;
            return out;
        }

        if (policy == SfoPolicy::Trusted && !catalogVersion.empty() &&
            !isUnreliableSfoVersion(sfo.appVer)) {
            if (comparable && sfoCmp < 0) {
                out.state = InstallDetectState::UpdateAvailable;
                return out;
            }
            out.state = InstallDetectState::InstalledUnknown;
            return out;
        }

        out.state = InstallDetectState::InstalledUnknown;
        return out;
    }

    out.state = InstallDetectState::InstalledUnknown;
    out.source = "none";
    if (sfo.hasAppVer) out.installedVersion = sfo.appVer;
    return out;
}

} // namespace update
} // namespace psvitaalive
