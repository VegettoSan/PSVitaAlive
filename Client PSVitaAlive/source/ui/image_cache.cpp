#include "ui/image_cache.hpp"
#include "diagnostic_logger.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"
#include <psp2/kernel/clib.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/net/netctl.h>
#include <png.h>
#include <jpeglib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <setjmp.h>
#include <vector>
namespace psvitaalive::ui { namespace {
constexpr const char* IMAGE_ROOT="ux0:data/psvitaalive/cache/images/v3";
constexpr const char* LEGACY_IMAGE_ROOT="ux0:data/psvitaalive/cache/images/v2";
constexpr const char* CATALOG_CACHE_ROOT="ux0:data/psvitaalive/cache/catalog";
constexpr const char* V3_LAYOUT_MARKER="ux0:data/psvitaalive/cache/images/v3/.per_image_v1";
constexpr int WORKER_PRIORITY=0x10000100,WORKER_STACK=128*1024,MAX_RETRIES=3;
// Fewer SSL rounds per image — UI should not burn 10× curl-35 on a missing icon.
// A failing Archive.org icon should not monopolize the image worker.
// Retain original retry budget for GitHub/GitLab/other image providers.
constexpr int IMAGE_HTTP_ATTEMPTS = 8;
constexpr int ARCHIVE_IMAGE_HTTP_ATTEMPTS = 3;

/* services/img removed — IA page thumb is often a screenshot, not icon0 */

// Cache-first browsing must remain usable while Wi-Fi is down.
// Fail open if NetCtl is unavailable (Vita3K / early startup).
bool imageNetworkConnected(){
    int state=0;
    int rc=sceNetCtlInetGetState(&state);
    if(rc<0){sceNetCtlInit();rc=sceNetCtlInetGetState(&state);}
    return rc<0 || state==SCE_NETCTL_STATE_CONNECTED;
}
constexpr uint64_t RETRY_COOLDOWN_US=3000000ULL;
constexpr size_t MAX_INTERACTIVE_QUEUE=12;
constexpr uint64_t IMAGE_PROGRESS_PUBLISH_INTERVAL_US=100000ULL; // UI progress <= 10 Hz
constexpr unsigned APP_IMAGE_MAX_DIM=128u,SCREENSHOT_IMAGE_MAX_DIM=256u;
constexpr uint64_t STARTUP_IMAGE_CACHE_LIMIT_BYTES=200ULL*1024ULL*1024ULL;
constexpr uint64_t STARTUP_IMAGE_CACHE_KEEP_BYTES=40ULL*1024ULL*1024ULL;
constexpr size_t STARTUP_PROGRESS_STRIDE=8;
unsigned maxImageDimForNamespace(const std::string&ns){return ns=="app"?APP_IMAGE_MAX_DIM:SCREENSHOT_IMAGE_MAX_DIM;}
uint32_t fnv1a(const std::string&v){uint32_t h=2166136261u;for(unsigned char c:v){h^=c;h*=16777619u;}return h;}
std::string hex32(uint32_t v){char b[16];sceClibSnprintf(b,sizeof(b),"%08X",v);return b;}
std::string extensionOf(const std::string&u){std::string c=u;size_t q=c.find('?');if(q!=std::string::npos)c.erase(q);size_t f=c.find('#');if(f!=std::string::npos)c.erase(f);size_t d=c.find_last_of('.');if(d==std::string::npos)return".img";std::string e=c.substr(d);for(char&x:e)if(x>='A'&&x<='Z')x=(char)(x-'A'+'a');if(e==".jpeg"||e==".jpg"||e==".png")return".png";return".img";}
std::string normalizeUrl(const std::string&u){if(u.rfind("https://",0)==0||u.rfind("http://",0)==0)return u;std::string p=u;while(p.rfind("../",0)==0)p.erase(0,3);return std::string("https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/")+p;}

struct TaggedImageUrl{std::string url;std::string catalogPrefix;std::string resourceKey;};
bool validCatalogPrefix(const std::string&p){return p=="H"||p=="PV"||p=="PSP"||p=="PS1";}
bool validResourceKey(const std::string&key){
    if(key.size()!=16)return false;
    for(char c:key){
        const bool digit=c>='0'&&c<='9';
        const bool upper=c>='A'&&c<='F';
        const bool lower=c>='a'&&c<='f';
        if(!digit&&!upper&&!lower)return false;
    }
    return true;
}
TaggedImageUrl splitCacheTag(const std::string&tagged){
    TaggedImageUrl out{tagged,"U",{}};
    const std::string hashMarker="#psva_cache=";
    const std::string ampMarker="&psva_cache=";
    const size_t hp=tagged.rfind(hashMarker),ap=tagged.rfind(ampMarker);
    size_t pos=std::string::npos,markerLen=0;
    if(hp!=std::string::npos&&(ap==std::string::npos||hp>ap)){pos=hp;markerLen=hashMarker.size();}
    else if(ap!=std::string::npos){pos=ap;markerLen=ampMarker.size();}
    if(pos==std::string::npos)return out;
    const std::string payload=tagged.substr(pos+markerLen);
    const size_t colon=payload.find(':');
    if(colon==std::string::npos)return out;
    const std::string prefix=payload.substr(0,colon);
    const std::string resource=payload.substr(colon+1);
    if(!validCatalogPrefix(prefix)||!validResourceKey(resource))return out;
    out.url=tagged.substr(0,pos);
    out.catalogPrefix=prefix;
    out.resourceKey=resource;
    return out;
}
std::string baseNameOf(const std::string&path){const size_t slash=path.find_last_of('/');return slash==std::string::npos?path:path.substr(slash+1);}
std::string parentDirOf(const std::string&path){const size_t slash=path.find_last_of('/');return slash==std::string::npos?std::string():path.substr(0,slash);}
std::string identityStemForPath(const std::string&path){
    const std::string name=baseNameOf(path);
    const size_t first=name.find('_');if(first==std::string::npos)return{};
    const size_t second=name.find('_',first+1);if(second==std::string::npos)return{};
    const size_t third=name.find('_',second+1);if(third==std::string::npos)return{};
    return name.substr(0,third+1);
}
bool pathMatchesIdentity(const std::string&path,const std::string&stem){return!stem.empty()&&baseNameOf(path).rfind(stem,0)==0;}
bool isObsoleteV3CatalogFile(const std::string&name){
    // Previous unpublished v3 draft: app_H_<urlhash>.png / shot_PV_<urlhash>.png.
    // Definitive v3 has an extra stable resource key before the URL hash.
    const size_t first=name.find('_');if(first==std::string::npos)return false;
    const std::string imageNs=name.substr(0,first);if(imageNs!="app"&&imageNs!="shot")return false;
    const size_t second=name.find('_',first+1);if(second==std::string::npos)return false;
    const size_t third=name.find('_',second+1);if(third!=std::string::npos)return false;
    return validCatalogPrefix(name.substr(first+1,second-first-1));
}
bool endsWith(const std::string&value,const char*suffix){if(!suffix)return false;const size_t n=std::strlen(suffix);return value.size()>=n&&value.compare(value.size()-n,n,suffix)==0;}
bool isCachePayloadName(const std::string&name){return endsWith(name,".png")||endsWith(name,".img")||endsWith(name,".normalized");}
bool isBucketDirectoryName(const std::string&name){
    if(name.size()!=2)return false;
    for(char c:name){
        const bool digit=c>='0'&&c<='9';
        const bool upper=c>='A'&&c<='F';
        const bool lower=c>='a'&&c<='f';
        if(!digit&&!upper&&!lower)return false;
    }
    return true;
}
uint64_t dateTimeKey(const SceDateTime&t){
    uint64_t v=t.year;
    v=v*13ULL+t.month;
    v=v*32ULL+t.day;
    v=v*24ULL+t.hour;
    v=v*60ULL+t.minute;
    v=v*60ULL+t.second;
    v=v*1000000ULL+t.microsecond;
    return v;
}
struct StartupCacheFile{std::string path;uint64_t size;uint64_t stamp;};
void accountCacheEntry(const std::string&full,const SceIoDirent&ent,uint64_t&total,std::vector<StartupCacheFile>*files,uint32_t&tempRemoved){
    const std::string name=ent.d_name;
    if(!isCachePayloadName(name)||SCE_S_ISDIR(ent.d_stat.st_mode))return;
    if(endsWith(name,".normalized")){
        if(sceIoRemove(full.c_str())>=0){++tempRemoved;return;}
    }
    if(ent.d_stat.st_size<=0)return;
    const uint64_t size=(uint64_t)ent.d_stat.st_size;
    total+=size;
    if(files){uint64_t stamp=dateTimeKey(ent.d_stat.st_mtime);if(stamp==0)stamp=dateTimeKey(ent.d_stat.st_ctime);files->push_back({full,size,stamp});}
}
void scanCachePayloadDirectory(const std::string&path,uint64_t&total,std::vector<StartupCacheFile>*files,uint32_t&tempRemoved){
    SceUID dir=sceIoDopen(path.c_str());if(dir<0)return;
    SceIoDirent ent;
    while(sceIoDread(dir,&ent)>0){
        if(std::strcmp(ent.d_name,".")==0||std::strcmp(ent.d_name,"..")==0||SCE_S_ISDIR(ent.d_stat.st_mode))continue;
        accountCacheEntry(path+"/"+ent.d_name,ent,total,files,tempRemoved);
    }
    sceIoDclose(dir);
}
bool removeTreeRecursive(const std::string&path,uint32_t*files,uint64_t*bytes){
    SceUID fd=sceIoDopen(path.c_str());
    if(fd<0)return false;
    SceIoDirent ent;
    while(sceIoDread(fd,&ent)>0){
        if(std::strcmp(ent.d_name,".")==0||std::strcmp(ent.d_name,"..")==0)continue;
        const std::string child=path+"/"+ent.d_name;
        if(SCE_S_ISDIR(ent.d_stat.st_mode))removeTreeRecursive(child,files,bytes);
        else if(sceIoRemove(child.c_str())>=0){if(files)++(*files);if(bytes&&ent.d_stat.st_size>0)*bytes+=(uint64_t)ent.d_stat.st_size;}
    }
    sceIoDclose(fd);
    sceIoRmdir(path.c_str());
    return true;
}
void cleanupUnpublishedV3Draft(){
    SceIoStat markerStat={};if(sceIoGetstat(V3_LAYOUT_MARKER,&markerStat)>=0&&markerStat.st_size>0)return;
    uint32_t removed=0;uint64_t freed=0;
    SceUID dir=sceIoDopen(IMAGE_ROOT);
    if(dir>=0){
        SceIoDirent ent;
        while(sceIoDread(dir,&ent)>0){
            if(std::strcmp(ent.d_name,".")==0||std::strcmp(ent.d_name,"..")==0||SCE_S_ISDIR(ent.d_stat.st_mode))continue;
            const std::string name=ent.d_name;
            const bool normalized=name.size()>=11&&name.compare(name.size()-11,11,".normalized")==0;
            if(!normalized&&!isObsoleteV3CatalogFile(name))continue;
            const std::string full=std::string(IMAGE_ROOT)+"/"+name;
            if(sceIoRemove(full.c_str())>=0){++removed;if(ent.d_stat.st_size>0)freed+=(uint64_t)ent.d_stat.st_size;}
        }
        sceIoDclose(dir);
    }
    const char* prefixes[]={"H","PV","PSP","PS1"};
    uint32_t manifests=0;
    for(const char*prefix:prefixes){
        const std::string path=std::string(CATALOG_CACHE_ROOT)+"/images_"+prefix+".manifest";
        if(sceIoRemove(path.c_str())>=0)++manifests;
        if(sceIoRemove((path+".new").c_str())>=0)++manifests;
    }
    const SceUID marker=sceIoOpen(V3_LAYOUT_MARKER,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    if(marker>=0){const char one='1';sceIoWrite(marker,&one,1);sceIoClose(marker);}
    if(removed||manifests){char m[220];sceClibSnprintf(m,sizeof(m),"[ImageCache] finalized v3 layout removed_old=%u manifests=%u freed=%llu",(unsigned)removed,(unsigned)manifests,(unsigned long long)freed);diagnostics::log(m);}
}
bool readMagic(const std::string&path,unsigned char*magic,size_t n){FILE*f=std::fopen(path.c_str(),"rb");if(!f)return false;size_t got=std::fread(magic,1,n,f);std::fclose(f);return got==n;}
bool cachedNormalizedPngLooksValid(const std::string&path,unsigned maxDim){unsigned char hdr[24]={};FILE*f=std::fopen(path.c_str(),"rb");if(!f)return false;size_t got=std::fread(hdr,1,sizeof(hdr),f);std::fclose(f);if(got!=sizeof(hdr))return false;static const unsigned char sig[8]={0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};if(std::memcmp(hdr,sig,sizeof(sig))!=0||std::memcmp(hdr+12,"IHDR",4)!=0)return false;auto be32=[](const unsigned char*p)->unsigned{return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|(unsigned)p[3];};unsigned w=be32(hdr+16),h=be32(hdr+20);return w>0&&h>0&&(maxDim==0||(w<=maxDim&&h<=maxDim));}
bool normalizePngForVita(const std::string&path,unsigned maxDim){png_image image{};image.version=PNG_IMAGE_VERSION;if(!png_image_begin_read_from_file(&image,path.c_str()))return false;if(image.width==0||image.height==0||image.width>2048||image.height>2048){png_image_free(&image);return false;}image.format=PNG_FORMAT_RGBA;size_t bytes=PNG_IMAGE_SIZE(image);if(bytes==0){png_image_free(&image);return false;}std::vector<unsigned char>pixels(bytes);if(!png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr)){png_image_free(&image);return false;}unsigned srcW=image.width,srcH=image.height,dstW=srcW,dstH=srcH;if(maxDim>0&&(srcW>maxDim||srcH>maxDim)){double scale=std::min((double)maxDim/(double)srcW,(double)maxDim/(double)srcH);dstW=std::max(1u,(unsigned)(srcW*scale+0.5));dstH=std::max(1u,(unsigned)(srcH*scale+0.5));}std::vector<unsigned char>outPixels((size_t)dstW*dstH*4);for(unsigned y=0;y<dstH;++y){unsigned sy=(unsigned)((uint64_t)y*srcH/dstH);for(unsigned x=0;x<dstW;++x){unsigned sx=(unsigned)((uint64_t)x*srcW/dstW);const unsigned char*src=&pixels[((size_t)sy*srcW+sx)*4];unsigned char*dst=&outPixels[((size_t)y*dstW+x)*4];dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=src[3];}}std::string temp=path+".normalized";png_image output{};output.version=PNG_IMAGE_VERSION;output.width=dstW;output.height=dstH;output.format=PNG_FORMAT_RGBA;if(!png_image_write_to_file(&output,temp.c_str(),0,outPixels.data(),0,nullptr)){png_image_free(&image);png_image_free(&output);sceIoRemove(temp.c_str());return false;}png_image_free(&image);png_image_free(&output);sceIoRemove(path.c_str());if(sceIoRename(temp.c_str(),path.c_str())<0){sceIoRemove(temp.c_str());return false;}return true;}
struct JpegError{jpeg_error_mgr pub;jmp_buf jump;};
void jpegErrorExit(j_common_ptr c){JpegError*e=reinterpret_cast<JpegError*>(c->err);longjmp(e->jump,1);}
bool normalizeJpegForVita(const std::string&path,unsigned maxDim){FILE*in=std::fopen(path.c_str(),"rb");if(!in)return false;jpeg_decompress_struct cinfo{};JpegError jerr{};cinfo.err=jpeg_std_error(&jerr.pub);jerr.pub.error_exit=jpegErrorExit;if(setjmp(jerr.jump)){jpeg_destroy_decompress(&cinfo);std::fclose(in);return false;}jpeg_create_decompress(&cinfo);jpeg_stdio_src(&cinfo,in);jpeg_read_header(&cinfo,TRUE);cinfo.out_color_space=JCS_RGB;jpeg_start_decompress(&cinfo);if(cinfo.output_width==0||cinfo.output_height==0||cinfo.output_width>2048||cinfo.output_height>2048){jpeg_abort_decompress(&cinfo);jpeg_destroy_decompress(&cinfo);std::fclose(in);return false;}size_t rowBytes=(size_t)cinfo.output_width*3;std::vector<unsigned char>pixels(rowBytes*(size_t)cinfo.output_height);while(cinfo.output_scanline<cinfo.output_height){JSAMPROW row=pixels.data()+(size_t)cinfo.output_scanline*rowBytes;jpeg_read_scanlines(&cinfo,&row,1);}unsigned w=cinfo.output_width,h=cinfo.output_height;jpeg_finish_decompress(&cinfo);jpeg_destroy_decompress(&cinfo);std::fclose(in);unsigned dw=w,dh=h;if(maxDim>0&&(w>maxDim||h>maxDim)){double scale=std::min((double)maxDim/(double)w,(double)maxDim/(double)h);dw=std::max(1u,(unsigned)(w*scale+0.5));dh=std::max(1u,(unsigned)(h*scale+0.5));}std::vector<unsigned char>outPixels((size_t)dw*dh*3);for(unsigned y=0;y<dh;++y){unsigned sy=(unsigned)((uint64_t)y*h/dh);for(unsigned x=0;x<dw;++x){unsigned sx=(unsigned)((uint64_t)x*w/dw);const unsigned char*src=&pixels[((size_t)sy*w+sx)*3];unsigned char*dst=&outPixels[((size_t)y*dw+x)*3];dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];}}std::string temp=path+".normalized";png_image out{};out.version=PNG_IMAGE_VERSION;out.width=dw;out.height=dh;out.format=PNG_FORMAT_RGB;if(!png_image_write_to_file(&out,temp.c_str(),0,outPixels.data(),0,nullptr)){sceIoRemove(temp.c_str());return false;}sceIoRemove(path.c_str());if(sceIoRename(temp.c_str(),path.c_str())<0){sceIoRemove(temp.c_str());return false;}return true;}
bool normalizeImageForVita(const std::string&path,unsigned maxDim){unsigned char magic[12]={};if(!readMagic(path,magic,sizeof(magic)))return false;bool png=magic[0]==0x89&&magic[1]==0x50&&magic[2]==0x4E&&magic[3]==0x47&&magic[4]==0x0D&&magic[5]==0x0A&&magic[6]==0x1A&&magic[7]==0x0A;bool jpg=magic[0]==0xFF&&magic[1]==0xD8&&magic[2]==0xFF;if(png)return normalizePngForVita(path,maxDim);if(jpg)return normalizeJpegForVita(path,maxDim);return false;}
}
ImageCache::ImageCache()=default;ImageCache::~ImageCache(){shutdown();}
bool ImageCache::ensureDirectory(const std::string&p)const{StorageManager s;return s.createDirectories(p);}
void ImageCache::enforceStartupDiskLimit(const StartupMaintenanceProgressFn&startupProgress){
    if(startupProgress)startupProgress(StartupMaintenancePhase::Checking,0,1);

    uint64_t totalBytes=0;uint32_t tempRemoved=0;
    std::vector<std::string>bucketDirs;
    SceUID root=sceIoDopen(IMAGE_ROOT);
    if(root>=0){
        SceIoDirent ent;
        while(sceIoDread(root,&ent)>0){
            if(std::strcmp(ent.d_name,".")==0||std::strcmp(ent.d_name,"..")==0)continue;
            const std::string name=ent.d_name;
            const std::string full=std::string(IMAGE_ROOT)+"/"+name;
            if(SCE_S_ISDIR(ent.d_stat.st_mode)){
                if(isBucketDirectoryName(name))bucketDirs.push_back(full);
                continue;
            }
            if(name==".per_image_v1")continue;
            accountCacheEntry(full,ent,totalBytes,nullptr,tempRemoved);
        }
        sceIoDclose(root);
    }
    std::sort(bucketDirs.begin(),bucketDirs.end());
    const uint64_t scanTotal=(uint64_t)bucketDirs.size()+1ULL;
    if(startupProgress)startupProgress(StartupMaintenancePhase::Checking,1,scanTotal);
    for(size_t i=0;i<bucketDirs.size();++i){
        scanCachePayloadDirectory(bucketDirs[i],totalBytes,nullptr,tempRemoved);
        const uint64_t done=(uint64_t)i+2ULL;
        if(startupProgress&&(done==scanTotal||done%STARTUP_PROGRESS_STRIDE==0))startupProgress(StartupMaintenancePhase::Checking,done,scanTotal);
    }

    {char m[220];sceClibSnprintf(m,sizeof(m),"[ImageCache] startup cache size=%llu limit=%llu temp_removed=%u",(unsigned long long)totalBytes,(unsigned long long)STARTUP_IMAGE_CACHE_LIMIT_BYTES,(unsigned)tempRemoved);diagnostics::log(m);}
    if(totalBytes<=STARTUP_IMAGE_CACHE_LIMIT_BYTES){if(startupProgress)startupProgress(StartupMaintenancePhase::Ready,1,1);return;}

    if(startupProgress)startupProgress(StartupMaintenancePhase::Cleaning,0,1);
    std::vector<StartupCacheFile>files;
    uint64_t collectedBytes=0;uint32_t secondPassTempRemoved=0;
    root=sceIoDopen(IMAGE_ROOT);
    if(root>=0){
        SceIoDirent ent;
        while(sceIoDread(root,&ent)>0){
            if(std::strcmp(ent.d_name,".")==0||std::strcmp(ent.d_name,"..")==0||SCE_S_ISDIR(ent.d_stat.st_mode))continue;
            const std::string name=ent.d_name;if(name==".per_image_v1")continue;
            accountCacheEntry(std::string(IMAGE_ROOT)+"/"+name,ent,collectedBytes,&files,secondPassTempRemoved);
        }
        sceIoDclose(root);
    }
    for(const std::string&dir:bucketDirs)scanCachePayloadDirectory(dir,collectedBytes,&files,secondPassTempRemoved);
    totalBytes=collectedBytes;
    if(totalBytes<=STARTUP_IMAGE_CACHE_KEEP_BYTES||files.empty()){if(startupProgress)startupProgress(StartupMaintenancePhase::Ready,1,1);return;}

    std::sort(files.begin(),files.end(),[](const StartupCacheFile&a,const StartupCacheFile&b){if(a.stamp!=b.stamp)return a.stamp<b.stamp;return a.path<b.path;});
    uint64_t simulated=totalBytes;size_t planned=0;
    for(const StartupCacheFile&f:files){if(simulated<=STARTUP_IMAGE_CACHE_KEEP_BYTES)break;simulated=f.size>=simulated?0:simulated-f.size;++planned;}
    if(planned==0){if(startupProgress)startupProgress(StartupMaintenancePhase::Ready,1,1);return;}

    {char m[220];sceClibSnprintf(m,sizeof(m),"[ImageCache] startup trim begin before=%llu keep=%llu planned_files=%u",(unsigned long long)totalBytes,(unsigned long long)STARTUP_IMAGE_CACHE_KEEP_BYTES,(unsigned)planned);diagnostics::log(m);}
    size_t completed=0,workTotal=planned,failed=0;const uint64_t beforeBytes=totalBytes;
    if(startupProgress)startupProgress(StartupMaintenancePhase::Cleaning,0,(uint64_t)workTotal);
    for(size_t i=0;i<files.size()&&totalBytes>STARTUP_IMAGE_CACHE_KEEP_BYTES;++i){
        if(i>=workTotal)workTotal=i+1;
        const StartupCacheFile&f=files[i];
        if(sceIoRemove(f.path.c_str())>=0){totalBytes=f.size>=totalBytes?0:totalBytes-f.size;++completed;}
        else ++failed;
        if(startupProgress&&(((i+1)%4)==0||totalBytes<=STARTUP_IMAGE_CACHE_KEEP_BYTES||i+1==files.size()))startupProgress(StartupMaintenancePhase::Cleaning,(uint64_t)completed,(uint64_t)workTotal);
    }
    {char m[260];sceClibSnprintf(m,sizeof(m),"[ImageCache] startup trim complete removed=%u failed=%u before=%llu after=%llu target=%llu",(unsigned)completed,(unsigned)failed,(unsigned long long)beforeBytes,(unsigned long long)totalBytes,(unsigned long long)STARTUP_IMAGE_CACHE_KEEP_BYTES);diagnostics::log(m);}
    if(startupProgress)startupProgress(StartupMaintenancePhase::Ready,1,1);
}
bool ImageCache::init(const StartupMaintenanceProgressFn&startupProgress){
    if(workerThread_>=0)return true;
    SceIoStat legacyStat={};
    if(sceIoGetstat(LEGACY_IMAGE_ROOT,&legacyStat)>=0&&SCE_S_ISDIR(legacyStat.st_mode)){
        uint32_t files=0;uint64_t bytes=0;
        removeTreeRecursive(LEGACY_IMAGE_ROOT,&files,&bytes);
        char m[180];sceClibSnprintf(m,sizeof(m),"[ImageCache] migrated cache v2->v3 removed=%u freed=%llu",(unsigned)files,(unsigned long long)bytes);diagnostics::log(m);
    }
    if(!ensureDirectory(IMAGE_ROOT))return false;
    cleanupUnpublishedV3Draft();
    enforceStartupDiskLimit(startupProgress);
    ensureDirectory("ux0:data/psvitaalive/logs");
    mutex_=sceKernelCreateMutex("PSVitaAliveImageCache",0,0,nullptr);if(mutex_<0)return false;stopping_=false;cancelRequested_=false;workerBusy_=false;workerThread_=sceKernelCreateThread("PSVitaAliveImageWorker",&ImageCache::workerEntry,WORKER_PRIORITY,WORKER_STACK,0,0,nullptr);if(workerThread_<0){sceKernelDeleteMutex(mutex_);mutex_=-1;return false;}ImageCache*self=this;int r=sceKernelStartThread(workerThread_,sizeof(self),&self);if(r<0){sceKernelDeleteThread(workerThread_);workerThread_=-1;sceKernelDeleteMutex(mutex_);mutex_=-1;return false;}diagnostics::log("[ImageCache] worker initialized cache=v3 replacement=per-image catalogs=H,PV,PSP,PS1");return true;
}
void ImageCache::shutdown(){stopping_=true;cancelRequested_=true;if(workerThread_>=0){sceKernelWaitThreadEnd(workerThread_,nullptr,nullptr);sceKernelDeleteThread(workerThread_);workerThread_=-1;}if(mutex_>=0){sceKernelDeleteMutex(mutex_);mutex_=-1;}queue_.clear();pending_.clear();ready_.clear();failed_.clear();retryAfter_.clear();workerBusy_=false;currentFile_.clear();currentPath_.clear();currentDownloaded_=0;currentTotal_=0;currentSpeed_=0;diagnostics::log("[ImageCache] shutdown");}
bool ImageCache::contains(const std::vector<std::string>&v,const std::string&s)const{return std::find(v.begin(),v.end(),s)!=v.end();}
std::string ImageCache::makePath(const std::string&url,const std::string&ns)const{
    const TaggedImageUrl tagged=splitCacheTag(url);
    const std::string clean=normalizeUrl(tagged.url);
    const std::string imageNs=ns.empty()?"misc":ns;
    if(!tagged.resourceKey.empty()){
        const std::string bucket=tagged.resourceKey.substr(0,2);
        return std::string(IMAGE_ROOT)+"/"+bucket+"/"+imageNs+"_"+tagged.catalogPrefix+"_"+tagged.resourceKey+"_"+hex32(fnv1a(clean))+extensionOf(clean);
    }
    return std::string(IMAGE_ROOT)+"/"+imageNs+"_U_"+hex32(fnv1a(clean))+extensionOf(clean);
}
std::string ImageCache::pathFor(const std::string& url, const std::string& namespaceName) const {
    if (url.empty()) return {};
    return makePath(url, namespaceName);
}

