#pragma once

namespace psvitaalive {
namespace ime_diagnostics {
// Captures only IME lifecycle, lengths, rendering and memory; never user text.
void init();
void setSession(unsigned session);
unsigned activeSession();
void record(const char* phase, unsigned session, int value = 0,
            int extra = 0, bool sampleMemory = false);
void shutdown();
} // namespace ime_diagnostics
} // namespace psvitaalive
