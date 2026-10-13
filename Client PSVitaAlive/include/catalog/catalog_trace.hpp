#pragma once
// Temporary catalog performance tracing is disabled for production-like VPKs.
// Re-enable only on a dedicated profiling branch/build. Call-site macros avoid
// constructing std::string detail arguments when disabled (unlike no-op functions).
#ifndef PSVITAALIVE_ENABLE_CATALOG_TRACE
#define PSVITAALIVE_ENABLE_CATALOG_TRACE 0
#endif

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

#if PSVITAALIVE_ENABLE_CATALOG_TRACE
#define PSVA_TRACE_EVENT(...) ::psvitaalive::catalog_trace::event(__VA_ARGS__)
#define PSVA_TRACE_DURATION(...) ::psvitaalive::catalog_trace::duration(__VA_ARGS__)
#else
#define PSVA_TRACE_EVENT(...) ((void)0)
#define PSVA_TRACE_DURATION(...) ((void)0)
#endif