void ImageCache::pruneSupersededVersions(const std::string&currentPath){
    const std::string stem=identityStemForPath(currentPath);if(stem.empty()||mutex_<0)return;
    const std::string dirPath=parentDirOf(currentPath);if(dirPath.empty())return;
    std::string activePath;
    sceKernelLockMutex(mutex_,1,nullptr);activePath=currentPath_;sceKernelUnlockMutex(mutex_,1);

    std::vector<std::string>removedPaths;uint64_t freed=0;
    SceUID dir=sceIoDopen(dirPath.c_str());if(dir<0)return;
    SceIoDirent ent;
    const std::string currentName=baseNameOf(currentPath);
    while(sceIoDread(dir,&ent)>0){
        if(std::strcmp(ent.d_name,".")==0||std::strcmp(ent.d_name,"..")==0||SCE_S_ISDIR(ent.d_stat.st_mode))continue;
        const std::string name=ent.d_name;
        if(name==currentName||name.rfind(stem,0)!=0)continue;
        const std::string full=dirPath+"/"+name;
        if(full==activePath)continue;
        if(sceIoRemove(full.c_str())>=0){removedPaths.push_back(full);if(ent.d_stat.st_size>0)freed+=(uint64_t)ent.d_stat.st_size;}
    }
    sceIoDclose(dir);
    if(removedPaths.empty())return;

    sceKernelLockMutex(mutex_,1,nullptr);
    for(const std::string&p:removedPaths){
        ready_.erase(std::remove(ready_.begin(),ready_.end(),p),ready_.end());
        failed_.erase(std::remove(failed_.begin(),failed_.end(),p),failed_.end());
        pending_.erase(std::remove(pending_.begin(),pending_.end(),p),pending_.end());
        retryAfter_.erase(p);
    }
    sceKernelUnlockMutex(mutex_,1);
    char m[240];sceClibSnprintf(m,sizeof(m),"[ImageCache] image replacement identity=%s removed=%u freed=%llu current=%s",stem.c_str(),(unsigned)removedPaths.size(),(unsigned long long)freed,currentPath.c_str());diagnostics::log(m);
}

