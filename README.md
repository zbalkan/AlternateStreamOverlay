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
- **Machine-wide installation:** Requires administrator privileges to register Explorer and COM components.
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

The extension supports **machine-wide installation only**, requiring an elevated terminal.

```powershell
regsvr32.exe .\src\AlternateStreamOverlay\x64\Release\AlternateStreamOverlay.dll
```

This registers the COM classes and Explorer overlay for all users. Per-user registration (including `/n /i:user`) is not supported.

## Uninstallation

Run from an **elevated** terminal:

```powershell
regsvr32.exe /u .\src\AlternateStreamOverlay\x64\Release\AlternateStreamOverlay.dll
```

Unregister the extension before deleting its DLL. The examples assume a Release build and commands executed from the repository root.

**Upgrading from earlier machine-wide builds:** Before replacing an older installed DLL, unregister that version using elevated `regsvr32.exe /u` **with the path to the older DLL**, then register the new DLL. Earlier repository builds registered a type library (`{C0034A83-7FA3-43A7-82DD-3ADF66F0086C}`), two custom COM interfaces (`{CFC72A5F-E58B-43BD-8968-BA89AFB2240D}` and `{96296677-6A18-4F22-AC2E-84A650C4081D}`), and an AppID (`{869100FF-B8AB-406F-A8E2-C8B561E7CC81}`). Current builds no longer manage those registrations, so an in-place replacement without prior unregistration may leave obsolete entries. If the old DLL is unavailable, inspect the underlying machine-wide and per-user `Software\Classes\TypeLib`, `Interface`, and `AppID` registry entries before considering manual cleanup; `HKCR` is a merged view. Back up relevant keys and remove only entries positively identified as belonging to this extension, never shared parent keys. The exact registrations present in each historical release have not been independently verified.

**Migration from earlier per-user installations:** Machine-wide unregistration does not remove legacy HKCU registration entries. Before treating an upgrade or uninstall as complete, inspect `HKCU\Software\Classes\CLSID\{FBAB8C58-59C9-4522-B7E8-1A6AA23B318C}` (overlay COM class), `HKCU\Software\Classes\CLSID\{4A26024B-5499-45BE-A23F-4A65E88888D0}` (context COM class), and `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers\AlternateStreamOverlayIcon` (overlay identifier) for each account that previously installed the extension. The legacy per-user installer also wrote shell-handler entries under `HKCU\Software\Classes\*\ShellEx` and `HKCU\Software\Classes\Directory\ShellEx`. Remove only entries confirmed to belong to this extension; do not delete shared parent keys or unrelated handlers.

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

Inspect overlay registrations (the extension installs machine-wide):

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
