# Development guide

This document covers building, testing and maintaining AlternateStreamOverlay. For installation and everyday usage, see the [user README](../README.md).

## Architecture

The Visual Studio solution (`src/AlternateStreamOverlay.sln`) contains four x64 C++14 projects:

| Project | Purpose |
| --- | --- |
| `AlternateStreamOverlay` | In-process Explorer COM DLL: property-sheet extension and icon-overlay handler |
| `altstrm_common` | Shared native stream enumeration, validation and preview formatting |
| `altstrm` | Command-line stream inventory tool |
| `AlternateStreamTests` | Native C++ MSTest regression suite |

The Explorer DLL uses ATL for COM, Win32 SRW locks and purpose-built RAII wrappers for native resources. There is no WIL dependency. Preserve the lifetime requirements of asynchronous preview callbacks and DLL pinning when modifying the worker.

Only Windows 10/11 x64 builds are supported. The solution targets the MSVC toolset specified in the project files and the Windows SDK. See the CI workflow for the currently exercised Visual Studio environment.

## Build

From a Visual Studio Developer PowerShell prompt at the repository root:

```powershell
msbuild .\src\AlternateStreamOverlay.sln /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /p:RegisterOutput=false
```

Use `Configuration=Debug` to build the Debug configuration. `RegisterOutput=false` prevents the build from automatically registering the COM DLL.

The extension DLL is built under `src/AlternateStreamOverlay/x64/<Configuration>/`. The CLI executable is built under `src/x64/<Configuration>/`.

## Testing

Run the native test project with Visual Studio Test Explorer. The tests exercise stream enumeration, malformed metadata, Unicode names, preview formatting, overlay behavior and COM interface contracts. Tests that create named streams require a filesystem supporting alternate data streams.

Run the CLI smoke test against a built executable:

```powershell
.\src\tests\Smoke-AlternateStreams.ps1 -ExePath .\src\x64\Release\altstrm.exe
```

The registration diagnostics are read-only unless explicitly documented otherwise:

```powershell
.\src\tests\Test-ShellHandlerRegistration.ps1
.\src\tests\Get-OverlayRegistration.ps1
```

The main GitHub workflow builds Debug and Release x64, runs native tests and smoke tests, and performs static analysis. Shell registration has a dedicated workflow. Some workflow triggers skip documentation-only changes; manual workflow dispatch remains available where configured.

## Key safety invariants

- Keep raw enumerated stream names separate from escaped display labels.
- Enumerate all named streams other than the default `::$DATA`; independently validate whether a name may be used for path-based preview.
- Do not perform potentially remote or offline filesystem operations synchronously inside Explorer callbacks.
- Keep previews bounded; do not load an unbounded alternate stream into Explorer.
- Ignore stale preview generations and avoid dereferencing state after page destruction.
- Balance COM references, `STGMEDIUM` ownership, thread-pool work and module lifetime.
- Preserve registration/unregistration behavior; do not delete unrelated registry entries.

## Registration testing

Use a disposable Windows x64 test machine or snapshot for shell integration testing. Register the DLL from an elevated console as described in the [user README](../README.md), test Explorer Properties for files and directories, then unregister before deleting the DLL. Native tests do **not** prove Explorer has loaded or activated the extension.

## Release engineering

Follow [RELEASING.md](RELEASING.md). Releases are staged as drafts and require manual inspection of version metadata, hashes, archive contents and Explorer behavior before publication.
