#pragma once
#include "installer/download_queue.hpp"
#include <utility>

namespace psvitaalive { namespace queue {

// Main-thread coordinator. Backend and Catalog expose the native adapter
// methods; the same code can be exercised on a host with deterministic fakes.
// No worker/thread is introduced here. The UI never owns the queue cursor.
enum class SubmitCode { Accepted, Rejected, ExternalBusy, ProtectionUnavailable, ReportPending };
struct SubmitResult {
    SubmitCode code=SubmitCode::Rejected;
    Admission admission=Admission::Invalid;
};
enum class TickState { Idle, WaitingBarrier, WaitingSpace, Running, ZipDecision,
                       Settled, StartFailed, Finished, InvariantBlocked };
struct TickEvent {
    TickState state=TickState::Idle;
    uint64_t job=0;
    size_t step=0;
    Dispatch dispatch=Dispatch::Nothing;
};

template<class Backend,class Catalog>
class Coordinator {
public:
    using Observation=decltype(std::declval<Backend&>().pollQueuedStep());
    Coordinator(Backend& backend,Catalog& catalog):backend_(backend),catalog_(catalog) {}
    Coordinator(const Coordinator&)=delete;
    Coordinator& operator=(const Coordinator&)=delete;
    const Session& session() const { return session_; }
    bool protectedSession() const { return engaged_ || gateHeld_; }
    bool reportReady() const { return reportReady_; }
    const Observation& observation() const { return observed_; }

    // externalReady excludes storage mutations, self-update and startup
    // plugin operations. Catalog activity is drained asynchronously in tick.
    SubmitResult submit(Job job,bool externalReady) {
        if(reportReady_) return {SubmitCode::ReportPending,Admission::Invalid};
        if(!externalReady) return {SubmitCode::ExternalBusy,Admission::Invalid};
        const bool first=!engaged_;
        if(first) {
            if(gateHeld_ || !backend_.canStartQueuedStep())
                return {SubmitCode::ExternalBusy,Admission::Invalid};
            if(!catalog_.setInstallSessionGate(true))
                return {SubmitCode::ProtectionUnavailable,Admission::Invalid};
            gateHeld_=true;
            if(!backend_.beginQueueProtection()) {
                releaseEmpty();
                return {SubmitCode::ProtectionUnavailable,Admission::Invalid};
            }
            engaged_=true;
        }
        const auto snapshot=backend_.captureQueueSnapshot(session_);
        const auto result=session_.admit(std::move(job),snapshot);
        if(result!=Admission::Accepted) {
            if(first) releaseEmpty();
            return {SubmitCode::Rejected,result};
        }
        hadJobs_=true; blocked_=Dispatch::Nothing;
        return {SubmitCode::Accepted,result};
    }

