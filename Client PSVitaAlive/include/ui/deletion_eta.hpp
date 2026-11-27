#pragma once
#include <cstdint>
#include <cmath>

namespace psvitaalive::ui {
// UI-thread only. Uses successful entries, never bytes or extra disk queries.
// Sample every 2 s; smooth recent speed, forget stale speed during a stall.
class DeletionEta {
    uint64_t start_=0, sample_=0, sampleDone_=0, lastMove_=0, lastDone_=0;
    bool active_=false;
    double rate_=0;
public:
    void reset() { *this=DeletionEta{}; }
    uint64_t update(uint64_t now, bool measurable, uint64_t done, uint64_t total) {
        if(!measurable || !total || done>=total) {reset();return 0;}
        if(!active_ || now<sample_ || done<lastDone_) {
            reset();active_=true;start_=sample_=lastMove_=now;
            sampleDone_=lastDone_=done;return 0;
        }
        if(done!=lastDone_) {lastMove_=now;lastDone_=done;}
        if(now-lastMove_>=8000000ULL) {
            rate_=0;sample_=now;sampleDone_=done;start_=now;return 0;
        }
        const uint64_t dt=now-sample_;
        if(dt>=2000000ULL) {
            const double current=static_cast<double>(done-sampleDone_)*1000000.0/static_cast<double>(dt);
            rate_=rate_>0?rate_*0.35+current*0.65:current;
            sample_=now;sampleDone_=done;
        }
        if(now-start_<3000000ULL || rate_<=0) return 0;
        const double seconds=static_cast<double>(total-done)/rate_;
        // Never display an unbounded or unrealistic countdown.
        if(!std::isfinite(seconds) || seconds>604800.0) return 0;
        return static_cast<uint64_t>(std::ceil(seconds));
    }
};
}
