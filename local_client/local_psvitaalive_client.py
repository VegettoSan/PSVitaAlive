#!/usr/bin/env python3
"""
PSVitaAlive Catalog Browser
A local Qt GUI for browsing, filtering, and downloading from the PSVitaAlive catalogs.
"""

import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlparse

from PySide6.QtCore import (
    Qt, QSortFilterProxyModel, QAbstractTableModel, QModelIndex, Signal,
    QObject, QThread,
)
from PySide6.QtGui import QFont, QColor, QPalette
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QTableView, QHeaderView, QLineEdit, QComboBox, QLabel, QPushButton,
    QFileDialog, QProgressBar, QStatusBar, QSplitter, QTextEdit,
    QCheckBox, QMessageBox, QAbstractItemView, QMenu,
)

import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

# ── Constants ────────────────────────────────────────────────────────────────

RAW_BASE = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main"
AUTHORS_FILE = "authors.json"
CATEGORIES_FILE = "categories.json"
CATALOGS = {
    "Homebrew":   "catalog.json",
    "Vita Games": "catalog_psvita_games.json",
    "PSP Games":  "catalog_psp_games.json",
    "PS1 Games":  "catalog_ps1_games.json",
}

DOWNLOADABLE_TYPES = {
    "Download", "PKG", "DLC", "Update", "Patch",
    "Data Files", "Game Files", "Mod", "Mod Pack", "Plugin", "Mirror",
}

COLUMNS = ["Name", "Catalog", "Category", "Subcategories", "Version", "Author", "Size", "Status", "Links"]
COL_NAME, COL_CATALOG, COL_CATEGORY, COL_SUBCATS, COL_VERSION, COL_AUTHOR, COL_SIZE, COL_STATUS, COL_LINKS = range(9)


# ── HTTP ─────────────────────────────────────────────────────────────────────

def make_session() -> requests.Session:
    s = requests.Session()
    retries = Retry(total=5, backoff_factor=1, status_forcelist=[429, 500, 502, 503, 504])
    adapter = HTTPAdapter(max_retries=retries, pool_maxsize=8)
    s.mount("https://", adapter)
    s.mount("http://", adapter)
    s.headers["User-Agent"] = "PSVitaAlive-Browser/1.0"
    return s


def human_size(n) -> str:
    if not n:
        return ""
    n = int(n)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if abs(n) < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} PB"


def safe_filename(url: str) -> str:
    path = unquote(urlparse(url).path)
    name = path.rsplit("/", 1)[-1] or "file"
    name = re.sub(r'[<>:"/\\|?*]', "_", name)
    return name[:200]


HTML_EXTENSIONS = {".html", ".htm", ".xhtml"}


def looks_like_html(chunk: bytes) -> bool:
    head = chunk[:512].lstrip().lower()
    return head.startswith((b"<!doctype html", b"<html", b"<head", b"<body"))


# ── Data model ───────────────────────────────────────────────────────────────

def build_lookups(authors_data, categories_data) -> dict:
    """Map IDs from authors.json / categories.json to display names."""
    authors = {a["id"]: a.get("name") or a["id"]
               for a in authors_data if isinstance(a, dict) and a.get("id")}
    categories = {}
    for c in categories_data:
        if not isinstance(c, dict) or not c.get("id"):
            continue
        categories[c["id"]] = {
            "name": c.get("name") or c["id"],
            "subcategories": {s["id"]: s.get("name") or s["id"]
                              for s in c.get("subcategories", []) if s.get("id")},
        }
    return {"authors": authors, "categories": categories}


def source_rank(link: dict) -> tuple:
    """Rank copies of one file. The highest rank is kept: the recommended copy first,
    then archive.org, which is the default source. Ties keep the first copy listed."""
    host = urlparse(link.get("url", "")).hostname or ""
    return (bool(link.get("recommended")), host == "archive.org" or host.endswith(".archive.org"))


def file_identity(link: dict) -> tuple:
    """Key that is equal for copies of one file and different for other files.

    A copy is a link with the same type, PlayStation content ID and exact byte
    size. The content ID alone is not enough: an Update keeps its content ID across
    versions, and its size tells the versions apart. Links without a known size
    are keyed by URL, so they are never collapsed.
    """
    kind = link.get("type", "")
    size = int(link.get("size") or 0)
    if not size:
        return (kind, link["url"])
    return (kind, link.get("content_id", ""), size)


