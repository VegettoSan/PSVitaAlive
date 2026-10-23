# PSVitaAlive homebrew catalog — licensing, attribution and hosting access

## Purpose

PS Vita Alive Store (PSVitaAlive) is a free, community-driven index. Its maintainers and contributors **collect, verify, normalize, deduplicate, organize and update catalog information** discovered from developers and PS Vita community sources.

PSVitaAlive asks downstream users to recognize that compilation and maintenance work. **Attribution is a requirement for new original catalog material made available under CC BY 4.0**, subject to the rights and transition limits described below.

## 1. Historical CC0 dedication — unchanged

All catalog compilation material already dedicated under **CC0 1.0 Universal** remains available under that dedication. The historical catalog snapshot immediately before this policy change is preserved in Git at:

- Repository snapshot: https://github.com/VegettoSan/PSVitaAlive/tree/78b739951e3f4f8b035519de02302f8a82ec370c
- Historical license text: [CATALOG_LICENSE_CC0_LEGACY.md](CATALOG_LICENSE_CC0_LEGACY.md)
- CC0 legal text: https://creativecommons.org/publicdomain/zero/1.0/legalcode

**The CC0 dedication is irrevocable.** Users of that historical material do not acquire a new attribution obligation. Merely publishing the old content again or referencing it from a new generated catalog does not remove its CC0 status.

## 2. New original contributions — CC BY 4.0

**Effective from the publication of this policy on 18 October 2026**, new and original catalog descriptions, compilation choices, categorization/arrangement, or other editorial contributions **first made available after the historical snapshot above** are offered under **Creative Commons Attribution 4.0 International (CC BY 4.0)** *only to the extent PSVitaAlive or the relevant contributors hold the rights needed to grant that license*.

- License: https://creativecommons.org/licenses/by/4.0/
- Legal terms: https://creativecommons.org/licenses/by/4.0/legalcode.en
- Applies, where legally protectable and properly licensed, to original contributions made in `apps/`, `authors/`, `categories/`, their compilation and the corresponding generated `catalog.json`, `authors.json`, `categories.json`.
- These generated files can contain a **mix of historical CC0 material, CC BY 4.0 original contributions and independent third-party material**. Their publication does **not** relicense the pre-existing CC0 material or other people's work.
- Names, versions, title IDs, URLs and other bare facts are not automatically owned or protected simply because they appear in the catalog. Database-related rights and attribution obligations depend on applicable law, the amount or nature of reuse, and the rights actually licensed.
- A contributor must own the relevant rights or have permission to submit new material under CC BY 4.0. Material originating from an external database remains subject to its own terms unless appropriate rights or permissions have been obtained.
- No retroactive relicensing of the historical CC0 snapshot is intended.

This approach preserves free and commercial reuse while attaching a credit requirement to reuse that is governed by the new CC BY 4.0 license.

## 3. Attribution for CC BY 4.0 material

When sharing or adapting **material covered by the new CC BY 4.0 terms** in a way that engages licensed rights, give appropriate credit, a reference to the licensed material, a link to the CC BY 4.0 license and an indication of modifications (if any). Attribution may appear in your project's README, credits, documentation, website, or About screen, **in any reasonable manner for the medium**. It does not have to be displayed on every catalog page, nor must it use exact wording.

Suggested credit:

