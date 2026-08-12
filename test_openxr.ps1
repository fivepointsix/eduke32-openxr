[CmdletBinding()]
param(
    [ValidateSet('OpenXR', 'Desktop')]
    [string]$Mode = 'OpenXR',
    [switch]$Build,
    [ValidateRange(5, 600)]
    [int]$TimeoutSeconds = 60,
    [ValidateRange(1, 20)]
    [int]$Repeat = 1,
    [switch]$EnterMission,
    [switch]$StartMission,
    [switch]$TestStatusBarPersistence,
    [ValidateSet('None', 'Options', 'Quit')]
    [string]$InGameMenuAction = 'None',
    [switch]$KeepRunning,
    [string[]]$GameArguments = @('-nosetup'),
    [string]$BuildRoot
)

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = if ($env:DUKEVR_BUILD_ROOT) {
        $env:DUKEVR_BUILD_ROOT
    }
    else {
        Join-Path (Split-Path $projectRoot -Parent) 'dukevr-build'
    }
}
$buildRoot = [IO.Path]::GetFullPath($BuildRoot)
$executable = Join-Path $buildRoot 'eduke32.exe'
$logFile = Join-Path $buildRoot 'eduke32.log'
$resultsRoot = Join-Path $buildRoot 'test-results'
$runtimeManifest = $null
$needsMissionInput = $EnterMission -or $TestStatusBarPersistence
$needsKeyboardInput = $needsMissionInput
$runMission = $StartMission -or $needsMissionInput

if ($InGameMenuAction -ne 'None' -and -not ($StartMission -or $EnterMission)) {
    throw "-InGameMenuAction requires -StartMission or -EnterMission."
}
if ($InGameMenuAction -ne 'None' -and $KeepRunning) {
    throw "-KeepRunning cannot be combined with -InGameMenuAction."
}
if ($TestStatusBarPersistence -and $Mode -ne 'Desktop') {
    throw "-TestStatusBarPersistence currently requires -Mode Desktop."
}

if ($Mode -eq 'OpenXR') {
    $runtimeManifest = [Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON', 'Process')
    if ([string]::IsNullOrWhiteSpace($runtimeManifest)) {
        try {
            $runtimeManifest = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime -ErrorAction Stop).ActiveRuntime
        }
        catch {
            $runtimeManifest = $null
        }
    }
}

function ConvertTo-ProcessArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') {
        return $Value
    }

    return '"' + $Value.Replace('"', '\"') + '"'
}

function Stop-TestProcess([Diagnostics.Process]$Process) {
    if ($null -eq $Process) {
        return
    }

    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            [void]$Process.CloseMainWindow()
            if (-not $Process.WaitForExit(2000)) {
                $Process.Kill()
                [void]$Process.WaitForExit(5000)
            }
        }
    }
    catch {
        Write-Warning "Could not stop test process $($Process.Id): $($_.Exception.Message)"
    }
}

function Read-TestLog {
    if (-not (Test-Path -LiteralPath $logFile)) {
        return ''
    }

    try {
        return Get-Content -LiteralPath $logFile -Raw -ErrorAction Stop
    }
    catch {
        return ''
    }
}

if ($needsKeyboardInput) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class DukeVrTestInput {
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint msg, UIntPtr wParam, UIntPtr lParam);

    public static bool PostKey(IntPtr hWnd, uint msg, byte key) {
        return PostMessage(hWnd, msg, (UIntPtr)key, UIntPtr.Zero);
    }

    public const uint KEYEVENTF_KEYUP = 0x0002;
    public const uint WM_KEYDOWN = 0x0100;
    public const uint WM_KEYUP = 0x0101;

    public static void SendKey(IntPtr hWnd, byte key, bool shift) {
        if (shift) keybd_event(0x10, 0, 0, UIntPtr.Zero);
        keybd_event(key, 0, 0, UIntPtr.Zero);
        keybd_event(key, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        if (shift) keybd_event(0x10, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        PostKey(hWnd, WM_KEYDOWN, key);
        PostKey(hWnd, WM_KEYUP, key);
    }
}
'@
}

