#!/usr/bin/env bash
# User installation for Linux (including ARM/Raspberry Pi) and macOS.
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
profile=""; job="default"; source_path=""; destination="$HOME/backups"; destination_given=false; deps=true; build_only=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --default) profile=default; shift;;
        --advanced) profile=advanced; shift;;
        --source) source_path="${2:?--source needs a path}"; shift 2;;
        --destination) destination="${2:?--destination needs a path}"; destination_given=true; shift 2;;
        --job) job="${2:?--job needs a name}"; shift 2;;
        --no-deps) deps=false; shift;;
        --build-only) build_only=true; shift;;
        *) echo "Usage: $0 [--default|--advanced] [--source PATH] [--destination PATH] [--job NAME] [--no-deps] [--build-only]" >&2; exit 2;;
    esac
done
if [[ "${EUID}" -eq 0 ]]; then echo 'Run as your normal user. Only dependencies use sudo.' >&2; exit 1; fi
if [[ -z "$profile" && "$build_only" == false ]]; then
    echo 'Default: incremental, zstd, every 2 hours, start at login, retain 90 days (about 3 months).'
    read -r -p 'Install default or advanced settings? [default]: ' profile; profile="${profile:-default}"
fi
[[ "$profile" == default || "$profile" == advanced || "$build_only" == true ]] || { echo 'Choose default or advanced.' >&2; exit 2; }
[[ "$job" =~ ^[a-zA-Z0-9_-]{1,60}$ ]] || { echo 'Invalid job name (letters, digits, _ and -).' >&2; exit 2; }
if [[ "$build_only" == false && -e "$HOME/.config/cbackup/jobs/$job.conf" ]]; then
    echo "Job '$job' already exists. Its settings were preserved." >&2
    echo "Edit it with: cbackup config $job show / cbackup config $job set KEY VALUE" >&2
    echo 'Use --job OTHER_NAME to install a separate backup job.' >&2
    exit 2
fi
if "$deps"; then
    if [[ "$(uname -s)" == Darwin ]]; then
        if ! xcode-select -p >/dev/null 2>&1; then
            xcode-select --install; echo 'Finish Apple Command Line Tools installation, then run again.' >&2; exit 1
        fi
        if ! command -v brew >/dev/null 2>&1; then echo 'Install Homebrew from https://brew.sh, then run again.' >&2; exit 1; fi
        brew install cmake pkg-config libarchive openssl@3 rclone
        export CMAKE_PREFIX_PATH="$(brew --prefix libarchive):$(brew --prefix openssl@3)${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update; sudo apt-get install -y build-essential cmake pkg-config libarchive-dev libssl-dev rclone
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config libarchive-devel openssl-devel rclone
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed base-devel cmake pkgconf libarchive openssl rclone
    elif command -v zypper >/dev/null 2>&1; then
        sudo zypper install -y gcc-c++ cmake pkg-config libarchive-devel libopenssl-devel rclone
    elif command -v apk >/dev/null 2>&1; then
        sudo apk add build-base cmake pkgconf libarchive-dev openssl-dev rclone
    else echo 'Install C++17, CMake, libarchive, OpenSSL, optional rclone; rerun with --no-deps.' >&2; exit 1
    fi
fi
cmake -S "$source_dir" -B "$source_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$source_dir/build" --parallel 2
"$build_only" && exit 0
cmake --install "$source_dir/build" --prefix "$HOME/.local"
binary="$HOME/.local/bin/cbackup"
if [[ -z "$source_path" ]]; then
    default_source="$HOME/Documents"
    if command -v xdg-user-dir >/dev/null 2>&1; then default_source="$(xdg-user-dir DOCUMENTS)"; fi
    read -r -p "Folder or file to back up [$default_source]: " source_path; source_path="${source_path:-$default_source}"
fi
[[ -e "$source_path" ]] || { echo "Source does not exist: $source_path" >&2; exit 1; }
if ! "$destination_given"; then
    read -r -p "Backup destination [$destination]: " answer; destination="${answer:-$destination}"