class CatalogEntry:
    __slots__ = (
        "id", "name", "catalog", "category", "category_id", "subcategories",
        "subcategory_names", "version", "author", "author_names", "size",
        "status", "links", "description", "long_description",
        "raw",
    )

    def __init__(self, raw: dict, catalog_label: str, lookups: dict | None = None):
        lk = lookups or {}
        authors = lk.get("authors", {})
        categories = lk.get("categories", {})
        self.raw = raw
        self.id = raw.get("id", "")
        self.name = raw.get("name", self.id)
        self.catalog = catalog_label
        self.category_id = raw.get("category_id", "")
        cat = categories.get(self.category_id, {})
        self.category = cat.get("name") or self.category_id
        sub_names = cat.get("subcategories", {})
        self.subcategory_names = [sub_names.get(s, s) for s in raw.get("subcategory_ids", [])]
        self.subcategories = ", ".join(self.subcategory_names)
        self.version = raw.get("version", "")
        self.author_names = [authors.get(a, a) for a in raw.get("author_ids", [])]
        self.author = ", ".join(self.author_names)
        self.size = raw.get("size", 0) or 0
        self.status = raw.get("status", "")
        self.links = raw.get("links", [])
        self.description = raw.get("description", "")
        self.long_description = raw.get("long_description", "")

    def downloadable_links(self):
        return [l for l in self.links if l.get("type") in DOWNLOADABLE_TYPES and l.get("url")]

    def selected_links(self, recommended_only: bool = True):
        """Return the links to download for this entry.

        With recommended_only, mirror copies of the same file are collapsed into
        one link, keeping the best copy by source_rank. See file_identity for what
        counts as the same file. Other regions, DLC, updates of other versions and
        extra files are never collapsed.
        """
        links = self.downloadable_links()
        if not recommended_only:
            return links
        picked: dict[tuple, dict] = {}
        for link in links:
            key = file_identity(link)
            current = picked.get(key)
            if current is None or source_rank(link) > source_rank(current):
                picked[key] = link
        return list(picked.values())

    def total_download_size(self) -> int:
        return sum(l.get("size", 0) or 0 for l in self.downloadable_links())


class CatalogTableModel(QAbstractTableModel):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._entries: list[CatalogEntry] = []

    def set_entries(self, entries: list[CatalogEntry]):
        self.beginResetModel()
        self._entries = entries
        self.endResetModel()

    def entry_at(self, row: int) -> CatalogEntry | None:
        if 0 <= row < len(self._entries):
            return self._entries[row]
        return None

    def rowCount(self, parent=QModelIndex()):
        return len(self._entries)

    def columnCount(self, parent=QModelIndex()):
        return len(COLUMNS)

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role == Qt.DisplayRole and orientation == Qt.Horizontal:
            return COLUMNS[section]
        return None

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        e = self._entries[index.row()]
        col = index.column()

        if role == Qt.DisplayRole:
            if col == COL_NAME:
                return e.name
            if col == COL_CATALOG:
                return e.catalog
            if col == COL_CATEGORY:
                return e.category
            if col == COL_SUBCATS:
                return e.subcategories
            if col == COL_VERSION:
                return e.version
            if col == COL_AUTHOR:
                return e.author
            if col == COL_SIZE:
                return human_size(e.total_download_size()) if e.total_download_size() else human_size(e.size)
            if col == COL_STATUS:
                return e.status
            if col == COL_LINKS:
                dl = e.downloadable_links()
                return f"{len(dl)} file{'s' if len(dl) != 1 else ''}"
            return None

        if role == Qt.UserRole:
            # For sorting: return raw sortable values
            if col == COL_SIZE:
                return e.total_download_size() or e.size
            if col == COL_LINKS:
                return len(e.downloadable_links())
            return None

        if role == Qt.ToolTipRole:
            return e.description or e.long_description

        return None

    def sort(self, column, order=Qt.AscendingOrder):
        self.beginResetModel()
        reverse = order == Qt.DescendingOrder

        def key(entry):
            if column == COL_NAME:
                return entry.name.lower()
            if column == COL_CATALOG:
                return entry.catalog.lower()
            if column == COL_CATEGORY:
                return entry.category.lower()
            if column == COL_SUBCATS:
                return entry.subcategories.lower()
            if column == COL_VERSION:
                return entry.version.lower()
            if column == COL_AUTHOR:
                return entry.author.lower()
            if column == COL_SIZE:
                return entry.total_download_size() or entry.size
            if column == COL_STATUS:
                return entry.status.lower()
            if column == COL_LINKS:
                return len(entry.downloadable_links())
            return ""

        self._entries.sort(key=key, reverse=reverse)
        self.endResetModel()