if ($Build) {
    & (Join-Path $projectRoot 'build_openxr.ps1') -BuildRoot $buildRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Executable was not found: $executable. Run with -Build first."
}

New-Item -ItemType Directory -Force -Path $resultsRoot | Out-Null

$effectiveGameArguments = @($GameArguments)
if ($runMission -and $effectiveGameArguments -notcontains '-l1') {
    $effectiveGameArguments += @('-l1', '-s1')
}
$argumentText = ($effectiveGameArguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' '
$allResults = @()

for ($run = 1; $run -le $Repeat; $run++) {
    $runStart = Get-Date
    $runStamp = $runStart.ToString('yyyyMMdd-HHmmss')
    $runDirectory = Join-Path $resultsRoot ("run-{0:D2}-{1}" -f $run, $runStamp)
    New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

    $beforeLog = Join-Path $runDirectory 'previous-eduke32.log'
    if (Test-Path -LiteralPath $logFile) {
        Copy-Item -LiteralPath $logFile -Destination $beforeLog -Force
        Remove-Item -LiteralPath $logFile -Force
    }

    $samePathProcesses = Get-Process -Name 'eduke32' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and ([IO.Path]::GetFullPath($_.Path) -ieq $executable) }
    if ($samePathProcesses) {
        throw "A test executable is already running from $executable. Close it before starting another run."
    }

    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $executable
    $startInfo.Arguments = $argumentText
    $startInfo.WorkingDirectory = $buildRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $false

    if ($Mode -eq 'Desktop') {
        $startInfo.EnvironmentVariables['DUKEVR_OPENXR_DISABLE'] = '1'
    }
    else {
        [void]$startInfo.EnvironmentVariables.Remove('DUKEVR_OPENXR_DISABLE')
        if (-not [string]::IsNullOrWhiteSpace($runtimeManifest) -and (Test-Path -LiteralPath $runtimeManifest)) {
            $startInfo.EnvironmentVariables['XR_RUNTIME_JSON'] = $runtimeManifest
        }
    }
    if ($InGameMenuAction -ne 'None') {
        $startInfo.EnvironmentVariables['DUKEVR_TEST_MENU'] = $InGameMenuAction.ToLowerInvariant()
    }
    if ($TestStatusBarPersistence) {
        $startInfo.EnvironmentVariables['DUKEVR_TEST_STATUS_PERSIST'] = '1'
    }

    $process = New-Object Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start $executable"
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $reachedLogoEnd = $false
    $reachedMenu = $false
    $reachedGameplay = $false
    $reachedStereoGameplay = $false
    $missionKeysSent = $false
    $statusPersistenceKeysSent = $false
    $statusPersistenceReady = -not $TestStatusBarPersistence
    $menuActionSent = $InGameMenuAction -ne 'None'
    $menuActionCompleted = $InGameMenuAction -eq 'None'
    $xrRuntimeInitialized = $false
    $xrGraphicsInitialized = $false
    $status = 'TimedOut'
    $exitCode = $null

    try {
        while ($true) {
            $logText = Read-TestLog
            $reachedLogoEnd = $logText -match 'OpenXR G_DisplayLogo end'
            $reachedMenu = $logText -match 'OpenXR main menu reached'
            $reachedGameplay = $logText -match 'OpenXR gameplay state reached|OpenXR stereo gameplay frame submitted'
            $reachedStereoGameplay = $logText -match 'OpenXR stereo gameplay frame submitted'
            $reachedQuitMenu = $logText -match 'OpenXR menu state: menu=502'
            $xrRuntimeInitialized = $logText -match 'OpenXR runtime initialized:'
            $xrGraphicsInitialized = $logText -match 'OpenXR graphics initialized:'

            if ($needsMissionInput -and $reachedMenu -and -not $missionKeysSent) {
                $process.Refresh()
                if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
                    Start-Sleep -Milliseconds 500
                    continue
                }

                [void][DukeVrTestInput]::SetForegroundWindow($process.MainWindowHandle)
                Write-Host ("  Sending mission input to window 0x{0:X}" -f $process.MainWindowHandle.ToInt64())
                Start-Sleep -Milliseconds 500
                # Main menu -> New Game -> episode -> skill -> mission.
                1..5 | ForEach-Object {
                    [DukeVrTestInput]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
                    [DukeVrTestInput]::keybd_event(0x0D, 0, [DukeVrTestInput]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
                    [void][DukeVrTestInput]::PostKey($process.MainWindowHandle,
                        [DukeVrTestInput]::WM_KEYDOWN, [byte]0x0D)
                    [void][DukeVrTestInput]::PostKey($process.MainWindowHandle,
                        [DukeVrTestInput]::WM_KEYUP, [byte]0x0D)
                    Start-Sleep -Milliseconds 700
                }
                $missionKeysSent = $true
            }

            if ($TestStatusBarPersistence -and $run -eq 1 -and $reachedGameplay -and
                -not $statusPersistenceKeysSent) {
                $process.Refresh()
                if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
                    Start-Sleep -Milliseconds 250
                    continue
                }

                [void][DukeVrTestInput]::SetForegroundWindow($process.MainWindowHandle)
                Write-Host ("  Sending status-bar persistence input to window 0x{0:X}" -f $process.MainWindowHandle.ToInt64())
                Start-Sleep -Milliseconds 250
                # One unshifted minus changes the status-bar presentation state;
                # shifted minus changes its scale. The second run verifies that
                # both values were loaded instead of changing them again.
                [DukeVrTestInput]::SendKey($process.MainWindowHandle, [byte]0xBD, $false)
                Start-Sleep -Milliseconds 250
                [DukeVrTestInput]::SendKey($process.MainWindowHandle, [byte]0xBD, $true)
                $statusPersistenceKeysSent = $true
            }

            if ($TestStatusBarPersistence) {
                if ($run -eq 1) {
                    $statusPersistenceReady = $logText -match 'Saved VR HUD settings: weapon=17/-9 status=13/-7 state=size:4 scale:95 mode:1 alt:0 custom:0'
                }
                elseif ($run -eq 2) {
                    $statusPersistenceReady = $logText -match 'Loaded VR HUD settings from .*weapon=17/-9 status=13/-7 state=size:4 scale:95 mode:1 alt:0 custom:0' -and
                        $logText -match 'Applied VR HUD settings after settings\.cfg: state=size:4 scale:95 mode:1 alt:0 custom:0'
                }
            }

            if ($InGameMenuAction -eq 'Options' -and $menuActionSent -and
                $logText -match 'OpenXR menu state: menu=202') {
                $menuActionCompleted = $true
                $status = 'ReachedInGameOptions'
                break
            }

            # The gameplay marker is emitted before the first stereo frame can
            # finish. Keep polling in OpenXR mission tests so a normal startup
            # race is not reported as a false ReachedGameplayWithoutStereo.
            $waitingForStereo = $Mode -eq 'OpenXR' -and $StartMission -and
                $reachedGameplay -and -not $reachedStereoGameplay
            $waitingForMenuAction = $InGameMenuAction -ne 'None' -and
                -not $menuActionCompleted
            $waitingForStatusPersistence = $TestStatusBarPersistence -and
                -not $statusPersistenceReady
            if ($reachedGameplay -and -not $KeepRunning -and
                -not $waitingForStereo -and -not $waitingForMenuAction -and
                -not $waitingForStatusPersistence) {
                if ($Mode -eq 'OpenXR' -and (-not $xrRuntimeInitialized -or -not $xrGraphicsInitialized)) {
                    $status = 'ReachedGameplayWithoutXR'
                }
                elseif ($Mode -eq 'OpenXR' -and -not $reachedStereoGameplay) {
                    $status = 'ReachedGameplayWithoutStereo'
                }
                else {
                    $status = 'ReachedGameplay'
                }
                break
            }

            if ($reachedMenu -and -not $KeepRunning -and -not $EnterMission -and -not $StartMission) {
                if ($Mode -eq 'OpenXR' -and (-not $xrRuntimeInitialized -or -not $xrGraphicsInitialized)) {
                    $status = 'ReachedMainMenuWithoutXR'
                }
                else {
                    $status = 'ReachedMainMenu'
                }
                break
            }

            $process.Refresh()
            if ($process.HasExited) {
                $exitCode = $process.ExitCode
                if ($InGameMenuAction -eq 'Quit' -and $menuActionSent -and $reachedQuitMenu) {
                    $menuActionCompleted = $true
                    $status = 'ExitedAfterQuit'
                }
                elseif ($reachedMenu -and $Mode -eq 'OpenXR' -and (-not $xrRuntimeInitialized -or -not $xrGraphicsInitialized)) {
                    $status = 'ExitedAfterMainMenuWithoutXR'
                }
                else {
                    $status = if ($reachedGameplay) { 'ExitedAfterGameplay' } elseif ($reachedMenu) { 'ExitedAfterMainMenu' } else { 'ExitedBeforeMainMenu' }
                }
                break
            }

            if ((Get-Date) -ge $deadline) {
                $status = 'TimedOut'
                break
            }

            Start-Sleep -Milliseconds 250
        }
    }
    finally {
        if (-not $KeepRunning -or $status -eq 'TimedOut') {
            Stop-TestProcess $process
        }

        $finalLog = Join-Path $runDirectory 'eduke32.log'
        if (Test-Path -LiteralPath $logFile) {
            Copy-Item -LiteralPath $logFile -Destination $finalLog -Force
        }
    }

    $crashDumpDirectory = Join-Path $env:LOCALAPPDATA 'CrashDumps'
    $newDumps = @()
    if (Test-Path -LiteralPath $crashDumpDirectory) {
        $newDumps = @(Get-ChildItem -LiteralPath $crashDumpDirectory -Filter 'eduke32*.dmp' -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $runStart })
    }

    $applicationEvents = @()
    try {
        $applicationEvents = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $runStart } -ErrorAction SilentlyContinue |
            Where-Object {
                $_.ProviderName -in @('Application Error', 'Application Hang', 'Windows Error Reporting') -and
                $_.Message -match 'eduke32'
            } |
            Select-Object TimeCreated, ProviderName, Id, Message)
    }
    catch {
        $applicationEvents = @()
    }

    $result = [ordered]@{
        mode = $Mode
        run = $run
        started = $runStart.ToString('o')
        finished = (Get-Date).ToString('o')
        status = $status
        exitCode = $exitCode
        reachedLogoEnd = $reachedLogoEnd
        reachedMainMenu = $reachedMenu
        reachedGameplay = $reachedGameplay
        reachedStereoGameplay = $reachedStereoGameplay
        reachedQuitMenu = $reachedQuitMenu
        statusPersistenceReady = $statusPersistenceReady
        inGameMenuAction = $InGameMenuAction
        inGameMenuActionSent = $menuActionSent
        inGameMenuActionCompleted = $menuActionCompleted
        xrRuntimeInitialized = $xrRuntimeInitialized
        xrGraphicsInitialized = $xrGraphicsInitialized
        log = (Join-Path $runDirectory 'eduke32.log')
        crashDumps = @($newDumps | ForEach-Object { $_.FullName })
        applicationEvents = @($applicationEvents)
    }

    $resultObject = [pscustomobject]$result
    $resultObject | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runDirectory 'summary.json') -Encoding UTF8
    $allResults += $resultObject

    Write-Host ("Run {0}: {1} (menu={2}, XR runtime={3}, XR graphics={4})" -f
        $run, $status, $reachedMenu, $xrRuntimeInitialized, $xrGraphicsInitialized)
    Write-Host "  Results: $runDirectory"
}

if ($TestStatusBarPersistence -and ($allResults | Where-Object { -not $_.statusPersistenceReady })) {
    Write-Error 'Status-bar persistence test did not observe the expected saved and reloaded values.'
    exit 1
}

    if ($allResults | Where-Object {
        $_.status -notin @('ReachedMainMenu', 'ExitedAfterMainMenu', 'ReachedGameplay', 'ExitedAfterGameplay', 'ReachedInGameOptions', 'ExitedAfterQuit') -or
        ($_.status -eq 'ExitedAfterQuit' -and $_.exitCode -ne 0)
    }) {
    exit 1
}

exit 0
