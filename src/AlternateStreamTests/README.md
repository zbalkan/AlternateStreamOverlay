Native C++ MSTest project
========================

This project uses the Visual Studio C++ Unit Test Framework already supplied
by the Visual Studio C++ testing component. No NuGet packages are required.

Build Debug|x64 or Release|x64 and run the tests from Visual Studio Test
Explorer. The tests use temporary files on the current Windows TEMP volume;
NTFS stream tests require a volume that supports named data streams.

The tests cover named data stream enumeration, preview formatting and decoding,
and synthetic malformed FILE_STREAM_INFO records (including invalid lengths,
offsets, and truncated headers). COM property-page lifecycle tests remain a
follow-up case; this project does not yet validate Explorer UI behavior.
