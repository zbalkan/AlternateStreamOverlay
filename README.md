# AlternateStreamOverlay

[![CI & Static Analysis](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml)
[![DevSkim](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/devskim.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/devskim.yml)
[![Quality gate status](https://sonarcloud.io/api/project_badges/measure?project=zbalkan_AlternateStreamOverlay&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=zbalkan_AlternateStreamOverlay)
[![Security issues](https://sonarcloud.io/api/project_badges/measure?project=zbalkan_AlternateStreamOverlay&metric=software_quality_security_issues)](https://sonarcloud.io/summary/new_code?id=zbalkan_AlternateStreamOverlay)

A Windows Explorer shell extension that makes **NTFS alternate data streams (ADS)** easier to discover and inspect.

AlternateStreamOverlay integrates with Windows Explorer to identify files containing alternate data streams and expose their contents through a property page.

This project is a maintained fork of Lee Benfield's original [AlternateStreamOverlay](https://www.benf.org/other/alternatestreamoverlay/index.html).

## Features

- **Icon overlay:** Identifies files containing alternate data streams in Windows Explorer.
- **Property page:** Displays alternate data streams associated with a file.
- **Context-menu integration:** Provides access to the extension through Explorer.
- **Per-user installation:** Supports registration without administrative privileges.
- **Native Windows integration:** Uses the documented `GetFileInformationByHandleEx(FileStreamInfo)` API to enumerate streams.
- **Registration diagnostics:** Includes read-only scripts for inspecting shell handler and overlay registration.

## Requirements

| Component | Requirement |
| --- | --- |
| Operating system | Windows 10 or Windows 11 (tested), Windows Server 2019, 2022, 2025 (untested) |
| Build tools | Visual Studio with MSVC and Windows SDK |
| Build configurations | `Debug\|x64`, `Release\|x64` |
| File system | NTFS for alternate data stream functionality |

## Building

Open `src/AlternateStreamOverlay.sln` in Visual Studio, or build from a Developer PowerShell session with MSBuild.

From the repository root:

```powershell
msbuild .\src\AlternateStreamOverlay.sln /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /p:RegisterOutput=false
```

For a Debug build, replace `Release` with `Debug`.

The build produces the shell extension DLL and supporting utilities. The `RegisterOutput=false` property prevents automatic COM registration during compilation.

Both supported configurations are built by GitHub Actions.

## Installation

The extension supports machine-wide and per-user registration. Choose one registration scope.

### Machine-wide installation

Run the following command from an **elevated** terminal:

```powershell
regsvr32.exe .\src\AlternateStreamOverlay\x64\Release\AlternateStreamOverlay.dll
```

This registers the extension for all users through the machine-wide COM and Explorer registration locations.

### Per-user installation

Administrative privileges are not required for current-user registration:

```powershell
regsvr32.exe /n /i:user .\src\AlternateStreamOverlay\x64\Release\AlternateStreamOverlay.dll
```

The `/n` option suppresses the default `DllRegisterServer` invocation, while `/i:user` requests per-user installation through `DllInstall`.

Per-user COM registrations are redirected to `HKCU\Software\Classes`. The overlay identifier is registered separately under:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers
```

Machine-wide overlay identifiers use the corresponding `HKLM` location.

The examples assume a Release build and that commands are executed from the repository root.

## Uninstallation

Use the command corresponding to the original installation scope.

**Machine-wide** (elevated terminal):

```powershell
regsvr32.exe /u .\src\AlternateStreamOverlay\x64\Release\AlternateStreamOverlay.dll
```

**Current user:**

```powershell
regsvr32.exe /u /n /i:user .\src\AlternateStreamOverlay\x64\Release\AlternateStreamOverlay.dll
```

Unregister the extension before deleting its DLL.

## Testing and diagnostics

The repository includes automated smoke tests and read-only registration diagnostics.

### Alternate data stream smoke tests

Build the solution and run:

```powershell
.\src\tests\Smoke-AlternateStreams.ps1 -ExePath .\src\x64\Release\altstrm.exe
```

The main CI workflow builds both supported configurations and runs the ADS smoke tests.

### Shell handler registration

Inspect effective context-menu and property-sheet registrations:

```powershell
.\src\tests\Test-ShellHandlerRegistration.ps1
```

The script checks registrations for files and directories, including the associated COM server path and threading model.

### Explorer overlay registration

Inspect machine-wide and per-user overlay registrations:

```powershell
.\src\tests\Get-OverlayRegistration.ps1
```

The script reports registered overlay identifiers and their effective COM server paths, warning when the extension's overlay registration is missing or its COM server cannot be found.

Both diagnostic scripts are read-only. They run in warning-only mode by default unless strict validation options such as `-FailOnIssues` or `-RequireRegistration` are supplied.

The separate `shell-registration.yml` workflow validates registration on an isolated Windows runner and unregisters the extension during cleanup.

## Releases

Official Windows x64 release builds are staged through the manually triggered GitHub Actions workflow. See [the release procedure](docs/RELEASING.md) for version checks, release artifact verification, and publication requirements.

## Known limitations

**Explorer overlay limits:** Windows Explorer supports only a limited number of shell icon overlay handlers. Other installed applications compete for these slots, so a correctly registered overlay is not guaranteed to appear.

**Registration versus activation:** Successful registration diagnostics confirm registry configuration, not that Explorer has loaded the extension or displayed its UI. Actual integration must be verified in Explorer.

**Platform coverage:** Windows Server versions are currently untested. Only x64 Debug and Release builds are supported.

## Credits

Originally developed by [Lee Benfield](https://www.benf.org/other/alternatestreamoverlay/index.html). Original attribution and historical source comments are preserved.

[fingerprint](https://icons8.com/icon/oNI1jC8ljJ7X/fingerprint) icon by [Icons8](https://icons8.com)
