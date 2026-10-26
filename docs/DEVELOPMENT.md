# Development guide

This document covers building, testing and maintaining AlternateStreamOverlay. For installation and everyday usage, see the [user README](../README.md).

## Architecture

The Visual Studio solution (`src/AlternateStreamOverlay.sln`) contains four x64 C++14 projects:

| Project | Purpose |
| --- | --- |
| `AlternateStreamOverlay` | In-process Explorer COM DLL: property-sheet extension and separate generic ADS/MOTW icon-overlay handlers |
| `altstrm_common` | Shared native stream enumeration, validation and preview formatting |
| `altstrm` | Command-line stream inventory tool |
| `AlternateStreamTests` | Native C++ MSTest regression suite |

The Explorer DLL uses ATL for COM, Win32 SRW locks and purpose-built RAII wrappers for native resources. There is no WIL dependency. Preserve the lifetime requirements of asynchronous preview callbacks and DLL pinning when modifying the worker.

The supported target is Windows 10 version 2004 (build 19041) or later on x64, and Windows 11 on x64. Windows 10 had both 32-bit and 64-bit editions, but this project supports only x64; Windows 11 itself is 64-bit only. ARM64 builds are not provided. Windows 10 version 2004 is a compatibility floor, not an assertion that it remains supported by Microsoft. The solution targets the MSVC toolset specified in the project files and the Windows SDK. See the CI workflow for the currently exercised Visual Studio environment.

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

## Continuous integration

**Repository Validation** runs on every pull request targeting `main` and every push to `main`, including Markdown-only changes. It checks Markdown tabs and final-newline formatting (MD010 and MD047), GitHub Actions syntax (actionlint, which also parses the workflow YAML), PowerShell parsing and isolated release-note generation; it never publishes a release.

**CI & Static Analysis** and **DevSkim** omit documentation-only, validation-workflow-only and release-helper-only changes. Shell Registration Diagnostics runs for changes under `src/AlternateStreamOverlay/**`, `src/altstrm_common/**`, any `src/tests/*.ps1` script, `src/AlternateStreamOverlay.sln`, or `.github/workflows/shell-registration.yml`. Changes to those components can run more than one relevant workflow. The manual Release workflow is never triggered by repository changes.

On pull requests, newer commits cancel superseded validation, native CI, DevSkim and shell-registration runs. Runs on `main` and manually requested release staging are not cancelled by this policy.

These path filters are not a replacement for branch protection: make **Repository Validation** the consistently required status check; consider conditional native jobs rather than path-filtered workflows if you also require every native check on every pull request.

## Key safety invariants

- Keep raw enumerated stream names separate from escaped display labels.
- Enumerate all named streams other than the default `::$DATA`; independently validate whether a name may be used for path-based preview.
- Do not perform potentially remote or offline filesystem operations synchronously inside Explorer callbacks.
- Keep previews bounded; do not load an unbounded alternate stream into Explorer.
- Ignore stale preview generations and avoid dereferencing state after page destruction.
- Balance COM references, `STGMEDIUM` ownership, thread-pool work and module lifetime.
- Preserve registration/unregistration behavior; do not delete unrelated registry entries.

## Accepted preview architecture

The asynchronous preview implementation is the accepted design. The following proposed simplifications **will not be implemented**:

- Replace the registered preview-completion window message and its registration-failure checks with a different notification mechanism.
- Simplify the allocation-free error-reporting path used when preview work fails.
- Remove or collapse the worker loop that coalesces superseded selection requests.

These mechanisms protect Explorer responsiveness and preview lifetime/selection behavior. They are not outstanding cleanup tasks. Revisit the design only for a separately demonstrated correctness or security defect.

## Registration testing

Use a disposable Windows 10 version 2004+ x64 or Windows 11 x64 test machine or snapshot for shell integration testing. Register the DLL from an elevated console as described in the [user README](../README.md), test Explorer Properties for files and directories, then unregister before deleting the DLL. Native tests do **not** prove Explorer has loaded or activated the extension.

## Release engineering

Follow [RELEASING.md](RELEASING.md). Releases are staged as drafts and require manual inspection of version metadata, hashes, archive contents and Explorer behavior before publication.

## Historical references

The original shell-extension project cited [FlexHex's alternate-stream article](https://web.archive.org/web/20230201200305/http://www.flexhex.com/docs/articles/alternate-streams.phtml) and [The Complete Idiot's Guide to Writing Shell Extensions](https://web.archive.org/web/20231221020619/https://www.codeproject.com/Articles/441/The-Complete-Idiot-s-Guide-to-Writing-Shell-Extens). These are historical references, not the authority for the current Windows 10/11 implementation.
