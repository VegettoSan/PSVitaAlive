#include "storage/installed_directory_cache.hpp"
#include "storage/storage_maintenance.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace psvitaalive::storage;

int main() {
    {
        CachedMedia media;
        media.path="ux0:pspemu/PSP/GAME/renamed";
        media.rootStamp=12;media.payloadStamp=34;media.directory=true;
        media.sizeKnown=true;media.bytes=UINT64_C(5000000000);
        media.validPbp=true;media.platform=2;media.id="SLUS12345";media.contentId="NPUJ12345";
        media.title="External game";
        auto other=media;other.path="uma0:pspemu/PSP/GAME/renamed";
        std::string raw;std::vector<CachedMedia> decodedMedia;
        assert(InstalledDirectoryCache::encodeMedia({media,other},raw));
        assert(InstalledDirectoryCache::decodeMedia(raw,decodedMedia));
        assert(decodedMedia.size()==2 && decodedMedia[0].bytes==media.bytes);
        assert(decodedMedia[0].contentId==media.contentId && decodedMedia[0].payloadStamp==34);
        assert(!InstalledDirectoryCache::decodeMedia(raw.substr(0,raw.size()-1),decodedMedia));
        assert(decodedMedia.empty());
        assert(!InstalledDirectoryCache::encodeMedia({media,media},raw));
        other.path="ux0:pspemu/PSP/SAVEDATA/game";
        assert(!InstalledDirectoryCache::encodeMedia({other},raw));
        other=media;other.title="bad\tfield";
        assert(!InstalledDirectoryCache::encodeMedia({other},raw));
        other=media;other.sizeKnown=false;
        assert(!InstalledDirectoryCache::encodeMedia({other},raw));
        other=media;other.platform=3;
        assert(!InstalledDirectoryCache::encodeMedia({other},raw));
        assert(!InstalledDirectoryCache::decodeMedia("PSVA_DIR_INDEX_V1\n",decodedMedia));
        assert(InstalledDirectoryCache::encodeMedia({media},raw));
        std::vector<CachedDirectory> dirs;
        assert(!InstalledDirectoryCache::decode(raw,dirs));
    }
    // A parent must be successfully checked before an absent row is pruned.
    // Cache reconciliation never conflates the same ID on two mounts.
    {
        CachedMedia ux;ux.path="ux0:pspemu/ISO/game.iso";ux.rootStamp=1;ux.payloadStamp=1;
        ux.platform=1;ux.id="ULUS12345";
        auto uma=ux;uma.path="uma0:pspemu/ISO/game.iso";
        auto uncertain=ux;uncertain.path="ux0:pspemu/ISO/unreadable.iso";
        auto ps1=ux;ps1.path="ux0:pspemu/PSP/GAME/ps1";ps1.directory=true;ps1.platform=2;
        std::vector<CachedMedia> previous{ux,uma,uncertain,ps1};
        std::vector<CachedMedia> observed;
        reconcileCacheObservations(previous,observed,{"ux0:pspemu/ISO"},{uncertain.path});
        assert(observed.size()==3);
        assert(observed[0].path==uma.path && observed[1].path==uncertain.path && observed[2].path==ps1.path);
        // No successful listing: preserve previous cache, not a live row.
        observed.clear();reconcileCacheObservations(previous,observed,{},{});
        assert(observed.size()==4);
        // A newer observation supersedes the old path, no duplicates.
        auto refreshed=uma;refreshed.rootStamp=2;observed={refreshed};
        reconcileCacheObservations(previous,observed,{"ux0:pspemu/ISO","ux0:pspemu/PSP/GAME"},{});
        assert(observed.size()==1 && observed[0].rootStamp==2);
        // Both mounts were checked and every game is gone.
        observed.clear();
        reconcileCacheObservations(previous,observed,{"ux0:pspemu/ISO","uma0:pspemu/ISO","ux0:pspemu/PSP/GAME"},{});
        assert(observed.empty());
    }
    {
        CachedDirectory removed{"PCSA12345","ux0:app/PCSA12345",1,123,true};
        CachedDirectory kept{"PCSB12345","ux0:app/PCSB12345",2,456,true};
        CachedDirectory unreadable{"PCSE12345","ux0:app/PCSE12345",3,789,true};
        std::vector<CachedDirectory> previous{removed,kept,unreadable}, observed{kept};
        reconcileCacheObservations(previous,observed,{"ux0:app"},{unreadable.path});
        assert(observed.size()==2 && observed[0].titleId==kept.titleId && observed[1].titleId==unreadable.titleId);
        observed.clear();reconcileCacheObservations(previous,observed,{},{});
        assert(observed.size()==3);
    }
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
    // Main storage summary includes cached installed usage, but its cleanup
    // authority remains limited to temporary files and saved ZIPs.
    UsageSnapshot usage;
    usage.temporaryBytes = 10;
    usage.savedZipBytes = 20;
    usage.catalogBytes = 30;
    usage.imageBytes = 40;
    usage.installedBytes = 50;
    usage.installedPartial = true;
    usage.installedHistorical = true;
    assert(usage.totalBytes() == 150);
    assert(usage.temporaryBytes + usage.savedZipBytes == 30);
    assert(InstalledDirectoryCache::encode(rows,encoded));
}
