# AlternateStreamOverlay Shell Extension

[![CI & Static Analysis](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml)
[![DevSkim](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/devskim.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/devskim.yml)

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

## Explorer overlay registration diagnostics

Run `./src/tests/Get-OverlayRegistration.ps1` on the target Windows machine to
inspect machine and per-user shell icon overlay identifiers and their
effective COM server paths. The audit is read-only and warns when this
extension's overlay registration is absent or its COM server is missing.

Explorer has a limited number of overlay slots shared by installed handlers.
A registration count does **not** establish which overlays Explorer selected;
the script does not change registration names, ordering, priorities or slots.
