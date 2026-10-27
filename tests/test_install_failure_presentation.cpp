#include "ui/install_failure_presentation.hpp"
#include <cassert>
using namespace psvitaalive::ui;
int main() {
    auto k=[](const char* raw,const char* stage="Error"){return classifyInstallFailure(raw,stage);};
    assert(k("source not responding (DNS/connect/timeout, curl 28)") == InstallFailureKind::Network);
    assert(k("curl error 6: Couldn't resolve host") == InstallFailureKind::Network);
    assert(k("curl error 35: SSL connect error") == InstallFailureKind::Tls);
    assert(k("HTTP status 404") == InstallFailureKind::HttpMissing);
    assert(k("HTTP status 410") == InstallFailureKind::HttpMissing);
    assert(k("HTTP status 403") == InstallFailureKind::HttpDenied);
    assert(k("HTTP status 429") == InstallFailureKind::HttpBusy);
    assert(k("HTTP status 503") == InstallFailureKind::HttpServer);
    assert(k("MediaFire resolve failed") == InstallFailureKind::MediaFire);
    assert(k("download link resolution failed") == InstallFailureKind::MediaFire);
    assert(k("not enough free space while downloading") == InstallFailureKind::Space);
    assert(k("any error", "Space") == InstallFailureKind::Space);
    assert(k("zip_fread failed entry=sce_sys/param.sfo method=8") == InstallFailureKind::ArchiveCorrupt);
    assert(k("[ZipDiag] entry_read_failed entry=data") == InstallFailureKind::ArchiveCorrupt);
    assert(k("unsafe ZIP entry path") == InstallFailureKind::ArchiveUnsafe);
    assert(k("Game Files / Data Files must be ZIP archives, not VPK/PKG") == InstallFailureKind::Unsupported);
    assert(k("sceIoWrite failed path=ux0:") == InstallFailureKind::Storage);
    assert(k("rename part->final failed") == InstallFailureKind::Storage);
    assert(k("promoter failed during install") == InstallFailureKind::Installer);
    assert(k("totally new failure") == InstallFailureKind::Other);
    assert(!shouldReportInstallFailure(InstallFailureKind::Network));
    assert(!shouldReportInstallFailure(InstallFailureKind::Space));
    assert(shouldReportInstallFailure(InstallFailureKind::ArchiveCorrupt));
    assert(shouldReportInstallFailure(InstallFailureKind::ArchiveUnsafe));
    assert(shouldReportInstallFailure(InstallFailureKind::Other));
    assert(!isDownloadFacingFailure(InstallFailureKind::Storage));
    assert(isDownloadFacingFailure(InstallFailureKind::HttpMissing));
    return 0;
}
