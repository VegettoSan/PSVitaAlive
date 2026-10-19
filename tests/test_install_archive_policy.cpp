#include "installer/install_archive_policy.hpp"
#include <cassert>

int main() {
    using namespace psvitaalive;
    assert(isDataOrGameFilesLinkType("Game Files"));
    assert(isDataOrGameFilesLinkType("DATA FILES"));
    assert(isDataOrGameFilesLinkType(" game  file "));
    assert(isDataOrGameFilesLinkType("data_files"));
    assert(!isDataOrGameFilesLinkType("Download"));
    assert(!isDataOrGameFilesLinkType("Repository"));

    // Blasphemous: .zip + explicit ux0:app/ must win over VPK-looking ZIP magic.
    assert(shouldExtractZipArchive("zip", true, true, true));
    // Other links, including Download with a ZIP destination, preserve the
    // pre-fix format dispatch: this override must not change their behavior.
    assert(!shouldExtractZipArchive("zip", true, false, true));
    assert(!shouldExtractZipArchive("zip", true, false, false));
    // No destination => never extract, and Game/Data Files must be rejected by caller.
    assert(!shouldExtractZipArchive("zip", false, true, true));
    assert(shouldExtractZipArchive("zip", true, true, false));
    // Opaque URL + typed Game Files + detected ZIP -> extract.
    assert(shouldExtractZipArchive("php", true, true, true));
    assert(!shouldExtractZipArchive("php", true, false, true));
    assert(!shouldExtractZipArchive("php", true, true, false));
    // Explicit .vpk cannot masquerade as Game Files.
    assert(!shouldExtractZipArchive("vpk", true, true, true));
    return 0;
}