void ImageCache::cancelQueuedExcept(const std::unordered_set<std::string>& keep) {
    if (mutex_ < 0) return;
    const uint64_t now = sceKernelGetSystemTimeWide();
    sceKernelLockMutex(mutex_, 1, nullptr);
    size_t removed = 0;
    std::vector<Job> keptJobs;
    keptJobs.reserve(queue_.size());
    for (const Job& j : queue_) {
        if (keep.find(j.path) != keep.end()) {
            keptJobs.push_back(j);
        } else {
            pending_.erase(std::remove(pending_.begin(), pending_.end(), j.path), pending_.end());
            ++removed;
        }
    }
    queue_.swap(keptJobs);

    // Scroll animations briefly report an empty/old viewport. Previously that
    // immediately cancelled the active transfer, sometimes multiple times in
    // one second, preventing small icon downloads from ever finishing.
    bool cancelActive = false;
    if (currentPath_.empty() || keep.find(currentPath_) != keep.end()) {
        activeOffscreenSinceUs_ = 0;
    } else if (activeOffscreenSinceUs_ == 0) {
        activeOffscreenSinceUs_ = now;
    } else if (!cancelRequested_ && now >= activeOffscreenSinceUs_ &&
               now - activeOffscreenSinceUs_ >= 1500000ULL) {
        cancelRequested_ = true;
        cancelActive = true;
    }
    sceKernelUnlockMutex(mutex_, 1);
    (void)removed;
    (void)cancelActive; // Normal viewport pruning is not an error-report event.
}

