# AlternateStreamOverlay Shell Extension

[![CI & Static Analysis](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml)
[![DevSkim](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/devskim.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/devskim.yml)
[![Quality gate status](https://sonarcloud.io/api/project_badges/measure?project=zbalkan_AlternateStreamOverlay&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=zbalkan_AlternateStreamOverlay)
[![Security issues](https://sonarcloud.io/api/project_badges/measure?project=zbalkan_AlternateStreamOverlay&metric=software_quality_security_issues)](https://sonarcloud.io/summary/new_code?id=zbalkan_AlternateStreamOverlay)

A fork of Lee Benfield's [AlternateStreamOverlay](https://www.benf.org/other/alternatestreamoverlay/index.html), which shows NTFS alternate data streams in Windows Explorer using an icon overlay and a property page.

## Supported platform

- Windows 10 or Windows 11 (tested), Server 2019, 2022, 2025 (untested).
- MSVC and the Windows SDK, using the toolset configured in the Visual C++ projects.

Only `Debug|x64` and `Release|x64` are supported.

The shared stream reader uses the documented `GetFileInformationByHandleEx(FileStreamInfo)` API. Original attribution and historical source comments are retained.

## Building and testing

The Visual Studio solution and project sources are located under `src/`.
From the repository root, build with:

```powershell
msbuild .\src\AlternateStreamOverlay.sln /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /p:RegisterOutput=false
.\src\tests\Smoke-AlternateStreams.ps1 -ExePath .\src\x64\Release\altstrm.exe
```

Debug builds use `/p:Configuration=Debug`. The GitHub Actions workflow builds both configurations.

## Registration scope

Machine-wide registration requires elevation and remains the default:

```powershell
regsvr32.exe .\\src\\AlternateStreamOverlay\\x64\\Release\\AlternateStreamOverlay.dll
```

To register for the current user without writing the overlay identifier to HKLM,
use `DllInstall("user")` through `regsvr32` with `/n` (which suppresses a
separate machine-wide `DllRegisterServer` call):

```powershell
regsvr32.exe /n /i:user .\\src\\AlternateStreamOverlay\\x64\\Release\\AlternateStreamOverlay.dll
```

For current-user removal, use `regsvr32.exe /u /n /i:user` with the same DLL
path. Default `regsvr32.exe /u` removes machine-wide registration. Run the
appropriate command for the scope originally installed.

ATL redirects per-user COM registrations into `HKCU\\Software\\Classes`;
the Explorer overlay identifier is registered independently beneath
`HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers`.
Machine-wide overlay identifiers remain under the corresponding `HKLM` key.

## Shell handler registration validation

Run `./src/tests/Test-ShellHandlerRegistration.ps1` on the target Windows
machine to check effective context-menu and property-sheet registrations for
files and directories, the associated COM server path and its threading model.
The script is read-only. A successful registration check does not prove that
Explorer loaded the extension or displayed its menu and property page.

The `shell-registration.yml` CI workflow builds and temporarily registers the
extension on an isolated Windows runner, then runs both registration diagnostics
with strict assertions. It unregisters the DLL during cleanup. The scripts
remain warning-only when run manually without `-FailOnIssues` or
`-RequireRegistration`. The existing main CI workflow already runs the ADS
smoke tests.

## Explorer overlay registration diagnostics

Run `./src/tests/Get-OverlayRegistration.ps1` on the target Windows machine to
inspect machine and per-user shell icon overlay identifiers and their
effective COM server paths. The audit is read-only and warns when this
extension's overlay registration is absent or its COM server is missing.

Explorer has a limited number of overlay slots shared by installed handlers.
A registration count does **not** establish which overlays Explorer selected;
the script does not change registration names, ordering, priorities or slots.

## Fingerprint icon

[fingerprint](https://icons8.com/icon/oNI1jC8ljJ7X/fingerprint) icon by [Icons8](https://icons8.com)
