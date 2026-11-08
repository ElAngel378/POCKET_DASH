#!/bin/sh
# Builds Pocket Dash on Linux and macOS. Needs python3 (almost always there already).
#   ./build.sh            builds the game
#   ./build.sh mylevel    builds it and starts that level
#   ./build.sh new mylevel "My Level"    makes a new level
cd "$(dirname "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3 is missing. Install it with your package manager (for example: sudo apt install python3 python3-venv)"
    exit 1
fi
if [ ! -x .tools/venv/bin/python ]; then
    echo "Setting up Python for Pocket Dash. This only happens once..."
    mkdir -p .tools
    python3 -m venv .tools/venv || { echo "Couldn't create a Python environment. On Debian or Ubuntu: sudo apt install python3-venv"; exit 1; }
    .tools/venv/bin/python -m pip install --quiet pillow || { echo "Couldn't install Pillow. Check your internet connection and try again."; rm -rf .tools/venv; exit 1; }
fi
if [ "$1" = "new" ]; then
    shift
    exec .tools/venv/bin/python tools/new_level.py "$@"
fi
exec .tools/venv/bin/python tools/build.py "$@"
