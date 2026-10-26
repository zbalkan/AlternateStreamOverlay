# Changelog

All notable changes to AlternateStreamOverlay are documented in this file.

Future releases follow [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Changes planned for the next release remain under **Unreleased** until the release is prepared.

Historical entries from Lee Benfield's original project are preserved verbatim below.

## [Unreleased]

## [2.0.0] - 2026-10-26

### Added

- Explorer's **Alternate Streams** property tab lists named alternate data streams for files and directories.
- Users can inspect named alternate stream contents as text or hexadecimal previews, with an indication when a preview is truncated.
- Two distinct Explorer overlays: a layered symbol for generic alternate streams and a download symbol for recognized Mark of the Web (`Zone.Identifier`) metadata, subject to Windows overlay-handler limits.
- Machine-wide installation and uninstallation through elevated COM registration.
- Command-line listing with Unicode file and stream names, including UTF-8 output when redirected.
- Diagnostics for shell integration and registrations.

### Changed

- The standard unnamed file data stream (`::$DATA`) is no longer shown as an alternate stream in Explorer or command-line results.
- Stream previews load asynchronously so Explorer remains responsive during slower reads; previews are limited to approximately 64 KiB.
- Stream listings and previews provide clearer feedback when enumeration is incomplete or a stream cannot be read.
- Improved text and hexadecimal display, including UTF-8 decoding and escaping potentially confusing stream names in Explorer and command-line output.
- Improved handling of directories, long and Unicode paths, drive-relative paths, and volume roots.
- Updated the Explorer icon artwork.

### Removed

- The legacy Explorer context-menu handler and its command; use the **Alternate Streams** property tab instead.
- Compatibility with Windows versions older than Windows 10 version 2004 (build 19041) if they are not 64-bit.
- 32-bit (x86) binaries. Only 64-bit Windows builds are provided.
- Legacy Windows XP-era alternate stream enumeration behavior.

### Fixed

- Incorrect or misleading stream listings for malformed, truncated, or incomplete enumeration results.
- Failures when working with long paths, large stream inventories, and unusual stream names.
- Command-line error handling and exit status for failed enumerations.
- Explorer delays and stale results when changing selection, closing the property page, or previewing streams.
- Registration and unregistration edge cases that could affect unrelated registry entries.

### Security

- Better validation of stream names and metadata and safer handling of malformed shell data.
- Reduced potentially blocking Explorer operations on remote, offline, and redirected paths.
- Improved isolation of failures in shell callbacks, along with additional Windows binary exploit mitigations.

## Original changelog (1.x)

The following entries are reproduced verbatim from [Lee Benfield's original project](https://www.benf.org/other/alternatestreamoverlay/index.html).

Minor rev - 1.0.0.2 - Added 64 bit version

Minor rev - 1.0.0.3 - Display alternate streams for directories. (thanks rmeyr)

Minor rev - 1.0.0.4 - Correctly display when more than 64k of alternate data present. (thanks Peter Allgaier)

Minor rev - 1.0.0.5 - Was missing a file close. (thanks Peter Allgaier)

Minor rev - 1.0.0.6 - Re-added XP functionality - I'd switched to using a documented function, but that's only available in Vista+! n.b. now dynamically chooses to use 'official' or undocumented function, as the official one is threadsafe, so I don't have to perform external locking.(thanks Peter Allgaier - again!)

[Unreleased]: https://github.com/zbalkan/AlternateStreamOverlay/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/zbalkan/AlternateStreamOverlay/compare/4d909b61d0d39dbea147a32762aac2c60f30492b...v2.0.0
