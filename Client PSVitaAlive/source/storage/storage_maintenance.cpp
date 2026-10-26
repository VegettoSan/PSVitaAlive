#include "storage/storage_maintenance.hpp"
#include "storage/storage_manager.hpp"
#include "network/startup_cleanup_policy.hpp"
#include "diagnostic_logger.hpp"
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace psvitaalive { namespace storage {
namespace {
constexpr const char* kJobs = "ux0:data/psvitaalive/downloads/jobs";
constexpr const char* kManual = "ux0:data/psvitaalive/manual";
constexpr const char* kTmp = "ux0:data/psvitaalive/tmp";
constexpr const char* kVpk = "ux0:data/psva_vpk";
constexpr size_t kMaxItems = 512;
constexpr unsigned kMaxDepth = 24;

bool starts(const std::string& s,const char* p) { return s.compare(0,std::strlen(p),p)==0; }
bool ends(const std::string& s,const char* p) { const size_t n=std::strlen(p); return s.size()>=n && s.compare(s.size()-n,n,p)==0; }
bool safeName(const std::string& s) {
    if(s.empty() || s=="." || s==".." || s.find('/')!=std::string::npos ||
       s.find('\\')!=std::string::npos || s.find(':')!=std::string::npos) return false;
    for(unsigned char c:s) if(c<32) return false;
    return true;
}
std::vector<std::string> entries(const std::string& dir) {
    std::vector<std::string> v;
    SceUID d=sceIoDopen(dir.c_str());
    if(d<0) return v;
    SceIoDirent e{};
    while(sceIoDread(d,&e)>0 && v.size()<4096) {
        if(safeName(e.d_name)) v.push_back(e.d_name);
    }
    sceIoDclose(d);
    return v;
}
uint64_t measure(const std::string& root) {
    StorageManager st;
    if(!st.exists(root)) return 0;
    struct Node { std::string path; unsigned depth; };
    std::vector<Node> stack{{root,0}};
    uint64_t bytes=0;
    uint32_t seen=0;
    while(!stack.empty() && ++seen<200000) {
        Node node=std::move(stack.back()); stack.pop_back();
        if(!st.isDirectory(node.path)) {
            const int64_t sz=st.fileSize(node.path);
            if(sz>0) bytes+=static_cast<uint64_t>(sz);
            continue;
        }
        if(node.depth>=kMaxDepth) continue;
        for(const auto& n:entries(node.path)) stack.push_back({node.path+"/"+n,node.depth+1});
    }
    return bytes;
}
bool eraseTree(const std::string& root,unsigned depth=0) {
    StorageManager st;
    if(!st.exists(root)) return true;
    if(!st.isDirectory(root)) return st.removeFile(root);
    if(depth>=kMaxDepth) return false;
    bool ok=true;
    for(const auto& name:entries(root))
        if(!eraseTree(root+"/"+name,depth+1)) ok=false;
    return ok && st.removeDirectory(root);
}
void add(UsageSnapshot& snap,const std::string& path,const std::string& label,bool saved,
         const std::vector<std::string>& paths={}) {
    if(snap.items.size()>=kMaxItems) return;
    UsageItem item;
    item.path=path; item.label=label; item.savedZip=saved;
    item.ownedPaths=paths.empty()?std::vector<std::string>{path}:paths;
    for(const auto& p:item.ownedPaths) item.bytes+=measure(p);
    if(saved) snap.savedZipBytes+=item.bytes;
    else snap.temporaryBytes+=item.bytes;
    snap.items.push_back(std::move(item));
}
unsigned readPartCount(const std::string& info) {
    StorageManager st;
    const int64_t sz=st.fileSize(info);
    if(sz<=0 || sz>8192) return 0;
    std::string data;
    if(!st.readTextFile(info,data)) return 0;
    const std::string key="Archive parts: ";
    const size_t i=data.find(key);
    if(i==std::string::npos) return 1; // previous single-ZIP sidecars
    const char c=i+key.size()<data.size()?data[i+key.size()]:'0';
    if(c=='1') return 1;
    if(c=='2') return 2;
    if(c=='4') return 4;
    return 0;
}
void scanSaved(UsageSnapshot& snap) {
    StorageManager st;
    for(const auto& folder:entries(kManual)) {
        const std::string dir=std::string(kManual)+"/"+folder;
        if(!st.isDirectory(dir)) continue;
        for(const auto& name:entries(dir)) {
            if(!ends(name,".zip.info.txt")) continue;
            const std::string info=dir+"/"+name;
            const unsigned count=readPartCount(info);
            if(count!=1 && count!=2 && count!=4) continue;
            const std::string zip=dir+"/"+name.substr(0,name.size()-9);
            std::vector<std::string> paths;
            bool complete=true;
            for(unsigned i=0;i<count;i++) {
                const std::string part=count==1?zip:
                    zip+"."+ (i+1<10?"00":"") + std::to_string(i+1);
                if(st.fileSize(part)<=0) {complete=false;break;}
                paths.push_back(part);
            }
            if(!complete) continue;
            paths.push_back(info);
            add(snap,info,"ZIP: "+folder+"/"+name.substr(0,name.size()-9),true,paths);
        }
    }
}
void scanJobs(UsageSnapshot& snap) {
    StorageManager st;
    for(const auto& name:entries(kJobs)) {
        if(!starts(name,"job_")) continue;
        const std::string dir=std::string(kJobs)+"/"+name;
        if(!st.isDirectory(dir)) continue;
        const bool kept=st.exists(dir+"/.psva_keep_zip");
        std::string metadata;
        st.readTextFile(dir+"/metadata.json",metadata);
        const bool legacy=metadata.find("\"cleanup_protocol\": 2")==std::string::npos &&
            metadata.find("\"state\": \"Completed\"")!=std::string::npos &&
            (metadata.find(".zip")!=std::string::npos || metadata.find(".ZIP")!=std::string::npos);
        add(snap,dir,kept||legacy?"Recovery job: "+name:"Download job: "+name,kept||legacy);
    }
}
void scanTmp(UsageSnapshot& snap) {
    StorageManager st;
    for(const auto& name:entries(kTmp)) {
        // Strict allowlist: do not delete a user-created file merely because it lives under tmp/.
        if(name!="nested_source.vpk" && name!="nested_vpk_stage" && name!="work.bin" &&
           !(starts(name,"pkg_") && ends(name,".pkg"))) continue;
        const std::string path=std::string(kTmp)+"/"+name;
        if(st.exists(path)) add(snap,path,"Temporary: "+name,false);
    }
}
void scanDeepStages(UsageSnapshot& snap,const char* root) {
    StorageManager st;
    for(const auto& name:entries(root)) {
        if(!starts(name,"PSVA_ZIP_STAGE_")) continue;
        const std::string path=std::string(root)+"/"+name;
        if(st.isDirectory(path)) add(snap,path,"ZIP staging: "+name,false);
    }
}
} // namespace

UsageSnapshot StorageMaintenance::scan() {
    UsageSnapshot snap;
    scanJobs(snap);
    scanTmp(snap);
    StorageManager st;
    if(st.isDirectory(kVpk)) add(snap,kVpk,"VPK promotion staging",false);
    scanDeepStages(snap,"ux0:data");
    scanDeepStages(snap,"uma0:data");
    scanSaved(snap);
    // Display caches for transparency without offering to delete them.
    snap.catalogBytes=measure("ux0:data/psvitaalive/cache/catalog");
    snap.imageBytes=measure("ux0:data/psvitaalive/cache/images");
    return snap;
}
bool StorageMaintenance::removeSelected(const UsageItem& selected) {
    // A fresh inventory checks both ownership and the exact set of known paths.
    const UsageSnapshot fresh=scan();
    const auto it=std::find_if(fresh.items.begin(),fresh.items.end(),
        [&](const UsageItem& item) {
            return item.path==selected.path && item.savedZip==selected.savedZip &&
                   item.ownedPaths==selected.ownedPaths;
        });
    if(it==fresh.items.end()) return false;
    bool ok=true;
    for(const auto& path:it->ownedPaths) {
        if(!eraseTree(path)) {
            ok=false;
            diagnostics::log("[StorageMaintenance] removal failed: "+path);
        }
    }
    return ok;
}
uint32_t StorageMaintenance::cleanupTransient() {
    const UsageSnapshot snap=scan();
    uint32_t removed=0;
    for(const auto& item:snap.items) {
        if(!item.savedZip && removeSelected(item)) ++removed;
    }
    diagnostics::log("[StorageMaintenance] startup transient cleanup removed="+std::to_string(removed));
    return removed;
}
}} // namespace psvitaalive::storage
