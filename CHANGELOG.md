# Changelog

All notable changes to AlternateStreamOverlay are documented in this file.

Releases from version 2.0.0 onward follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Historical entries from Lee Benfield's original project are preserved verbatim at the end of this document.

## [Unreleased]

## [2.0.0] - 2026-10-22

A modernization of AlternateStreamOverlay for contemporary Windows systems, with improved reliability, safer Explorer integration, and automated testing.

### Added

- Per-user COM and Explorer overlay registration without administrator privileges.
- Asynchronous alternate data stream previews to keep the Explorer property-page interface responsive.
- Stream enumeration diagnostics that distinguish successful, failed, and incomplete operations.
- Unicode-aware command-line output, including UTF-8 encoding when redirected.
- Read-only diagnostics for context-menu, property-sheet, and icon overlay registration.
- Automated native regression tests and PowerShell smoke tests for alternate data stream handling.
- GitHub Actions workflows for builds, tests, static analysis, and shell registration validation.

### Changed

- Migrated the original Visual C++ 2008 projects to modern MSVC and Windows SDK tooling.
- Standardized native projects on C++14 and x64 Debug and Release configurations.
- Modernized stream enumeration around the documented `GetFileInformationByHandleEx(FileStreamInfo)` API.
- Improved handling of Unicode paths, long filenames, directory paths, and volume roots.
- Limited stream previews to approximately 64 KiB to avoid unbounded memory consumption within Explorer.
- Improved hexadecimal preview formatting and UTF-8 text decoding.
- Reduced unnecessary allocations during Explorer overlay detection.
- Reorganized the source tree and established consistent formatting, compiler warnings, and build configurations.
- Replaced original fingerprint icon with an icon set of different sizes from icons8 website

### Removed

- Support for legacy Windows XP-era stream enumeration.
- 32-bit build support.

### Fixed

- Incorrect handling of malformed or truncated stream enumeration records.
- Incomplete enumeration results being treated as successful empty results.
- Explorer initialization delays caused by synchronous stream preview reads.
- Potentially blocking Explorer operations involving remote, offline, or reparse-point paths.
- Resource ownership and cleanup errors involving COM storage media, dialogs, and worker threads.
- Stale selection state during COM reinitialization and property-page lifecycle changes.
- Race conditions involving asynchronous preview completion and dialog teardown.
- Incorrect handling of command identifiers and context-menu invocation.
- Stream-name validation and path construction edge cases.
- Command-line enumeration errors involving relative paths, drive-relative paths, directories, and volume roots.
- Ambiguous command-line output for stream names containing control or other unsafe Unicode characters.
- Incorrect command-line success reporting after enumeration failures.
- Registration failures and cleanup behavior that could affect existing or independently owned registry entries.
- Long-path failures and large stream enumeration buffer handling.

### Security

- Enabled Control Flow Guard (CFG), SDL security checks, and CET shadow-stack compatibility for Release binaries.
- Strengthened validation of stream metadata, stream names, shell data objects, and Win32 API results.
- Restricted Explorer-triggered stream enumeration on potentially remote, offline, or redirected paths.
- Improved exception containment at COM and Windows callback boundaries.
- Added automated static analysis using MSVC analysis, Cppcheck, clang-tidy, DevSkim, and additional security checks.
- Enforced stricter compiler diagnostics, including warnings as errors for Release production builds.

## Original changelog (1.x)

The following entries are reproduced verbatim from [Lee Benfield's original project](https://www.benf.org/other/alternatestreamoverlay/index.html).

Minor rev - 1.0.0.2 - Added 64 bit version

Minor rev - 1.0.0.3 - Display alternate streams for directories. (thanks rmeyr)

Minor rev - 1.0.0.4 - Correctly display when more than 64k of alternate data present. (thanks Peter Allgaier)

Minor rev - 1.0.0.5 - Was missing a file close. (thanks Peter Allgaier)

Minor rev - 1.0.0.6 - Re-added XP functionality - I'd switched to using a documented function, but that's only available in Vista+! n.b. now dynamically chooses to use 'official' or undocumented function, as the official one is threadsafe, so I don't have to perform external locking.(thanks Peter Allgaier - again!)

[Unreleased]: https://github.com/zbalkan/AlternateStreamOverlay/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/zbalkan/AlternateStreamOverlay/compare/4d909b61d0d39dbea147a32762aac2c60f30492b...v2.0.0
