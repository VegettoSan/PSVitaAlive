# Saved ZIP recovery for serial and segmented downloads (2026-10-25)

## Contract

Only fully completed, ZIP-data extraction jobs enter this recovery path. VPK
promotion, PKG/BGDL, plugin installs, incomplete HTTP transfers and explicit
cancellation retain their existing behavior.

When ZIP extraction fails, the installer preserves the completed payload *before*
publishing the recovery choice. It never has to join the original range files.
The app/title folder under `ux0:data/psvitaalive/manual/` stores:

- Single file: `Archive.zip` + `Archive.zip.info.txt`
- Two or four ranges: `Archive.zip.001`, `.002` ... + `Archive.zip.info.txt`

The info file retains app ID, Title ID, intended extraction destination and
part count. Older single-ZIP metadata without a part count remains readable.

Saving after a download failure is not supported, because the archive might
be incomplete; an error from the extractor may still mean a downloaded ZIP
is internally corrupt. CRC and damaged-data warnings remain visible.

A completed extraction automatically removes the saved archive and metadata.
Failed or canceled offline extraction retains them for another attempt.
Files are never concatenated in Vita RAM or on disk for normal recovery.

## UI

The app detail's `Saved ZIP Files` lists both types, distinguishing complete
ZIPs from 2/4-part archives. Selecting a saved item opens a dialog:

1. Retry extraction using the existing worker and `ZipExtractor`.
2. Delete; requires a separate confirmation before removal of all parts.
3. Cancel and keep files intact.

The standard failed-install dialog retains the existing Keep/Delete/Report
actions. The selected archive is moved to persistent storage immediately
after extraction failure; confirmation of Keep does not copy it again.

## External tools

The `.001` etc. files are *raw ordered byte ranges*, not independently
compressed ZIP files. GUI file-join utilities (such as 7-Zip's Combine Files
feature) can concatenate them in numerical order to reconstruct the original
ZIP, then a normal archive extractor can open it. External compatibility,
especially automatic handling by particular Android versions of ZArchiver,
must be tested on actual devices before making unconditional claims.

The client can re-extract the retained parts itself without joining them.

## Interrupted relocation

Before relocation, the transient job gets a `.psva_keep_zip` marker so
startup cleanup does not destroy it if a relocation fails. Successful
relocation moves all parts using same-volume renames. Any failed move triggers
reverse-order rollback. This path is instrumented and must be validated
against simulated rename failures on hardware/emulation.

## Regression checklist

- Single ZIP failure -> Keep -> offline retry -> cleanup
- Segmented 2/4 parts failure -> saved .001... files -> offline retry
- Wrong/missing range or mismatched total -> no false 'saved complete' state
- CRC failure -> preserved payload plus corruption warning
- Power-off after recovery prompt -> files still listed on next startup
- Delete canceled at confirmation -> all parts unchanged
- Confirm delete -> every part and metadata removed
- Repeated failed extraction -> no network activity and same saved files
- >2 GiB ZIP64, deep-path staging, ordinary VPK, BGDL, plugins, HTTP failure
- Check frontend controls using buttons and touch
- Real Vita hardware validation after VitaSDK build

The canonical app/author/category JSON sources and generated catalogs are unchanged.
