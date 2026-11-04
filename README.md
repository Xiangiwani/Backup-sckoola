# CBackup 1.3

[Nederlandse snelstart en instelvoorbeelden](README.nl.md)

CBackup is a C++17 command-line backup program for regular files and folders. It supports full, incremental, differential and mirror backups, compression, optional authenticated encryption, restore by snapshot ID, change history, undo of in-place restores, exclusions and scheduled background runs.

The native platform layer targets Windows 10/11, Linux (including Raspberry Pi OS) and macOS. The same source is built separately for each operating system and architecture; a Linux binary is not a Windows executable. See **Validation and limits** before relying on it for important data.

## Simple setup

Run the installer for your operating system and choose the default setup. It asks which folder to protect and where to store backups. Choose advanced setup for individual settings. Neither installer assumes your entire system disk is a suitable source.

Linux / Raspberry Pi OS / macOS:

```sh
cd src
bash install.sh
```

Windows, from PowerShell:

```powershell
cd src
.\install.ps1
```

The installers check/install the compiler and build dependencies with the supported package manager, build CBackup, and offer job creation. Read any package-manager prompts. They do not install a disk-image driver or database snapshot service.

If CBackup is already installed:

```sh
cbackup init personal /path/to/documents --dest /path/to/backup-storage
cbackup run personal
cbackup ids personal
```

Use paths in double quotes when they contain spaces. Pick a destination **outside every source folder**. The default destination is `~/backups/<job>/`. Configuration lives at `~/.config/cbackup/jobs/<job>.conf`; on Windows the home directory is resolved by the platform layer.

New jobs use these defaults:

| Setting | Default | Meaning |
|---|---|---|
| Backup mode | `auto` | First full, then incremental; periodically starts a new full chain |
| Interval | Every 2 hours | Background scheduler; automatic launch enabled |
| Full checkpoint | 12 snapshots / approximately daily | Allows old incremental chains to be removed safely |
| Retention | 90 days | Approximately three months; retains prerequisite chain archives |
| Compression | `zstd` | Fast compressed archives, level 3 unless overridden |
| Encryption | `none` | Enable explicitly with a secret environment variable |
| Source selection | `all` | All eligible regular files in the supplied sources |
| Dependency exclusion | Enabled | Skips common reinstallable dependency/cache directories |
| Save-triggered watch | Disabled | Optional polling mode; avoids a continuously scanning process by default |

Scheduled jobs use low resource priority where the OS provides it. Unchanged incremental runs skip a new snapshot and reuse recorded metadata to avoid rehashing unchanged files. A full checkpoint still reads the selected files. Resource usage depends on source size and the compression selected.

For manual backups, use `cbackup init personal /path/to/source --no-schedule`, or disable an existing schedule with `cbackup schedule personal off`.

## Advanced installation and later changes

Choose **Advanced** in the installer to configure the job in one session. The advanced flow covers additional source files/folders, the destination, backup mode, compression and level, full-checkpoint frequency, retention days and minimum chain count, settings/installers/all profiles, dependency exclusions, extension/include/exclude filters, USB and cloud copies, login startup, polling after saved changes, polling interval, interval/daily/weekly/startup/manual planning, encryption and its secret environment-variable name, log size, health/alert settings, automatic sample restores, a storage budget, offline reminders and terminal language. Optional lists and remote destinations can be left empty. The terminal explains the available settings before prompting.

You can select the installer flow explicitly:

```sh
bash src/install.sh --advanced --job personal --source "/path/to/documents" --destination "/path/to/backups"
```

```powershell
.\src\install.ps1 -Profile Advanced -Job personal -Source 'C:\Users\me\Documents' -Destination 'D:\Backups'
```

All job choices remain editable after installation; you do not need to reinstall:

```sh
cbackup config personal show
cbackup settings
cbackup config personal set dest /path/to/backup-storage
cbackup config personal add-source /path/to/project /path/to/settings.ini
cbackup config personal remove-source /path/to/old-source
cbackup config personal add-exclude private-folder '*.tmp'
cbackup config personal clear-exclude
cbackup config personal add-include '*.json' '*.yaml'
cbackup config personal clear-include
cbackup config personal add-ext cpp h json
cbackup config personal clear-ext
cbackup config personal set usb /path/on/mounted/external-drive
cbackup config personal set cloud 'myremote:backups'
cbackup config personal set mode auto
cbackup config personal set compression zstd:3
cbackup config personal set profile all
cbackup config personal set keep_days 90
cbackup schedule personal --every 2h
cbackup config personal set encryption aes-256-gcm
cbackup config personal set password_env CBACKUP_PASSWORD
cbackup language nl
```

