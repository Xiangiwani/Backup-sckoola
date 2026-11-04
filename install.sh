#!/usr/bin/env bash
# Build en installeer CBackup voor Linux/Raspberry Pi.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")"
if [[ "${EUID}" -eq 0 ]]; then
    echo "Voer dit script uit als je gewone gebruiker; sudo wordt alleen gebruikt voor installatie." >&2
    exit 1
fi
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y build-essential cmake pkg-config libarchive-dev libssl-dev rclone
else
    echo "Installeer C++17, cmake, pkg-config, libarchive-dev, libssl-dev en rclone met je pakketbeheerder." >&2
fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"
sudo cmake --install build --prefix /usr/local
printf '\nCBackup geïnstalleerd. Begin met:\n'
printf '  cbackup init mijnbackup\n  cbackup config mijnbackup add-source "$HOME/Documenten"\n  cbackup run mijnbackup\n'
printf 'Installatie plant niet automatisch back-ups in. Gebruik: cbackup schedule mijnbackup --every 10min\n'
