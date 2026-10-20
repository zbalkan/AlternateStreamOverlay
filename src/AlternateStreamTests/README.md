Native C++ MSTest project
========================

This project uses the Visual Studio C++ Unit Test Framework already supplied
by the Visual Studio C++ testing component. No NuGet packages are required.

Build Debug|x64 or Release|x64 and run the tests from Visual Studio Test
Explorer. The tests use temporary files on the current Windows TEMP volume;
NTFS stream tests require a volume that supports named data streams.

This is the first MSTest integration milestone. Synthetic malformed
FILE_STREAM_INFO records and COM property-page lifecycle tests are separate
follow-up cases; this project does not yet validate Explorer UI behavior.
