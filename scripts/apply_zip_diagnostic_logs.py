#!/usr/bin/env python3
"""Apply ZIP extraction diagnostics and isolate ZIP failure reports."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZE = ROOT / "Client PSVitaAlive/source/archive/zip_extractor.cpp"
ER = ROOT / "Client PSVitaAlive/source/network/error_reporter.cpp"
WF = ROOT / ".github/workflows/apply-zip-diagnostic-logs.yml"
TRIGGER = ROOT / "scripts/.zip_diag_trigger"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"{label}: target not found")
    return text.replace(old, new, 1)


def patch_zip_extractor() -> None:
    text = ZE.read_text(encoding="utf-8")
    if "[ZipDiag] entry_begin" in text:
        print("zip_extractor: diagnostics already present")
        return

    old = '''        SceUID fd = sceIoOpen(outPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);\n        if (fd < 0) {\n            zip_fclose(zf);\n            setError(std::string("cannot create file: ") + outPath);\n            outcome = ZipResult::IoError;\n            break;\n        }\n\n        bool fileOk = true;\n        uint64_t writtenTotal = 0;\n        while (true) {'''
    new = '''        SceUID fd = sceIoOpen(outPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);\n        if (fd < 0) {\n            zip_fclose(zf);\n            setError(std::string("cannot create file: ") + outPath);\n            outcome = ZipResult::IoError;\n            break;\n        }\n\n        const unsigned method = (zs.valid & ZIP_STAT_COMP_METHOD) ? static_cast<unsigned>(zs.comp_method) : 0u;\n        const uint64_t archiveDiskSize = diskFileSize64(zipPath);\n        bool fileOk = true;\n        uint64_t writtenTotal = 0;\n        {\n            char diag[512];\n            sceClibSnprintf(\n                diag, sizeof(diag),\n                "[ZipDiag] entry_begin index=%llu name=%s method=%u method_name=%s comp=%llu uncomp=%llu archive_size=%llu",\n                (unsigned long long)i,\n                name,\n                method,\n                compressionMethodName(static_cast<zip_uint16_t>(method)),\n                (unsigned long long)zs.comp_size,\n                (unsigned long long)zs.size,\n                (unsigned long long)archiveDiskSize);\n            diagnostics::log(diag);\n        }\n        while (true) {'''
    text = replace_once(text, old, new, "zip_extractor entry begin")

    old = '''                    "zip_fread failed entry=%s method=%u (%s) uncomp=%llu written=%llu libzip=%s — archive may be corrupt, use STORE for huge files, or free RAM and retry",\n                    name,\n                    method,\n                    compressionMethodName(static_cast<zip_uint16_t>(method)),\n                    (unsigned long long)zs.size,\n                    (unsigned long long)writtenTotal,\n                    zipFileError(zf).c_str());'''
    new = '''                    "[ZipDiag] entry_read_failed entry=%s method=%u (%s) comp=%llu uncomp=%llu written=%llu libzip_code=%d libzip=%s archive_size=%llu",\n                    name,\n                    method,\n                    compressionMethodName(static_cast<zip_uint16_t>(method)),\n                    (unsigned long long)zs.comp_size,\n                    (unsigned long long)zs.size,\n                    (unsigned long long)writtenTotal,\n                    zip_file_get_error(zf) ? zip_error_code_zip(zip_file_get_error(zf)) : -1,\n                    zipFileError(zf).c_str(),\n                    (unsigned long long)archiveDiskSize);'''
    text = replace_once(text, old, new, "zip_extractor read failure")

    old = '''        sceIoClose(fd);\n        zip_fclose(zf);\n\n        if (!fileOk) {'''
    new = '''        sceIoClose(fd);\n        zip_fclose(zf);\n\n        if (fileOk) {\n            char diag[384];\n            sceClibSnprintf(\n                diag, sizeof(diag),\n                "[ZipDiag] entry_read_complete entry=%s method=%u expected=%llu written=%llu",\n                name,\n                method,\n                (unsigned long long)zs.size,\n                (unsigned long long)writtenTotal);\n            diagnostics::log(diag);\n        }\n\n        if (!fileOk) {'''
    text = replace_once(text, old, new, "zip_extractor completion")
    ZE.write_text(text, encoding="utf-8")
    print("zip_extractor: patched")


def patch_reporter() -> None:
    text = ER.read_text(encoding="utf-8")
    if "[ZipDiag] entry_read_failed" in text:
        print("error_reporter: ZIP isolation already present")
        return

    old = '''    size_t hit = std::string::npos;\n    if (!needle.empty())\n        hit = text.rfind(needle);\n    if (hit == std::string::npos) {\n        static const char* kToks[] = {\n            "zip_open failed", "zip_open_from_source failed", "EOCD pre-check failed",\n            "ZipExtractor", "zip_fread failed", "Zlib error",'''
    new = '''    const bool zipFailure =\n        context.find("zip") != std::string::npos ||\n        context.find("ZIP") != std::string::npos ||\n        context.find(".zip") != std::string::npos ||\n        context.find("ZipExtractor") != std::string::npos;\n\n    size_t hit = std::string::npos;\n    if (zipFailure) {\n        hit = text.rfind("[ZipDiag]");\n        if (hit == std::string::npos)\n            hit = text.rfind("zip_fread failed");\n    } else if (!needle.empty()) {\n        hit = text.rfind(needle);\n    }\n    if (hit == std::string::npos) {\n        static const char* kToks[] = {\n            "[ZipDiag] entry_read_failed", "[ZipDiag] entry_begin",\n            "[ZipDiag] entry_read_complete",\n            "zip_open failed", "zip_open_from_source failed", "EOCD pre-check failed",\n            "ZipExtractor", "zip_fread failed", "Zlib error",'''
    text = replace_once(text, old, new, "error_reporter ZIP selector")

    old = '''    static const char* kStarts[] = {\n        "LINK INSTALL",\n        "[UI] Install All step",\n        "[Installer] request job=",\n        "[Installer] installing job=",\n        "HTTP BEGIN url=",\n        "[DownloadManager] attempt",\n        "[InstallDispatcher] detect format=",\n        "[ZipExtractor]",\n        nullptr\n    };'''
    new = '''    static const char* kStarts[] = {\n        "[ZipDiag]",\n        "LINK INSTALL",\n        "[UI] Install All step",\n        "[Installer] request job=",\n        "[Installer] installing job=",\n        "HTTP BEGIN url=",\n        "[DownloadManager] attempt",\n        "[InstallDispatcher] detect format=",\n        "[ZipExtractor]",\n        nullptr\n    };'''
    text = replace_once(text, old, new, "error_reporter ZIP start")

    old = '''    session = relevantLogWindow(session, req.fileName, req.context);\n    install = relevantLogWindow(install, req.fileName, req.context);'''
    new = '''    const bool zipFailure =\n        req.context.find("zip") != std::string::npos ||\n        req.context.find("ZIP") != std::string::npos ||\n        req.context.find(".zip") != std::string::npos ||\n        req.context.find("ZipExtractor") != std::string::npos;\n\n    session = relevantLogWindow(session, req.fileName, req.context);\n    install = relevantLogWindow(install, req.fileName, req.context);\n\n    if (zipFailure) {\n        if (session.find("[ZipDiag]") == std::string::npos &&\n            session.find("zip_fread failed") == std::string::npos)\n            session.clear();\n        if (install.find("[ZipDiag]") == std::string::npos &&\n            install.find("zip_fread failed") == std::string::npos)\n            install.clear();\n    }'''
    text = replace_once(text, old, new, "error_reporter ZIP log isolation")
    ER.write_text(text, encoding="utf-8")
    print("error_reporter: patched")


def cleanup() -> None:
    # The helper exists only to perform this one controlled migration on main.
    for p in (WF, TRIGGER, Path(__file__)):
        if p.exists():
            p.unlink()


def main() -> int:
    patch_zip_extractor()
    patch_reporter()
    cleanup()
    print("OK: ZIP diagnostic logging and report isolation applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
