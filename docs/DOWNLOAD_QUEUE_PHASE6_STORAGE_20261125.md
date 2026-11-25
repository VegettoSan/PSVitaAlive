# Download queue — restricted storage maintenance

Branch only. Capacity-blocked sessions with no active token or native controller owner can enter the existing Storage overlay. The catalog and shell gates remain held. The frame coordinator is barred from dispatch while the overlay/worker is active; after closing, explicit Retry re-probes capacity. No automatic restart or deletion.

Only abandoned PSVitaAlive-owned temporary files (individual or temporary category) can be selected. The combined cleanup, saved ZIP cleanup, and installed Game/Data management are blocked during the session, including direct entry points. Existing fresh ownership checks remain in force. This is intentionally conservative: deleting arbitrary installed data or saved archives could invalidate queued dependencies. The user must cancel/finish the session to use those operations.

Host regression assertions cover the capacity latch, overlay barrier, absence of probes during maintenance, protected shell/catalog, retry and active/report rejection. Native CI and physical device testing are not yet confirmed at commit time. No catalog or release changes.
