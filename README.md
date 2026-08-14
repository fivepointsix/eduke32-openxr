# EDuke32 OpenXR / DukeVR

An OpenXR build of EDuke32 for Duke Nukem 3D and Ion Fury, with stereo
rendering and head tracking in the launcher, menus, and gameplay.

Most users should download the latest Windows x64 package from **Releases**.
The repository contains source code only; commercial game data is not included.

## Install from a release

1. Extract the release ZIP to a writable folder.
2. Start an OpenXR runtime such as SteamVR, VirtualDesktopXR, or Windows Mixed
   Reality, and connect the headset.
3. Copy the required game files into the same folder as `eduke32.exe`.
4. Launch `eduke32.exe` from that folder.

### Duke Nukem 3D

Copy one of these group files:

- `DUKE3D.GRP` or `duke3d.grp` from a registered/retail installation.
- `duke3d_sw.grp` for the shareware version (Episode 1 only).

Also copy `DUKE.RTS` when available. For Steam Megaton, the files are commonly
under:

```text
<SteamLibrary>\steamapps\common\Duke Nukem 3D\gameroot\
```

Example:

```powershell
$release = 'C:\Games\DukeVR'
$game = 'C:\SteamLibrary\steamapps\common\Duke Nukem 3D\gameroot'
Copy-Item (Join-Path $game 'duke3d.grp') $release
Copy-Item (Join-Path $game 'DUKE.RTS') $release
```

### Ion Fury

Ion Fury is automatically detected when installed through Steam or GOG in its
registered location. No manual copying is needed in that case.

For a portable installation, copy both `fury.grp` and `fury.grpinfo` beside
`eduke32.exe`.

## Performance

SteamVR's render resolution and refresh rate have a large effect on
performance, especially in Ion Fury. High SteamVR supersampling values create
very large per-eye OpenXR render targets, while higher refresh rates give the
game less time to render each frame.

If performance is poor, use this troubleshooting baseline:

1. Open the SteamVR dashboard and select **Settings** (the gear icon).
2. Under **Video**, set **Render Resolution** to **Custom** and **100%**.
3. Set the headset refresh rate to **90 Hz**, when that option is available.
4. Restart SteamVR after changing the refresh rate.

Once performance is stable, increase the resolution gradually if desired.
Per-application video settings can be used to change the resolution for
DukeVR without reducing it for other SteamVR applications.

## Build from source

Build requirements:

- Windows 10/11 64-bit.
- MSYS2 with the UCRT64 GCC toolchain and `make`.
- An OpenXR SDK containing `include\openxr\openxr.h` and
  `lib\openxr_loader.lib`.

From the repository root in PowerShell:

```powershell
.\build_openxr.ps1 -GameDataRoot 'C:\Games\Duke3D-data'
```

The script also uses a sibling `DukeVR-openxr` folder as its default runtime
source when available. Override paths with `-OpenXRRoot`, `-MsysRoot`,
`-RuntimeSource`, or `-BuildRoot`.

The output is created outside the source tree:

```text
..\dukevr-build\eduke32.exe
```

For a release ZIP, include the executable, `openxr_loader.dll`, and the
intended configuration/support files. Do not include `obj`, `tmp`, logs, saves,
or test results. Users supply their own game data as described above.

## License

EDuke32 is distributed under the GNU GPL. The Build engine license is in
`buildlic.txt` and the EDuke32 license text is in `GNU.TXT` when runtime files
are staged.
