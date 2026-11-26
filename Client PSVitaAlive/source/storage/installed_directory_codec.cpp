#include "storage/installed_directory_cache.hpp"
#include "storage/installed_data_policy.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_set>

namespace psvitaalive { namespace storage {
namespace {
constexpr size_t MAX_ROWS = 1024;
constexpr size_t MAX_BYTES = 128 * 1024;
constexpr const char* MAGIC = "PSVA_DIR_INDEX_V1\n";
bool decimal(const std::string& source, uint64_t& value) {
    if (source.empty() || source.size() > 20) return false;
    for (char c : source) if (c < '0' || c > '9') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long number = std::strtoull(source.c_str(), &end, 10);
    if (errno != 0 || !end || *end != '\0') return false;
    value = static_cast<uint64_t>(number);
    return true;
}
bool validate(const CachedDirectory& row) {
    return validInstalledTitleId(row.titleId) &&
           (safeExternalDataPath(row.path) ||
            row.path == "ux0:app/" + row.titleId) &&
           (row.sizeKnown || row.bytes == 0);
}

} // namespace

bool InstalledDirectoryCache::encode(
    const std::vector<CachedDirectory>& entries, std::string& out) {
    out.assign(MAGIC);
    if (entries.size() > MAX_ROWS) return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& row = entries[i];
        if (!validate(row)) return false;
        for (size_t j = 0; j < i; ++j)
            if (entries[j].titleId == row.titleId &&
                entries[j].path == row.path) return false;
        out += row.titleId + "\t" + row.path + "\t" +
               std::to_string(row.rootStamp) + "\t" +
               (row.sizeKnown ? "1" : "0") + "\t" +
               std::to_string(row.bytes) + "\n";
        if (out.size() > MAX_BYTES) return false;
    }
    return true;
}

bool InstalledDirectoryCache::decode(
    const std::string& raw, std::vector<CachedDirectory>& out) {
    out.clear();
    if (raw.size() > MAX_BYTES ||
        raw.compare(0, std::strlen(MAGIC), MAGIC) != 0) return false;
    size_t pos = std::strlen(MAGIC);
    while (pos < raw.size()) {
        const size_t end = raw.find('\n', pos);
        if (end == std::string::npos) { out.clear(); return false; }
        const std::string line = raw.substr(pos, end - pos);
        pos = end + 1;
        if (line.empty()) { out.clear(); return false; }
        size_t fields[4];
        size_t seek = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const size_t tab = line.find('\t', seek);
            if (tab == std::string::npos) { out.clear(); return false; }
            fields[i] = tab;
            seek = tab + 1;
        }
        // Exactly four tabs: title, path, fingerprint, measured, bytes.
        if (line.find('\t', seek) != std::string::npos) {
            out.clear(); return false;
        }
        CachedDirectory row;
        row.titleId = line.substr(0, fields[0]);
        row.path = line.substr(fields[0]+1, fields[1]-fields[0]-1);
        const std::string stamp = line.substr(
            fields[1]+1, fields[2]-fields[1]-1);
        const std::string known = line.substr(
            fields[2]+1, fields[3]-fields[2]-1);
        const std::string bytes = line.substr(fields[3]+1);
        if ((known != "0" && known != "1") ||
            !decimal(stamp, row.rootStamp) || !decimal(bytes, row.bytes)) {
            out.clear(); return false;
        }
        row.sizeKnown = known == "1";
        if (!validate(row)) { out.clear(); return false; }
        if (find(out, row.titleId, row.path)) { out.clear(); return false; }
        out.push_back(std::move(row));
        if (out.size() > MAX_ROWS) { out.clear(); return false; }
    }
    return true;
}
const CachedDirectory* InstalledDirectoryCache::find(
    const std::vector<CachedDirectory>& entries,
    const std::string& titleId, const std::string& path) {
    for (const auto& row : entries)
        if (row.titleId == titleId && row.path == path) return &row;
    return nullptr;
}


namespace {
constexpr const char* MEDIA_MAGIC="PSVA_MEDIA_INDEX_V1\n";
constexpr size_t MEDIA_MAX_ROWS=4096,MEDIA_MAX_BYTES=512*1024;
bool cleanField(const std::string& value,size_t max) {
    if(value.size()>max)return false;
    for(unsigned char c:value)if(c<32||c==127)return false;
    return true;
}
bool validMedia(const CachedMedia& row) {
    return safeGameMediaPath(row.path,row.directory) && row.rootStamp && row.payloadStamp &&
        row.platform<=2 && (row.sizeKnown||!row.bytes) &&
        cleanField(row.title,256) && (row.id.empty()||validInstalledTitleId(row.id)) &&
        (row.contentId.empty()||validInstalledTitleId(row.contentId));
}
}
bool InstalledDirectoryCache::encodeMedia(const std::vector<CachedMedia>& entries,std::string& out) {
    out.assign(MEDIA_MAGIC);
    if(entries.size()>MEDIA_MAX_ROWS)return false;
    std::unordered_set<std::string> seen;
    for(const auto& row:entries) {
        if(!validMedia(row)||!seen.insert(row.path).second)return false;
        out+=row.path+"\t"+std::to_string(row.rootStamp)+"\t"+std::to_string(row.payloadStamp)+"\t"+
            std::to_string(row.bytes)+"\t"+(row.directory?"1":"0")+"\t"+(row.sizeKnown?"1":"0")+"\t"+
            (row.validPbp?"1":"0")+"\t"+std::to_string(row.platform)+"\t"+row.title+"\t"+row.id+"\t"+row.contentId+"\n";
        if(out.size()>MEDIA_MAX_BYTES)return false;
    }
    return true;
}
bool InstalledDirectoryCache::decodeMedia(const std::string& raw,std::vector<CachedMedia>& out) {
    out.clear();
    if(raw.size()>MEDIA_MAX_BYTES||raw.compare(0,std::strlen(MEDIA_MAGIC),MEDIA_MAGIC)!=0)return false;
    std::vector<CachedMedia> rows;
    std::unordered_set<std::string> seen;
    size_t pos=std::strlen(MEDIA_MAGIC);
    while(pos<raw.size()) {
        const auto end=raw.find('\n',pos);
        if(end==std::string::npos||rows.size()>=MEDIA_MAX_ROWS)return false;
        const std::string line=raw.substr(pos,end-pos);pos=end+1;
        std::string fields[11];size_t start=0;
        for(unsigned i=0;i<10;++i) {
            const auto tab=line.find('\t',start);if(tab==std::string::npos)return false;
            fields[i]=line.substr(start,tab-start);start=tab+1;
        }
        if(line.find('\t',start)!=std::string::npos)return false;
        fields[10]=line.substr(start);
        CachedMedia row;row.path=fields[0];
        uint64_t platform=0;
        if(!decimal(fields[1],row.rootStamp)||!decimal(fields[2],row.payloadStamp)||
           !decimal(fields[3],row.bytes)||!decimal(fields[7],platform)||platform>2)return false;
        for(unsigned i=4;i<7;++i)if(fields[i]!="0"&&fields[i]!="1")return false;
        row.directory=fields[4]=="1";row.sizeKnown=fields[5]=="1";row.validPbp=fields[6]=="1";
        row.platform=static_cast<uint8_t>(platform);row.title=fields[8];row.id=fields[9];row.contentId=fields[10];
        if(!validMedia(row)||!seen.insert(row.path).second)return false;
        rows.push_back(std::move(row));
    }
    out=std::move(rows);return true;
}

}} // namespace psvitaalive::storage