The examples are independent choices: enable encryption only after supplying the secret as described below, and configure an rclone remote before enabling cloud copies. Changing `dest` selects another storage location for future commands; it does not migrate existing archives. To use a prior backup location again, set `dest` back to that location. Changing sources/filters starts a fresh full chain on the next run.

## Help and languages

```sh
cbackup help
cbackup settings
cbackup language
cbackup language nl
cbackup --lang en help
cbackup --lang uk settings
cbackup config personal show
```

English is the default. Language selection covers translated help and setting descriptions, including Dutch, Spanish, German, Ukrainian, Russian and Polish and other major languages listed by `cbackup language`. Technical tokens, paths and command names stay the same. Some older detailed diagnostics still use Dutch or English; this is not a claim that every runtime sentence is translated.

`cbackup settings` explains the settings in the terminal. Change one with `cbackup config JOB set KEY VALUE`. Existing jobs keep explicitly saved settings; new defaults do not silently replace an old saved configuration.

## Backup types and compression

```sh
cbackup run personal --type full
cbackup run personal --type incremental
cbackup run personal --type differential
cbackup run personal --type mirror
cbackup run personal --dry-run
cbackup compression
cbackup config personal set compression gzip:6
cbackup run personal --compression zstd:3
```

| Mode | What it stores |
|---|---|
| `auto` | Incremental backups with periodic full checkpoints; recommended for automatic pruning |
| `full` | All selected files in a standalone snapshot |
| `incremental` | Changes since the previous snapshot; restore requires its chain |
| `differential` | Changes since the last full snapshot |
| `mirror` | Current uncompressed file tree; removed source files disappear from the next mirror |

Mirror has no snapshot version history and is not the default. Encryption is rejected for mirror rather than silently writing plaintext.

Supported archive choices are `zstd`, `gzip`, `xz`, `bzip2`, `lz4`, `zip` and `none`, subject to the codecs in the installed libarchive. `cbackup compression` checks availability. Compression level can be supplied as `TYPE:LEVEL`; `0` selects that codec's default.

## Restore by ID, file or folder

Find the code/ID first:

```sh
cbackup ids personal
cbackup list personal
cbackup changes personal SNAPSHOT_ID
cbackup changes personal OLDER_ID NEWER_ID
cbackup verify personal SNAPSHOT_ID
```

The change report distinguishes added, modified and deleted files. Each snapshot manifest records the complete selected file state, so deleted files stay absent when reconstructing that snapshot into a new directory.

```sh
cbackup restore personal SNAPSHOT_ID --to /path/to/new-recovery-folder
cbackup restore personal SNAPSHOT_ID --only report.docx --to /path/to/new-file-recovery
cbackup restore personal SNAPSHOT_ID --only project/subfolder --to /path/to/new-folder-recovery
cbackup restore personal SNAPSHOT_ID --original --dry-run
cbackup restore personal SNAPSHOT_ID --original --yes
```

`--to` requires a new destination. Recovered sources are separated under `files/source_0/`, `files/source_1/`, etc.; the source map is written alongside them. `--only` accepts a file name, folder path or glob and may be repeated.

An in-place restore (`--original`) preserves overwritten current files in a safety journal and prints a separate **undo ID**. It leaves extra current files alone; it does not delete unrelated data to force an exact historical tree. Undo can also remove a file newly created by the restore. Files changed after restoration cause undo to refuse, so a later edit is not overwritten unnoticed.

```sh
cbackup undo-list personal
cbackup undo personal UNDO_ID --dry-run
cbackup undo personal UNDO_ID --yes
```

Keep the safety journal until you are sure you no longer need it. It is separate from snapshot retention. An interrupted restore may leave a partially completed operation; inspect its reported journal and restore state before continuing.

Date/time selection remains available, for example `latest`, `yesterday`, an ISO date or Dutch `woensdag`; IDs are unambiguous across languages. Archive SHA-256 checks precede extraction and recovered files are checked against their recorded hashes. `verify` checks the required archive chain; it does not replace a real test restore.

## Selection, settings-only and installers

```sh
cbackup config personal add-source /another/folder /path/to/single-file
cbackup config personal remove-source /another/folder
cbackup config personal add-exclude '*.tmp' private-folder
cbackup config personal clear-exclude
cbackup config personal add-include '*.json' '*.yaml'
cbackup config personal clear-include
cbackup config personal add-ext cpp h json
cbackup config personal clear-ext
cbackup config personal set profile settings
cbackup config personal set profile installers
cbackup config personal set profile all
```

