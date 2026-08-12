[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$SkipRuntimeData,
    [ValidateRange(1, 64)]
    [int]$Jobs = 4,
    [string]$OpenXRRoot = $(if ($env:DUKEVR_OPENXR_ROOT) { $env:DUKEVR_OPENXR_ROOT } else { 'C:\VR projects\carmageddon-vr-prototype\deps\openxr' }),
    [string]$MsysRoot = $(if ($env:DUKEVR_MSYS_ROOT) { $env:DUKEVR_MSYS_ROOT } else { 'C:\msys64' }),
    [string]$BuildRoot,
    [string]$RuntimeSource,
    [string]$GameDataRoot
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
if ([string]::IsNullOrWhiteSpace($RuntimeSource)) {
    $RuntimeSource = if ($env:DUKEVR_RUNTIME_SOURCE) {
        $env:DUKEVR_RUNTIME_SOURCE
    }
    else {
        Join-Path (Split-Path $projectRoot -Parent) 'DukeVR-openxr'
    }
}
if (-not [string]::IsNullOrWhiteSpace($GameDataRoot)) {
    $GameDataRoot = [IO.Path]::GetFullPath($GameDataRoot)
}
$projectRootFull = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\')
$buildRootFull = [IO.Path]::GetFullPath($BuildRoot).TrimEnd('\')
$projectPrefix = $projectRootFull + [IO.Path]::DirectorySeparatorChar
if ($buildRootFull.Equals($projectRootFull, [StringComparison]::OrdinalIgnoreCase) -or
    $buildRootFull.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "BuildRoot must be separate from the source tree: $buildRootFull"
}

$buildRoot = $buildRootFull
$openxrInclude = Join-Path $OpenXRRoot 'include\openxr\openxr.h'
$openxrLoader = Join-Path $OpenXRRoot 'lib\openxr_loader.lib'
$bashPath = Join-Path $MsysRoot 'usr\bin\bash.exe'
$makePath = Join-Path $MsysRoot 'usr\bin\make.exe'
$tmpRoot = Join-Path $buildRoot 'tmp'

function Convert-ToMsysPath([string]$PathValue) {
    $fullPath = [IO.Path]::GetFullPath($PathValue)
    $drive = $fullPath.Substring(0, 1).ToLowerInvariant()
    $rest = $fullPath.Substring(2).Replace('\', '/')
    return "/$drive$($rest -replace ' ', '\ ' )"
}

foreach ($requiredPath in @($openxrInclude, $openxrLoader, $bashPath, $makePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required build file was not found: $requiredPath"
    }
}

if (-not $SkipRuntimeData -and
    -not (Test-Path -LiteralPath $RuntimeSource) -and
    [string]::IsNullOrWhiteSpace($GameDataRoot)) {
    throw "Runtime data source was not found: $RuntimeSource. Supply -RuntimeSource, -GameDataRoot, or use -SkipRuntimeData."
}
if (-not [string]::IsNullOrWhiteSpace($GameDataRoot) -and
    -not (Test-Path -LiteralPath $GameDataRoot)) {
    throw "Game data directory was not found: $GameDataRoot"
}

New-Item -ItemType Directory -Force -Path $buildRoot, $tmpRoot | Out-Null

$projectMsys = Convert-ToMsysPath $projectRoot
$tmpMsys = Convert-ToMsysPath $tmpRoot
$openxrMsys = Convert-ToMsysPath (Resolve-Path $OpenXRRoot).Path
$loaderMsys = Convert-ToMsysPath (Resolve-Path $openxrLoader).Path
$projectUri = New-Object Uri(($projectRoot.TrimEnd('\') + '\'))
$buildRelative = [Uri]::UnescapeDataString($projectUri.MakeRelativeUri((New-Object Uri($buildRoot))).ToString()).TrimEnd('/')
$buildRelative = $buildRelative.Replace('\', '/')
if ($buildRelative -match '\s') {
    throw "BuildRoot must be reachable from the source tree through a relative path without spaces: $buildRoot"
}
$buildTarget = "$buildRelative/eduke32.exe"
$buildObjectRoot = "$buildRelative/obj"

$makeOptions = @(
    '-f GNUmakefile',
    "-j$Jobs",
    $buildTarget,
    'OPENXRWIN=1',
    'RENDERTYPE=WIN',
    'PLATFORM=WINDOWS',
    'USE_LIBVPX=0',
    'HAVE_GTK2=0',
    'HAVE_FLAC=0',
    'PRETTY_OUTPUT=0',
    'RELEASE=1',
    'LTO=0',
    "BINDIR=$buildRelative",
    "OBJDIR=$buildObjectRoot"
) -join ' '

$bashLines = @(
    'export PATH=/ucrt64/bin:/usr/bin',
    "export TMPDIR=$tmpMsys",
    'export TMP=$TMPDIR',
    'export TEMP=$TMPDIR',
    "export OPENXR_ROOT=$openxrMsys",
    "export OPENXR_LOADER=$loaderMsys",
    "export BINDIR=$buildRelative",
    "export OBJDIR=$buildObjectRoot",
    "cd $projectMsys"
)

if ($Clean) {
    $bashLines += "make -f GNUmakefile clean"
}

$bashLines += "make $makeOptions"
$bashScript = [string]::Join("`n", $bashLines)

Write-Host "Building DukeVR modern OpenXR in $projectRoot"
& $bashPath -lc $bashScript
if ($LASTEXITCODE -ne 0) {
    throw "OpenXR build failed with exit code $LASTEXITCODE"
}

if (-not $SkipRuntimeData) {
    $runtimeFiles = @(
        'buildlic.txt',
        'DUKE.RTS',
        'duke3d_sw.grp',
        'eduke32.cfg',
        'eduke32_backup.cfg',
        'fury.grp',
        'fury.grpinfo',
        'GNU.TXT',
        'grpfiles.cache',
        'hrp_art_license.txt',
        'openxr_loader.dll',
        'OPENXR_README.txt',
        'settings.cfg',
        'settings_backup.cfg',
        'textures.cache',
        'xinput1_3.dll'
    )

    foreach ($runtimeFile in $runtimeFiles) {
        if (Test-Path -LiteralPath $RuntimeSource) {
            $sourceFile = Join-Path $RuntimeSource $runtimeFile
            if (Test-Path -LiteralPath $sourceFile) {
                Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $buildRoot $runtimeFile) -Force
            }
        }
    }

    if (Test-Path -LiteralPath $RuntimeSource) {
        $texturesSource = Join-Path $RuntimeSource 'textures'
        if (Test-Path -LiteralPath $texturesSource) {
            Copy-Item -LiteralPath $texturesSource -Destination $buildRoot -Recurse -Force
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($GameDataRoot)) {
        foreach ($gameFile in @('DUKE3D.GRP', 'duke3d.grp', 'duke3d_sw.grp', 'DUKE.RTS', 'duke.rts')) {
            $gameSource = Join-Path $GameDataRoot $gameFile
            if (Test-Path -LiteralPath $gameSource) {
                Copy-Item -LiteralPath $gameSource -Destination (Join-Path $buildRoot $gameFile) -Force
            }
        }
    }

    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'openxr_loader.dll'))) {
        foreach ($loaderCandidate in @(
            (Join-Path $OpenXRRoot 'bin\openxr_loader.dll'),
            (Join-Path $OpenXRRoot 'bin\win64\openxr_loader.dll')
        )) {
            if (Test-Path -LiteralPath $loaderCandidate) {
                Copy-Item -LiteralPath $loaderCandidate -Destination (Join-Path $buildRoot 'openxr_loader.dll') -Force
                break
            }
        }
    }

    $hasDukeGroup = @('duke3d_sw.grp', 'DUKE3D.GRP', 'duke3d.grp') |
        Where-Object { Test-Path -LiteralPath (Join-Path $buildRoot $_) }
    if (-not $hasDukeGroup) {
        throw "No Duke 3D group file was staged. Supply -GameDataRoot pointing to a legal game installation or provide duke3d_sw.grp/DUKE3D.GRP in the runtime source."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $buildRoot 'openxr_loader.dll'))) {
        throw "openxr_loader.dll was not staged. Supply it through the runtime source or an OpenXR SDK installation."
    }
}

$buildExecutable = Join-Path $buildRoot 'eduke32.exe'
if (-not (Test-Path -LiteralPath $buildExecutable)) {
    throw "Build completed without producing the executable: $buildExecutable"
}

Write-Host "Build complete: $buildExecutable"
