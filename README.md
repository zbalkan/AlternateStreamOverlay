# AlternateStreamOverlay Shell Extension

A fork of Lee Benfield's [AlternateStreamOverlay](https://www.benf.org/other/alternatestreamoverlay/index.html), which shows NTFS alternate data streams in Windows Explorer using an icon overlay and a property page.

## Supported platform

- Windows 10 or Windows 11, x64.
- 64-bit Windows Explorer. The in-process shell extension is built exclusively for its host architecture.
- MSVC and the Windows SDK, using the toolset configured in the Visual C++ projects.

Only `Debug|x64` and `Release|x64` are supported.

The shared stream reader uses the documented `GetFileInformationByHandleEx(FileStreamInfo)` API. Original attribution and historical source comments are retained.
