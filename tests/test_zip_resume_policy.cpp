#include "archive/zip_resume_policy.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <zlib.h>

int main() {
    using psvitaalive::ZipResumeVerifier;
    const std::string payload = "A previously extracted ZIP entry with correct bytes.";
    const uint32_t crc = static_cast<uint32_t>(crc32(0L,
        reinterpret_cast<const Bytef*>(payload.data()),
        static_cast<uInt>(payload.size())));

    // A correct file can be skipped without reopening the archive entry.
    ZipResumeVerifier completed(payload.size(), crc);
    assert(completed.accept(payload.data(), 12));
    assert(!completed.matches(false, true));
    assert(completed.accept(payload.data() + 12, payload.size() - 12));
    assert(completed.matches(true, true));
    assert(!completed.matches(true, false));

    // A same-size but corrupted file must never be skipped.
    std::string corrupt = payload;
    corrupt[7] ^= 1;
    ZipResumeVerifier wrongCrc(payload.size(), crc);
    assert(wrongCrc.accept(corrupt.data(), corrupt.size()));
    assert(!wrongCrc.matches(true, true));

    // Aborted or partial file cannot pass, even if a partial CRC was computed.
    ZipResumeVerifier truncated(payload.size(), crc);
    assert(truncated.accept(payload.data(), payload.size() - 1));
    assert(!truncated.matches(true, true));

    // Excess bytes invalidate the match, even if they are appended to a valid file.
    ZipResumeVerifier oversized(payload.size(), crc);
    assert(oversized.accept(payload.data(), payload.size()));
    assert(!oversized.accept("!", 1));
    assert(!oversized.matches(true, true));

    // Empty files are valid only if CRC32 and size match and the read/close ended cleanly.
    ZipResumeVerifier empty(0, 0);
    assert(empty.matches(true, true));
    ZipResumeVerifier badEmpty(0, 1);
    assert(!badEmpty.matches(true, true));

    // Read errors/null input are not valid for resume.
    ZipResumeVerifier failed(payload.size(), crc);
    assert(!failed.accept(nullptr, 4));
    assert(!failed.matches(true, true));

    // Corrupt CRC metadata is rejected, not assumed correct based on size.
    ZipResumeVerifier badMetadata(payload.size(), crc ^ 0x00000001);
    assert(badMetadata.accept(payload.data(), payload.size()));
    assert(!badMetadata.matches(true, true));

    return 0;
}