> Homebrew catalog curation and original metadata: [PS Vita Alive Store (PSVitaAlive)](https://github.com/VegettoSan/PSVitaAlive) and contributors — [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Modifications: [describe modifications, if any].

Do not imply that PSVitaAlive, its contributors, or the original homebrew developers endorse your application.

## 4. PSVitaAlive does not own the content it indexes

**PSVitaAlive does not own, and does not claim to own:**

- Third-party homebrew applications, plugins, ports, mods and tools
- VPKs, source code, Game Files, extra data packs and other downloadable binaries
- Icons, screenshots, artwork, brands, trademarks, logos and original releases
- Commercial PKGs, DLC, updates, game licenses or distribution rights
- External databases and contributions for which others retain applicable rights

The authors and rights holders of those works retain their rights. PSVitaAlive contributes its **original collection, selection, organization and maintenance work**, not ownership of the indexed applications or their files. The catalog's links, records or references **are not permissions to copy, distribute or relicense third-party content**.

Whenever possible, retain and respect original developer attribution, relevant upstream catalog acknowledgements, and links to official sources.

## 5. Upstream sources and commercial catalogs

Information may be discovered from creators and community sources, including [VitaDB](https://www.rinnegatamante.eu/vitadb/) (Rinnegatamante and contributors) and [VitaHomebrewDB](https://drdecki.github.io/VitaHomebrewDB/) (DrDecki and contributors). Those projects and their original authors retain credit and rights to their own work; PSVitaAlive does not apply CC BY 4.0 to their contributions merely by importing or referencing them.

PS Vita, PSP and PS1 **commercial-game catalogs**, license sidecars such as zRIF indexes, and all referenced third-party packages are **outside the license grant for new original homebrew catalog curation described here**. Respect each item's applicable rights and distribution terms.

## 6. VegettoSanDev / Cloudflare R2 service access policy

**This section governs access to PSVitaAlive-operated hosting infrastructure, not copyright or database reuse rights in catalog content.** The project maintainer privately funds Cloudflare R2 storage, bandwidth-related infrastructure and associated services for PSVitaAlive Store. Publicly listing a download URL in a GitHub catalog is **not** an invitation to make third-party applications rely on that hosting service.

- **Protected endpoints:** PSVitaAlive-operated Cloudflare R2-backed download/API endpoints, including `downloads.vegettosandev.com`, and any other VegettoSanDev-hosted R2 endpoints explicitly designated by the maintainer. This does not cover independent GitHub, Internet Archive, developer-hosted or other third-party URLs merely because they are present in the catalog.
- **Authorized use:** The official PSVitaAlive Store client and official first-party PSVitaAlive/VegettoSanDev websites may request or download files from these endpoints as intended by the maintainer, including user-initiated downloads through those websites.
- **Third-party use requires prior written authorization:** Other applications, stores, mirrors, bots and services must obtain explicit written permission from the PSVitaAlive maintainer **before** using the R2-backed endpoints for hotlinking, automated retrieval, proxies, bulk downloading or as their own download source. Merely copying a published URL, including one found in a GitHub JSON catalog, does not grant infrastructure access. A request for permission is not approval; approval may specify endpoints, duration, volume or other conditions.
- **No circumvention:** Do not impersonate the official PSVitaAlive client, spoof its headers/identity, reuse access credentials or otherwise bypass endpoint access controls to obtain third-party service access. PSVitaAlive may apply and update server-side access restrictions or rate limits to protect its hosting resources.
- **Other links remain governed by their existing terms:** This project does not impose this R2 service-use requirement on unrelated external URLs. The destination host's terms, the applicable catalog-data licenses and the original authors' rights continue to apply. A catalog link is not a grant of redistribution rights in any third-party download.

**No change to existing open licenses:** This separate service-access notice does **not** revoke or narrow historical CC0 permissions, add conditions to CC BY 4.0 licensed catalog material, or amend the MIT software license. Copying or reusing legally licensed catalog records and URL text from an authorized source remains permitted under those existing terms; that permission does not itself include a commitment by PSVitaAlive to serve files or authorize consumption of its privately operated R2 endpoints. The notice does not assert ownership over third-party files or independently hosted links.

To request special R2 endpoint access, contact the maintainer through this repository's GitHub Issues and obtain an **explicit written approval** before integrating the URLs. Endpoint restrictions are to be enforced by hosting configuration, not by this notice alone.

## 7. Software license

Client software, updater, build scripts, website tooling and other software covered by the repository's [MIT LICENSE](LICENSE) remain under **MIT**, unchanged by this catalog policy.

---

This document describes licensing scope and publication policy. It does not create rights in third-party material or override limitations and exceptions under applicable law.