fi
"$binary" init "$job" "$source_path" --dest "$destination" --no-schedule
schedule_args=(--every 2h); automatic=true; encrypted=false; password_variable=CBACKUP_PASSWORD; backup_mode=auto
if [[ "$profile" == advanced ]]; then
    read -r -p 'Terminal language code (en,nl,es,de,uk,ru,pl,...) [en]: ' answer
    "$binary" language "${answer:-en}"
    "$binary" settings
    echo 'All choices remain editable afterwards with cbackup config / schedule / language.'
    while true; do
        read -r -p 'Another source file or folder (empty = done): ' answer
        [[ -n "$answer" ]] || break
        [[ -e "$answer" ]] || { echo "Source does not exist: $answer" >&2; exit 2; }
        "$binary" config "$job" add-source "$answer"
    done
    read -r -p 'What to protect: all/settings/installers [all]: ' answer
    "$binary" config "$job" set profile "${answer:-all}"
    read -r -p 'Mode: auto/full/incremental/differential/mirror [auto]: ' answer
    backup_mode="${answer:-auto}"
    "$binary" config "$job" set mode "$backup_mode"
    read -r -p 'Compression TYPE[:LEVEL]: zstd/gzip/xz/bzip2/lz4/zip/none [zstd]: ' answer
    "$binary" config "$job" set compression "${answer:-zstd}"
    read -r -p 'Retention days (0 disables the age limit) [90]: ' answer
    "$binary" config "$job" set keep_days "${answer:-90}"
    read -r -p 'Minimum full backup chains (0 = never prune) [1]: ' answer
    "$binary" config "$job" set keep_chains "${answer:-1}"
    read -r -p 'Automatic full checkpoint after this many snapshots [12]: ' answer
    "$binary" config "$job" set full_every "${answer:-12}"
    read -r -p 'Skip node_modules, virtual environments and known caches? true/false [true]: ' answer
    "$binary" config "$job" set exclude_dependencies "${answer:-true}"
    echo 'Optional filters: type one value per prompt. Leave empty to keep all files.'
    for filter in ext include exclude; do
        case "$filter" in
            ext) explanation='Allowed extension, e.g. txt (empty = done)';;
            include) explanation='Include pattern, e.g. src/* (empty = done)';;
            exclude) explanation='Excluded file/folder/pattern, e.g. private or *.tmp (empty = done)';;
        esac
        while true; do
            read -r -p "$explanation: " answer
            [[ -n "$answer" ]] || break
            "$binary" config "$job" "add-$filter" "$answer"
        done
    done
    read -r -p 'Extra copy on a mounted external drive (empty = off): ' answer
    [[ -z "$answer" ]] || "$binary" config "$job" set usb "$answer"
    read -r -p 'Extra cloud copy: configured rclone remote:path (empty = off): ' answer
    [[ -z "$answer" ]] || "$binary" config "$job" set cloud "$answer"
    read -r -p 'Maximum event log size in MB before rotation [5]: ' answer
    "$binary" config "$job" set log_max_mb "${answer:-5}"
    for health_key in notifications desktop_notifications stale_hours notify_repeat_hours test_restore_days test_restore_max_mb storage_quota_mb offline_reminder_days; do
        case "$health_key" in
            notifications) health_default=true; health_prompt='Record and report failures/stale backups? true/false';;
            desktop_notifications) health_default=false; health_prompt='Also display desktop notifications? true/false (requires a desktop session)';;
            stale_hours) health_default=24; health_prompt='Hours without a successful run before warning (0 = off)';;
            notify_repeat_hours) health_default=24; health_prompt='Repeat the same alert after N hours';;
            test_restore_days) health_default=7; health_prompt='Test restore interval in days (0 = off; tested after successful runs)';;
            test_restore_max_mb) health_default=64; health_prompt='Maximum sample file size for a test restore in MiB';;
            storage_quota_mb) health_default=0; health_prompt='Local backup storage budget in MiB (0 = unlimited; chains stay protected)';;
            offline_reminder_days) health_default=14; health_prompt='Offline-copy reminder interval in days (0 = off)';;
        esac
        read -r -p "$health_prompt [$health_default]: " answer
        "$binary" config "$job" set "$health_key" "${answer:-$health_default}"
    done
    read -r -p 'Start at login? true/false [true]: ' answer
    "$binary" config "$job" set autostart "${answer:-true}"
    read -r -p 'Back up after saved changes? true/false [false]: ' answer
    "$binary" config "$job" set watch "${answer:-false}"
    if [[ "${answer:-false}" == true ]]; then
        read -r -p 'Check for saved changes every N seconds (minimum 5) [30]: ' answer
        "$binary" config "$job" set watch_seconds "${answer:-30}"
    fi
    read -r -p 'Schedule: interval/daily/weekly/startup/manual [interval]: ' answer
    case "${answer:-interval}" in
        interval) read -r -p 'Interval (10min,2h,1d) [2h]: ' answer; schedule_args=(--every "${answer:-2h}");;
        daily) read -r -p 'Daily local time HH:MM [22:00]: ' answer; schedule_args=(--daily "${answer:-22:00}");;
        weekly) read -r -p 'Weekday and local time (Mon@HH:MM) [Sun@02:00]: ' answer; schedule_args=(--weekly "${answer:-Sun@02:00}");;
        startup) schedule_args=(--startup);;
        manual) automatic=false;;
        *) echo 'Invalid schedule.' >&2; exit 2;;
    esac
    read -r -p 'Encryption: none/aes-256-gcm [none]: ' answer
    if [[ "$backup_mode" == mirror && "${answer:-none}" != none ]]; then
        echo 'Encryption requires auto/full/incremental/differential; mirror cannot be encrypted.' >&2
        exit 2
    fi
    "$binary" config "$job" set encryption "${answer:-none}"
    if [[ "${answer:-none}" != none ]]; then
        encrypted=true; automatic=false
        echo 'Encrypted backups read CBACKUP_PASSWORD. Automatic scheduling is left off until you configure that secret in your OS scheduler environment.'
        echo 'Keep the password separately: losing it makes these backups unrecoverable.'
        read -r -p 'Environment variable holding the secret [CBACKUP_PASSWORD]: ' answer
        "$binary" config "$job" set password_env "${answer:-CBACKUP_PASSWORD}"
        password_variable="${answer:-CBACKUP_PASSWORD}"
    fi
fi
"$binary" config "$job" show
if "$encrypted" && [[ -z "${!password_variable:-}" ]]; then
    echo "Initial backup pending: set $password_variable securely, then run: cbackup run $job"
else "$binary" run "$job"
fi
if "$automatic"; then "$binary" schedule "$job" "${schedule_args[@]}"; else "$binary" schedule "$job" off; fi
printf '\nInstalled: %s\n' "$binary"
printf 'Add %s/.local/bin to PATH if needed.\n' "$HOME"
printf 'Manual backup: "%s" run %s\n' "$binary" "$job"
printf 'Settings: "%s" settings\nRestore IDs: "%s" list %s\n' "$binary" "$binary" "$job"
printf 'Later changes: cbackup config %s show\n' "$job"
printf '  cbackup config %s set keep_days 90\n  cbackup schedule %s --every 2h\n' "$job" "$job"
echo 'For an encrypted job, start a new full backup immediately after changing the password.'
echo 'Tasks use your user session. Linux: enable linger to run while logged out.'