std::string ImageCache::request(const std::string&url,const std::string&ns){
    if(url.empty()||mutex_<0)return{};
    const TaggedImageUrl tagged=splitCacheTag(url);
    const std::string full=normalizeUrl(tagged.url);
    const std::string path=makePath(url,ns);
    const std::string identityStem=identityStemForPath(path);
    const uint64_t now=sceKernelGetSystemTimeWide();

    size_t supersededQueued=0;bool cancelledSupersededActive=false;
    sceKernelLockMutex(mutex_,1,nullptr);
    if(!identityStem.empty()){
        for(size_t i=0;i<queue_.size();){
            if(queue_[i].path!=path&&pathMatchesIdentity(queue_[i].path,identityStem)){
                pending_.erase(std::remove(pending_.begin(),pending_.end(),queue_[i].path),pending_.end());
                queue_.erase(queue_.begin()+i);++supersededQueued;
            }else ++i;
        }
        if(!currentPath_.empty()&&currentPath_!=path&&pathMatchesIdentity(currentPath_,identityStem)){
            cancelRequested_=true;cancelledSupersededActive=true;
        }
    }

    if(contains(ready_,path)){sceKernelUnlockMutex(mutex_,1);return path;}
    if(contains(pending_,path)){sceKernelUnlockMutex(mutex_,1);return path;}
    if(contains(failed_,path)){
        auto it=retryAfter_.find(path);
        if(it!=retryAfter_.end()&&now<it->second){sceKernelUnlockMutex(mutex_,1);return path;}
        failed_.erase(std::remove(failed_.begin(),failed_.end(),path),failed_.end());
        retryAfter_[path]=now+RETRY_COOLDOWN_US;
        queue_.push_back({full,path,0});pending_.push_back(path);
        sceKernelUnlockMutex(mutex_,1);
        if(supersededQueued||cancelledSupersededActive){char m[180];sceClibSnprintf(m,sizeof(m),"[ImageCache] superseded work identity=%s queued=%u active_cancel=%d",identityStem.c_str(),(unsigned)supersededQueued,cancelledSupersededActive?1:0);diagnostics::log(m);}
        return path;
    }
    sceKernelUnlockMutex(mutex_,1);

    if(supersededQueued||cancelledSupersededActive){char m[180];sceClibSnprintf(m,sizeof(m),"[ImageCache] superseded work identity=%s queued=%u active_cancel=%d",identityStem.c_str(),(unsigned)supersededQueued,cancelledSupersededActive?1:0);diagnostics::log(m);}

    SceIoStat st={};
    if(sceIoGetstat(path.c_str(),&st)>=0&&st.st_size>0){
        const bool validCached=cachedNormalizedPngLooksValid(path,maxImageDimForNamespace(ns));
        if(validCached){
            sceKernelLockMutex(mutex_,1,nullptr);if(!contains(ready_,path))ready_.push_back(path);retryAfter_.erase(path);sceKernelUnlockMutex(mutex_,1);
            pruneSupersededVersions(path);
            return path;
        }
        sceIoRemove(path.c_str());
    }

    // Disk cache directories are ensured by the image worker, not the UI.
    // A cold cache can make sceIoMkdir/createDirectories stall for >1 second.
    sceKernelLockMutex(mutex_,1,nullptr);
    if(networkPaused_){sceKernelUnlockMutex(mutex_,1);return path;}
    const bool queued=std::any_of(queue_.begin(),queue_.end(),[&](const Job&j){return j.path==path;});
    if(!queued&&!contains(pending_,path)){
        if(!bulkPreload_&&queue_.size()>=MAX_INTERACTIVE_QUEUE){
            const Job dropped=queue_.back();queue_.pop_back();pending_.erase(std::remove(pending_.begin(),pending_.end(),dropped.path),pending_.end());(void)dropped;
        }
        pending_.push_back(path);if(bulkPreload_)queue_.push_back({full,path,0});else queue_.insert(queue_.begin(),{full,path,0});
    }
    sceKernelUnlockMutex(mutex_,1);
    return path;
}
void ImageCache::preload(const std::vector<std::string>&urls,const std::string&ns){if(mutex_<0||urls.empty())return;sceKernelLockMutex(mutex_,1,nullptr);bulkPreload_=true;sceKernelUnlockMutex(mutex_,1);size_t queued=0;for(const auto&url:urls){if(url.empty())continue;std::string before=request(url,ns);if(!before.empty())++queued;}sceKernelLockMutex(mutex_,1,nullptr);bulkPreload_=false;sceKernelUnlockMutex(mutex_,1);if(queued){char m[160];sceClibSnprintf(m,sizeof(m),"[ImageCache] preload requested ns=%s count=%u",ns.c_str(),(unsigned)queued);diagnostics::log(m);}}
bool ImageCache::isReady(const std::string&p)const{if(p.empty()||mutex_<0)return false;sceKernelLockMutex(mutex_,1,nullptr);bool r=contains(ready_,p);sceKernelUnlockMutex(mutex_,1);return r;}
bool ImageCache::isFailed(const std::string&p)const{if(mutex_<0||p.empty())return false;sceKernelLockMutex(mutex_,1,nullptr);bool r=contains(failed_,p);sceKernelUnlockMutex(mutex_,1);return r;}
bool ImageCache::isPending(const std::string& localPath) const {
    if (mutex_ < 0 || localPath.empty()) return false;
    sceKernelLockMutex(mutex_, 1, nullptr);
    const bool pending = contains(pending_, localPath) || currentPath_ == localPath;
    sceKernelUnlockMutex(mutex_, 1);
    return pending;
}

