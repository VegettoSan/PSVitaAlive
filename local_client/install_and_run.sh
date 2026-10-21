#!/usr/bin/env bash
# PSVitaAlive Catalog Browser (PC client) - installer and launcher for macOS / Linux.
#
#   ./install_and_run.sh          check Python, install dependencies, start the app
#   ./install_and_run.sh --setup-only   only check and install, don't start the app
#
# Every problem prints a clear message and exits non-zero. Nothing fails silently.

set -u
cd "$(dirname "$0")" || exit 1

APP="local_psvitaalive_client.py"
REQ="requirements.txt"
VENV=".venv"
MIN_MAJOR=3
MIN_MINOR=10

die() {
    echo "" >&2
    echo "ERROR: $*" >&2
    exit 1
}

info() { echo "==> $*"; }

find_python() {
    local cand
    for cand in python3.13 python3.12 python3.11 python3.10 python3 python; do
        if command -v "$cand" >/dev/null 2>&1 &&
           "$cand" -c "import sys; sys.exit(0 if sys.version_info >= ($MIN_MAJOR, $MIN_MINOR) else 1)" >/dev/null 2>&1; then
            command -v "$cand"
            return 0
        fi
    done
    return 1
}

PY="$(find_python)" || {
    echo "Python $MIN_MAJOR.$MIN_MINOR or newer was not found on this computer." >&2
    echo "" >&2
    case "$(uname -s)" in
        Darwin)
            echo "Install it with Homebrew:   brew install python@3.12" >&2
            echo "or download it from:        https://www.python.org/downloads/" >&2
            ;;
        Linux)
            echo "Install it with your package manager, for example:" >&2
            echo "  Debian / Ubuntu:  sudo apt install python3 python3-venv" >&2
            echo "  Fedora:           sudo dnf install python3" >&2
            echo "  Arch:             sudo pacman -S python" >&2
            ;;
        *)
            echo "Download it from: https://www.python.org/downloads/" >&2
            ;;
    esac
    exit 1
}
info "Using Python: $PY ($("$PY" -c 'import sys; print(sys.version.split()[0])'))"

if [ ! -x "$VENV/bin/python" ]; then
    info "Creating a private Python environment in $VENV ..."
    "$PY" -m venv "$VENV" || die "Could not create the environment. On Debian/Ubuntu run: sudo apt install python3-venv"
fi

info "Checking dependencies ..."
"$VENV/bin/python" -m pip install --quiet --disable-pip-version-check -r "$REQ" ||
    die "Installing dependencies failed. Check your internet connection and try again. If it keeps failing, delete the $VENV folder and run this again."

if [ "${1:-}" = "--setup-only" ]; then
    info "Setup complete."
    exit 0
fi

info "Starting PSVitaAlive Catalog Browser ..."
exec "$VENV/bin/python" "$APP"
