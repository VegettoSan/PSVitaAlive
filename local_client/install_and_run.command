#!/usr/bin/env bash
# macOS: double-click this file in Finder to install and start the app.
cd "$(dirname "$0")" || exit 1
./install_and_run.sh
status=$?
echo ""
read -r -n 1 -s -p "Press any key to close this window..."
echo ""
exit $status