Profiles select common configuration/manifest files or common installer archive extensions. They are convenience filters, not a complete inventory of every application's settings. Use explicit sources and include/exclude patterns for an application with unusual file names. Include and extension filters both apply when present; exclusions win.

Common dependency/cache directories such as `node_modules`, `.venv`, `venv`, `__pycache__`, `.cache`, `.next`, `.nuxt` and `.tox` are skipped by default, including when nested in a project. User-defined exclusions are optional and empty by default. To keep dependencies too:

```sh
cbackup config personal set exclude_dependencies false
```

Project manifests and lockfiles remain eligible for backup. Preserve `package.json` **and** `package-lock.json`/`yarn.lock`/`pnpm-lock.yaml`; likewise preserve Python pinned requirements or lockfiles, Cargo.lock, composer.lock, Gemfile.lock and other ecosystem lockfiles. Recovering a lockfile is what allows the package manager to use the recorded versions rather than resolving a newer major version.

```sh
cbackup dependencies personal SNAPSHOT_ID --to /path/to/new-dependency-report
```

This produces recovery guidance for the recorded dependency files. CBackup never executes a restored install script automatically. Review the guidance and run the appropriate package manager yourself (for example `npm ci` with a valid npm lockfile). Exact reconstruction depends on a complete lockfile, the matching runtime/package-manager version and the continued availability of the recorded packages. A bare `package.json` with version ranges cannot guarantee the original installed version.

## Scheduling and changes after saving

```sh
cbackup schedule personal --every 2h
cbackup schedule personal --hourly
cbackup schedule personal --daily 22:00
cbackup schedule personal --weekly Sun@02:00
cbackup schedule personal --startup
cbackup schedule personal off
cbackup schedule personal on
cbackup config personal set autostart false
cbackup config personal set autostart true
```

The native scheduler uses systemd user units on Linux, Task Scheduler on Windows and launchd on macOS. A user login/session is normally required. On systemd systems, running while logged out may require the administrator to enable lingering for that user. Setting `autostart` controls automatic activation; it does not grant permissions to read protected files or run before login.

For backups after detected file changes, enable polling watch mode:

```sh
cbackup config personal set watch_seconds 30
cbackup config personal set watch true
```

Polling is not an instantaneous hook on every application's Save command. It detects eligible file changes on its interval, coalesces changes and uses the normal job lock to avoid concurrent writes. A shorter interval costs more scanning work. For an existing scheduled job, this starts the watcher through the platform scheduler; only one watcher may run per job. For a manual foreground watcher, first use `cbackup schedule personal off`, then run `cbackup watch personal` until stopped. Choose a regular interval for a large source tree.

## Encryption

```sh
cbackup config personal set encryption aes-256-gcm
cbackup config personal set password_env CBACKUP_PASSWORD
```

Supply the secret in the named environment variable before running or restoring. Examples for an interactive shell (do not commit the secret to a script):

```sh
read -r -s CBACKUP_PASSWORD
export CBACKUP_PASSWORD
cbackup run personal
```

PowerShell:

```powershell
$secret = Read-Host 'Backup password' -AsSecureString
$env:CBACKUP_PASSWORD = [System.Net.NetworkCredential]::new('', $secret).Password
cbackup run personal
Remove-Item Env:CBACKUP_PASSWORD
```

Encryption uses authenticated AES-256-GCM, PBKDF2-HMAC-SHA256 (600,000 iterations), a random 16-byte salt and a random 12-byte nonce per archive. The secret must be at least 12 bytes long. Wrong passwords and modified encrypted archives fail. Losing the secret prevents recovery. Store it independently of the backup. The secret is not saved in the job configuration or command log.

The archive payload is encrypted; manifests, paths, file hashes and logs remain metadata visible on disk. In-place restore safety copies are also ordinary local files. Select protected storage if that metadata/safety data must be confidential. A scheduled job must receive its secret through its own environment; a variable set only in a terminal usually is not inherited by an already running scheduler. Test an encrypted scheduled backup and restore before relying on it. Existing plaintext snapshots stay plaintext when you enable encryption; the setting applies to newly created snapshots. Keep one password throughout an incremental/differential chain. When rotating the password, make a new full snapshot explicitly and retain the old password for older chains.

## Logs and retention

```sh
cbackup log personal
cbackup log personal --tail 20
cbackup config personal set log_max_mb 5
cbackup config personal set keep_days 90
cbackup config personal set keep_chains 1
cbackup config personal set full_every 12
```

