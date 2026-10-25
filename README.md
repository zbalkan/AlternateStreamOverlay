# AlternateStreamOverlay

A Windows Explorer extension for **finding and inspecting NTFS alternate data streams (ADS)**.

[![CI & Static Analysis](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml/badge.svg)](https://github.com/zbalkan/AlternateStreamOverlay/actions/workflows/ci.yml)

## What does it do?

A file on NTFS can contain additional named data streams beyond its normal contents. Those streams are generally not visible in the usual Explorer file listing. AlternateStreamOverlay helps you discover them without opening a separate forensic tool.

- **Explorer icon overlays:** A layered badge identifies named alternate data streams without recognized Mark of the Web (MOTW); a download-arrow badge identifies recognized `Zone.Identifier` zone metadata. Explorer may suppress either overlay when slots are exhausted.
- **Alternate Streams property tab:** Lists the named streams of a selected file or directory. Select one to view a bounded text or hexadecimal preview.
- **Command-line listing:** Includes `altstrm.exe` for listing streams from a terminal.

The tool is **read-only**: it does not create, edit, or delete alternate streams. Displaying a stream does not imply that the stream is malicious; ADS are also used legitimately by Windows and applications.

## Requirements

- **Windows 10 version 2004 (build 19041) or later, x64**, or **Windows 11, x64**. This project does not provide 32-bit (x86) or ARM64 binaries.
- A filesystem supporting named streams (normally NTFS).
- Administrator privileges to register or unregister the Explorer extension.

Windows 10 32-bit editions are unsupported (Windows 11 itself is 64-bit only). Windows 10 version 2004 is the minimum intended compatibility baseline, not a claim of current Microsoft security support. Windows Server 2019/2022/2025 have not been validated. Explorer integration and icon-overlay visibility can vary by system.

## Install

1. Get the Windows x64 release ZIP from [Releases](https://github.com/zbalkan/AlternateStreamOverlay/releases), when a release is available. Extract it into a **permanent local folder** such as `C:\Program Files\AlternateStreamOverlay`; do not register the DLL from a temporary folder or directly inside a ZIP.
2. Open an **elevated 64-bit PowerShell or Command Prompt**.
3. Register the extracted DLL with its full path:

   ```powershell
   regsvr32.exe "C:\Program Files\AlternateStreamOverlay\AlternateStreamOverlay.dll"
   ```

Registration installs the property-sheet handler and two icon-overlay handlers for all users. No per-user installation mode is supported. Restart Explorer or sign out and back in if the integration does not appear immediately.

If no published release is available, the application can be [built from source](docs/DEVELOPMENT.md).

## Use in Explorer

1. Select an NTFS file or directory and open **Properties** (right-click → Properties, or `Alt+Enter`).
2. Open the **Alternate Streams** tab if one is offered.
3. Choose a named stream from the list. Its contents can be viewed as text or hex using **Show as hex**.

The normal unnamed `::$DATA` stream is excluded because it is the file's primary data, not an alternate stream. Previews are limited to approximately 64 KiB; larger streams are not fully displayed. If a native stream name cannot be opened safely through a Windows path, the tab still lists it but reports that its preview is unavailable.

The overlays are visual hints, not a complete inventory: Windows limits the number of shell icon overlay handlers Explorer can use, and other installed software competes for those positions. Use the tab or CLI to inspect a particular file even if no icon overlay appears.

## Use from the command line

The release archive also contains `altstrm.exe`. Run it with a file, directory, or wildcard pattern:

```powershell
.\altstrm.exe "C:\path\to\file.txt"
.\altstrm.exe "C:\path\to\folder"
.\altstrm.exe "C:\path\to\folder\*"
```

The CLI lists named streams; it does not print their contents. Unicode paths are supported, and redirected output is encoded as UTF-8. Quotes matter when paths contain spaces.

## Uninstall

Open an elevated terminal and unregister **the DLL from its installed location**:

```powershell
regsvr32.exe /u "C:\Program Files\AlternateStreamOverlay\AlternateStreamOverlay.dll"
```

Restart Explorer or sign out and back in if necessary, then remove the installation folder. **Unregister before deleting the DLL.** Uninstallation removes shell integration; it does not modify or erase alternate data streams on your files.

When upgrading, unregister the old installed DLL before registering a new one. Older releases and per-user installations may require additional cleanup; see the [migration guide](docs/MIGRATION.md). Do not manually delete registry entries without verifying ownership.

## How does this differ from the original?

This is a maintained fork of [Lee Benfield's AlternateStreamOverlay](https://www.benf.org/other/alternatestreamoverlay/index.html), not a new unrelated product.

| Area | Original project | This maintained fork |
| --- | --- | --- |
| Explorer integration | Alternate stream discovery and shell integration | Icon overlay and **Properties → Alternate Streams** tab |
| Stream inspection | Original Windows shell-extension behavior | Bounded text and hex preview, including feedback for unreadable streams |
| Enumeration | Older Windows compatibility and historical stream-enumeration paths | Native Windows 10 version 2004+ stream enumeration with additional malformed-data validation |
| Architecture | Historical 32-bit and 64-bit support | x64 only, Windows 10 version 2004+ and Windows 11 |
| Additional tooling | Original shell-extension functionality | Unicode-aware `altstrm.exe`, regression tests and registration diagnostics |

**Important change:** The fork deliberately **removes the Explorer context-menu command**. Open the file or directory's Properties dialog to inspect its streams instead.

The [changelog](CHANGELOG.md) contains the detailed development history and preserves the original project's historical notes.

## Troubleshooting

**The Alternate Streams tab is missing.** Confirm that you registered the x64 DLL with administrator privileges, that you opened Properties for a local supported file or directory, and that the extension is still installed at its registered path. The extension may decline paths that are remote, offline or redirected.

**The icon overlay is missing.** Windows has limited overlay-handler capacity; another application's overlay may take precedence. Missing overlays do not necessarily mean registration failed or that a file has no alternate streams.

**A preview says it is unavailable.** Some enumerated names are not suitable for path-based preview, and other streams may be inaccessible. The extension does not attempt to bypass those restrictions.

**Upgrading an old installation fails.** Unregister the old DLL from its original location and consult the [migration guide](docs/MIGRATION.md).

## Documentation

- [Development, builds, tests and architecture](docs/DEVELOPMENT.md)
- [Upgrading older installations and registry diagnostics](docs/MIGRATION.md)
- [Release procedure](docs/RELEASING.md)
- [Changelog](CHANGELOG.md)

## Credits

Originally developed by [Lee Benfield](https://www.benf.org/other/alternatestreamoverlay/index.html). Historical attribution is preserved.

Fingerprint icon by [Icons8](https://icons8.com/icon/oNI1jC8ljJ7X/fingerprint).
