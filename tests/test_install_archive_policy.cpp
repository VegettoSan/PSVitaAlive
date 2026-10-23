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

    // A VPK contains ZIP entries: direct reads from two/four downloaded ranges
    // may replace the costly file join, but only for true VPK install paths.
    assert(shouldKeepSegmentedContainer("vpk", false, false, false, false, false));
    assert(!shouldKeepSegmentedContainer("vpk", true, false, false, false, false)); // PKG BGDL
    assert(!shouldKeepSegmentedContainer("vpk", false, true, false, false, false)); // plugin
    assert(!shouldKeepSegmentedContainer("vpk", false, false, true, false, false)); // typed data
    assert(!shouldKeepSegmentedContainer("vpk", false, false, false, false, true)); // explicit destination
    assert(!shouldKeepSegmentedContainer("pkg", false, false, false, false, false));
    assert(!shouldKeepSegmentedContainer("php", false, false, false, false, false));
    assert(shouldKeepSegmentedContainer("zip", false, false, true, true, true));
    assert(!shouldKeepSegmentedContainer("zip", false, false, true, false, true));
    assert(!shouldKeepSegmentedContainer("zip", false, false, false, true, true));
    return 0;
}
