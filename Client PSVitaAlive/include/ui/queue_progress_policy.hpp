#pragma once
#include <algorithm>
#include <cstdint>

namespace psvitaalive { namespace ui {
struct QueueProgressPresentation {
    bool known=false,indeterminate=false;
    unsigned percent=0;
    uint64_t etaSeconds=0;
};
inline QueueProgressPresentation makeQueueProgress(uint64_t current,uint64_t total,uint64_t speed,
    bool transfer,bool paused,bool unknownPhase) {
    QueueProgressPresentation p;
    p.known=total!=0 && !unknownPhase;
    p.indeterminate=!p.known && !paused;
    if(!p.known)return p;
    const auto bounded=std::min(current,total);
    p.percent=static_cast<unsigned>(100.0*static_cast<double>(bounded)/static_cast<double>(total));
    if(p.percent>100)p.percent=100;
    if(transfer && !paused && speed && total>bounded) {
        const uint64_t left=total-bounded;
        p.etaSeconds=left/speed+(left%speed!=0);
    }
    return p;
}
}} // namespace psvitaalive::ui
