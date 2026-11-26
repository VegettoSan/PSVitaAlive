#include "installer/queue_coordinator.hpp"
#include <cassert>
#include <iostream>

using namespace psvitaalive;
using namespace psvitaalive::queue;
constexpr uint64_t GiB=1024ULL*1024*1024;
struct FakeCatalog {
    bool gate=false, drained=true, failAcquire=false, failRelease=false;
    bool setInstallSessionGate(bool v) {
        if(v?failAcquire:failRelease) return false;
        gate=v; return true;
    }
    bool installSessionQuiescent() const { return gate && drained; }
    bool actionBlockedByCatalog() const { return !drained; }
};
struct FakeStatus {
    bool zipRecoveryAvailable=false, terminal=false, needsReboot=false;
    Outcome outcome=Outcome::Completed;
};
struct FakeObservation { Token token; FakeStatus status; };
struct FakeBackend {
    bool locked=false, failAcquire=false, failStart=false, failKeep=false;
    bool allowCancel=true, allowSettle=true;
    uint64_t free=20*GiB;
    int probes=0, polls=0, recoveryPolls=0, keeps=0, cancels=0, starts=0;
    Token owner{};
    FakeStatus status{};
    bool canStartQueuedStep() const { return !owner.job; }
    bool beginQueueProtection() { if(failAcquire) return false; locked=true; return true; }
    bool endQueueProtection(const Session& s) {
        if(s.protectedSession() || owner.job) return false;
        locked=false; return true;
    }
    Snapshot captureQueueSnapshot(const Session& s) {
        ++probes;
        Snapshot x; x.generation=s.generation(); x.valid=true;
        x.free[0]=free; x.measured[0]=true;
        if(const auto* j=s.find(s.ownerId())) x.activeRemaining=remainingGroupSpace(*j);
        return x;
    }
    bool ownsQueuedStep() const { return owner.job!=0; }
    bool startQueuedStep(const Session& s,Token t) {
        ++starts;
        assert(locked && s.activeToken()==t && !owner.job);
        if(failStart) return false;
        owner=t; status={}; return true;
    }
    void pollDownloadRecovery() { ++recoveryPolls; }
    FakeObservation pollQueuedStep() { ++polls; return {owner,status}; }
    bool settleQueuedStep(Session& s,const FakeObservation& o) {
        if(!allowSettle || !o.status.terminal || o.status.zipRecoveryAvailable ||
            !(o.token==owner)) return false;
        if(!s.settle(o.token,o.status.outcome,true,o.status.needsReboot,"result")) return false;
        owner={}; status={}; return true;
    }
    bool cancelQueuedStep(Token t) {
        if(!allowCancel || !(t==owner)) return false;
        ++cancels; return true;
    }
    bool pauseQueuedStep(Token t) { return t==owner; }
    bool resumeQueuedStep(Token t) { return t==owner; }
    bool resolveQueuedZip(Token t,bool keep,std::string* path) {
        if(!(t==owner) || !status.zipRecoveryAvailable) return false;
        if(keep) { ++keeps; if(failKeep) return false; }
        status.zipRecoveryAvailable=false;
        if(path) *path=keep?"saved.zip":"";
        return true;
    }
};
using TestQueue=Coordinator<FakeBackend,FakeCatalog>;
static Job makeJob(const char* key,unsigned count=1) {
    Job j; j.appKey=key; j.installAll=count>1;
    j.titleId="TEST00001"; j.appId=key; j.dataRevision=3;
    std::vector<Space> budget;
    for(unsigned i=0;i<count;++i) {
        Step s; s.url="https://example.invalid/archive.zip"; s.fileName="archive.zip";
        s.expectedBytes=GiB; s.space=ux0Space(GiB,GiB,InstallSpaceMode::ZipArchive);
        s.component=i==0?Component::App:i==1?Component::GameFiles:Component::Plugin;
        budget.push_back(s.space); j.steps.push_back(s);
    }
    j.admissionSpace=sequenceSpace(budget); return j;
}
int main() {
    {
        FakeBackend b; FakeCatalog c; TestQueue q(b,c);
        assert(q.submit(makeJob("A"),false).code==SubmitCode::ExternalBusy);
        b.failAcquire=true;
        assert(q.submit(makeJob("A"),true).code==SubmitCode::ProtectionUnavailable);
        assert(!b.locked && !c.gate && !q.protectedSession());
        b.failAcquire=false; b.free=0;
        assert(q.submit(makeJob("A"),true).admission==Admission::NoSpace);
        assert(!b.locked && !c.gate && !q.reportReady());
        b.free=20*GiB; c.drained=false;
        assert(q.submit(makeJob("A"),true).code==SubmitCode::CatalogBusy);
        assert(!b.locked && !c.gate && !q.protectedSession());
        c.drained=true;
        assert(q.submit(makeJob("A"),true).code==SubmitCode::Accepted);
        c.drained=false; // Defensive dispatch barrier even after admission.
        assert(b.locked && c.gate);
        const int probes=b.probes;
        assert(q.tick(true,0).state==TickState::WaitingBarrier && b.probes==probes);
        c.drained=true;
        assert(q.tick(false,1).state==TickState::WaitingBarrier);
        b.free=0;
        assert(q.tick(true,2).state==TickState::WaitingSpace);
        int stopped=b.probes;
        for(int i=0;i<20;++i) assert(q.tick(true,3+i).state==TickState::WaitingSpace);
        assert(b.probes==stopped && !b.owner.job);
        // Maintenance can enter only at a latched capacity barrier, with
        // no native owner. A visible storage screen keeps externalReady false.
        assert(q.maintenanceAllowed());
        assert(q.tick(false,24).state==TickState::WaitingBarrier);
        assert(q.maintenanceAllowed() && b.probes==stopped && b.locked && c.gate);
        b.free=20*GiB; q.retryCapacity();
        assert(!q.maintenanceAllowed());
        assert(q.tick(true,30).state==TickState::Running && b.polls==0);
        const auto token=b.owner;
        assert(!q.maintenanceAllowed());
        assert(q.submit(makeJob("B"),true).code==SubmitCode::Accepted);
        assert(q.cancel(q.session().jobs()[1].id) && b.cancels==0);
        assert(!q.pause({token.job,token.serial+1,token.step}));
        assert(q.pause(token) && q.resume(token));
        b.status.terminal=true; b.status.needsReboot=true; b.allowSettle=false;
        assert(q.tick(true,31).state==TickState::Running && b.locked);
        b.allowSettle=true;
        assert(q.tick(true,32).state==TickState::Settled && b.locked);
        assert(q.tick(true,33).state==TickState::Finished);
        assert(!b.locked && !c.gate && q.reportReady() && q.session().restartRequired());
        assert(!q.maintenanceAllowed());
        assert(q.submit(makeJob("C"),true).code==SubmitCode::ReportPending);
        assert(q.acknowledgeReport() && !q.reportReady());
    }
    {
        FakeBackend b; FakeCatalog c; TestQueue q(b,c);
        auto j=makeJob("group",3); j.continueIndependentOnFailure=true;
        assert(q.submit(j,true).code==SubmitCode::Accepted);
        assert(q.tick(true,0).state==TickState::Running);
        b.status.terminal=true; b.status.outcome=Outcome::Failed;
        assert(q.tick(true,1).state==TickState::Settled);
        assert(q.tick(true,2).state==TickState::Running && b.owner.step==1);
        b.status.terminal=true; b.status.zipRecoveryAvailable=true; b.status.outcome=Outcome::Failed;
        b.failKeep=true;
        assert(q.tick(true,100).state==TickState::ZipDecision);
        assert(q.tick(true,10000099).state==TickState::ZipDecision && b.keeps==0);
        assert(q.tick(true,10000100).state==TickState::ZipDecision && b.keeps==1);
        assert(q.tick(true,20000100).state==TickState::ZipDecision && b.keeps==1);
        assert(!q.resolveZip({b.owner.job,b.owner.serial+1,1},false));
        assert(q.resolveZip(b.owner,false));
        assert(q.tick(true,20000101).state==TickState::Settled);
        assert(q.tick(true,20000102).state==TickState::Running && b.owner.step==2);
        b.status.terminal=true; b.status.needsReboot=true;
        assert(q.tick(true,20000103).state==TickState::Settled);
        assert(q.session().jobs()[0].state==State::Partial);
        assert(!shouldWriteDataRevision(q.session().jobs()[0]));
        assert(q.tick(true,20000104).state==TickState::Finished);
    }
    {
        FakeBackend b; FakeCatalog c; TestQueue q(b,c);
        assert(q.submit(makeJob("fail"),true).code==SubmitCode::Accepted);
        b.failStart=true;
        assert(q.tick(true,0).state==TickState::StartFailed);
        assert(q.session().jobs()[0].state==State::Failed);
        c.failRelease=true;
        assert(q.tick(true,1).state==TickState::InvariantBlocked && q.protectedSession());
        c.failRelease=false;
        assert(q.tick(true,2).state==TickState::Finished && q.reportReady());
    }
    {
        FakeBackend b; FakeCatalog c; TestQueue q(b,c);
        auto j=makeJob("revision",3); j.continueIndependentOnFailure=true;
        assert(q.submit(j,true).code==SubmitCode::Accepted);
        for(unsigned i=0;i<3;++i) {
            assert(q.tick(true,i*2).state==TickState::Running);
            b.status.terminal=true;
            if(i==2) b.status.outcome=Outcome::Failed;
            assert(q.tick(true,i*2+1).state==TickState::Settled);
        }
        assert(q.session().jobs()[0].state==State::Partial);
        assert(shouldWriteDataRevision(q.session().jobs()[0]));
    }
    {
        FakeBackend b; FakeCatalog c; TestQueue q(b,c);
        auto j=makeJob("cancel",3); j.continueIndependentOnFailure=true;
        assert(q.submit(j,true).code==SubmitCode::Accepted);
        assert(q.tick(true,0).state==TickState::Running);
        b.status.terminal=true;
        assert(q.tick(true,1).state==TickState::Settled);
        assert(q.tick(true,2).state==TickState::Running);
        b.allowCancel=false;
        assert(!q.cancel(b.owner.job) && !q.session().cancelRequested());
        b.allowCancel=true;
        assert(q.cancel(b.owner.job) && q.session().cancelRequested());
        assert(!q.resume(b.owner));
        b.status.terminal=true; b.status.outcome=Outcome::Cancelled;
        assert(q.tick(true,3).state==TickState::Settled);
        assert(q.session().jobs()[0].aborted && !shouldWriteDataRevision(q.session().jobs()[0]));
        assert(q.session().jobs()[0].steps[2].state==State::Skipped);
        assert(q.tick(true,4).state==TickState::Finished);
    }
    {
        FakeBackend b; FakeCatalog c; TestQueue q(b,c);
        int checkpoints=0;
        q.setBeforeStart([&](const Session& s){++checkpoints;assert(s.activeToken().job && !b.owner.job);return false;});
        auto j=makeJob("checkpoint",2);j.continueIndependentOnFailure=true;
        assert(q.submit(j,true).code==SubmitCode::Accepted);
        assert(q.tick(true,0).state==TickState::StartFailed && checkpoints==1 && b.starts==0);
        assert(q.session().jobs()[0].aborted && q.session().jobs()[0].steps[1].state==State::Skipped);
        assert(q.tick(true,1).state==TickState::Finished);
    }
    std::cout<<"Queue coordinator tests passed\n";
}