class FilterProxyModel(QSortFilterProxyModel):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._search = ""
        self._catalog_filter = "All"
        self._category_filter = "All"
        self._subcategory_filter = "All"
        self._status_filter = "All"

    def set_search(self, text: str):
        self._search = text.lower()
        self.invalidateFilter()

    def set_catalog_filter(self, catalog: str):
        self._catalog_filter = catalog
        self.invalidateFilter()

    def set_category_filter(self, category: str):
        self._category_filter = category
        self.invalidateFilter()

    def set_subcategory_filter(self, subcategory: str):
        self._subcategory_filter = subcategory
        self.invalidateFilter()

    def set_status_filter(self, status: str):
        self._status_filter = status
        self.invalidateFilter()

    def filterAcceptsRow(self, source_row, source_parent):
        model = self.sourceModel()
        entry = model.entry_at(source_row)
        if not entry:
            return False

        if self._catalog_filter != "All" and entry.catalog != self._catalog_filter:
            return False

        if self._category_filter != "All":
            if entry.category.lower() != self._category_filter.lower():
                return False

        if self._subcategory_filter != "All":
            subs = {s.lower() for s in entry.subcategory_names}
            if self._subcategory_filter.lower() not in subs:
                return False

        if self._status_filter != "All":
            if (entry.status or "").lower() != self._status_filter.lower():
                return False

        if self._search:
            haystack = f"{entry.name} {' '.join(entry.author_names)} {entry.description} {entry.id} {entry.subcategories}".lower()
            for word in self._search.split():
                if word not in haystack:
                    return False

        return True


# ── Download worker ──────────────────────────────────────────────────────────

OK, UNVERIFIED, FAILED, CANCELLED = "ok", "unverified", "failed", "cancelled"


class DownloadSignals(QObject):
    progress = Signal(str, int, int)   # filename, downloaded, total
    finished = Signal(str, str)        # filename, one of OK / UNVERIFIED / FAILED / CANCELLED
    log = Signal(str)                  # log message
    # succeeded (including unverified), unverified, failed, cancelled
    all_done = Signal(int, int, int, int)


