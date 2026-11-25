#include "installer/download_queue.hpp"
#include "ui/queue_progress_policy.hpp"
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
    Snapshot x; x.generation=s.generation(); x.free[0]=free; x.measured[0]=true;
    x.valid=true; x.activeRemaining=remaining;
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
        Space remaining=creditStaging(ux0Space(2*GiB,2*GiB,InstallSpaceMode::ZipArchive),
                                     Bytes{GiB,0,0});
        assert(remaining.retained[0]==GiB); // net growth, not full installed size
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
        assert(s.jobs()[0].handoffDeferred);
        assert(!s.protectedSession());
    }
    {
        const auto zip=ux0Space(2*GiB,2*GiB,InstallSpaceMode::ZipArchive);
        const auto remaining=creditStaging(zip,Bytes{GiB,0,0});
        assert(remaining.peak[0]==zip.peak[0]-GiB && remaining.retained[0]==GiB);
        assert(!validSpace(creditStaging(zip,Bytes{UINT64_MAX,0,0})));
        const auto small=creditStaging(zip,Bytes{3*GiB,0,0});
        assert(validSpace(small) && small.retained[0]==0); // no promised free-space credit
        const auto cross=mountedSpace(2*GiB,InstallSpaceMode::ZipArchive,
                                      Mount::Uma0,3*GiB,2*GiB);
        assert(validSpace(cross));
        assert(cross.peak[0]==zip.peak[0] && cross.peak[1]==3*GiB);
        assert(cross.retained[0]==0 && cross.retained[1]==2*GiB);
        assert(!validSpace(mountedSpace(GiB,InstallSpaceMode::SystemBgdl,Mount::Ux0,GiB,GiB)));
        Session s; auto j=job("cross",2*GiB);
        j.steps[0].space=cross; j.admissionSpace=cross;
        auto x=snap(s,10*GiB);
        assert(s.admit(j,x)==Admission::UnknownSpace);
        x.measured[1]=true; x.free[1]=GiB;
        assert(s.admit(j,x)==Admission::NoSpace);
        x.free[1]=4*GiB;
        assert(s.admit(j,x)==Admission::Accepted);
        auto after=snap(s,10*GiB); // mount disappeared before dispatch
        assert(s.claim(after,true).status==Dispatch::UnknownSpace);
        assert(!s.activeToken().job);
        after.measured[1]=true; after.free[1]=4*GiB;
        assert(s.claim(after,true).status==Dispatch::Started);
    }
    {
        Session s;
        auto j=job("group recheck",GiB,3);
        assert(s.admit(j,snap(s,10*GiB))==Admission::Accepted);
        auto a=s.claim(snap(s,10*GiB),true);
        assert(s.settle(a.token,Outcome::Completed,true,false,"done"));
        const auto* group=s.find(a.token.job);
        assert(group && group->nextStep==1);
        const auto remainder=remainingGroupSpace(*group);
        assert(remainder.retained[0]==2*GiB);
        assert(remainder.peak[0]>group->steps[1].space.peak[0]);
        assert(s.claim(snap(s,3*GiB),true).status==Dispatch::NoSpace);
        assert(s.claim(snap(s,5*GiB),true).status==Dispatch::Started);
    }
    {
        Session s;
        auto unknown=job("unknown tail",GiB);
        unknown.provisionalSpace=true;
        unknown.steps[0].expectedBytes=0;
        unknown.steps[0].space=provisionalMountedSpace(InstallSpaceMode::ZipArchive,Mount::Ux0);
        unknown.admissionSpace=unknown.steps[0].space;
        assert(unknown.admissionSpace.peak[0]==kSpaceUnknownZipReserve);
        assert(s.admit(job("known",GiB),snap(s,10*GiB))==Admission::Accepted);
        assert(s.admit(unknown,snap(s,10*GiB))==Admission::Accepted);
        assert(s.admit(job("later",GiB),snap(s,10*GiB))==Admission::UnknownSpace);
        assert(s.cancel(s.jobs()[1].id));
        assert(s.admit(job("later",GiB),snap(s,10*GiB))==Admission::Accepted);
        auto cross=provisionalMountedSpace(InstallSpaceMode::Plugin,Mount::Ur0);
        assert(cross.peak[0]==kSpaceUnknownPluginReserve && cross.peak[2]==kSpaceUnknownPluginReserve);
        assert(!validSpace(provisionalMountedSpace(InstallSpaceMode::SystemBgdl,Mount::Ux0)));
    }
    {
        Session s;
        auto system=job("system",GiB);
        system.exclusiveTail=true; system.steps[0].component=Component::SystemPackage;
        system.steps[0].space=systemHandoffSpace(GiB);
        system.admissionSpace=system.steps[0].space;
        assert(system.admissionSpace.retained[0]==system.admissionSpace.peak[0]);
        assert(s.admit(system,snap(s,10*GiB))==Admission::Accepted);
        assert(s.admit(job("later",GiB),snap(s,10*GiB))==Admission::UnknownSpace);
        auto start=s.claim(snap(s,10*GiB),true);
        assert(s.settle(start.token,Outcome::HandedOff,true,false,"system"));
        auto local=job("saved",GiB); local.exclusiveTail=true;
        local.steps[0].url.clear(); local.steps[0].savedZipPath="ux0:data/psvitaalive/manual/test.zip";
        local.steps[0].component=Component::SavedZip;
        local.steps[0].space=savedArchiveSpace(GiB,Mount::Uma0);
        local.admissionSpace=local.steps[0].space;
        assert(local.admissionSpace.peak[0]==0);
        auto x=snap(s,10*GiB); x.measured[1]=true; x.free[1]=5*GiB;
        assert(s.admit(local,x)==Admission::Accepted);
        auto invalid=local; invalid.appKey="invalid"; invalid.steps[0].url="https://example.invalid";
        x.generation=s.generation();
        assert(s.admit(invalid,x)==Admission::Invalid);
        assert(!validSpace(savedArchiveSpace(0,Mount::Ux0)));
    }
    {
        LiveStaging live;
        live.publish(2*kSpaceMiB+123);
        const auto initial=live.sample();
        assert(initial.bytes==2*kSpaceMiB && live.stillValid(initial));
        live.publish(4*kSpaceMiB);
        assert(live.stillValid(initial)); // later growth cannot invalidate a lower bound
        live.reset();
        assert(!live.stillValid(initial) && live.sample().bytes==0);
        live.publish(3*kSpaceMiB);const auto old=live.sample();
        live.publish(kSpaceMiB);assert(!live.stillValid(old));
        assert(live.sample().bytes==kSpaceMiB);
    }
    {
        Session s;auto group=job("system then data",GiB,2);
        group.steps[0].component=Component::SystemPackage;
        group.steps[0].space=systemHandoffSpace(GiB);
        group.admissionSpace=sequenceSpace({group.steps[0].space,group.steps[1].space});
        group.continueIndependentOnFailure=true;
        assert(s.admit(group,snap(s,20*GiB))==Admission::Invalid);
        group.exclusiveTail=true;
        assert(s.admit(group,snap(s,20*GiB))==Admission::Accepted);
        const auto a=s.claim(snap(s,20*GiB),true);
        assert(s.settle(a.token,Outcome::Failed,true,false,"handoff failed"));
        assert(s.jobs()[0].steps[1].state==State::Skipped && !s.jobs()[0].handoffDeferred);
    }
    {
        using psvitaalive::ui::makeQueueProgress;
        auto p=makeQueueProgress(20,100,5,true,false,false);
        assert(p.known && !p.indeterminate && p.percent==20 && p.etaSeconds==16);
        p=makeQueueProgress(20,0,5,true,false,false);
        assert(!p.known && p.indeterminate && !p.etaSeconds);
        p=makeQueueProgress(20,100,5,true,true,false);
        assert(p.known && !p.indeterminate && !p.etaSeconds);
        p=makeQueueProgress(100,100,0,true,false,true);
        assert(!p.known && p.indeterminate);
        p=makeQueueProgress(UINT64_MAX,UINT64_MAX,1,false,false,false);
        assert(p.known && p.percent==100 && !p.etaSeconds);
        p=makeQueueProgress(101,100,1,true,false,false);
        assert(p.percent==100 && !p.etaSeconds);
    }
    std::cout<<"Queue core tests passed\n";
}
