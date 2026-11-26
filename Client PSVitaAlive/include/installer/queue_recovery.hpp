#pragma once
// Non-executable checkpoint. Never persist request credentials or paths.
// Fingerprints are matching hints, not authentication. Restoration must
// reselect exactly one current catalog link, re-plan and ask the user.
#include "installer/download_queue.hpp"
#include <algorithm>
#include <string>
#include <vector>
namespace psvitaalive { namespace queue {
inline void recoveryHashNumber(uint64_t& h,uint64_t v) {
    for(unsigned i=0;i<8;++i){h^=(v>>(8*i))&255;h*=1099511628211ULL;}
}
inline void recoveryHashField(uint64_t& h,const std::string& s) {
    recoveryHashNumber(h,s.size());
    for(unsigned char c:s){h^=c;h*=1099511628211ULL;}
}
inline uint64_t recoverySource(const std::string& url,const std::string& type,const std::string& cid) {
    uint64_t h=14695981039346656037ULL;
    recoveryHashField(h,url);recoveryHashField(h,type);recoveryHashField(h,cid);return h;
}
inline uint64_t recoveryRequest(const Step& s) {
    uint64_t h=recoverySource(s.url,s.linkType,s.contentId);
    recoveryHashField(h,s.fileName);recoveryHashField(h,s.destination);
    recoveryHashField(h,s.pluginSection);recoveryHashField(h,s.pluginLine);
    recoveryHashField(h,s.savedZipPath);recoveryHashNumber(h,s.expectedBytes);return h;
}
inline uint64_t recoveryRouting(const Routing& r) {
    return static_cast<uint64_t>(r.method)|(static_cast<uint64_t>(r.target)<<8)|
           (static_cast<uint64_t>(r.format)<<16)|(static_cast<uint64_t>(r.storage)<<24);
}
inline bool recoveryRestorable(const Job& j) {
    if(j.catalogType>3 || j.state!=State::Waiting || j.nextStep || j.aborted ||
       j.provisionalSpace || j.exclusiveTail || j.appId.empty() || j.titleId.empty() ||
       j.appId.size()>128 || j.titleId.size()>32 || j.version.size()>64 || j.versionDate.size()>32 ||
       j.steps.empty() || j.steps.size()>Session::maxSteps)return false;
    for(const auto& s:j.steps) {
        if(s.state!=State::Waiting || !s.expectedBytes || s.url.empty() || !s.zrif.empty() ||
           !s.savedZipPath.empty() || !s.pluginLine.empty() || !s.pluginSection.empty() ||
           (s.component!=Component::App && s.component!=Component::GameFiles &&
            s.component!=Component::DataFiles && s.component!=Component::Other && s.component!=Component::PspMedia))return false;
    }
    return true;
}
struct RecoveryStep {uint64_t source=0,request=0;};
struct RecoveryEntry {
    std::string appId,titleId,name,version,versionDate;
    uint8_t catalogType=0;
    uint32_t dataRevision=0;
    uint64_t routing=0;
    bool interrupted=false,restorable=false,installAll=false,systemDeferred=false;
    std::vector<RecoveryStep> steps;
};
class Recovery {
public:
    static constexpr size_t maxEntries=Session::maxJobs,maxBytes=262144;
    // Only loaded unresolved records are exposed to the idle UI.
    const std::vector<RecoveryEntry>& entries() const {return previous_;}
    bool canAdmit() const {return previous_.size()+live_.size()<maxEntries;}
    uint64_t observedGeneration() const {return generation_;}
    bool take(size_t i,RecoveryEntry& out) {
        if(i>=previous_.size())return false;
        out=std::move(previous_[i]);previous_.erase(previous_.begin()+i);return true;
    }
    void insert(size_t i,RecoveryEntry entry) {
        previous_.insert(previous_.begin()+std::min(i,previous_.size()),std::move(entry));
    }
    bool observe(const Session& session) {
        if(generation_==session.generation())return false;
        const auto before=encode(1);
        generation_=session.generation();live_.clear();
        deferredIds_.erase(std::remove_if(deferredIds_.begin(),deferredIds_.end(),
            [&](uint64_t id){return !session.find(id);}),deferredIds_.end());
        for(const auto& original:session.jobs()) {
            const bool deferred=original.handoffDeferred && original.state==State::Partial &&
                std::find(deferredIds_.begin(),deferredIds_.end(),original.id)==deferredIds_.end();
            if(!deferred && original.state!=State::Waiting && original.state!=State::Running)continue;
            Job tail;
            if(deferred) {
                tail=original;tail.steps.clear();tail.nextStep=0;tail.state=State::Waiting;
                tail.aborted=false;tail.exclusiveTail=false;tail.provisionalSpace=false;
                for(size_t i=0;i<original.steps.size();++i) {
                    const auto& s=original.steps[i];
                    if(i>=original.nextStep && s.state==State::Skipped) {
                        tail.steps.push_back(s);tail.steps.back().state=State::Waiting;
                        if(!s.expectedBytes)tail.provisionalSpace=true;
                        if(s.component==Component::SystemPackage || s.component==Component::SavedZip ||
                           s.component==Component::Other)tail.exclusiveTail=true;
                    } else if((s.component==Component::GameFiles || s.component==Component::DataFiles) && s.state!=State::Completed)
                        tail.installAll=false;
                }
                if(tail.steps.empty())continue;
            }
            const auto& j=deferred?tail:original;
            RecoveryEntry e;
            e.appId=display(j.appId,128);e.titleId=display(j.titleId,32);e.name=display(j.name,240);
            if(e.name.empty())e.name=e.titleId.empty()?"-":e.titleId;
            e.version=display(j.version,64);e.versionDate=display(j.versionDate,32);
            e.catalogType=j.catalogType;e.dataRevision=j.dataRevision<0?0:static_cast<uint32_t>(j.dataRevision);
            e.routing=recoveryRouting(j.routing);e.installAll=j.installAll;
            e.interrupted=j.state==State::Running;e.systemDeferred=deferred;
            e.restorable=recoveryRestorable(j) && e.appId==j.appId && e.titleId==j.titleId &&
                e.version==j.version && e.versionDate==j.versionDate;
            for(const auto& s:j.steps)e.steps.push_back({recoverySource(s.url,s.linkType,s.contentId),recoveryRequest(s)});
            if(deferred) { previous_.push_back(std::move(e));deferredIds_.push_back(original.id); }
            else live_.push_back(std::move(e));
        }
        return before!=encode(1);
    }
    std::string encode(uint64_t serial) const {
        std::string out="PSQR2";put(out,serial,8);put(out,previous_.size()+live_.size(),2);
        for(const auto* records:{&previous_,&live_})for(const auto& e:*records) {
            str(out,e.appId);str(out,e.titleId);str(out,e.name);str(out,e.version);str(out,e.versionDate);
            put(out,e.catalogType,1);put(out,e.dataRevision,4);put(out,e.routing,8);
            put(out,(e.interrupted?1:0)|(e.restorable?2:0)|(e.installAll?4:0)|(e.systemDeferred?8:0),1);
            put(out,e.steps.size(),1);
            for(const auto& s:e.steps){put(out,s.source,8);put(out,s.request,8);}
        }
        put(out,checksum(out,out.size()),4);return out;
    }
    bool decode(const std::string& data,uint64_t& serial) {
        if(data.size()<19 || data.size()>maxBytes || (data.compare(0,5,"PSQR1")!=0 && data.compare(0,5,"PSQR2")!=0))return false;
        const bool v2=data.compare(0,5,"PSQR2")==0;
        const size_t end=data.size()-4;size_t pos=end;uint64_t crc=0;
        if(!get(data,pos,data.size(),4,crc) || crc!=checksum(data,end))return false;
        pos=5;uint64_t seq=0,count=0;
        if(!get(data,pos,end,8,seq)||!seq||!get(data,pos,end,2,count)||count>maxEntries)return false;
        std::vector<RecoveryEntry> loaded;loaded.reserve(static_cast<size_t>(count));
        for(size_t i=0;i<count;++i) {
            RecoveryEntry e;uint64_t catalog=0,revision=0,flags=0,steps=0;
            if(!readStr(data,pos,end,128,e.appId)||!readStr(data,pos,end,32,e.titleId)||
               !readStr(data,pos,end,240,e.name)||e.name.empty()||
               !readStr(data,pos,end,64,e.version)||!readStr(data,pos,end,32,e.versionDate)||
               !get(data,pos,end,1,catalog)||catalog>3||!get(data,pos,end,4,revision)||
               !get(data,pos,end,8,e.routing)||!get(data,pos,end,1,flags)||flags>(v2?15u:7u)||
               !get(data,pos,end,1,steps)||!steps||steps>Session::maxSteps)return false;
            e.catalogType=static_cast<uint8_t>(catalog);e.dataRevision=static_cast<uint32_t>(revision);
            e.interrupted=(flags&1)!=0;e.restorable=(flags&2)!=0;e.installAll=(flags&4)!=0;e.systemDeferred=(flags&8)!=0;
            if(e.systemDeferred && e.interrupted)return false;
            if(e.restorable && (e.interrupted||e.appId.empty()||e.titleId.empty()))return false;
            for(size_t k=0;k<steps;++k) {
                RecoveryStep s;if(!get(data,pos,end,8,s.source)||!get(data,pos,end,8,s.request))return false;
                e.steps.push_back(s);
            }
            loaded.push_back(std::move(e));
        }
        if(pos!=end)return false;
        previous_=std::move(loaded);live_.clear();deferredIds_.clear();generation_=0;serial=seq;return true;
    }
private:
    static std::string display(const std::string& s,size_t max) {
        size_t n=std::min(s.size(),max);
        while(n<s.size()&&n&&(static_cast<unsigned char>(s[n])&0xC0)==0x80)--n;
        std::string v=s.substr(0,n);for(char& c:v)if(static_cast<unsigned char>(c)<32||c==127)c=' ';return v;
    }
    static void put(std::string& out,uint64_t v,unsigned n) {
        for(unsigned i=0;i<n;++i)out.push_back(static_cast<char>((v>>(8*i))&255));
    }
    static bool get(const std::string& d,size_t& pos,size_t end,unsigned n,uint64_t& v) {
        if(pos>end||n>end-pos)return false;
        v=0;for(unsigned i=0;i<n;++i)v|=static_cast<uint64_t>(static_cast<unsigned char>(d[pos++]))<<(8*i);
        return true;
    }
    static void str(std::string& out,const std::string& s){put(out,s.size(),2);out+=s;}
    static bool readStr(const std::string& d,size_t& pos,size_t end,size_t max,std::string& s) {
        uint64_t n=0;if(!get(d,pos,end,2,n)||n>max||n>end-pos)return false;
        s=d.substr(pos,static_cast<size_t>(n));pos+=static_cast<size_t>(n);return display(s,max)==s;
    }
    static uint32_t checksum(const std::string& d,size_t n) {
        uint32_t h=2166136261u;for(size_t i=0;i<n;++i){h^=static_cast<unsigned char>(d[i]);h*=16777619u;}return h;
    }
    std::vector<RecoveryEntry> previous_,live_;
    std::vector<uint64_t> deferredIds_;
    uint64_t generation_=0;
};
}} // namespace psvitaalive::queue