class DownloadWorker(QThread):
    """Downloads files one at a time.

    Each file is written to "<name>.part" and only renamed into place after the
    byte count has been verified. A file is never reported as successful unless
    its size matches the catalog size (when known) and the server's
    Content-Length (when it is sent unencoded). A response that is an HTML page
    instead of the requested file is reported as failed. Existing files are only
    kept when their size is known and matches; otherwise they are fetched again.
    A file whose size is unknown and has no Content-Length cannot be checked for
    completeness, so it is reported as unverified rather than as fully confirmed.
    """

    def __init__(self, tasks: list[dict], parent=None):
        super().__init__(parent)
        self.tasks = tasks  # [{url, dest, size, name}]
        self.signals = DownloadSignals()
        self._cancel = False

    def cancel(self):
        self._cancel = True

    def run(self):
        session = make_session()
        counts = {OK: 0, UNVERIFIED: 0, FAILED: 0, CANCELLED: 0}
        for index, task in enumerate(self.tasks):
            if self._cancel:
                counts[CANCELLED] += len(self.tasks) - index
                break
            status = self._process(session, task)
            counts[status] += 1
            self.signals.finished.emit(Path(task["dest"]).name, status)

        self.signals.all_done.emit(
            counts[OK] + counts[UNVERIFIED], counts[UNVERIFIED],
            counts[FAILED], counts[CANCELLED],
        )

    def _process(self, session, task: dict) -> str:
        url = task["url"]
        dest = Path(task["dest"])
        expected = int(task.get("size") or 0)
        fname = dest.name
        self.signals.log.emit(f"Downloading: {task.get('name', fname)}")
        try:
            dest.parent.mkdir(parents=True, exist_ok=True)
            if expected and dest.is_file() and dest.stat().st_size == expected:
                self.signals.log.emit(f"  Skipped (complete): {fname}")
                return OK

            part = dest.with_name(dest.name + ".part")
            allow_html = dest.suffix.lower() in HTML_EXTENSIONS
            status, detail = self._fetch(session, url, part, expected, fname, allow_html)
            if status in (FAILED, CANCELLED):
                discard(part)
                if status == CANCELLED:
                    self.signals.log.emit(f"  Cancelled: {fname}")
                else:
                    self.signals.log.emit(f"  FAILED: {fname} — {detail}")
                return status

            # The file is only moved to its final name once it is complete. If the
            # move fails (e.g. the destination is locked), discard the partial copy.
            try:
                os.replace(part, dest)
            except OSError as exc:
                discard(part)
                self.signals.log.emit(f"  FAILED: {fname} — could not move into place: {exc}")
                return FAILED

            if status == UNVERIFIED:
                self.signals.log.emit(f"  Done (size unknown, not verified): {fname} ({detail})")
            else:
                self.signals.log.emit(f"  Done: {fname} ({detail})")
            return status
        except Exception as exc:
            self.signals.log.emit(f"  FAILED: {fname} — {exc}")
            return FAILED

    def _fetch(self, session, url, part: Path, expected: int, fname: str, allow_html: bool):
        """Stream url into part. Returns (status, detail)."""
        html_error = "server returned an HTML page instead of the file"
        try:
            with session.get(url, stream=True, timeout=(30, 300)) as r:
                r.raise_for_status()
                if not allow_html and "html" in r.headers.get("Content-Type", "").lower():
                    return FAILED, html_error
                encoding = r.headers.get("Content-Encoding", "identity").lower()
                declared = None
                if encoding in ("", "identity"):
                    # Content-Length is only comparable to the body when not compressed.
                    declared = int(r.headers.get("Content-Length") or 0) or None
                total = expected or declared or 0
                written = 0
                with open(part, "wb") as f:
                    for chunk in r.iter_content(chunk_size=1 << 18):
                        if self._cancel:
                            return CANCELLED, "cancelled"
                        if written == 0 and not allow_html and looks_like_html(chunk):
                            return FAILED, html_error
                        f.write(chunk)
                        written += len(chunk)
                        self.signals.progress.emit(fname, written, total)

            if written == 0:
                return FAILED, "server returned an empty response"
            if expected and written != expected:
                return FAILED, f"size mismatch: got {human_size(written)}, expected {human_size(expected)}"
            if declared and written != declared:
                return FAILED, f"incomplete: got {written} of {declared} bytes"
            if not expected and not declared:
                return UNVERIFIED, human_size(written)
            return OK, human_size(written)
        except Exception as exc:
            return FAILED, str(exc)


def discard(path: Path):
    """Remove a partial file, ignoring errors (it may be locked or already gone)."""
    try:
        path.unlink(missing_ok=True)
    except OSError:
        pass


# ── Catalog loader (background thread) ───────────────────────────────────────

def _get_json(session, filename: str):
    r = session.get(f"{RAW_BASE}/{filename}", timeout=30)
    r.raise_for_status()
    return r.json()


class CatalogLoader(QThread):
    message = Signal(str)   # status text
    loaded = Signal(object) # dict: entries, categories, subcategories, statuses

    def run(self):
        session = make_session()
        try:
            lookups = build_lookups(_get_json(session, AUTHORS_FILE),
                                    _get_json(session, CATEGORIES_FILE))
        except Exception as exc:
            lookups = {"authors": {}, "categories": {}}
            self.message.emit(f"Author/category names unavailable, showing IDs: {exc}")

        entries, categories, subcategories, statuses = [], set(), set(), set()
        for label, filename in CATALOGS.items():
            try:
                data = _get_json(session, filename)
            except Exception as exc:
                self.message.emit(f"Failed to load {label}: {exc}")
                continue
            for item in data:
                entry = CatalogEntry(item, label, lookups)
                entries.append(entry)
                if entry.category:
                    categories.add(entry.category)
                subcategories.update(entry.subcategory_names)
                if entry.status:
                    statuses.add(entry.status)
            self.message.emit(f"Loaded {label}: {len(data)} entries")

        self.loaded.emit({
            "entries": entries,
            "categories": categories,
            "subcategories": subcategories,
            "statuses": statuses,
        })


