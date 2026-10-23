#include "network/startup_cleanup_policy.hpp"
#include <cassert>

int main() {
    using namespace psvitaalive;
    assert(startupJobAction(false, true, false, "data.zip") == StartupJobAction::Remove);
    assert(startupJobAction(false, true, true, "data.zip") == StartupJobAction::Remove);
    assert(startupJobAction(false, false, false, "data.zip") == StartupJobAction::Remove);
    assert(startupJobAction(false, false, true, "data.ZIP") == StartupJobAction::PreserveLegacyZip);
    assert(startupJobAction(false, false, true, "game.vpk") == StartupJobAction::Remove);
    assert(startupJobAction(true, true, true, "data.zip") == StartupJobAction::PreserveKept);
    assert(startupJobAction(true, false, false, "data.vpk") == StartupJobAction::PreserveKept);
    assert(!isLegacyZipName("no_zip"));
    assert(!isLegacyZipName("file.zip.info.txt"));
    return 0;
}