Logging records operations, change counts and failures, with bounded rotation. Logs are stored with the job. They are diagnostics, not a signed audit trail.

Retention removes **whole old local chains**, after successful replication, while preserving the archives required to recover the retained period. It may retain data older than exactly 90 days to keep the oldest recoverable chain intact. It never deletes an arbitrary incremental archive that a newer snapshot still needs. Periodic full checkpoints keep this extra retention bounded during active automatic backups. A job configured to make only incrementals forever cannot discard its original required full archive safely.

`keep_chains=0` disables local pruning. `keep_days=0` removes the age minimum and keeps the configured chain count only. Mirror, undo journals and remote copies are not part of automatic local snapshot pruning.

## External drive and cloud copies

```sh
cbackup config personal set usb /path/on/mounted/external-drive
cbackup replicate personal --check
rclone config
cbackup config personal set cloud 'myremote:backups'
cbackup cloud-push personal
cbackup cloud-pull personal
```

USB replication checks that the destination is on mounted external storage rather than silently substituting an unmounted local folder. A missing drive/network can make replication fail after a local snapshot has already been saved; retry with `replicate`. rclone must be configured separately. Remote copies are not automatically pruned. `mirror` uses synchronization within the job's remote `mirror` directory and therefore mirrors deletions there too.

## Health alerts, test restores, storage budgets and offline copies

These four maintenance features are built into 1.3. All are configurable in Advanced installation and later with `config JOB set KEY VALUE`.

| Setting | Default | Behavior |
|---|---|---|
| `notifications` | `true` | Failed runs, stale success, failed tests, quota violations and offline reminders are reported to stderr and a persistent alerts log |
| `desktop_notifications` | `false` | Optional local desktop display; requires an available logged-in session |
| `stale_hours` | `24` | Warn when no run has succeeded within this many hours; `0` disables the age check |
| `notify_repeat_hours` | `24` | Suppress repeat alerts for the same unchanged condition until this interval passes |
| `test_restore_days` | `7` | Test after a successful run when due, including an unchanged incremental run; `0` disables automatic testing |
| `test_restore_max_mb` | `64` | Maximum size of the single rotating sample file, in MiB |
| `storage_quota_mb` | `0` | Local committed archive/manifest/mirror budget in MiB; `0` is unlimited |
| `offline_reminder_days` | `14` | Remind when a separate offline copy is due; `0` disables reminders |

```sh
cbackup health personal
cbackup alerts personal
cbackup config personal set stale_hours 48
cbackup config personal set desktop_notifications true
cbackup config personal set notify_repeat_hours 24
cbackup test-restore personal
cbackup config personal set test_restore_days 7
cbackup config personal set test_restore_max_mb 64
cbackup run personal --dry-run
cbackup config personal set storage_quota_mb 20480
cbackup config personal set offline_reminder_days 14
cbackup offline-confirm personal SNAPSHOT_ID
```

`health` returns exit code `0` when healthy, `2` when attention is needed, and `1` on an operational/input error. A run counts as successful only after backup, configured replication and retention complete; unchanged runs count too. A failed automatic test also makes the run fail while preserving the saved snapshot. A later successful run clears the run failure; a successful test clears the test failure. Offline reminders remain until acknowledged. Turning `notifications` off suppresses alert delivery; `health` still reports the condition and its exit status.

State and rotated alerts are saved under `~/.config/cbackup/state/JOB/`, separately from backup storage, so a failed destination remains diagnosable. `alerts` shows the current rotated-log segment; `alerts.log.1` retains the previous one. Identical conditions are throttled; a different error or recurrence after recovery can generate a new alert. Local desktop display uses Windows `msg.exe`, macOS `osascript`, or Linux `notify-send` (usually the `libnotify-bin`/libnotify package). Missing desktop facilities leave the persistent alert available. No email or network notification service is configured.

Newly installed/enabled schedules include an independent hourly `health` task alongside the backup task, including daily/weekly backup schedules. Upgrade existing scheduled jobs with `cbackup schedule JOB on` to install it. `schedule JOB off` removes both. Checks need a functioning OS scheduler/user session; a powered-off machine cannot send a local notification. Manual jobs are checked with `health`, and a manual watcher refreshes an unchanged backup at least hourly while active.

The test restore verifies the archive chain, rotates through files no larger than `test_restore_max_mb`, extracts one into a unique system-temporary directory, and verifies its recorded size and SHA-256. Temporary files are removed on success and failure. Empty snapshots receive a chain/extraction check. If every file exceeds the sample limit, the test reports failure rather than claiming coverage. One sample is not a full restore test. Encrypted chains require the password and enough temporary space to decrypt the largest required archive, which can exceed the sample size. Mirror has no archive history and is excluded from automatic sample restores.

