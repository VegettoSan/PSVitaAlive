#pragma once

#include <string>

namespace psvitaalive {
namespace diagnostics {

void init();
void log(const std::string& message);
void shutdown();

} // namespace diagnostics
} // namespace psvitaalive
