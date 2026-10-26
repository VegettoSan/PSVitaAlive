#pragma once
#include <string>

namespace psvitaalive { namespace storage {
// Pure ownership checks, testable without VitaSDK. Never accept broad ux0:data
// paths, image/catalog caches, settings or unrecognized user-created files.
inline bool isOwnedZipStagePath(const std::string& path) {
    const std::string ux="ux0:data/PSVA_ZIP_STAGE_";
    const std::string uma="uma0:data/PSVA_ZIP_STAGE_";
    size_t start=0;
    if(path.compare(0,ux.size(),ux)==0) start=ux.size();
    else if(path.compare(0,uma.size(),uma)==0) start=uma.size();
    else return false;
    if(start==path.size()) return false;
    for(size_t i=start;i<path.size();++i)
        if(!((path[i]>='0'&&path[i]<='9')||path[i]=='_')) return false;
    return true;
}
inline bool isOwnedTempName(const std::string& name) {
    if(name=="nested_source.vpk" || name=="nested_vpk_stage" || name=="work.bin") return true;
    return name.size()>8 && name.compare(0,4,"pkg_")==0 &&
           name.compare(name.size()-4,4,".pkg")==0 &&
           name.find('/')==std::string::npos && name.find('\\')==std::string::npos &&
           name.find("..")==std::string::npos;
}
}} // namespace psvitaalive::storage
