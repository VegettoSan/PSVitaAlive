#pragma once
// Session queue core. Main-thread owned; no Vita APIs or UI callbacks.
// The integration must supply measured, generation-matched storage snapshots,
// immutable requests, and terminal results only after the worker is settled.
#include "installer/install_space_policy.hpp"
#include "installer/app_settings.hpp"
#include <cstddef>
#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace psvitaalive { namespace queue {
enum class Mount : size_t { Ux0, Uma0, Ur0, Count };
using Bytes = std::array<uint64_t, static_cast<size_t>(Mount::Count)>;
using MeasuredMounts = std::array<bool, static_cast<size_t>(Mount::Count)>;
// Single writer (HTTP worker; main may reset only before a worker starts).
// Published bytes are a conservative lower bound of physically written,
// contiguous prefixes. Reset must precede any operation that can shrink them.
class LiveStaging {
public:
    struct Sample { uint32_t epoch=0; uint64_t bytes=0; };
    void reset() {
        epoch_.fetch_add(1); bytes_.store(0); epoch_.fetch_add(1);
    }
    void publish(uint64_t bytes) {
        // Round down to MiB; don't charge buffered writes or tiny fluctuations.
        const uint64_t rounded=(bytes/kSpaceMiB)*kSpaceMiB;
        if(rounded>=bytes_.load())bytes_.store(rounded);
        else { reset(); bytes_.store(rounded); }
    }
    Sample sample() const {
        const auto e=epoch_.load(); const auto b=bytes_.load();
        return (e&1u) || e!=epoch_.load()?Sample{1,0}:Sample{e,b};
    }
    bool stillValid(Sample s) const { return !(s.epoch&1u) && s.epoch==epoch_.load(); }
private:
    std::atomic<uint32_t> epoch_{0};
    std::atomic<uint64_t> bytes_{0};
};
struct Space {
    Bytes peak{};      // additional bytes needed at maximum occupancy
    Bytes retained{};  // conservative additional bytes remaining after cleanup
    bool known = false;
};
inline bool validSpace(const Space& s) {
    if (!s.known) return false;
    for (size_t i=0;i<s.peak.size();++i)
        if (s.peak[i] == UINT64_MAX || s.retained[i] > s.peak[i]) return false;
    return true;
}
inline bool consume(Bytes& free, const Space& s) {
    if (!validSpace(s)) return false;
    for (size_t i=0;i<free.size();++i) if (s.peak[i] > free[i]) return false;
    for (size_t i=0;i<free.size();++i) free[i] -= s.retained[i];
    return true;
}
// A fresh job with a known payload, staging and destination on ux0.
// Retained is explicitly provided by the caller: no guessing from URL/size.
// Unknown expansion or unsupported mounts must use a different validated
// Space or remain unapproved. Existing installer checks still run.
inline Space ux0Space(uint64_t payload, uint64_t retained, InstallSpaceMode mode) {
    if (!payload || mode == InstallSpaceMode::SystemBgdl || mode == InstallSpaceMode::Plugin)
        return {};
    Space s;
    s.peak[0] = initialInstallSpaceBytes(payload, mode);
    s.retained[0] = retained;
    s.known = true;
    if (!validSpace(s)) return {};
    return s;
}
inline Space sequenceSpace(const std::vector<Space>& steps) {
    Space total; total.known = !steps.empty();
    for (const auto& s : steps) {
        if (!validSpace(s)) return {};
        for (size_t i=0;i<total.peak.size();++i) {
            const uint64_t peak=saturatingSpaceAdd(total.retained[i],s.peak[i]);
            if (peak > total.peak[i]) total.peak[i]=peak;
            total.retained[i]=saturatingSpaceAdd(total.retained[i],s.retained[i]);
        }
    }
    return validSpace(total) ? total : Space{};
}
// Credit only already allocated, owned and stable staging bytes, never UI
// progress. Net retained growth cannot go negative: do not promise cleanup
// space before the next physical probe.
inline Space creditStaging(Space space, const Bytes& allocated) {
    if (!validSpace(space)) return {};
    for (size_t i=0;i<allocated.size();++i) {
        if (allocated[i]>space.peak[i]) return {};
        space.peak[i]-=allocated[i];
        space.retained[i]=allocated[i]>=space.retained[i]?0:space.retained[i]-allocated[i];
    }
    return space;
}
inline bool mountsMeasured(const Space& s, const MeasuredMounts& measured) {
    for (size_t i=0;i<s.peak.size();++i)
        if ((s.peak[i] || s.retained[i]) && !measured[i]) return false;
    return true;
}
// For non-BGDL known-size requests. Caller supplies explicit destination
// peak and retained estimate, including staging/conversion there. Preserve
// existing ux0 preflight even for cross-mount paths until Vita-tested.
inline Space mountedSpace(uint64_t payload, InstallSpaceMode mode, Mount target,
                          uint64_t targetPeak, uint64_t retained) {
    if (!payload || mode==InstallSpaceMode::SystemBgdl || !targetPeak ||
        retained>targetPeak || target>=Mount::Count) return {};
    Space s; s.known=true;
    s.peak[0]=initialInstallSpaceBytes(payload,mode);
    const size_t i=static_cast<size_t>(target);
    if(i==0) {
        const uint64_t coexist=saturatingSpaceAdd(payload,targetPeak);
        if(coexist>s.peak[0]) s.peak[0]=coexist;
    } else s.peak[i]=targetPeak;
    s.retained[i]=retained;
    return validSpace(s)?s:Space{};
}
// Unknown catalog length can enter only at the tail; the existing native
// HTTP/extraction checks decide real capacity before writes. No later jobs
// may rely on this provisional budget until the group is terminal.
inline Space provisionalMountedSpace(InstallSpaceMode mode,Mount target) {
    if(mode==InstallSpaceMode::SystemBgdl || target>=Mount::Count) return {};
    Space s; s.known=true;
    const auto floor=initialInstallSpaceBytes(0,mode);
    s.peak[0]=floor;
    const auto i=static_cast<size_t>(target);
    s.peak[i]=floor; s.retained[i]=floor;
    return s;
}
inline Space systemHandoffSpace(uint64_t payload) {
    Space s; s.known=true;
    s.peak[0]=initialInstallSpaceBytes(payload,InstallSpaceMode::SystemBgdl);
    s.retained[0]=s.peak[0]; // no promise of reclaimed space after system submission
    return validSpace(s)?s:Space{};
}
inline Space savedArchiveSpace(uint64_t compressed,Mount target) {
    if(!compressed || target>=Mount::Count) return {};
    Space s; s.known=true;
    const auto i=static_cast<size_t>(target);
    s.peak[i]=saturatingSpaceAdd(saturatingSpaceAdd(compressed,compressed/4),kSpaceZipReserve);
    s.retained[i]=s.peak[i]; // already allocated input; don't promise deletion
    return validSpace(s)?s:Space{};
}
enum class State { Waiting, Running, Completed, HandedOff, Partial, Failed, Cancelled, Skipped };
enum class Outcome { Completed, HandedOff, Failed, Cancelled };
enum class Admission { Accepted, Duplicate, Invalid, Limit, Stale, UnknownSpace, NoSpace };
enum class Dispatch { Started, Nothing, Busy, Barrier, Stale, UnknownSpace, NoSpace };

