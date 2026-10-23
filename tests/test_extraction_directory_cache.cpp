#include "archive/extraction_directory_cache.hpp"

#include <cassert>
#include <string>

int main() {
    psvitaalive::ExtractionDirectoryCache dirs;
    assert(!dirs.contains("ux0:data/homebrew"));
    dirs.remember("ux0:data/homebrew");
    assert(dirs.contains("ux0:data/homebrew"));
    dirs.remember("ux0:data/homebrew");
    assert(dirs.size() == 1);
    dirs.remember("");
    assert(dirs.size() == 1);

    // Cycles through alternating parents, unlike a last-parent-only cache.
    dirs.remember("ux0:data/a");
    dirs.remember("ux0:data/b");
    assert(dirs.contains("ux0:data/a"));
    assert(dirs.contains("ux0:data/b"));

    for (std::size_t i = 0; i < 3 * dirs.kMaxDirectories; ++i) {
        dirs.remember("ux0:data/unique" + std::to_string(i));
        assert(dirs.size() <= dirs.kMaxDirectories);
    }
    dirs.remember("ux0:data/revisited");
    assert(dirs.contains("ux0:data/revisited"));
    return 0;
}
