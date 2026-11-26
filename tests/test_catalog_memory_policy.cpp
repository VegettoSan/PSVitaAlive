#include "catalog/catalog_memory_policy.hpp"
#include <cassert>
#include <string>
#include <vector>
int main() {
    using namespace psvitaalive::catalog_memory;
    std::vector<std::string> cache[4];
    bool valid[4] = {true,true,true,true};
    for (auto& v : cache) { v.reserve(64); v.emplace_back(4096, 'x'); }
    assert(releaseExcept(cache, valid, 1) == 3);
    for (std::size_t i=0;i<4;++i) {
        if (i==1) { assert(valid[i] && cache[i].size()==1); }
        else { assert(!valid[i] && cache[i].empty() && cache[i].capacity()==0); }
    }
    auto visible=cache[1]; // independent UI ownership
    assert(releaseEntry(cache, valid, 1)==1);
    assert(cache[1].capacity()==0 && !valid[1]);
    assert(visible[0].size()==4096);
    assert(releaseEntry(cache, valid, 4)==0);
    cache[2].reserve(20); // even empty vectors release capacity
    releaseExcept(cache, valid, 0);
    assert(cache[2].capacity()==0);
}
