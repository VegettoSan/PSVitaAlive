#include "network/download_recovery_policy.hpp"
#include <cassert>
using namespace psvitaalive;
int main() {
    constexpr uint64_t s = 1000000ULL;
    DownloadLinkGrace link;
    assert(!link.observe(0, true));
    assert(!link.observe(2*s, true));
    assert(!link.observe(2*s+1, false));
    assert(!link.observe(3*s, true));
    assert(link.observe(6*s, true));
    assert(!link.observe(7*s, false));
    DownloadRecoveryPolicy p;
    assert(!p.poll(0, false, 100));
    assert(!p.poll(100*s, false, 100));
    assert(p.attempts == 0);
    assert(!p.poll(101*s, true, 100));
    assert(!p.poll(105*s, true, 100));
    assert(p.poll(106*s, true, 100));
    assert(!p.poll(107*s, true, 100));
    assert(p.poll(122*s, true, 100));
    assert(!p.poll(123*s, true, 100));
    assert(p.poll(153*s, true, 100));
    assert(!p.poll(154*s, true, 100));
    assert(p.poll(214*s, true, 100));
    assert(p.exhausted());
    assert(!p.poll(1000*s, true, 100));
    assert(!p.poll(1001*s, true, 2*1024*1024));
    assert(!p.exhausted());
    assert(p.poll(1006*s, true, 2*1024*1024));
    assert(recoverableDownloadStatus(true, false, 0));
    assert(recoverableDownloadStatus(true, false, 206));
    assert(recoverableDownloadStatus(false, true, 429));
    assert(recoverableDownloadStatus(false, true, 503));
    assert(!recoverableDownloadStatus(false, true, 404));
    assert(!recoverableDownloadStatus(true, false, 403));
    assert(!recoverableDownloadStatus(false, false, 0));
    return 0;
}
