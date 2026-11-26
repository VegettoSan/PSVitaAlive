#include "installer/queue_recovery.hpp"
#include "installer/queue_history.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace psvitaalive;
using namespace psvitaalive::queue;
static Job job(const std::string& key) {
    Job j;j.appKey=key;j.appId=key;j.titleId="TEST00001";j.name=key;j.version="1.2";j.versionDate="2026-11-25";
    Step s;s.component=Component::App;s.url="https://example.invalid/SECRET";s.fileName="SECRET.vpk";
    s.expectedBytes=1024;s.space=ux0Space(1024,1024,InstallSpaceMode::ZipArchive);
    j.steps.push_back(s);j.admissionSpace=s.space;return j;
}
static Snapshot snap(const Session& s) {
    Snapshot x;x.generation=s.generation();x.valid=true;x.free[0]=1ULL<<40;x.measured[0]=true;
    if(const auto* j=s.find(s.ownerId())) x.activeRemaining=remainingGroupSpace(*j);
    return x;
}
struct Disk {
    std::map<std::string,std::string> data;
    bool exists(const std::string& p)const{return data.count(p)!=0;}
    int64_t fileSize(const std::string& p)const{auto i=data.find(p);return i==data.end()?-1:static_cast<int64_t>(i->second.size());}
    bool readFile(const std::string& p,std::vector<uint8_t>& out)const{
        auto i=data.find(p);if(i==data.end())return false;out.assign(i->second.begin(),i->second.end());return true;
    }
    bool writeTextFile(const std::string& p,const std::string& s){data[p]=s;return true;}
};
int main() {
    Session s;Recovery r;
    auto a=job("A");assert(recoveryRestorable(a));
    assert(s.admit(a,snap(s))==Admission::Accepted);
    auto b=job("B");b.steps[0].zrif="SECRET_LICENSE";assert(!recoveryRestorable(b));
    assert(s.admit(b,snap(s))==Admission::Accepted && r.observe(s));
    assert(r.entries().empty() && !r.observe(s));
    auto encoded=r.encode(1);assert(encoded.find("SECRET")==std::string::npos);
    Recovery restored;uint64_t serial=0;
    assert(restored.decode(encoded,serial) && serial==1 && restored.entries().size()==2);
    assert(restored.entries()[0].restorable && !restored.entries()[1].restorable);
    assert(restored.entries()[0].steps[0].request==recoveryRequest(a.steps[0]));
    auto changed=a.steps[0];changed.destination="other";assert(recoveryRequest(changed)!=recoveryRequest(a.steps[0]));
    const auto token=s.claim(snap(s),true).token;assert(token.job && r.observe(s));
    Disk d;HistoryStore<Disk,Recovery> store(d,"checkpoint");assert(store.save(r));
    Recovery boot;HistoryStore<Disk,Recovery> next(d,"checkpoint");assert(next.load(boot));
    assert(boot.entries()[0].interrupted && !boot.entries()[0].restorable);
    assert(!boot.entries()[1].interrupted && !boot.entries()[1].restorable);
    assert(s.settle(token,Outcome::Completed,true,false,"done") && r.observe(s));
    assert(store.save(r));
    Recovery after;HistoryStore<Disk,Recovery> afterStore(d,"checkpoint");assert(afterStore.load(after));
    assert(after.entries().size()==1 && after.entries()[0].name=="B");
    RecoveryEntry old;assert(after.take(0,old) && after.entries().empty());after.insert(0,std::move(old));
    assert(after.entries().size()==1);
    assert(!after.decode(encoded.substr(0,encoded.size()-1),serial));
    assert(!after.decode(std::string(Recovery::maxBytes+1,'x'),serial));
    {
        Session group;Recovery checkpoints;auto mixed=job("mixed");mixed.catalogType=1;
        mixed.installAll=true;mixed.exclusiveTail=true;
        mixed.steps[0].component=Component::SystemPackage;mixed.steps[0].space=systemHandoffSpace(1024);
        auto data=job("data").steps[0];data.component=Component::DataFiles;data.destination="ux0:data/test/";
        mixed.steps.push_back(data);mixed.admissionSpace=sequenceSpace({mixed.steps[0].space,data.space});
        assert(group.admit(mixed,snap(group))==Admission::Accepted && checkpoints.observe(group));
        const auto t=group.claim(snap(group),true).token;assert(t.job && checkpoints.observe(group));
        assert(group.settle(t,Outcome::HandedOff,true,false,"system") && checkpoints.observe(group));
        assert(checkpoints.entries().size()==1 && checkpoints.entries()[0].systemDeferred);
        assert(checkpoints.entries()[0].restorable && checkpoints.entries()[0].steps.size()==1);
        Recovery reboot;uint64_t seq=0;assert(reboot.decode(checkpoints.encode(1),seq));
        assert(reboot.entries()[0].systemDeferred && !reboot.entries()[0].interrupted);
        assert(!checkpoints.observe(group));
        assert(group.clearFinished());checkpoints.observe(group);
        assert(checkpoints.entries().size()==1);
    }
    std::cout<<"Queue recovery tests passed\n";
}
