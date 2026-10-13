#pragma once

#include <string>

namespace psvitaalive {
namespace diagnostics {

void init();
void log(const std::string& message);
// Wait for queued session.log writes only when assembling an error report.
// Never call this from the render loop or image-cache hot path.
void flush();
void shutdown();

} // namespace diagnostics
} // namespace psvitaalive
