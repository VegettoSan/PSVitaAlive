# `assets/sce_sys/` — LiveArea package assets

Assets packaged into the client VPK for LiveArea presentation.

## Typical contents

```text
icon0.png
pic0.png
livearea/contents/bg0.png
livearea/contents/startup.png
livearea/contents/template.xml
```

## Notes

- Keep dimensions and formats compatible with Vita LiveArea requirements (indexed/PNG constraints as enforced by the toolchain).
- Title ID and LiveArea metadata must stay consistent with CMake / `vita_create_vpk` configuration (**PSVAS1178**).
- Do not replace these casually without testing install + LiveArea appearance on device or Vita3K.
