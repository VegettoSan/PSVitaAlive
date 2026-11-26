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

    // Tab navigation is free while only checking validators, but locked
    // throughout the actual refresh and final publication handoff.
    assert(!catalogSwitchBlocked(false, false, true, false));
    assert(catalogSwitchBlocked(true, false, true, false));
    assert(catalogSwitchBlocked(true, false, false, false));
    assert(catalogSwitchBlocked(false, true, true, false));
    assert(catalogSwitchBlocked(false, true, false, true));
    assert(!catalogSwitchBlocked(false, true, false, false));
    assert(!catalogSwitchBlocked(false, false, false, false));

    // Normal browsing: keep only current and previous, even on a disk miss.
    std::vector<std::string> outgoing{"Homebrew"};
    retainForSelection(cache, valid, 1, 0, &outgoing, false);
    assert(valid[0] && cache[0][0]=="Homebrew" && outgoing.capacity()==0);
    cache[1] = {"Vita"}; valid[1] = true;
    outgoing = {"Vita"};
    retainForSelection(cache, valid, 2, 1, &outgoing, false);
    assert(!valid[0] && valid[1] && cache[0].capacity()==0);
    cache[2] = {"PSP"}; valid[2] = true;
    outgoing = {"PSP"};
    retainForSelection(cache, valid, 1, 2, &outgoing, false);
    assert(valid[1] && valid[2] && !valid[0] && !valid[3]);

    // Actual update: inactive catalogs go away; UI remains independent.
    assert(releaseExcept(cache, valid, 1)==1);
    assert(valid[1] && !valid[2]);
    auto active = cache[1];
    releaseEntry(cache, valid, 1);
    assert(active[0]=="Vita");
    // No automatic refill. The updated catalog is retained only when leaving.
    active = {"Vita updated"};
    retainForSelection(cache, valid, 3, 1, &active, false);
    assert(valid[1] && cache[1][0]=="Vita updated" && !valid[3]);
    // Switching while a transfer is active must not reintroduce old payload.
    outgoing = {"PS1"};
    retainForSelection(cache, valid, 2, 3, &outgoing, true);
    assert(!valid[1] && !valid[3] && outgoing.capacity()==0);
    // Same-tab recovery must not accidentally retain a third catalog.
    cache[0] = {"old"}; valid[0] = true;
    cache[2] = {"PSP"}; valid[2] = true;
    retainForSelection(cache, valid, 2, 2, static_cast<std::vector<std::string>*>(nullptr), false);
    assert(!valid[0] && valid[2]);
}
