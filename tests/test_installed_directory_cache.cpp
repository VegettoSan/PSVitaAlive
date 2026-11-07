#include "storage/installed_directory_cache.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace psvitaalive::storage;

int main() {
    const CachedDirectory kotor{"KOTR00001","ux0:data/kotor",12345,26000000,true};
    const CachedDirectory another{"ABCD12345","ux0:data/example",0,0,false};
    std::vector<CachedDirectory> rows{kotor,another};
    std::string encoded;
    assert(InstalledDirectoryCache::encode(rows,encoded));
    assert(encoded.find("PSVA_DIR_INDEX_V1\n") == 0);
    std::vector<CachedDirectory> decoded;
    assert(InstalledDirectoryCache::decode(encoded,decoded));
    assert(decoded.size() == 2);
    const CachedDirectory* hit = InstalledDirectoryCache::find(
        decoded,"KOTR00001","ux0:data/kotor");
    assert(hit && hit->sizeKnown && hit->bytes == 26000000 &&
           hit->rootStamp == 12345);
    assert(!InstalledDirectoryCache::find(decoded,"KOTR00001","ux0:data/other"));
    assert(!InstalledDirectoryCache::find(decoded,"KOTR00002","ux0:data/kotor"));
    assert(!InstalledDirectoryCache::find(decoded,"ABCD12345","ux0:data/kotor"));

    assert(!InstalledDirectoryCache::decode("bad header\n",decoded));
    assert(decoded.empty());
    assert(!InstalledDirectoryCache::decode(encoded.substr(0,encoded.size()-1),decoded));
    assert(decoded.empty());
    assert(!InstalledDirectoryCache::decode(encoded + "BROKEN\n",decoded));
    assert(decoded.empty());

    assert(!InstalledDirectoryCache::encode(
        {{"KOTR00001","ux0:data/../tai",4,6,true}},encoded));
    // Exact package roots are permitted as read-only size records.
    const CachedDirectory package{"KOTR00001","ux0:app/KOTR00001",9,85000000,true};
    rows.push_back(package);
    assert(InstalledDirectoryCache::encode(rows,encoded));
    assert(InstalledDirectoryCache::decode(encoded,decoded));
    const CachedDirectory* packageHit = InstalledDirectoryCache::find(
        decoded,"KOTR00001","ux0:app/KOTR00001");
    assert(packageHit && packageHit->bytes == 85000000);
    assert(!InstalledDirectoryCache::encode(
        {{"KOTR00001","ux0:app/ABCD12345",4,6,true}},encoded));
    assert(!InstalledDirectoryCache::encode(
        {{"KOTR00001","ux0:app/KOTR00001/sce_sys",4,6,true}},encoded));
    assert(!InstalledDirectoryCache::encode(
        {{"KOTR00001","ux0:app/KOTR00001",4,6,false}},encoded));
    assert(!InstalledDirectoryCache::encode(
        {{"KOTR00001","ux0:data/psvitaalive/cache",4,6,true}},encoded));
    assert(!InstalledDirectoryCache::encode(
        {{"KOTR00001","ux0:data/kotor",4,6,false}},encoded));
    assert(!InstalledDirectoryCache::encode(
        {{"invalid","ux0:data/kotor",4,6,true}},encoded));
    assert(InstalledDirectoryCache::encode(rows,encoded));
    assert(!InstalledDirectoryCache::decode(
        encoded + "KOTR00001\tux0:data/../bad\t42\t0\t0\n",decoded));
    assert(!InstalledDirectoryCache::decode(
        encoded + "KOTR00001\tux0:data/kotor\t42\t0\t0\n",decoded));
    assert(!InstalledDirectoryCache::decode(
        "PSVA_DIR_INDEX_V1\nKOTR00001\tux0:data/kotor\tx\t1\t20\n",decoded));
    assert(InstalledDirectoryCache::encode(rows,encoded));
}
