# Windows 10/11 installation; MSYS2 UCRT64 produces a native executable.
[CmdletBinding()]
param(
    [ValidateSet('Default','Advanced')][string]$Profile,
    [string]$Source,
    [string]$Destination = "$env:USERPROFILE\backups",
    [ValidatePattern('^[a-zA-Z0-9_-]{1,60}$')][string]$Job = 'default',
    [string]$MsysRoot = 'C:\msys64',
    [switch]$NoDependencies,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked([scriptblock]$Command) { & $Command; if ($LASTEXITCODE -ne 0) { throw "Command failed: $LASTEXITCODE" } }
if (-not $BuildOnly) {
    $configHome = $env:USERPROFILE
    if ($env:HOME) { $configHome = $env:HOME }
    $jobConfig = Join-Path $configHome ".config\cbackup\jobs\$Job.conf"
    if (Test-Path -LiteralPath $jobConfig) {
        throw "Job '$Job' already exists. Edit it with 'cbackup config $Job show', 'cbackup config $Job set KEY VALUE' and 'cbackup schedule $Job ...'; or choose a different -Job. No files were changed."
    }
}
if (-not $Profile -and -not $BuildOnly) {
    Write-Host 'Default: incremental, zstd, every 2 hours, start at login, retain 90 days (about 3 months).'
    $Profile = Read-Host 'Install Default or Advanced [Default]'
    if (-not $Profile) { $Profile = 'Default' }
    if ($Profile -notin @('Default','Advanced')) { throw 'Choose Default or Advanced.' }
}
if (-not $NoDependencies) {
    if (-not (Test-Path -LiteralPath "$MsysRoot\usr\bin\bash.exe")) {
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw 'Install MSYS2 from https://www.msys2.org, then rerun with -MsysRoot.' }
        Invoke-Checked { winget install --exact --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements --silent }
        if (-not (Test-Path -LiteralPath "$MsysRoot\usr\bin\bash.exe")) {
            $candidate = "$env:LOCALAPPDATA\Programs\msys64"
            if (Test-Path -LiteralPath "$candidate\usr\bin\bash.exe") { $MsysRoot = $candidate }
            else { throw 'Rerun with -MsysRoot set to the installed MSYS2 directory.' }
        }
    }
    $env:MSYSTEM = 'UCRT64'
    Invoke-Checked { & "$MsysRoot\usr\bin\bash.exe" -lc 'pacman -Syu --noconfirm' }
    Invoke-Checked { & "$MsysRoot\usr\bin\bash.exe" -lc 'pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-libarchive mingw-w64-ucrt-x86_64-openssl' }
}
$toolBin = Join-Path $MsysRoot 'ucrt64\bin'; $env:PATH = "$toolBin;$env:PATH"
$build = Join-Path $PSScriptRoot 'build-windows'
Invoke-Checked { & "$toolBin\cmake.exe" -S $PSScriptRoot -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' }
Invoke-Checked { & "$toolBin\cmake.exe" --build $build --parallel 2 }
if ($BuildOnly) { return }
$install = Join-Path $env:LOCALAPPDATA 'CBackup\bin'
New-Item -ItemType Directory -Force -Path $install | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'cbackup.exe') -Destination $install -Force
# Copy recursive runtime dependencies, keeping compiler tools out of the installed application.
$pending = [System.Collections.Generic.Queue[string]]::new(); $pending.Enqueue((Join-Path $install 'cbackup.exe')); $seen = @{}
while ($pending.Count) {
    $file = $pending.Dequeue(); $imports = & "$toolBin\objdump.exe" -p $file
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect DLL dependencies: $file" }
    foreach ($line in $imports) {
        if ($line -match 'DLL Name:\s*(.+)$') {
            $dll = $Matches[1].Trim(); if ($seen.ContainsKey($dll)) { continue }; $seen[$dll] = $true
            $dllPath = Join-Path $toolBin $dll
            if (Test-Path -LiteralPath $dllPath) { Copy-Item -LiteralPath $dllPath -Destination $install -Force; $pending.Enqueue((Join-Path $install $dll)) }
        }
    }
}
$binary = Join-Path $install 'cbackup.exe'
if (-not $Source) {
    $defaultSource = [Environment]::GetFolderPath('MyDocuments'); $Source = Read-Host "Folder or file to back up [$defaultSource]"
    if (-not $Source) { $Source = $defaultSource }
}
if (-not (Test-Path -LiteralPath $Source)) { throw "Source does not exist: $Source" }
if (-not $PSBoundParameters.ContainsKey('Destination')) { $answer = Read-Host "Backup destination [$Destination]"; if ($answer) { $Destination = $answer } }
Invoke-Checked { & $binary init $Job $Source --dest $Destination --no-schedule }
$sourcePaths = [System.Collections.Generic.List[string]]::new()
$sourcePaths.Add((Get-Item -LiteralPath $Source).FullName)
$scheduleArgs = @('--every','2h'); $automatic = $true; $encrypted = $false
$passwordVariable = 'CBACKUP_PASSWORD'
if ($Profile -eq 'Advanced') {
    $answer = Read-Host 'Terminal language code (en,nl,es,de,uk,ru,pl,...) [en]'
    if (-not $answer) { $answer = 'en' }; Invoke-Checked { & $binary language $answer }
    Invoke-Checked { & $binary settings }
    while ($true) {
        $additionalSource = Read-Host 'Additional source file/folder (one path at a time; Enter to finish)'
        if (-not $additionalSource) { break }
        if (-not (Test-Path -LiteralPath $additionalSource)) { Write-Host 'That source does not exist. Enter an existing path.'; continue }
        Invoke-Checked { & $binary config $Job add-source $additionalSource }
        $sourcePaths.Add((Get-Item -LiteralPath $additionalSource).FullName)
    }
    $answer = Read-Host 'File profile: all/settings/installers [all]'
    if (-not $answer) { $answer = 'all' }; Invoke-Checked { & $binary config $Job set profile $answer }
    $answer = Read-Host 'Skip reinstallable dependency folders such as node_modules/.venv? true/false [true]'
    if (-not $answer) { $answer = 'true' }; Invoke-Checked { & $binary config $Job set exclude_dependencies $answer }
    Write-Host 'Dependency lockfiles and manifests stay included. Restore exact packages with the ecosystem lockfile command after restoring.'
    Write-Host 'Filters are optional. Include patterns and extension filters combine; exclusions take precedence. Enter patterns without shell quotes.'
    foreach ($filter in @(@('add-exclude','Exclude pattern (e.g. private/* or *.tmp)'), @('add-include','Include pattern (e.g. src/*; leave empty for all)'), @('add-ext','File extension (e.g. cpp or jpg; leave empty for all)'))) {
        while ($true) {
            $filterValue = Read-Host "$($filter[1]); one value at a time, Enter to finish"
            if (-not $filterValue) { break }
            $filterAction = $filter[0]
            Invoke-Checked { & $binary config $Job $filterAction $filterValue }
        }
    }
    $answer = Read-Host 'Mode: auto/full/incremental/differential/mirror [auto]'
    if (-not $answer) { $answer = 'auto' }; Invoke-Checked { & $binary config $Job set mode $answer }
    $selectedMode = $answer
    $answer = Read-Host 'Make a full backup every N changed snapshots in auto mode (bounds restore chains) [12]'
    if (-not $answer) { $answer = '12' }; Invoke-Checked { & $binary config $Job set full_every $answer }
    $answer = Read-Host 'Compression: zstd/gzip/xz/bzip2/lz4/zip/none [zstd]'
    if (-not $answer) { $answer = 'zstd' }; Invoke-Checked { & $binary config $Job set compression $answer }
    $answer = Read-Host 'Compression level (0 uses the codec default) [0]'
    if (-not $answer) { $answer = '0' }; Invoke-Checked { & $binary config $Job set compression_level $answer }
    $answer = Read-Host 'Retention days [90]'
    if (-not $answer) { $answer = '90' }; Invoke-Checked { & $binary config $Job set keep_days $answer }
    $answer = Read-Host 'Minimum full chains to retain (0 keeps everything) [1]'
    if (-not $answer) { $answer = '1' }; Invoke-Checked { & $binary config $Job set keep_chains $answer }
    $answer = Read-Host 'Log rotation size in MiB (previous log is kept as .1) [5]'
    if (-not $answer) { $answer = '5' }; Invoke-Checked { & $binary config $Job set log_max_mb $answer }
    $answer = Read-Host 'Additional USB/external destination (must be mounted; Enter to skip)'
    if ($answer) { Invoke-Checked { & $binary config $Job set usb $answer } }
    $answer = Read-Host 'Additional rclone cloud destination (remote:path; Enter to skip)'
    if ($answer) {
        if (-not (Get-Command rclone -ErrorAction SilentlyContinue)) {
            if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw 'Install rclone from https://rclone.org/install, configure the remote with rclone config, then set cloud using cbackup config.' }
            Invoke-Checked { winget install --exact --id Rclone.Rclone --accept-source-agreements --accept-package-agreements --silent }
            # winget may change PATH in the registry; use it in this installer session too.
            $env:PATH = [Environment]::GetEnvironmentVariable('PATH','Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH','User') + ";$toolBin"
            if (-not (Get-Command rclone -ErrorAction SilentlyContinue)) { throw 'rclone was installed. Open a new terminal, run rclone config, then set the cloud destination with cbackup config.' }
        }
        Write-Host 'The remote must already exist in rclone config. Failed replication keeps the local backup and reports the failure.'
        Invoke-Checked { & $binary config $Job set cloud $answer }
    }
    $answer = Read-Host 'Start at login? true/false [true]'
    if (-not $answer) { $answer = 'true' }; Invoke-Checked { & $binary config $Job set autostart $answer }
    $answer = Read-Host 'Back up after saved changes? true/false [false]'
    if (-not $answer) { $answer = 'false' }; Invoke-Checked { & $binary config $Job set watch $answer }
    $answer = Read-Host 'Watch polling interval in seconds (minimum 5; shorter intervals use more resources) [30]'
    if (-not $answer) { $answer = '30' }; Invoke-Checked { & $binary config $Job set watch_seconds $answer }
    $answer = Read-Host 'Schedule: interval/daily/weekly/startup/manual [interval]'
    if (-not $answer) { $answer = 'interval' }
    switch ($answer) {
        'interval' { $answer = Read-Host 'Interval (10min,2h,1d) [2h]'; if (-not $answer) { $answer = '2h' }; $scheduleArgs = @('--every',$answer) }
        'daily' { $answer = Read-Host 'Daily local time HH:MM [22:00]'; if (-not $answer) { $answer = '22:00' }; $scheduleArgs = @('--daily',$answer) }
        'weekly' { $answer = Read-Host 'Weekday and local time (Mon@HH:MM) [Sun@02:00]'; if (-not $answer) { $answer = 'Sun@02:00' }; $scheduleArgs = @('--weekly',$answer) }
        'startup' { $scheduleArgs = @('--startup') }
        'manual' { $automatic = $false }
        default { throw 'Invalid schedule.' }
    }
    $answer = Read-Host 'Encryption: none/aes-256-gcm [none]'
    if (-not $answer) { $answer = 'none' }
    if ($selectedMode -eq 'mirror' -and $answer -ne 'none') { throw 'Encrypted mirror is unsupported. Choose auto/full/incremental/differential, then enable encryption using cbackup config.' }
    Invoke-Checked { & $binary config $Job set encryption $answer }
    if ($answer -ne 'none') {
        $encrypted = $true; $automatic = $false
        $answer = Read-Host 'Name of environment variable holding the encryption password (never the password itself) [CBACKUP_PASSWORD]'
        if ($answer) { $passwordVariable = $answer }
        Invoke-Checked { & $binary config $Job set password_env $passwordVariable }
        Write-Host "Encrypted backups read $passwordVariable. Scheduling stays off until you configure the secret in the OS scheduler environment."
        Write-Host 'Keep the password separately: losing it makes these backups unrecoverable.'
    }
}
# Validate all selected source/destination relationships before any backup begins.
$destinationFullPath = [System.IO.Path]::GetFullPath($Destination)
$normalizedDestination = $destinationFullPath.TrimEnd('\','/')
$normalizedStore = (Join-Path $destinationFullPath $Job).TrimEnd('\','/')
if ((Test-Path -LiteralPath $destinationFullPath) -and -not (Test-Path -LiteralPath $destinationFullPath -PathType Container)) { throw 'Backup destination must be a directory.' }
foreach ($sourcePath in $sourcePaths) {
    if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Source disappeared before backup: $sourcePath" }
    if (Test-Path -LiteralPath $sourcePath -PathType Container) {
        $normalizedSource = [System.IO.Path]::GetFullPath($sourcePath).TrimEnd('\','/')
        if ($normalizedDestination.Equals($normalizedSource,[StringComparison]::OrdinalIgnoreCase) -or $normalizedDestination.StartsWith($normalizedSource + '\',[StringComparison]::OrdinalIgnoreCase) -or $normalizedStore.Equals($normalizedSource,[StringComparison]::OrdinalIgnoreCase) -or $normalizedStore.StartsWith($normalizedSource + '\',[StringComparison]::OrdinalIgnoreCase)) {
            throw "Backup destination cannot be inside source folder '$sourcePath'. Choose a separate destination with cbackup config $Job set dest PATH."
        }
    }
}
if ($encrypted -and -not [Environment]::GetEnvironmentVariable($passwordVariable,'Process')) { Write-Host "Initial backup pending: set $passwordVariable securely, then run: cbackup run $Job" }
else { Invoke-Checked { & $binary run $Job } }
if ($automatic) { Invoke-Checked { & $binary schedule $Job @scheduleArgs } }
else { Invoke-Checked { & $binary schedule $Job off } }
$userPath = [Environment]::GetEnvironmentVariable('PATH','User')
if (-not $userPath) { $userPath = '' }
if (($userPath -split ';') -notcontains $install) { [Environment]::SetEnvironmentVariable('PATH',($userPath.TrimEnd(';') + ';' + $install),'User') }
Write-Host "Installed: $binary"
Write-Host "Open a new terminal, then: cbackup run $Job"
Write-Host "Restore IDs: cbackup list $Job"
Write-Host "Every choice remains editable later: cbackup config $Job show; cbackup settings"
Write-Host "Examples: cbackup config $Job set dest PATH; cbackup config $Job add-source PATH"
Write-Host "Filters: cbackup config $Job add-exclude PATTERN; cbackup config $Job clear-exclude"
Write-Host "Schedule: cbackup schedule $Job --every 2h; cbackup schedule $Job off"
Write-Host 'Scheduled tasks use your logged-in account without administrator rights.'
Write-Host 'Optional cloud: winget install --exact --id Rclone.Rclone; then rclone config.'
