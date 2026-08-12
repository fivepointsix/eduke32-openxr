DukeVR modern OpenXR build
===========================

This is the current eduke32-openxr source tree with the OpenXR stereo and
head-tracking implementation ported from the legacy DukeVR-openxr source.

Build from PowerShell:

    .\build_openxr.ps1

The script expects the OpenXR SDK at:

    C:\VR projects\carmageddon-vr-prototype\deps\openxr

Override it with DUKEVR_OPENXR_ROOT or -OpenXRRoot. MSYS2/UCRT64 is expected
at C:\msys64; override it with DUKEVR_MSYS_ROOT or -MsysRoot.

The script builds an optimized Windows executable with LTO disabled for
compatibility with the legacy MinGW audio libraries. The executable, object
files, temporary files, runtime data, logs, saves, and test results are placed
in the sibling output directory:

    ..\dukevr-build\eduke32.exe

The source tree remains free of generated binaries and runtime data. Override
the output directory with DUKEVR_BUILD_ROOT or -BuildRoot.

The OpenXR runtime must be installed and selected in Windows before launching
..\dukevr-build\eduke32.exe. The modern startup launcher and menus are rendered into both eye
images; in-game rendering adds head tracking, stereo eye views, and the
legacy-style OpenXR projection submission.

For a desktop fallback, set DUKEVR_OPENXR_DISABLE=1 before launching.

Automated smoke testing
-----------------------

Run a watchdog-controlled XR test; it archives the previous log, launches the
game, records crash dumps and matching Windows Application events, then closes
only the process it started:

    .\test_openxr.ps1 -Build

To skip directly into Episode 1, Level 1 without manual input:

    .\test_openxr.ps1 -StartMission

For a desktop-only control test:

    .\test_openxr.ps1 -Mode Desktop -StartMission

Repeat a test several times or change the timeout as needed:

    .\test_openxr.ps1 -Repeat 5 -TimeoutSeconds 90

Each run is saved below `..\dukevr-build\test-results\run-*` as `summary.json`
and `eduke32.log`. `-StartMission` appends `-l1 -s1`; the XR test requires runtime
and graphics initialization plus a submitted stereo gameplay frame. The
desktop test validates the same mission path without calling OpenXR.
