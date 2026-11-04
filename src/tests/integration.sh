#!/usr/bin/env sh
# Current portable integration suite. Uses only a temporary user profile.
set -eu
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 PATH_TO_CBACKUP" >&2
    exit 2
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v python3 >/dev/null 2>&1; then
    exec python3 "$script_dir/integration.py" "$1"
elif command -v python >/dev/null 2>&1; then
    exec python "$script_dir/integration.py" "$1"
else
    echo 'Python 3 is required to run the integration tests.' >&2
    exit 1
fi
