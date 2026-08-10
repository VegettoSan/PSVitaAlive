#include "ui/image_cache.hpp"
#include "diagnostic_logger.hpp"
#include "network/http_client.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <png.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace psvitaalive::ui {
namespace {
constexpr const char* IMAGE_ROOT="ux0:data/psvitaalive/cache/images";
constexpr int WORKER_PRIORITY=0x10000100,WORKER_STACK=48*1024;
constexpr int MAX_RETRIES=3;

uint32_t fnv1a(const std::string&v){uint32_t h=2166136261u;for(unsigned char c:v){h^=c;h*=16777619u;}return h;}
std::string hex32(uint32_t v){char b[16];sceClibSnprintf(b,sizeof(b),"%08X",v);return b;}
std::string extensionOf(const std::string&u){std::string c=u;size_t q=c.find('?');if(q!=std::string::npos)c.erase(q);size_t f=c.find('#');if(f!=std::string::npos)c.erase(f);size_t d=c.find_last_of('.');if(d==std::string::npos)return".img";std::string e=c.substr(d);for(char&x:e)if(x>='A'&&x<='Z')x=(char)(x-'A'+'a');if(e==".jpeg")return".jpg";if(e==".png"||e==".jpg")return e;return".img";}
std::string normalizeUrl(const std::string&u){if(u.rfind("https://",0)==0||u.rfind("http://",0)==0)return u;std::string p=u;while(p.rfind("../",0)==0)p.erase(0,3);return std::string("https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/")+p;}

bool normalizePngForVita(const std::string&path){
    png_image image{};
    image.version=PNG_IMAGE_VERSION;
    if(!png_image_begin_read_from_file(&image,path.c_str())){
        diagnostics::log(std::string("[ImageCache] PNG decode failed path=")+path+" error="+image.message);
        return false;
    }
    image.format=PNG_FORMAT_RGBA;
    const size_t bytes=PNG_IMAGE_SIZE(image);
    if(bytes==0){
        png_image_free(&image);
        diagnostics::log(std::string("[ImageCache] PNG has invalid size path=")+path);
        return false;
    }
    std::vector<unsigned char> pixels(bytes);
    if(!png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr)){
        diagnostics::log(std::string("[ImageCache] PNG pixel decode failed path=")+path+" error="+image.message);
        png_image_free(&image);
        return false;
    }
    const std::string temp=path+".normalized";
    png_image output{};
    output.version=PNG_IMAGE_VERSION;
    output.width=image.width;
    output.height=image.height;
    output.format=PNG_FORMAT_RGBA;
    if(!png_image_write_to_file(&output,temp.c_str(),0,pixels.data(),0,nullptr)){
        diagnostics::log(std::string("[ImageCache] PNG normalize write failed path=")+path+" error="+output.message);
        png_image_free(&image);
        png_image_free(&output);
        sceIoRemove(temp.c_str());
        return false;
    }
    png_image_free(&image);
    png_image_free(&output);
    if(sceIoRemove(path.c_str())<0){sceIoRemove(temp.c_str());return false;}
    if(sceIoRename(temp.c_str(),path.c_str())<0){sceIoRemove(temp.c_str());return false;}
    diagnostics::log(std::string("[ImageCache] PNG normalized path=")+path);
    return true;
}
}

