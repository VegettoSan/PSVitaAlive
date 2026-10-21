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

1. Double-click `install_and_run.command`. The first time, macOS may ask you to
   confirm opening a file from an unidentified developer. Right-click it and choose
   Open if so.
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
- **Recommended sources only** (on by default) skips mirror and alternate links
  when the catalog marks a recommended link for the same file type. Untick it to
  download every available link, including mirrors. The confirmation dialog shows
  which mode is active.
- **Cancel** stops after the current file. Unfinished files are removed.
- **Choose Folder** changes where files are saved. The default is
  `Downloads/PSVitaAlive_Archive` in your home folder.

## How downloads are checked

- Each file is written to a temporary `.part` name and only renamed to its final name
  once it is complete.
- A file counts as successful only if its size matches the catalog size, and the
  server's reported length when one is sent.
- An existing file is kept only if its size matches the catalog. If the catalog has
  no size for it, the file is downloaded again.
- Failed or cancelled downloads leave no partial file behind.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| "Python 3.10 or newer was not found" | Install Python from python.org and tick "Add to PATH" (Windows), or use the command the message shows. |
| Installing dependencies failed | Check your internet connection and run the installer again. If it keeps failing, delete the `.venv` folder next to the script and run it again. |
| Author or category shows as an ID | The catalog metadata could not be downloaded. Use Refresh Catalogs once you're back online. |
| A file failed with "size mismatch" | The catalog size and the server copy disagree. Try again later; if it persists, report it. |

## Files

- `local_psvitaalive_client.py`: the application.
- `requirements.txt`: Python dependencies (PySide6 and requests).
- `install_and_run.bat`: Windows installer and launcher.
- `install_and_run.command`: macOS double-click launcher.
- `install_and_run.sh`: Linux and macOS installer and launcher.