struct Routing {
    InstallMethod method=InstallMethod::Auto;
    PspTarget target=PspTarget::Adrenaline;
    PspMediaFormat format=PspMediaFormat::Folder;
    PspStorage storage=PspStorage::Ux0;
    static Routing capture(const AppSettingsData& s) {
        return {s.installMethod,s.pspTarget,s.pspMediaFormat,s.pspStorage};
    }
    bool matches(const AppSettingsData& s) const {
        return method==s.installMethod && target==s.pspTarget &&
               format==s.pspMediaFormat && storage==s.pspStorage;
    }
};
struct ResultDetails {
    std::string stage, fileName, installPath, titleId, savedZipPath;
    bool liveAreaOk=false, needsReboot=false, archiveSourceUnavailable=false;
};
enum class Component { App, GameFiles, DataFiles, Plugin, SavedZip, SystemPackage, PspMedia, Other };
struct Step {
    Component component=Component::Other;
    std::string url, fileName, linkType, destination, contentId, zrif;
    std::string savedZipPath; // mutually exclusive with remote URL
    std::string pluginSection, pluginLine;
    uint64_t expectedBytes=0;
    Space space;
    State state=State::Waiting;
    std::string result;
    ResultDetails details;
};
struct Job {
    std::string appKey; // canonical catalog identity, not a visible row index
    std::string appId, titleId, name, version, versionDate;
    uint8_t catalogType=0; // display/recovery identity, not a catalog row index
    int dataRevision=0, releaseRevision=0;
    Routing routing;
    bool installAll=false;
    bool provisionalSpace=false;
    bool exclusiveTail=false; // system handoff/unknown output: no dependent reservations
    // Explicit opt-in only for groups whose selected steps are independent.
    bool continueIndependentOnFailure=false;
    bool aborted=false;
    std::vector<Step> steps;
    Space admissionSpace; // may be more conservative than sequential estimate
    uint64_t id=0;
    State state=State::Waiting;
    size_t nextStep=0;
};
// Preserve the old Install All receipt rule: only all selected Game/Data
// components successful, and no deliberate abort. App receipt is independent.
inline bool shouldWriteDataRevision(const Job& j) {
    if (!j.installAll || j.aborted || j.dataRevision<=0 ||
        j.titleId.empty() || j.appId.empty() ||
        j.state==State::Waiting || j.state==State::Running) return false;
    bool hasFiles=false;
    for(const auto& s:j.steps) {
        if(s.component!=Component::GameFiles && s.component!=Component::DataFiles) continue;
        hasFiles=true;
        if(s.state!=State::Completed) return false;
    }
    return hasFiles;
}
// Compute the entire remaining group, not merely the current transfer.
inline Space remainingGroupSpace(const Job& j, const Bytes& stableStaging={}) {
    if(j.nextStep>=j.steps.size()) return {};
    std::vector<Space> rest;
    rest.reserve(j.steps.size()-j.nextStep);
    for(size_t i=j.nextStep;i<j.steps.size();++i)
        rest.push_back(i==j.nextStep?creditStaging(j.steps[i].space,stableStaging):j.steps[i].space);
    return sequenceSpace(rest);
}
struct Token {
    uint64_t job=0, serial=0;
    size_t step=0;
    bool operator==(const Token& r) const {
        return job==r.job && serial==r.serial && step==r.step;
    }
};
struct Snapshot {
    uint64_t generation=0;
    Bytes free{};
    MeasuredMounts measured{};
    bool valid=false;
    // While a group is active (including between steps), the adapter must
    // account for its entire remaining peak/growth relative to current free.
    // This includes bytes already on disk, retained ZIPs and remaining steps.
    Space activeRemaining;
};
struct Start {
    Dispatch status=Dispatch::Nothing;
    Token token{};
};
class Session {
public:
    static constexpr size_t maxJobs=100, maxSteps=64;
    uint64_t generation() const { return generation_; }
    const std::vector<Job>& jobs() const { return jobs_; }
    const Job* find(uint64_t id) const {
        for(const auto& j:jobs_) if(j.id==id) return &j;
        return nullptr;
    }
    bool protectedSession() const {
        for(const auto& j:jobs_)
            if(j.state==State::Waiting || j.state==State::Running) return true;
        return false;
    }
    bool restartRequired() const { return reboot_; }
    bool cancelRequested() const { return cancel_; }
    Token activeToken() const { return active_; }
    uint64_t ownerId() const { return owner_; }
    Admission admit(Job job,const Snapshot& snapshot) {
        if (snapshot.generation!=generation_) return Admission::Stale;
        if (job.appKey.empty() || job.steps.empty() || job.steps.size()>maxSteps)
            return Admission::Invalid;
        for(const auto& s:job.steps)
            if(s.url.empty()==s.savedZipPath.empty() || s.fileName.empty() || s.state!=State::Waiting)
                return Admission::Invalid;
        for(const auto& j:jobs_)
            if(j.appKey==job.appKey && (j.state==State::Waiting || j.state==State::Running))
                return Admission::Duplicate;
        if(jobs_.size()>=maxJobs) return Admission::Limit;
        for(const auto& j:jobs_) if((j.provisionalSpace || j.exclusiveTail) &&
            (j.state==State::Waiting || j.state==State::Running)) return Admission::UnknownSpace;
        if(!snapshot.valid || !validSpace(job.admissionSpace) ||
           !mountsMeasured(job.admissionSpace,snapshot.measured)) return Admission::UnknownSpace;
        std::vector<Space> steps;
        for(const auto& s:job.steps) steps.push_back(s.space);
        const Space required=sequenceSpace(steps);
        if(!validSpace(required)) return Admission::UnknownSpace;
        for(size_t i=0;i<required.peak.size();++i)
            if(job.admissionSpace.peak[i]<required.peak[i] ||
               job.admissionSpace.retained[i]<required.retained[i]) return Admission::Invalid;
        Bytes free=snapshot.free;
        if(owner_!=0) {
            if(!validSpace(snapshot.activeRemaining) ||
               !mountsMeasured(snapshot.activeRemaining,snapshot.measured)) return Admission::UnknownSpace;
            if(!consume(free,snapshot.activeRemaining)) return Admission::NoSpace;
        }
        for(const auto& j:jobs_) if(j.state==State::Waiting) {
            if(!mountsMeasured(j.admissionSpace,snapshot.measured)) return Admission::UnknownSpace;
            if(!consume(free,j.admissionSpace)) return Admission::NoSpace;
        }
        if(!consume(free,job.admissionSpace)) return Admission::NoSpace;
        job.id=nextId_++; job.state=State::Waiting; job.nextStep=0; job.aborted=false;
        jobs_.push_back(std::move(job)); ++generation_;
        return Admission::Accepted;
    }
    // Claim a step only after all other heavy operations have quiesced.
    // Adapter must use its token when publishing any result (including failure
    // to start). No callback or view is allowed to advance this session.
    Start claim(const Snapshot& snapshot,bool barrierClear) {
        if(active_.job) return {Dispatch::Busy,{}};
        if(!protectedSession()) return {Dispatch::Nothing,{}};
        if(!barrierClear) return {Dispatch::Barrier,{}};
        if(snapshot.generation!=generation_) return {Dispatch::Stale,{}};
        if(!snapshot.valid) return {Dispatch::UnknownSpace,{}};
        Job* j=owner_?mutableJob(owner_):nullptr;
        if(!j) for(auto& candidate:jobs_) if(candidate.state==State::Waiting) {
            j=&candidate; break;
        }
        if(!j || j->nextStep>=j->steps.size()) return {Dispatch::Nothing,{}};
        const Space space=owner_?remainingGroupSpace(*j):j->admissionSpace;
        Bytes free=snapshot.free;
        if(!validSpace(space) || !mountsMeasured(space,snapshot.measured)) return {Dispatch::UnknownSpace,{}};
        if(!consume(free,space)) return {Dispatch::NoSpace,{}};
        owner_=j->id; j->state=State::Running;
        j->steps[j->nextStep].state=State::Running;
        active_={j->id,nextSerial_++,j->nextStep}; cancel_=false; ++generation_;
        return {Dispatch::Started,active_};
    }
    // Pending cancellation never calls the worker. Active cancellation is
    // cooperative and keeps protection until a settled result is recorded.
    bool cancel(uint64_t id) {
        Job* j=mutableJob(id); if(!j) return false;
        if(j->state==State::Waiting || (owner_==id && !active_.job)) {
            j->aborted=true; skipRemaining(*j); finish(*j,State::Cancelled); ++generation_; return true;
        }
        if(j->state==State::Running && active_.job==id && !cancel_) {
            cancel_=true; ++generation_; return true;
        }
        return false;
    }
    // Independent grouped failures may continue if explicitly opted in.
    // Cancellation and unresolved system handoff abort the remainder.
    // Adapter must resolve recoverable ZIP ownership
    // before setting settled=true. Reboot is latched even on partial results.
    bool settle(Token token,Outcome outcome,bool settled,bool needsReboot,std::string result, ResultDetails details={}) {
        if(!settled || !active_.job || !(token==active_)) return false;
        Job* j=mutableJob(token.job);
        if(!j || j->nextStep!=token.step) return false;
        Step& s=j->steps[token.step]; s.result=std::move(result);
        s.details=std::move(details);
        s.state=outcome==Outcome::Completed?State::Completed:
                outcome==Outcome::HandedOff?State::HandedOff:
                outcome==Outcome::Failed?State::Failed:State::Cancelled;
        reboot_=reboot_||needsReboot;
        ++j->nextStep; active_={};
        const bool cancelled=cancel_; cancel_=false;
        // A system handoff cannot satisfy an internal dependency. Do not start
        // remaining components until a future reconciliation confirms it.
        const bool stop=cancelled || outcome==Outcome::Cancelled ||
            (outcome==Outcome::Failed && !j->continueIndependentOnFailure) ||
            (outcome==Outcome::HandedOff && j->nextStep<j->steps.size());
        if(stop) {
            j->aborted=true;
            skipRemaining(*j);
            finish(*j,outcome==Outcome::Failed?State::Failed:State::Cancelled);
        } else if(j->nextStep==j->steps.size()) {
            finish(*j,State::Completed);
        }
        ++generation_; return true;
    }
    // After presenting/capturing the report. Reboot state is intentionally
    // retained; only a verified reboot/explicit lifecycle policy may clear it.
    bool clearFinished() {
        if(protectedSession()) return false;
        jobs_.clear(); ++generation_; return true;
    }
private:
    Job* mutableJob(uint64_t id) {
        for(auto& j:jobs_) if(j.id==id) return &j;
        return nullptr;
    }
    static void skipRemaining(Job& j) {
        for(auto& s:j.steps) if(s.state==State::Waiting) s.state=State::Skipped;
    }
    void finish(Job& j,State terminal) {
        bool some=false,all=true,handoff=false,failed=false;
        for(const auto& s:j.steps) {
            const bool success=s.state==State::Completed || s.state==State::HandedOff;
            some=some||success;
            all=all&&success;
            handoff=handoff||s.state==State::HandedOff;
            failed=failed||s.state==State::Failed;
        }
        j.state=all?(handoff?State::HandedOff:State::Completed):some?State::Partial:
            (terminal==State::Cancelled?terminal:failed?State::Failed:terminal);
        if(owner_==j.id) owner_=0;
    }
    std::vector<Job> jobs_;
    uint64_t generation_=1,nextId_=1,nextSerial_=1,owner_=0;
    Token active_{};
    bool cancel_=false,reboot_=false;
};
}} // namespace psvitaalive::queue
