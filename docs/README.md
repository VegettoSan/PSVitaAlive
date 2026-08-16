# PS Vita Alive Store documentation

This directory is the technical index and project memory. It exists so a contributor can understand the reason and responsibility of each layer before changing it.

## Official architecture

```text
apps/ + authors/ + categories/
            ↓
     external import / discovery
            ↓
       normalize + dedupe
            ↓
          merge
            ↓
        Overrides
            ↓
        validation
            ↓
catalog.json + authors.json + categories.json
            ↓
       Website + PS Vita
```

The three generated Homebrew catalogs are the public compatibility contract. They are outputs, not editable source files.

## Important decisions

### External catalogs

VitaDB and VitaDBtoo are external inputs. Their formats are not the canonical PS Vita Alive Store format.

Each source is adapted and normalized before its data is merged into the canonical model. The original source data is not modified to fit VitaHub.

### Title ID

`title_id` is the primary Vita identity used for deduplication, installation identity and update detection. External numeric IDs must not automatically become the canonical internal `id`.

### Versions

When several sources describe the same application, version and release-date freshness take precedence over fixed source priority. Source priority is used as a tie-breaker and preference when records are otherwise equivalent.

### Overrides

Manual PS Vita Alive Store data can complement or replace incomplete external fields. It must survive later external imports unless the protected-field rules explicitly prevent the change.

### Authors

Authors are individual entities. External records containing multiple names are split into separate profiles when identity separation is sufficiently clear.

### Media

The canonical layer should prefer absolute public URLs for images and downloads. Relative paths are only valid when they genuinely resolve inside a published PS Vita Alive Store resource tree.

### Generated catalogs

Never patch `catalog.json`, `authors.json` or `categories.json` by hand. Fix the responsible source, canonical record, Override, adapter, normalizer or generation rule instead.

## PS Vita client packaging and LiveArea

The native client now has a validated Vita LiveArea build layout under:

```text
Client PSVitaAlive/assets/sce_sys/
├── icon0.png
├── pic0.png
└── livearea/
    └── contents/
        ├── bg0.png
        ├── startup.png
        └── template.xml
```

The client packages this directory into `sce_sys/` when generating the VPK. The project path may contain spaces, so the client CMake configuration intentionally invokes `vita-pack-vpk` with relative arguments and `VERBATIM` rather than relying on the problematic absolute-path behavior of `vita_create_vpk()` for this step.

The four PNG assets are prepared as 8-bit indexed PNGs for the current Vita packaging flow. `pic0.png` is 960×544 and is also indexed to a maximum of 256 colors. See `Client PSVitaAlive/assets/sce_sys/README.md` for the exact asset contract.

## Historical technical problems preserved here

The current architecture was shaped by issues including:

- changing external endpoint behavior;
- VitaDB returning `200 + []` to incompatible User-Agents;
- external screenshots being stored as invalid relative paths;
- broken icons/avatars without fallbacks;
- multiple developers represented as one author;
- newly discovered authors without canonical profiles;
- concurrent Actions publishing the same records;
- recommended downloads being selected from source priority instead of release freshness;
- VPK packaging failing when the local project path contained spaces;
- Vita LiveArea PNGs requiring consistent indexed-image preparation.

These cases are documented so future changes do not reintroduce the same classes of failure.

## Change classification

Before changing a component, identify which layer owns the problem:

1. external acquisition;
2. normalization;
3. identity/deduplication;
4. merge/enrichment;
5. Overrides;
6. canonical persistence;
7. catalog generation;
8. validation;
9. publication/GitHub Pages;
10. website;
11. PS Vita client/build system.

Fix the responsible layer instead of introducing a patch in a generated catalog or downstream consumer.

## Documentation priority

When documentation conflicts with the real repository state, verify the current code and generated artifacts first, then update the documentation so the repository remains self-describing.