bool ImageCache::isCached(const std::string&url,const std::string&ns)const{if(mutex_<0||url.empty())return false;const std::string path=makePath(url,ns);SceIoStat st={};return sceIoGetstat(path.c_str(),&st)>=0&&st.st_size>0;}
ImageCache::ProgressSnapshot ImageCache::progress()const{
    ProgressSnapshot s;
    if(mutex_<0)return s;
    sceKernelLockMutex(mutex_,1,nullptr);
    s.active=!currentPath_.empty();
    s.downloaded=currentDownloaded_;
    s.total=currentTotal_;
    s.speed=currentSpeed_;
    s.completedBytes=completedBytes_;
    s.knownTotalBytes=completedTotalBytes_;
    if(s.active&&s.total>0)s.knownTotalBytes+=s.total;
    s.fileName=currentFile_;
    s.localPath=currentPath_;
    sceKernelUnlockMutex(mutex_,1);
    return s;
}
void ImageCache::resetProgress(){if(mutex_<0)return;sceKernelLockMutex(mutex_,1,nullptr);completedBytes_=0;completedTotalBytes_=0;currentDownloaded_=0;currentTotal_=0;currentSpeed_=0;currentFile_.clear();currentPath_.clear();sceKernelUnlockMutex(mutex_,1);}
void ImageCache::cancelAll(){if(mutex_<0)return;sceKernelLockMutex(mutex_,1,nullptr);const bool active=workerBusy_;const size_t queued=queue_.size();for(const Job&j:queue_)pending_.erase(std::remove(pending_.begin(),pending_.end(),j.path),pending_.end());queue_.clear();cancelRequested_=active;sceKernelUnlockMutex(mutex_,1);(void)queued; /* Routine image cancellation during navigation is intentionally silent. */}
bool ImageCache::isQuiescentForIme() const {
    if(mutex_<0) return true; // image worker was never initialized
    sceKernelLockMutex(mutex_,1,nullptr);
    const bool quiet=networkPaused_ && !workerBusy_ && currentPath_.empty();
    sceKernelUnlockMutex(mutex_,1);
    return quiet;
}
void ImageCache::cancelQueuedRequests(){if(mutex_<0)return;sceKernelLockMutex(mutex_,1,nullptr);std::vector<std::string>cancelled;cancelled.reserve(queue_.size());for(const Job&j:queue_)cancelled.push_back(j.path);const size_t count=queue_.size();queue_.clear();for(const auto&p:cancelled)pending_.erase(std::remove(pending_.begin(),pending_.end(),p),pending_.end());sceKernelUnlockMutex(mutex_,1);(void)count;}
void ImageCache::markReady(const std::string&p){sceKernelLockMutex(mutex_,1,nullptr);if(!contains(ready_,p))ready_.push_back(p);pending_.erase(std::remove(pending_.begin(),pending_.end(),p),pending_.end());failed_.erase(std::remove(failed_.begin(),failed_.end(),p),failed_.end());retryAfter_.erase(p);sceKernelUnlockMutex(mutex_,1);}
void ImageCache::markFailed(const std::string&p,uint64_t cooldownUs){sceKernelLockMutex(mutex_,1,nullptr);pending_.erase(std::remove(pending_.begin(),pending_.end(),p),pending_.end());if(!contains(failed_,p))failed_.push_back(p);retryAfter_[p]=sceKernelGetSystemTimeWide()+(cooldownUs?cooldownUs:RETRY_COOLDOWN_US);sceKernelUnlockMutex(mutex_,1);}
int ImageCache::workerEntry(SceSize a,void*arg){(void)a;ImageCache*self=nullptr;if(arg)std::memcpy(&self,arg,sizeof(self));return self?self->workerMain():-1;}
void ImageCache::setNetworkPaused(bool paused){if(mutex_>=0)sceKernelLockMutex(mutex_,1,nullptr);const bool changed=(networkPaused_!=paused);networkPaused_=paused;if(mutex_>=0)sceKernelUnlockMutex(mutex_,1);if(changed){if(paused)diagnostics::log("[ImageCache] network paused (install/download active)");else diagnostics::log("[ImageCache] network resumed");}}int ImageCache::workerMain(){HttpClient http;if(http.init()!=HttpResult::Ok){diagnostics::log("[ImageCache] HTTP initialization failed");return-1;}while(!stopping_){bool paused=false;if(mutex_>=0){sceKernelLockMutex(mutex_,1,nullptr);paused=networkPaused_;sceKernelUnlockMutex(mutex_,1);}if(paused){sceKernelDelayThread(100*1000);continue;}
    // Do not spend eight curl DNS retries on each image while offline.
    // Keep requests queued and retry after connectivity returns.
    bool queuedForNetwork=false;
    sceKernelLockMutex(mutex_,1,nullptr);
    queuedForNetwork=!queue_.empty();
    sceKernelUnlockMutex(mutex_,1);
    if(queuedForNetwork && !imageNetworkConnected()){
        sceKernelDelayThread(500*1000);
        continue;
    }
    Job job;bool have=false;sceKernelLockMutex(mutex_,1,nullptr);if(!networkPaused_&&!queue_.empty()){job=queue_.front();queue_.erase(queue_.begin());have=true;workerBusy_=true;const size_t slash=job.url.find_last_of('/');currentFile_=(slash==std::string::npos?job.url:job.url.substr(slash+1));currentPath_=job.path;activeOffscreenSinceUs_=0;currentDownloaded_=0;currentTotal_=0;currentSpeed_=0;}sceKernelUnlockMutex(mutex_,1);if(!have){sceKernelDelayThread(50*1000);continue;}uint64_t latestDownloaded=0,latestTotal=0,latestSpeed=0,lastProgressPublishUs=0;HttpProgressFn onProgress=[this,&latestDownloaded,&latestTotal,&latestSpeed,&lastProgressPublishUs](const HttpProgress&p){latestDownloaded=p.downloaded;latestTotal=p.total;latestSpeed=p.bytesPerSecond;const uint64_t now=sceKernelGetSystemTimeWide();const bool finalKnown=p.total>0&&p.downloaded>=p.total;if(!finalKnown&&lastProgressPublishUs!=0&&now-lastProgressPublishUs<IMAGE_PROGRESS_PUBLISH_INTERVAL_US)return;lastProgressPublishUs=now;if(mutex_<0)return;sceKernelLockMutex(mutex_,1,nullptr);currentDownloaded_=latestDownloaded;currentTotal_=latestTotal;currentSpeed_=latestSpeed;sceKernelUnlockMutex(mutex_,1);};HttpCancelFn shouldCancel=[this](){if(mutex_<0)return true;sceKernelLockMutex(mutex_,1,nullptr);const bool c=cancelRequested_||stopping_;sceKernelUnlockMutex(mutex_,1);return c;};/* A: only real catalog URL (icon0/shots). B: IMAGE_HTTP_ATTEMPTS retries to fill cache. */const bool archiveImage=job.url.find("archive.org")!=std::string::npos;
    const int attempts=archiveImage?ARCHIVE_IMAGE_HTTP_ATTEMPTS:IMAGE_HTTP_ATTEMPTS;
    // Create the parent bucket here, off the render thread, before curl opens
    // the destination. Keep the same on-disk v3 layout and failure handling.
    const std::string parent=parentDirOf(job.path);
    const bool directoryReady=parent.empty()||ensureDirectory(parent);
    if(!directoryReady)diagnostics::log(std::string("[ImageCache] cannot create image bucket path=")+parent);
    HttpResult r=directoryReady?http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,attempts):HttpResult::IoError;
    if(directoryReady&&archiveImage){/* archive.org image spacing */sceKernelDelayThread(150*1000);}const uint64_t doneBytes=latestDownloaded,doneTotal=latestTotal;sceKernelLockMutex(mutex_,1,nullptr);const bool cancelled=cancelRequested_||r==HttpResult::Cancelled||stopping_;sceKernelUnlockMutex(mutex_,1);if(cancelled){sceIoRemove(job.path.c_str());sceKernelLockMutex(mutex_,1,nullptr);pending_.erase(std::remove(pending_.begin(),pending_.end(),job.path),pending_.end());currentFile_.clear();currentPath_.clear();currentDownloaded_=0;currentTotal_=0;currentSpeed_=0;cancelRequested_=false;workerBusy_=false;sceKernelUnlockMutex(mutex_,1);/* routine off-screen image cancellation */continue;}bool valid=false;if(r==HttpResult::Ok){SceIoStat st={};valid=sceIoGetstat(job.path.c_str(),&st)>=0&&st.st_size>0;}const uint64_t normStart=sceKernelGetSystemTimeWide();if(valid)valid=normalizeImageForVita(job.path,job.path.find("/app_")!=std::string::npos?APP_IMAGE_MAX_DIM:SCREENSHOT_IMAGE_MAX_DIM);const uint64_t normUs=sceKernelGetSystemTimeWide()-normStart;if(valid&&normUs>=150000ULL){char pm[220];sceClibSnprintf(pm,sizeof(pm),"[Perf] image normalize slow us=%llu path=%s",(unsigned long long)normUs,job.path.c_str());diagnostics::log(pm);}sceKernelLockMutex(mutex_,1,nullptr);const bool cancelledAfterNormalize=cancelRequested_||stopping_;if(!cancelledAfterNormalize&&r==HttpResult::Ok&&valid){completedBytes_+=doneBytes;if(doneTotal>0)completedTotalBytes_+=doneTotal;}currentFile_.clear();currentPath_.clear();currentDownloaded_=0;currentTotal_=0;currentSpeed_=0;cancelRequested_=false;sceKernelUnlockMutex(mutex_,1);if(cancelledAfterNormalize){sceIoRemove(job.path.c_str());sceKernelLockMutex(mutex_,1,nullptr);pending_.erase(std::remove(pending_.begin(),pending_.end(),job.path),pending_.end());cancelRequested_=false;workerBusy_=false;sceKernelUnlockMutex(mutex_,1);/* routine cancellation after normalization */continue;}if(valid){markReady(job.path);pruneSupersededVersions(job.path);/* Successful image downloads are silent. Errors remain logged below. */}else{sceIoRemove(job.path.c_str());char m[1000];sceClibSnprintf(m,sizeof(m),"[ImageCache] failed url=%s path=%s attempt=%d http=%d error=%s",job.url.c_str(),job.path.c_str(),job.attempt+1,http.lastStatusCode(),http.lastError().c_str());diagnostics::log(m);if(job.attempt+1<MAX_RETRIES&&!stopping_){sceKernelDelayThread((job.attempt+1)*250*1000);job.attempt++;sceKernelLockMutex(mutex_,1,nullptr);queue_.push_back(job);sceKernelUnlockMutex(mutex_,1);}else {
            // When an Archive edge serves HTTP 5xx, back off this single URL
            // so other visible icons have a chance to download first.
            const long status=http.lastStatusCode();
            const uint64_t retryDelay=(archiveImage && status>=500 && status<=599)
                ? 45000000ULL : RETRY_COOLDOWN_US;
            markFailed(job.path,retryDelay);
        }}
        // Includes any post-download image normalization/pruning/retries;
        // only now can the IME safely consider the image worker quiescent.
        sceKernelLockMutex(mutex_,1,nullptr);
        workerBusy_=false;
        cancelRequested_=false;
        sceKernelUnlockMutex(mutex_,1);
    }http.shutdown();return 0;}
} // namespace psvitaalive::ui
