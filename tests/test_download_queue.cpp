#include "installer/download_queue.hpp"
#include <cassert>
#include <iostream>

using namespace psvitaalive;
using namespace psvitaalive::queue;
constexpr uint64_t GiB=1024ULL*1024*1024;
static Job job(const char* key,uint64_t size,unsigned steps=1) {
    Job j; j.appKey=key; j.name=key; j.installAll=steps>1;
    std::vector<Space> spaces;
    for(unsigned i=0;i<steps;++i) {
        Step s; s.url="https://example.invalid/archive.zip"; s.fileName="archive.zip";
        s.expectedBytes=size; s.space=ux0Space(size,size,InstallSpaceMode::ZipArchive);
        spaces.push_back(s.space); j.steps.push_back(s);
    }
    j.admissionSpace=sequenceSpace(spaces);
    return j;
}
static Snapshot snap(const Session& s,uint64_t free, Space remaining={}) {
    Snapshot x; x.generation=s.generation(); x.free[0]=free; x.valid=true; x.activeRemaining=remaining;
    return x;
}
int main() {
    // Order affects the peak: 2 GiB then 4 fails; reverse order fits.
    {
        Session s;
        assert(s.admit(job("A",2*GiB),snap(s,10*GiB))==Admission::Accepted);
        assert(s.admit(job("B",4*GiB),snap(s,10*GiB))==Admission::NoSpace);
        assert(s.admit(job("A",2*GiB),snap(s,10*GiB))==Admission::Duplicate);
        assert(s.cancel(s.jobs()[0].id));
        assert(s.admit(job("B",4*GiB),snap(s,10*GiB))==Admission::Accepted);
        assert(s.admit(job("A",2*GiB),snap(s,10*GiB))==Admission::Accepted);
    }
    {
        Session s; auto old=snap(s,10*GiB);
        assert(s.admit(job("A",2*GiB),old)==Admission::Accepted);
        assert(s.admit(job("B",GiB),old)==Admission::Stale);
        assert(s.claim(snap(s,10*GiB),false).status==Dispatch::Barrier);
        assert(s.claim(snap(s,GiB),true).status==Dispatch::NoSpace);
        auto a=s.claim(snap(s,10*GiB),true);
        assert(a.status==Dispatch::Started && s.protectedSession());
        assert(s.claim(snap(s,10*GiB),true).status==Dispatch::Busy);
        assert(s.admit(job("B",GiB),snap(s,9*GiB))==Admission::UnknownSpace);
        // 1 GiB already written: free is measured after the write. The
        // adapter gives only future additional peak and permanent growth.
        Space remaining=ux0Space(2*GiB,2*GiB,InstallSpaceMode::ZipArchive);
        remaining.peak[0]-=GiB; // downloaded temporary already charged
        assert(s.admit(job("B",GiB),snap(s,9*GiB,remaining))==Admission::Accepted);
        auto pending=s.jobs()[1].id;
        assert(s.cancel(pending));
        assert(!s.cancelRequested() && s.activeToken()==a.token);
        assert(!s.settle(a.token,Outcome::Completed,false,false,"early"));
        assert(s.protectedSession());
        assert(s.settle(a.token,Outcome::Completed,true,true,"done"));
        assert(!s.settle(a.token,Outcome::Completed,true,false,"duplicate"));
        assert(!s.protectedSession() && s.restartRequired());
        assert(s.clearFinished() && s.restartRequired());
    }
    {
        Session s;
        assert(s.admit(job("group",GiB,3),snap(s,10*GiB))==Admission::Accepted);
        assert(s.admit(job("other",GiB),snap(s,10*GiB))==Admission::Accepted);
        auto a=s.claim(snap(s,10*GiB),true);
        assert(s.settle(a.token,Outcome::Completed,true,true,"first component"));
        assert(s.protectedSession() && s.restartRequired());
        auto b=s.claim(snap(s,9*GiB),true);
        assert(b.token.job==a.token.job && b.token.step==1);
        assert(!s.settle(a.token,Outcome::Completed,true,false,"stale"));
        assert(s.cancel(b.token.job) && s.cancelRequested());
        assert(!s.settle(b.token,Outcome::Cancelled,false,false,"still writing"));
        assert(s.settle(b.token,Outcome::Cancelled,true,false,"cancelled"));
        assert(s.jobs()[0].state==State::Partial);
        assert(s.jobs()[0].steps[2].state==State::Skipped);
        auto c=s.claim(snap(s,9*GiB),true);
        assert(c.status==Dispatch::Started && c.token.job!=a.token.job);
        assert(s.settle(c.token,Outcome::Failed,true,false,"failure"));
        assert(!s.protectedSession() && s.restartRequired());
    }
    {
        Session s;
        Job j=job("unknown",GiB);
        j.steps[0].space={}; j.admissionSpace={};
        assert(s.admit(j,snap(s,10*GiB))==Admission::UnknownSpace);
        assert(!validSpace(ux0Space(UINT64_MAX,0,InstallSpaceMode::ZipArchive)));
        assert(!validSpace(ux0Space(GiB,GiB,InstallSpaceMode::SystemBgdl)));
        Space multi; multi.known=true; multi.peak={GiB,2*GiB,0}; multi.retained={0,GiB,0};
        Bytes free{3*GiB,GiB,0};
        auto before=free;
        assert(!consume(free,multi) && free==before);
        free[1]=3*GiB;
        assert(consume(free,multi) && free[1]==2*GiB && free[0]==3*GiB);
    }
    {
        AppSettingsData settings;
        auto routing=Routing::capture(settings);
        assert(routing.matches(settings));
        settings.colorTheme=ColorTheme::Cyan;
        assert(routing.matches(settings));
        settings.pspStorage=PspStorage::Uma0;
        assert(!routing.matches(settings));
        settings.pspStorage=PspStorage::Ux0;
        settings.pspTarget=PspTarget::LiveArea;
        assert(!routing.matches(settings));
    }
    {
        Session s; auto j=job("system",GiB);
        j.steps[0].zrif="test-license";
        assert(s.admit(j,snap(s,10*GiB))==Admission::Accepted);
        j.steps[0].zrif="changed";
        auto a=s.claim(snap(s,10*GiB),true);
        assert(s.find(a.token.job)->steps[0].zrif=="test-license");
        ResultDetails d; d.stage="BGDL"; d.fileName="archive.zip";
        assert(s.settle(a.token,Outcome::HandedOff,true,false,"queued by system",d));
        assert(s.jobs()[0].state==State::HandedOff);
        assert(s.jobs()[0].steps[0].details.stage=="BGDL");
        assert(!s.protectedSession());
    }
    {
        Session s;
        assert(s.admit(job("system group",GiB,2),snap(s,10*GiB))==Admission::Accepted);
        auto a=s.claim(snap(s,10*GiB),true);
        assert(s.settle(a.token,Outcome::HandedOff,true,false,"system accepted"));
        assert(s.jobs()[0].state==State::Partial);
        assert(s.jobs()[0].steps[1].state==State::Skipped);
        assert(!s.protectedSession());
    }
    std::cout<<"Queue core tests passed\n";
}
