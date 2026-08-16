# `source/ui/` — Native user interface

The client UI is rendered with **vita2d** and does not depend on Dear ImGui.

## Main screens

`FullCatalogScreen` currently supports the main catalog/detail presentation and the related overlays:

- `FULL_CATALOG` grid view;
- `SPLIT_DETAIL` detail view;
- catalog loading/error state;
- download/install progress;
- final success/error result state.

The visual design targets the PS Vita's 960×544 landscape display and uses the project's dark background with the green `#3BFF00` accent.

## Installation overlay

The installation progress/result state can carry:

- progress percentage;
- cancel action;
- success/error outcome;
- destination path;
- Title ID;
- LiveArea verification result;
- a diagnostic hint pointing to the session log.

## Callbacks

- `setInstallCancelCallback` — cancels an active operation.
- `setInstallAcknowledgeCallback` — closes the final result panel.

## UI contracts

The UI must not perform filesystem writes or call the Promoter directly. It reads state from the installation/controller layers and requests actions such as install, cancel and acknowledge.

Keep rendering, navigation and application state separate from network/storage/installer implementation details.
