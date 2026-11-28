# ZIP release inspection (phase 1)

One shared inspector in PSVitaAlive consumes the existing Orbit release snapshot and exact GitHub release tags from canonical apps with no VPK entry. Direct VPK updating remains unchanged. If a release now has a direct VPK, ZIP alternatives are skipped.

The cache lives in data/archive_zip_inspections.json, not in apps/ or generated catalogs. Keys include inspector schema, app ID, expected Title ID, repository, tag, numeric release and asset IDs, asset URL/name/size, updated_at and official SHA-256 if available. Negative results are reused only for that exact identity. A changed release/asset invalidates the lookup. Transient network, integrity and time-budget failures are never cached. Limits/review outcomes are not classified as no VPK.

The inspector uses bounded downloads with redirect validation, fixed temporary names, no extractall or execution, path/link/duplicate checks, CRC inspection, inner size limits, eboot.bin and bounded param.sfo Title ID validation, and SHA-256 of both outer ZIP and matching inner VPK. More than one matching candidate or incomplete inspection requires review. The result is structural validation, not a guarantee of runtime compatibility, malware absence, or redistribution permission.

Four ZIP downloads and 400 MiB per run, max 200 MiB per archive/VPK, 16 release lookups, five-minute target budget (eight-minute workflow hard cap). Rotating releases and a persistent bounded cache avoid repeated downloads. Manual previews do not write persistent data.

Phase 1 only audits and records candidates; it does not publish extracted VPKs, update app versions or change Game Files. Phase 2 must re-download the exact pinned asset, verify both digests and Title ID, apply redistribution policy, use the existing owned-item/first-creation checkpoint and verify public bytes before atomically changing the canonical Download/VPK entry. Never publish a ZIP URL as VPK. A port requiring separate game data stays unchanged until compatibility is reviewed.

Star Fox Adventures (STFX00001), catalog v0.1.3, upstream v0.2.0 ZIP is the first real candidate. CrossCraft Classic, CrossCraft SurvivalTest and TheXTech use exact tagged ZIP releases and are also candidates. The cache is not proof that their packages contain a VPK until the audit runs.
