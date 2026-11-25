#include "ui/detail_wrap_cache.hpp"
#include <cassert>

int main() {
    using psvitaalive::ui::DetailWrapCache;
    DetailWrapCache cache;
    int builds = 0;
    auto build = [&] { ++builds; return DetailWrapCache::Lines{"line", "two"}; };
    assert(cache.get("", 400, false, 100, build).empty());
    assert(builds == 0);
    assert(cache.get("café", 400, false, 100, build).size() == 2);
    assert(cache.get("café", 400, false, 100, build).front() == "line");
    assert(builds == 1);
    cache.get("café", 401, false, 100, build);
    cache.get("café", 400, true, 100, build);
    cache.get("café", 400, false, 105, build);
    cache.get("cafe", 400, false, 100, build);
    assert(builds == 5);
    cache.get("café", 400, false, 100, build);
    assert(builds == 5);
    cache.clear();
    cache.get("café", 400, false, 100, build);
    assert(builds == 6);
    for (std::size_t i = 0; i < DetailWrapCache::capacity; ++i)
        cache.get(std::to_string(i), 400, false, 100, build);
    assert(builds == 14);
    cache.get("café", 400, false, 100, build);
    assert(builds == 15);
    const std::string huge(DetailWrapCache::maxTextBytes + 1, 'x');
    cache.get(huge, 400, false, 100, build);
    cache.get(huge, 400, false, 100, build);
    assert(builds == 17);
    auto many = [&] { ++builds; return DetailWrapCache::Lines(DetailWrapCache::maxLines + 1, "x"); };
    assert(cache.get("many", 400, false, 100, many).size() == DetailWrapCache::maxLines + 1);
    cache.get("many", 400, false, 100, many);
    assert(builds == 19);
    assert(cache.get("café", 400, false, 100, build).size() == 2);
    assert(builds == 19);
}
