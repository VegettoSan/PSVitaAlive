# PSVitaAlive Catalog Browser (PC client)

A desktop app for browsing the PSVitaAlive catalogs and downloading files for
archiving. Files are saved in folders named after the catalog and entry.

## Install and run

You need Python 3.10 or newer. The installer checks for it and tells you what to
do if it is missing. It creates a private environment and installs the
dependencies, so nothing is added to the rest of your system.

**Windows**

1. Double-click `install_and_run.bat`.
2. If Python is missing, it will say so and link to the installer. Tick
   **"Add python.exe to PATH"** on the first screen, then double-click the file again.

**macOS**

1. Double-click `install_and_run.sh`. Finder opens it in Terminal. If macOS refuses,
   run `chmod +x install_and_run.sh` once, then try again.
2. If Python is missing, install it with `brew install python@3.12` or from
   https://www.python.org/downloads/, then run it again.

**Linux**

```sh
./install_and_run.sh
```

On Debian or Ubuntu, if it asks for `python3-venv`, install it with
`sudo apt install python3-venv`.

To install without starting the app, run `./install_and_run.sh --setup-only`
(Linux and macOS).

## Using the app

- **Search** filters by name, author, description and ID. The Catalog, Category,
  Subcategory and Status drop-downs narrow the list further.
- **Download Selected** downloads the files for the rows you have selected.
  **Download All Visible** downloads everything in the current filtered list, after
  asking you to confirm.
- **Recommended sources only** (on by default) collapses mirror copies of the same
  file into one download. It uses the recommended copy when the catalog marks one,
  otherwise the archive.org copy.
  Other regions, DLC, updates and extra files are always kept. Untick it to download
  every available link, including every mirror. The confirmation dialog shows which
  mode is active.
- **Cancel** stops after the current file. Unfinished files are removed.
- When a batch finishes, the log and status bar show how many files succeeded, how
  many failed, and how many were cancelled.
- **Choose Folder** changes where files are saved. The default is
  `Downloads/PSVitaAlive_Archive` in your home folder.

## How downloads are checked

- Each file is written to a temporary `.part` name and only renamed to its final name
  once it is complete.
- A file counts as successful only if its size matches the catalog size, and the
  server's reported length when one is sent.
- If the catalog and the server both give no size, the file is saved but reported
  as "not verified", since a cut-off transfer cannot be detected.
- A response that is an HTML web page instead of the file is reported as failed. This
  happens with some file-hosting pages, which return a web page rather than the file.
- An empty response is reported as failed.
- An existing file is kept only if its size matches the catalog. If the catalog has
  no size for it, the file is downloaded again.
- If a finished file cannot be moved to its final name (for example because it is
  open in another program), it is reported as failed and the partial copy is removed.
- Failed or cancelled downloads leave no partial file behind.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| "Python 3.10 or newer was not found" | Install Python from python.org and tick "Add to PATH" (Windows), or use the command the message shows. |
| Installing dependencies failed | Check your internet connection and run the installer again. If it keeps failing, delete the `.venv` folder next to the script and run it again. |
| Author or category shows as an ID | The catalog metadata could not be downloaded. Use Refresh Catalogs once you're back online. |
| A file failed with "size mismatch" | The catalog size and the server copy disagree. Try again later; if it persists, report it. |
| A file failed with "server returned an HTML page" | The link opened a web page instead of the file, usually a file-hosting page. Untick "Recommended sources only" to try the other links for that entry, or report the link. |
