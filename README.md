# AlternateStreamOverlay Shell Extension

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
