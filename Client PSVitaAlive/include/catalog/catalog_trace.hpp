#pragma once
// Temporary instrumentation for catalog startup investigations.
// Remove this header and CATTRACE calls after diagnosing the issue.
// Event-based only: no per-item network/content logging, no credentials or URLs.
#include "diagnostic_logger.hpp"
#include <psp2/kernel/processmgr.h>
#include <cstdint>
#include <string>

namespace psvitaalive { namespace catalog_trace {
inline uint64_t nowUs() { return sceKernelGetProcessTimeWide(); }
inline void event(const char* phase, const std::string& detail = std::string()) {
    diagnostics::log(std::string("[CATTRACE] ") + phase
        + " t_us=" + std::to_string(nowUs())
        + (detail.empty() ? "" : " ") + detail);
}
inline void duration(const char* phase, uint64_t started,
                     const std::string& detail = std::string()) {
    const uint64_t end = nowUs();
    event(phase, "duration_ms=" + std::to_string((end >= started ? end - started : 0) / 1000ULL)
        + (detail.empty() ? "" : " " + detail));
}
} } // namespace psvitaalive::catalog_trace
