#pragma once
// Bounded display-only history. This is NOT an executable queue checkpoint.
// Never copy URLs, zRIF, plugin lines, paths or raw backend messages here.
#include "installer/download_queue.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace psvitaalive { namespace queue {
struct HistoryStep { Component component=Component::Other; State state=State::Skipped; };
struct HistoryEntry {
    std::string name, titleId, version;
    uint64_t epoch=0;
    State state=State::Failed;
    bool installAll=false;
    std::vector<HistoryStep> steps;
};
inline bool historyJobTerminal(State s) {
    return s==State::Completed || s==State::HandedOff || s==State::Partial ||
           s==State::Failed || s==State::Cancelled;
}
inline bool historyStepTerminal(State s) {
    return s==State::Completed || s==State::HandedOff || s==State::Failed ||
           s==State::Cancelled || s==State::Skipped;
}
class History {
public:
    static constexpr size_t maxEntries=40, maxBytes=32768;
    const std::vector<HistoryEntry>& entries() const { return entries_; }
    uint64_t observedGeneration() const { return observedGeneration_; }
    bool observe(const Session& session,uint64_t epoch) {
        if(observedGeneration_==session.generation()) return false;
        observedGeneration_=session.generation();
        // Session IDs are process-local; prune old batch IDs, not disk records.
        seen_.erase(std::remove_if(seen_.begin(),seen_.end(),
            [&](uint64_t id){return !session.find(id);}),seen_.end());
        bool changed=false;
        for(const auto& job:session.jobs()) {
            if(!historyJobTerminal(job.state) ||
               std::find(seen_.begin(),seen_.end(),job.id)!=seen_.end() ||
               job.steps.empty() || job.steps.size()>Session::maxSteps) continue;
            HistoryEntry e;
            e.name=display(job.name,240);
            e.titleId=display(job.titleId,32);
            e.version=display(job.version,64);
            if(e.name.empty()) e.name=e.titleId.empty()?"-":e.titleId;
            e.epoch=epoch; e.state=job.state; e.installAll=job.installAll;
            bool valid=true;
            for(const auto& step:job.steps) {
                if(!historyStepTerminal(step.state) || step.component>Component::Other) {valid=false;break;}
                e.steps.push_back({step.component,step.state});
            }
            if(!valid)continue;
            if(entries_.size()>=maxEntries)entries_.pop_back();
            entries_.insert(entries_.begin(),std::move(e));
            seen_.push_back(job.id); changed=true;
        }
        return changed;
    }
    // Explicit little-endian format, independent of native struct padding.
    // The checksum detects torn writes/corruption, not malicious modification.
    std::string encode(uint64_t serial) const {
        std::string out="PSQH1";
        put(out,serial,8); put(out,entries_.size(),1);
        for(const auto& e:entries_) {
            put(out,e.epoch,8); put(out,static_cast<unsigned>(e.state),1);
            put(out,e.installAll?1:0,1); str(out,e.name); str(out,e.titleId); str(out,e.version);
            put(out,e.steps.size(),1);
            for(const auto& step:e.steps) {
                put(out,static_cast<unsigned>(step.component),1);
                put(out,static_cast<unsigned>(step.state),1);
            }
        }
        put(out,checksum(out,out.size()),4);
        return out;
    }
    bool decode(const std::string& data,uint64_t& serial) {
        if(data.size()<18 || data.size()>maxBytes || data.compare(0,5,"PSQH1")!=0) return false;
        const size_t end=data.size()-4; size_t pos=end; uint64_t crc=0;
        if(!get(data,pos,data.size(),4,crc) || crc!=checksum(data,end))return false;
        pos=5; uint64_t seq=0,count=0;
        if(!get(data,pos,end,8,seq) || !seq || !get(data,pos,end,1,count) || count>maxEntries)return false;
        std::vector<HistoryEntry> loaded; loaded.reserve(static_cast<size_t>(count));
        for(size_t n=0;n<count;++n) {
            HistoryEntry e; uint64_t state=0,all=0,steps=0;
            if(!get(data,pos,end,8,e.epoch) || !get(data,pos,end,1,state) ||
               state>static_cast<unsigned>(State::Skipped) || !historyJobTerminal(static_cast<State>(state)) ||
               !get(data,pos,end,1,all) || all>1 ||
               !readStr(data,pos,end,240,e.name) || e.name.empty() ||
               !readStr(data,pos,end,32,e.titleId) || !readStr(data,pos,end,64,e.version) ||
               !get(data,pos,end,1,steps) || !steps || steps>Session::maxSteps)return false;
            e.state=static_cast<State>(state); e.installAll=all!=0;
            for(size_t i=0;i<steps;++i) {
                uint64_t component=0,stepState=0;
                if(!get(data,pos,end,1,component) || component>static_cast<unsigned>(Component::Other) ||
                   !get(data,pos,end,1,stepState) || stepState>static_cast<unsigned>(State::Skipped) ||
                   !historyStepTerminal(static_cast<State>(stepState)))return false;
                e.steps.push_back({static_cast<Component>(component),static_cast<State>(stepState)});
            }
            loaded.push_back(std::move(e));
        }
        if(pos!=end)return false;
        entries_=std::move(loaded); serial=seq; seen_.clear(); observedGeneration_=0;
        return true;
    }
private:
    static std::string display(const std::string& s,size_t max) {
        size_t n=std::min(s.size(),max);
        // Do not split a multibyte UTF-8 sequence at the truncation boundary.
        while(n<s.size() && n && (static_cast<unsigned char>(s[n])&0xC0)==0x80)--n;
        std::string out=s.substr(0,n);
        for(char& c:out)if(static_cast<unsigned char>(c)<32 || c==127)c=' ';
        return out;
    }
    static void put(std::string& out,uint64_t v,unsigned n) {
        for(unsigned i=0;i<n;++i)out.push_back(static_cast<char>((v>>(8*i))&255));
    }
    static bool get(const std::string& data,size_t& pos,size_t end,unsigned n,uint64_t& v) {
        if(pos>end || n>end-pos)return false;
        v=0; for(unsigned i=0;i<n;++i)v|=static_cast<uint64_t>(static_cast<unsigned char>(data[pos++]))<<(8*i);
        return true;
    }
    static void str(std::string& out,const std::string& s) {put(out,s.size(),2);out+=s;}
    static bool readStr(const std::string& d,size_t& pos,size_t end,size_t max,std::string& s) {
        uint64_t n=0; if(!get(d,pos,end,2,n) || n>max || n>end-pos)return false;
        s=d.substr(pos,static_cast<size_t>(n));pos+=static_cast<size_t>(n);
        return display(s,max)==s;
    }
    static uint32_t checksum(const std::string& d,size_t n) {
        uint32_t h=2166136261u;
        for(size_t i=0;i<n;++i){h^=static_cast<unsigned char>(d[i]);h*=16777619u;}
        return h;
    }
    std::vector<HistoryEntry> entries_;
    std::vector<uint64_t> seen_;
    uint64_t observedGeneration_=0;
};

