#include "installer/queue_history.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace psvitaalive;
using namespace psvitaalive::queue;
static Job job(const std::string& key) {
    Job j;j.appKey=key;j.name=key;j.titleId="TEST00001";j.version="1.2";
    Step s;s.url="https://example.invalid/SECRET";s.fileName="SECRET.vpk";s.zrif="SECRET_LICENSE";
    s.pluginLine="SECRET_PLUGIN";s.destination="SECRET_PATH";s.component=Component::App;
    s.space=ux0Space(1024,1024,InstallSpaceMode::ZipArchive);j.steps.push_back(s);j.admissionSpace=s.space;
    return j;
}
static Snapshot snap(const Session& s) {
    Snapshot x;x.generation=s.generation();x.valid=true;x.free[0]=1ULL<<40;x.measured[0]=true;return x;
}
struct Disk {
    std::map<std::string,std::string> data;
    bool torn=false;
    bool exists(const std::string& p) const {return data.count(p)!=0;}
    int64_t fileSize(const std::string& p) const {auto i=data.find(p);return i==data.end()?-1:static_cast<int64_t>(i->second.size());}
    bool readFile(const std::string& p,std::vector<uint8_t>& out) const {
        auto i=data.find(p);if(i==data.end())return false;out.assign(i->second.begin(),i->second.end());return true;
    }
    bool writeTextFile(const std::string& p,const std::string& s) {data[p]=torn?s.substr(0,5):s;return !torn;}
};
int main() {
    Session s;History h;
    assert(s.admit(job("A"),snap(s))==Admission::Accepted);
    assert(!h.observe(s,1) && h.entries().empty());
    const auto start=s.claim(snap(s),true);
    assert(start.status==Dispatch::Started && !h.observe(s,2));
    assert(s.settle(start.token,Outcome::Completed,true,false,"SECRET_RESULT"));
    assert(h.observe(s,3) && h.entries().size()==1 && !h.observe(s,4));
    assert(h.entries()[0].state==State::Completed && h.entries()[0].epoch==3);
    assert(h.entries()[0].steps[0].state==State::Completed);
    const auto encoded=h.encode(1);
    assert(encoded.find("SECRET")==std::string::npos);
    History loaded;uint64_t serial=0;
    assert(loaded.decode(encoded,serial) && serial==1 && loaded.entries()[0].name=="A");
    auto broken=encoded;broken[6]^=1;
    assert(!loaded.decode(broken,serial) && loaded.entries().size()==1);
    assert(!loaded.decode(encoded.substr(0,encoded.size()-1),serial));
    assert(!loaded.decode(std::string(History::maxBytes+1,'x'),serial));
    Disk d;HistoryStore<Disk> store(d,"history");
    assert(store.load(loaded) && store.save(h));
    assert(s.clearFinished());
    assert(s.admit(job("B"),snap(s))==Admission::Accepted);
    assert(s.cancel(s.jobs()[0].id) && h.observe(s,5));
    assert(h.entries().size()==2 && h.entries()[0].state==State::Cancelled);
    d.torn=true;assert(!store.save(h));
    History fallback;HistoryStore<Disk> store2(d,"history");
    assert(store2.load(fallback) && fallback.entries().size()==1);
    d.torn=false;assert(store2.save(h));
    History newest;HistoryStore<Disk> store3(d,"history");
    assert(store3.load(newest) && newest.entries().size()==2);
    for(size_t i=0;i<History::maxEntries+5;++i) {
        assert(s.clearFinished());
        assert(s.admit(job("C"+std::to_string(i)),snap(s))==Admission::Accepted);
        assert(s.cancel(s.jobs()[0].id) && h.observe(s,6+i));
    }
    assert(h.entries().size()==History::maxEntries);
    assert(h.encode(2).size()<=History::maxBytes);
    assert(h.entries()[0].name=="C"+std::to_string(History::maxEntries+4));
    std::cout<<"Queue history tests passed\n";
}