    // Once per rendered frame, in place of legacy status() polling whenever
    // protectedSession() is true. A step launch and its first poll never
    // happen in the same tick, preserving BGDL's presentation contract.
    TickEvent tick(bool externalReady,uint64_t nowUs) {
        if(!engaged_) {
            if(gateHeld_ && !releaseEmpty()) return {TickState::InvariantBlocked};
            return {};
        }
        if(!session_.protectedSession()) {
            if(!releaseEmpty()) return {TickState::InvariantBlocked};
            if(hadJobs_) {
                hadJobs_=false; reportReady_=true;
                return {TickState::Finished};
            }
            return {};
        }
        const auto token=session_.activeToken();
        if(token.job) {
            if(!backend_.ownsQueuedStep()) return {TickState::InvariantBlocked,token.job,token.step};
            backend_.pollDownloadRecovery();
            observed_=backend_.pollQueuedStep();
            if(!(observed_.token==token))
                return {TickState::InvariantBlocked,token.job,token.step};
            if(observed_.status.zipRecoveryAvailable) {
                if(!(recoveryToken_==token)) {
                    recoveryToken_=token; recoverySince_=nowUs; autoKeepAttempted_=false;
                }
                const auto* job=session_.find(token.job);
                if(job && job->installAll && !autoKeepAttempted_ &&
                    nowUs>=recoverySince_ && nowUs-recoverySince_>=10000000ULL) {
                    autoKeepAttempted_=true;
                    // Never auto-delete. Even a successful resolution needs
                    // a fresh tagged observation on the next rendered frame.
                    backend_.resolveQueuedZip(token,true,nullptr);
                }
                return {TickState::ZipDecision,token.job,token.step};
            }
            if(backend_.settleQueuedStep(session_,observed_)) {
                observed_={}; recoveryToken_={}; blocked_=Dispatch::Nothing;
                // The caller consumes terminal result/receipt events before
                // the next tick claims another worker.
                return {TickState::Settled,token.job,token.step};
            }
            return {TickState::Running,token.job,token.step};
        }
        if(backend_.ownsQueuedStep()) return {TickState::InvariantBlocked};
        if(!externalReady || !catalog_.installSessionQuiescent() ||
           !backend_.canStartQueuedStep()) return {TickState::WaitingBarrier};
        // A failed capacity check is latched: retry on explicit user action
        // or a queue mutation, never probe three mounts on every frame.
        if(blocked_!=Dispatch::Nothing)
            return {TickState::WaitingSpace,0,0,blocked_};
        const auto snapshot=backend_.captureQueueSnapshot(session_);
        const auto start=session_.claim(snapshot,true);
        if(start.status==Dispatch::NoSpace || start.status==Dispatch::UnknownSpace) {
            blocked_=start.status;
            return {TickState::WaitingSpace,0,0,blocked_};
        }
        if(start.status!=Dispatch::Started) return {TickState::WaitingBarrier,0,0,start.status};
        if(!backend_.startQueuedStep(session_,start.token)) {
            // The native contract returns false only before taking ownership.
            if(backend_.ownsQueuedStep()) return {TickState::InvariantBlocked,start.token.job,start.token.step};
            if(!session_.settle(start.token,Outcome::Failed,true,false,"Could not start queued step"))
                return {TickState::InvariantBlocked,start.token.job,start.token.step};
            return {TickState::StartFailed,start.token.job,start.token.step};
        }
        return {TickState::Running,start.token.job,start.token.step};
    }
    bool cancel(uint64_t id) {
        const auto token=session_.activeToken();
        if(token.job==id && id!=0) {
            if(session_.cancelRequested() || !backend_.cancelQueuedStep(token)) return false;
        }
        if(!session_.cancel(id)) return false;
        blocked_=Dispatch::Nothing;
        return true;
    }
    bool pause(Token token) {
        return token.job && token==session_.activeToken() &&
            !session_.cancelRequested() && backend_.pauseQueuedStep(token);
    }
    bool resume(Token token) {
        return token.job && token==session_.activeToken() &&
            !session_.cancelRequested() && backend_.resumeQueuedStep(token);
    }
    bool resolveZip(Token token,bool keep,std::string* savedPath=nullptr) {
        if(!token.job || !(token==session_.activeToken()) || !(observed_.token==token) ||
           !observed_.status.zipRecoveryAvailable) return false;
        autoKeepAttempted_=true;
        return backend_.resolveQueuedZip(token,keep,savedPath);
    }
    void retryCapacity() { blocked_=Dispatch::Nothing; }
    bool acknowledgeReport() {
        if(!reportReady_ || protectedSession() || !session_.clearFinished()) return false;
        reportReady_=false; return true;
    }
private:
    bool releaseEmpty() {
        if(session_.protectedSession() || backend_.ownsQueuedStep()) return false;
        if(engaged_) {
            if(!backend_.endQueueProtection(session_)) return false;
            engaged_=false;
        }
        if(gateHeld_) {
            if(!catalog_.setInstallSessionGate(false)) return false;
            gateHeld_=false;
        }
        return true;
    }
    Backend& backend_;
    Catalog& catalog_;
    Session session_;
    Observation observed_{};
    bool engaged_=false,gateHeld_=false,hadJobs_=false,reportReady_=false;
    Dispatch blocked_=Dispatch::Nothing;
    Token recoveryToken_{};
    uint64_t recoverySince_=0;
    bool autoKeepAttempted_=false;
};
}} // namespace psvitaalive::queue