ImageCache::ImageCache()=default;ImageCache::~ImageCache(){shutdown();}
bool ImageCache::ensureDirectory(const std::string&p)const{StorageManager s;return s.createDirectories(p);}
bool ImageCache::init(){if(workerThread_>=0)return true;if(!ensureDirectory(IMAGE_ROOT))return false;ensureDirectory("ux0:data/psvitaalive/logs");mutex_=sceKernelCreateMutex("PSVitaAliveImageCache",0,0,nullptr);if(mutex_<0)return false;stopping_=false;workerThread_=sceKernelCreateThread("PSVitaAliveImageWorker",&ImageCache::workerEntry,WORKER_PRIORITY,WORKER_STACK,0,0,nullptr);if(workerThread_<0){sceKernelDeleteMutex(mutex_);mutex_=-1;return false;}ImageCache*self=this;int r=sceKernelStartThread(workerThread_,sizeof(self),&self);if(r<0){sceKernelDeleteThread(workerThread_);workerThread_=-1;sceKernelDeleteMutex(mutex_);mutex_=-1;return false;}diagnostics::log("[ImageCache] worker initialized");return true;}
void ImageCache::shutdown(){stopping_=true;if(workerThread_>=0){sceKernelWaitThreadEnd(workerThread_,nullptr,nullptr);sceKernelDeleteThread(workerThread_);workerThread_=-1;}if(mutex_>=0){sceKernelDeleteMutex(mutex_);mutex_=-1;}queue_.clear();pending_.clear();diagnostics::log("[ImageCache] shutdown");}
bool ImageCache::contains(const std::vector<std::string>&v,const std::string&s)const{return std::find(v.begin(),v.end(),s)!=v.end();}
std::string ImageCache::makePath(const std::string&url,const std::string&ns)const{return std::string(IMAGE_ROOT)+"/"+(ns.empty()?"misc":ns)+"_"+hex32(fnv1a(url))+extensionOf(url);}
std::string ImageCache::request(const std::string&url,const std::string&ns){
    if(url.empty()||mutex_<0)return{};
    const std::string full=normalizeUrl(url),path=makePath(full,ns);
    sceKernelLockMutex(mutex_,1,nullptr);
    const bool known=contains(ready_,path)||contains(failed_,path)||contains(pending_,path);
    sceKernelUnlockMutex(mutex_,1);
    if(known)return path;

    SceIoStat st={};
    if(sceIoGetstat(path.c_str(),&st)>=0&&st.st_size>0){
        sceKernelLockMutex(mutex_,1,nullptr);
        if(!contains(ready_,path))ready_.push_back(path);
        sceKernelUnlockMutex(mutex_,1);
        return path;
    }

    sceKernelLockMutex(mutex_,1,nullptr);
    bool queued=std::any_of(queue_.begin(),queue_.end(),[&](const Job&j){return j.path==path;});
    if(!queued&&!contains(pending_,path)){
        pending_.push_back(path);
        queue_.push_back({full,path,0});
    }
    sceKernelUnlockMutex(mutex_,1);
    return path;
}
bool ImageCache::isReady(const std::string&p)const{if(p.empty()||mutex_<0)return false;sceKernelLockMutex(mutex_,1,nullptr);bool r=contains(ready_,p);sceKernelUnlockMutex(mutex_,1);return r;}
bool ImageCache::isFailed(const std::string&p)const{if(mutex_<0||p.empty())return false;sceKernelLockMutex(mutex_,1,nullptr);bool r=contains(failed_,p);sceKernelUnlockMutex(mutex_,1);return r;}
void ImageCache::markReady(const std::string&p){sceKernelLockMutex(mutex_,1,nullptr);if(!contains(ready_,p))ready_.push_back(p);pending_.erase(std::remove(pending_.begin(),pending_.end(),p),pending_.end());failed_.erase(std::remove(failed_.begin(),failed_.end(),p),failed_.end());sceKernelUnlockMutex(mutex_,1);}
void ImageCache::markFailed(const std::string&p){sceKernelLockMutex(mutex_,1,nullptr);pending_.erase(std::remove(pending_.begin(),pending_.end(),p),pending_.end());if(!contains(failed_,p))failed_.push_back(p);sceKernelUnlockMutex(mutex_,1);}
int ImageCache::workerEntry(SceSize a,void*arg){(void)a;ImageCache*self=nullptr;if(arg)std::memcpy(&self,arg,sizeof(self));return self?self->workerMain():-1;}
int ImageCache::workerMain(){
    HttpClient http;
    if(http.init()!=HttpResult::Ok){diagnostics::log("[ImageCache] HTTP initialization failed");return-1;}
    while(!stopping_){
        Job job;bool have=false;
        sceKernelLockMutex(mutex_,1,nullptr);
        if(!queue_.empty()){job=queue_.front();queue_.erase(queue_.begin());have=true;}
        sceKernelUnlockMutex(mutex_,1);
        if(!have){sceKernelDelayThread(50*1000);continue;}

        HttpResult r=http.downloadToFile(job.url,job.path);
        bool valid=(r==HttpResult::Ok&&sceIoGetstat(job.path.c_str(),nullptr)==0);
        if(r==HttpResult::Ok){SceIoStat st={};valid=sceIoGetstat(job.path.c_str(),&st)>=0&&st.st_size>0;}
        if(valid&&extensionOf(job.path)==".png")valid=normalizePngForVita(job.path);

        if(valid){
            markReady(job.path);
            char m[900];sceClibSnprintf(m,sizeof(m),"[ImageCache] ready url=%s path=%s attempt=%d",job.url.c_str(),job.path.c_str(),job.attempt+1);diagnostics::log(m);
        }else{
            sceIoRemove(job.path.c_str());
            char m[1000];sceClibSnprintf(m,sizeof(m),"[ImageCache] failed url=%s path=%s attempt=%d http=%d error=%s",job.url.c_str(),job.path.c_str(),job.attempt+1,http.lastStatusCode(),http.lastError().c_str());diagnostics::log(m);
            if(job.attempt+1<MAX_RETRIES&&!stopping_){sceKernelDelayThread((job.attempt+1)*250*1000);job.attempt++;sceKernelLockMutex(mutex_,1,nullptr);queue_.push_back(job);sceKernelUnlockMutex(mutex_,1);}else markFailed(job.path);
        }
    }
    http.shutdown();return 0;
}
} // namespace psvitaalive::ui