// Alternating complete snapshots: never truncate the currently valid slot.
// Read back and verify a write before changing the active slot. A failed or
// interrupted write leaves the preceding valid slot available at startup.
template<class Storage,class Data=History>
class HistoryStore {
public:
    HistoryStore(Storage& storage,std::string base):storage_(storage),base_(std::move(base)) {}
    bool load(Data& history) {
        Data a,b; uint64_t x=0,y=0;
        const bool va=read(0,a,x), vb=read(1,b,y);
        if(va && (!vb || x>=y)){history=std::move(a);serial_=x;slot_=0;return true;}
        if(vb){history=std::move(b);serial_=y;slot_=1;return true;}
        return !storage_.exists(path(0)) && !storage_.exists(path(1));
    }
    bool save(const Data& history) {
        if(serial_==UINT64_MAX)return false;
        const int next=slot_==0?1:0;
        const auto data=history.encode(serial_+1);
        if(data.size()>Data::maxBytes || !storage_.writeTextFile(path(next),data))return false;
        Data verify; uint64_t v=0;
        if(!read(next,verify,v) || v!=serial_+1 || verify.encode(v)!=data)return false;
        serial_=v;slot_=next;return true;
    }
private:
    std::string path(int i) const { return base_+(i==0?".a":".b"); }
    bool read(int i,Data& history,uint64_t& serial) {
        const auto p=path(i); const int64_t bytes=storage_.fileSize(p);
        if(bytes<=0 || static_cast<uint64_t>(bytes)>Data::maxBytes)return false;
        std::vector<uint8_t> data;
        if(!storage_.readFile(p,data) || data.size()!=static_cast<size_t>(bytes))return false;
        return history.decode(std::string(data.begin(),data.end()),serial);
    }
    Storage& storage_; std::string base_; uint64_t serial_=0; int slot_=-1;
};
}} // namespace psvitaalive::queue