# ── Main Window ──────────────────────────────────────────────────────────────

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("PSVitaAlive Catalog Browser")
        self.resize(1280, 800)

        self._download_dir = str(Path.home() / "Downloads" / "PSVitaAlive_Archive")
        self._worker: DownloadWorker | None = None
        self._loader: CatalogLoader | None = None
        self._dl_done_count = 0
        self._dl_total = 0

        self._build_ui()
        self._load_catalogs()

    # ── UI ────────────────────────────────────────────────────────────────

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # -- Toolbar row --
        toolbar = QHBoxLayout()

        toolbar.addWidget(QLabel("Search:"))
        self.search_box = QLineEdit()
        self.search_box.setPlaceholderText("Type to filter by name, author, description...")
        self.search_box.setClearButtonEnabled(True)
        self.search_box.textChanged.connect(self._on_search)
        toolbar.addWidget(self.search_box, 3)

        toolbar.addWidget(QLabel("Catalog:"))
        self.catalog_combo = QComboBox()
        self.catalog_combo.addItem("All")
        for name in CATALOGS:
            self.catalog_combo.addItem(name)
        self.catalog_combo.currentTextChanged.connect(self._on_catalog_filter)
        toolbar.addWidget(self.catalog_combo, 1)

        toolbar.addWidget(QLabel("Category:"))
        self.category_combo = QComboBox()
        self.category_combo.addItem("All")
        self.category_combo.currentTextChanged.connect(self._on_category_filter)
        toolbar.addWidget(self.category_combo, 1)

        toolbar.addWidget(QLabel("Subcategory:"))
        self.subcategory_combo = QComboBox()
        self.subcategory_combo.addItem("All")
        self.subcategory_combo.currentTextChanged.connect(self._on_subcategory_filter)
        toolbar.addWidget(self.subcategory_combo, 1)

        toolbar.addWidget(QLabel("Status:"))
        self.status_combo = QComboBox()
        self.status_combo.addItem("All")
        self.status_combo.currentTextChanged.connect(self._on_status_filter)
        toolbar.addWidget(self.status_combo, 1)

        self.refresh_btn = QPushButton("Refresh Catalogs")
        self.refresh_btn.clicked.connect(self._load_catalogs)
        toolbar.addWidget(self.refresh_btn)

        layout.addLayout(toolbar)

        # -- Splitter: table + detail panel --
        splitter = QSplitter(Qt.Vertical)

        # Table
        self.model = CatalogTableModel()
        self.proxy = FilterProxyModel()
        self.proxy.setSourceModel(self.model)

        self.table = QTableView()
        self.table.setModel(self.proxy)
        self.table.setSortingEnabled(True)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setDefaultSectionSize(24)
        self.table.verticalHeader().hide()
        self.table.setContextMenuPolicy(Qt.CustomContextMenu)
        self.table.customContextMenuRequested.connect(self._context_menu)

        header = self.table.horizontalHeader()
        header.setStretchLastSection(True)
        header.setSectionResizeMode(COL_NAME, QHeaderView.Stretch)
        for col in (COL_CATALOG, COL_CATEGORY, COL_VERSION, COL_STATUS, COL_LINKS):
            header.setSectionResizeMode(col, QHeaderView.ResizeToContents)

        self.table.selectionModel().selectionChanged.connect(self._on_selection)
        splitter.addWidget(self.table)

        # Detail panel
        detail_widget = QWidget()
        detail_layout = QVBoxLayout(detail_widget)
        detail_layout.setContentsMargins(4, 4, 4, 4)

        self.detail_title = QLabel("Select an entry to see details")
        self.detail_title.setFont(QFont("", 12, QFont.Bold))
        detail_layout.addWidget(self.detail_title)

        self.detail_text = QTextEdit()
        self.detail_text.setReadOnly(True)
        self.detail_text.setMaximumHeight(180)
        detail_layout.addWidget(self.detail_text)

        # Download controls
        dl_row = QHBoxLayout()

        self.dest_label = QLabel(f"Download to: {self._download_dir}")
        self.dest_label.setWordWrap(True)
        dl_row.addWidget(self.dest_label, 3)

        self.choose_dir_btn = QPushButton("Choose Folder...")
        self.choose_dir_btn.clicked.connect(self._choose_dir)
        dl_row.addWidget(self.choose_dir_btn)

        self.recommended_only = QCheckBox("Recommended sources only")
        self.recommended_only.setChecked(True)
        self.recommended_only.setToolTip(
            "When ticked, mirror copies of the same file are collapsed to one link, "
            "preferring the recommended one, then archive.org. Other regions, DLC and updates are always kept. "
            "Untick to download every available link, including mirrors."
        )
        dl_row.addWidget(self.recommended_only)

        self.dl_selected_btn = QPushButton("Download Selected")
        self.dl_selected_btn.clicked.connect(self._download_selected)
        dl_row.addWidget(self.dl_selected_btn)

        self.dl_all_btn = QPushButton("Download All Visible")
        self.dl_all_btn.clicked.connect(self._download_all_visible)
        dl_row.addWidget(self.dl_all_btn)

        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setEnabled(False)
        self.cancel_btn.clicked.connect(self._cancel_download)
        dl_row.addWidget(self.cancel_btn)

        detail_layout.addLayout(dl_row)

        # Progress
        self.progress_bar = QProgressBar()
        self.progress_bar.setVisible(False)
        detail_layout.addWidget(self.progress_bar)

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setMaximumHeight(100)
        self.log_text.setFont(QFont("Consolas", 9))
        detail_layout.addWidget(self.log_text)

        splitter.addWidget(detail_widget)
        splitter.setSizes([500, 300])
        layout.addWidget(splitter)

        # Status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.count_label = QLabel()
        self.status_bar.addPermanentWidget(self.count_label)

    # ── Catalog loading ───────────────────────────────────────────────────

    def _load_catalogs(self):
        if self._loader and self._loader.isRunning():
            return
        self.status_bar.showMessage("Loading catalogs...")
        self.refresh_btn.setEnabled(False)
        self._loader = CatalogLoader(self)
        self._loader.message.connect(lambda m: self.status_bar.showMessage(m))
        self._loader.loaded.connect(self._on_catalogs_loaded)
        self._loader.start()

    def _on_catalogs_loaded(self, result: dict):
        entries = result["entries"]
        categories = result["categories"]
        subcategories = result["subcategories"]
        statuses = result["statuses"]

        self.model.set_entries(entries)
        self.table.sortByColumn(COL_NAME, Qt.AscendingOrder)

        # Populate category combo
        self.category_combo.blockSignals(True)
        self.category_combo.clear()
        self.category_combo.addItem("All")
        for cat in sorted(categories):
            self.category_combo.addItem(cat)
        self.category_combo.blockSignals(False)

        # Populate subcategory combo
        self.subcategory_combo.blockSignals(True)
        self.subcategory_combo.clear()
        self.subcategory_combo.addItem("All")
        for sc in sorted(subcategories):
            self.subcategory_combo.addItem(sc)
        self.subcategory_combo.blockSignals(False)

        # Populate status combo
        self.status_combo.blockSignals(True)
        self.status_combo.clear()
        self.status_combo.addItem("All")
        for st in sorted(statuses):
            self.status_combo.addItem(st)
        self.status_combo.blockSignals(False)

        self._update_count()
        self.refresh_btn.setEnabled(True)
        self.status_bar.showMessage(f"Ready — {len(entries)} entries loaded", 5000)

    # ── Filters ───────────────────────────────────────────────────────────

    def _on_search(self, text):
        self.proxy.set_search(text)
        self._update_count()

    def _on_catalog_filter(self, text):
        self.proxy.set_catalog_filter(text)
        self._update_count()

    def _on_category_filter(self, text):
        self.proxy.set_category_filter(text)
        self._update_count()

    def _on_subcategory_filter(self, text):
        self.proxy.set_subcategory_filter(text)
        self._update_count()

    def _on_status_filter(self, text):
        self.proxy.set_status_filter(text)
        self._update_count()

    def _update_count(self):
        visible = self.proxy.rowCount()
        total = self.model.rowCount()
        self.count_label.setText(f"Showing {visible} of {total}")

    # ── Selection / detail ────────────────────────────────────────────────

    def _on_selection(self):
        indexes = self.table.selectionModel().selectedRows()
        if not indexes:
            self.detail_title.setText("Select an entry to see details")
            self.detail_text.clear()
            return

        if len(indexes) == 1:
            source_row = self.proxy.mapToSource(indexes[0]).row()
            entry = self.model.entry_at(source_row)
            if not entry:
                return
            self.detail_title.setText(f"{entry.name}  [{entry.catalog}]")

            lines = []
            if entry.description:
                lines.append(entry.description)
            if entry.long_description and entry.long_description != entry.description:
                lines.append("")
                lines.append(entry.long_description)
            lines.append("")
            lines.append(f"ID: {entry.id}   Version: {entry.version}   Author: {entry.author}")

            dl_links = entry.downloadable_links()
            if dl_links:
                lines.append("")
                lines.append(f"Downloadable files ({len(dl_links)}):")
                for l in dl_links:
                    sz = human_size(l.get("size")) if l.get("size") else "?"
                    lines.append(f"  [{l.get('type')}] {l.get('name', '')} — {sz}")
                    lines.append(f"    {l.get('url', '')}")

            self.detail_text.setPlainText("\n".join(lines))
        else:
            count = len(indexes)
            total_files = 0
            total_size = 0
            for idx in indexes:
                source_row = self.proxy.mapToSource(idx).row()
                entry = self.model.entry_at(source_row)
                if entry:
                    dl = entry.downloadable_links()
                    total_files += len(dl)
                    total_size += sum(l.get("size", 0) or 0 for l in dl)
            self.detail_title.setText(f"{count} entries selected")
            self.detail_text.setPlainText(
                f"Total downloadable files: {total_files}\n"
                f"Total known size: {human_size(total_size)}"
            )

    # ── Context menu ──────────────────────────────────────────────────────

    def _context_menu(self, pos):
        indexes = self.table.selectionModel().selectedRows()
        if not indexes:
            return
        menu = QMenu(self)
        dl_action = menu.addAction("Download selected entries")
        dl_action.triggered.connect(self._download_selected)
        menu.exec(self.table.viewport().mapToGlobal(pos))

    # ── Download ──────────────────────────────────────────────────────────

    def _choose_dir(self):
        d = QFileDialog.getExistingDirectory(self, "Choose download folder", self._download_dir)
        if d:
            self._download_dir = d
            self.dest_label.setText(f"Download to: {d}")

    def _entries_from_selection(self) -> list[CatalogEntry]:
        indexes = self.table.selectionModel().selectedRows()
        entries = []
        for idx in indexes:
            source_row = self.proxy.mapToSource(idx).row()
            e = self.model.entry_at(source_row)
            if e:
                entries.append(e)
        return entries

    def _entries_visible(self) -> list[CatalogEntry]:
        entries = []
        for row in range(self.proxy.rowCount()):
            source_row = self.proxy.mapToSource(self.proxy.index(row, 0)).row()
            e = self.model.entry_at(source_row)
            if e:
                entries.append(e)
        return entries

    def _build_tasks(self, entries: list[CatalogEntry], recommended_only: bool) -> list[dict]:
        tasks = []
        seen = set()
        for entry in entries:
            for link in entry.selected_links(recommended_only):
                url = link.get("url", "")
                if not url or url in seen:
                    continue
                seen.add(url)
                fname = safe_filename(url)
                dest = Path(self._download_dir) / entry.catalog / entry.id / fname
                tasks.append({
                    "url": url,
                    "dest": str(dest),
                    "size": link.get("size"),
                    "name": f"{entry.name} — {link.get('name', fname)}",
                })
        return tasks

    def _download_selected(self):
        entries = self._entries_from_selection()
        if not entries:
            self.status_bar.showMessage("No entries selected", 3000)
            return
        tasks = self._build_tasks(entries, self.recommended_only.isChecked())
        self._start_download(tasks)

    def _download_all_visible(self):
        entries = self._entries_visible()
        if not entries:
            self.status_bar.showMessage("No visible entries", 3000)
            return

        recommended = self.recommended_only.isChecked()
        tasks = self._build_tasks(entries, recommended)
        total_size = sum(t["size"] or 0 for t in tasks)
        mode = "recommended sources only" if recommended else "ALL sources, including mirrors"
        reply = QMessageBox.question(
            self, "Confirm bulk download",
            f"Download {len(tasks)} file(s) from {len(entries)} visible entries?\n"
            f"Mode: {mode}\n"
            f"Estimated size: {human_size(total_size) or 'unknown'}\n\n"
            f"Destination: {self._download_dir}",
            QMessageBox.Yes | QMessageBox.No,
        )
        if reply != QMessageBox.Yes:
            return
        self._start_download(tasks)

    def _start_download(self, tasks: list[dict]):
        if not tasks:
            self.status_bar.showMessage("Nothing to download", 3000)
            return

        self.log_text.clear()
        self.log_text.append(f"Starting download of {len(tasks)} file(s)...")
        self.progress_bar.setVisible(True)
        self.progress_bar.setMaximum(len(tasks))
        self.progress_bar.setValue(0)
        self.dl_selected_btn.setEnabled(False)
        self.dl_all_btn.setEnabled(False)
        self.cancel_btn.setEnabled(True)
        self._dl_done_count = 0
        self._dl_total = len(tasks)

        self._worker = DownloadWorker(tasks)
        self._worker.signals.log.connect(self._on_dl_log)
        self._worker.signals.progress.connect(self._on_dl_progress)
        self._worker.signals.finished.connect(self._on_dl_file_done)
        self._worker.signals.all_done.connect(self._on_dl_all_done)
        self._worker.start()

    def _on_dl_log(self, msg):
        self.log_text.append(msg)
        self.log_text.verticalScrollBar().setValue(self.log_text.verticalScrollBar().maximum())

    def _on_dl_progress(self, fname, downloaded, total):
        if total > 0:
            pct = int(downloaded * 100 / total)
            self.status_bar.showMessage(f"{fname}: {human_size(downloaded)} / {human_size(total)} ({pct}%)")
        else:
            self.status_bar.showMessage(f"{fname}: {human_size(downloaded)}")

    def _on_dl_file_done(self, fname, status):
        self._dl_done_count += 1
        self.progress_bar.setValue(self._dl_done_count)

    def _on_dl_all_done(self, succeeded, unverified, failed, cancelled):
        self.progress_bar.setVisible(False)
        self.dl_selected_btn.setEnabled(True)
        self.dl_all_btn.setEnabled(True)
        self.cancel_btn.setEnabled(False)
        summary = f"{succeeded} succeeded"
        if unverified:
            summary += f" ({unverified} with unknown size, not verified)"
        summary += f", {failed} failed, {cancelled} cancelled, of {self._dl_total} files"
        self.status_bar.showMessage(f"Download finished: {summary}", 10000)
        self.log_text.append(f"\nAll done — {summary}.")

    def _cancel_download(self):
        if self._worker:
            self._worker.cancel()
            self.status_bar.showMessage("Cancelling...")


# ── Entry point ──────────────────────────────────────────────────────────────

def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    # Dark palette
    palette = QPalette()
    palette.setColor(QPalette.Window, QColor(30, 30, 30))
    palette.setColor(QPalette.WindowText, QColor(220, 220, 220))
    palette.setColor(QPalette.Base, QColor(25, 25, 25))
    palette.setColor(QPalette.AlternateBase, QColor(35, 35, 35))
    palette.setColor(QPalette.ToolTipBase, QColor(50, 50, 50))
    palette.setColor(QPalette.ToolTipText, QColor(220, 220, 220))
    palette.setColor(QPalette.Text, QColor(220, 220, 220))
    palette.setColor(QPalette.Button, QColor(45, 45, 45))
    palette.setColor(QPalette.ButtonText, QColor(220, 220, 220))
    palette.setColor(QPalette.Highlight, QColor(42, 130, 218))
    palette.setColor(QPalette.HighlightedText, QColor(255, 255, 255))
    palette.setColor(QPalette.PlaceholderText, QColor(120, 120, 120))
    app.setPalette(palette)

    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
