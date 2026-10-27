#pragma once

#include <psp2/net/netctl.h>
#include <atomic>

namespace psvitaalive {

// SceNetCtl reports network association/IP state, NOT radio power or proven
// Internet reachability. A disconnected console may have Wi-Fi disabled,
// no configured access point, or be temporarily out of range.
enum class LinkConnectivity {
    Ready,
    Connecting,
    Disconnected,
    Unknown
};

inline LinkConnectivity queryLinkConnectivity() {
    int state = SCE_NETCTL_STATE_DISCONNECTED;
    int result = sceNetCtlInetGetState(&state);
    if (result < 0) {
        // NetCtl might not have been initialized yet. Attempt one initialization
        // per process, never every 500ms from the libcurl cancellation callback.
        static std::atomic<bool> initAttempted{false};
        if (!initAttempted.exchange(true, std::memory_order_acq_rel))
            sceNetCtlInit();
        result = sceNetCtlInetGetState(&state);
    }
    if (result < 0) return LinkConnectivity::Unknown;
    switch (state) {
        case SCE_NETCTL_STATE_CONNECTED: return LinkConnectivity::Ready;
        case SCE_NETCTL_STATE_CONNECTING:
        case SCE_NETCTL_STATE_FINALIZING: return LinkConnectivity::Connecting;
        case SCE_NETCTL_STATE_DISCONNECTED: return LinkConnectivity::Disconnected;
        default: return LinkConnectivity::Unknown;
    }
}

inline const char* downloadPreflightStatusKey(LinkConnectivity state) {
    switch (state) {
        case LinkConnectivity::Disconnected: return "NET_LINK_DISCONNECTED";
        case LinkConnectivity::Connecting: return "NET_LINK_CONNECTING";
        default: return nullptr;
    }
}

// Loss of an established network is observable even though radio power is not.
inline bool linkUnavailable(LinkConnectivity state) {
    return state == LinkConnectivity::Disconnected;
}

} // namespace psvitaalive