`run --dry-run` reports existing committed data and a conservative estimate for the proposed archive. Free space is checked before writing, with room for both temporary copies during encryption. The quota is checked against the exact completed archive plus its manifest before publishing it; compressed backups can fit even when the estimate is larger. Eligible expired chains may be replicated and pruned first, following the existing retention policy. The quota never overrides `keep_days` or removes a required incremental ancestor to force a backup to fit. If protected chains use the budget, the new archive is rejected and the old snapshots remain restorable. Increase the budget or explicitly adjust retention. Mirror replacement accounts for its old tree; its temporary new tree still needs free disk space. The budget excludes logs, health state, restore safety journals, remote copies and in-flight temporary files, so it is not a filesystem-wide disk limit.

`offline-confirm JOB SNAPSHOT_ID` verifies the local chain and records **your confirmation** that you copied it to separate storage and disconnected that copy. It does not create or inspect an offline copy. USB replication alone is never treated as proof of an offline copy. Reminders become due after the configured interval from job creation or the last confirmation; the snapshot ID remains in the health state even if that snapshot later expires locally.

## Build and validation

The build needs a C++17 compiler, CMake, libarchive and OpenSSL. The installers provide platform-specific dependency setup. For an already prepared toolchain:

```sh
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
python src/tests/integration.py /absolute/path/to/build/cbackup
python src/tests/maintenance.py /absolute/path/to/build/cbackup
```

On Windows the executable ends in `.exe`; multi-configuration generators may put it under `build/Release/`. Use a matching library architecture. The portable Python regression suite creates its own temporary HOME/USERPROFILE and uses `--no-schedule` so it never changes your real startup tasks. It covers backup chains, deletions, selections, dependency exclusions, IDs, encryption failures and undo. The included CI workflow is configured to build and run it on Linux, macOS and Windows; a passing CI run is still required to validate those platforms. The shell entrypoint `src/tests/integration.sh PATH_TO_CBACKUP` delegates to the same Python suite. `integration-1.1.sh` is preserved solely as a historical test for the old release and is not a test for the current release.

Validation in this editing session: the Windows x64 C++ source compiled successfully with GCC/UCRT64, and **25 integration groups passed (19 general + 6 maintenance)** against the native Windows executable during development. The final build, which also adds the mirror dry-run space estimate, compiled successfully; its final runtime rerun was blocked by Windows Application Control (WinError 4551), including outside the sandbox. That last rerun is therefore unverified. These cover Unicode filenames, full/incremental/differential/mirror modes, deleted files, partial recovery, dependency exclusions, undo and conflicts, authenticated encryption and tampering, retention, mixed compression formats and the polling watcher/lock. Maintenance checks additionally cover notification throttling, stale-success detection, independent failure state for unavailable destinations, offline confirmation/reminders, automatic sample rotation and cleanup, corrupt archives, sample limits, and exact quotas preserving older restores. The installers passed Bash syntax and PowerShell parsing checks. The installers were not executed and no background tasks were registered on this computer. Native scheduling, cloud accounts and removable-drive replication still require a real platform test. No Linux, macOS or Raspberry Pi runtime results are claimed; their CI workflow is configured but has not been run in this session.

Platform-specific startup behavior, removable drives and cloud accounts need validation on the intended computer. A successful local build does not prove all operating-system/architecture combinations have been tested. Raspberry Pi uses the Linux source build; there is no single universal binary.

## Validation and limits

- This protects selected ordinary files. It is not a bootable disk image, bare-metal system restore, Windows VSS service or transactional database snapshot. Export live databases first.
- Symlinks, special devices and empty directories are not preserved as independent backup items. Platform-specific ACLs, extended attributes, resource forks and alternate data streams are not a complete supported backup contract.
- Files that change during archiving cause that operation to fail rather than silently accept a mismatched snapshot. Permission errors also need attention.
- Incremental metadata reuse trusts size, timestamps and file identity. Full checkpoints read and hash the files again.
- SHA-256 detects corruption relative to the manifest; plaintext manifests are not signed. Keep a second protected or offline copy.
- An automatic task runs with the user's rights and depends on the computer/storage being available. No backup-at-shutdown guarantee is made.
- Regularly restore a sample to a new directory and compare it with the expected data. Keep your encryption secret and recovery instructions outside the machine being protected.
