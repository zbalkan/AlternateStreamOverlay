Native C++ MSTest project
========================

This project uses the Visual Studio C++ Unit Test Framework already supplied
by the Visual Studio C++ testing component. No NuGet packages are required.

Build Debug|x64 or Release|x64 and run the tests from Visual Studio Test
Explorer. The tests use temporary files on the current Windows TEMP volume;
NTFS stream tests require a volume that supports named data streams.

The tests cover named data stream enumeration, preview formatting and decoding,
synthetic malformed FILE_STREAM_INFO records (including invalid lengths,
offsets, and truncated headers), and COM interface contracts.

The COM tests load the overlay DLL directly without registering it in Explorer.
They verify that the removed context-menu interface is unavailable, exercise property-page creation and release paths, and check overlay icon metadata,
buffer truncation, and overlay membership for a temporary file as its named
stream is created and removed. These tests do not validate Explorer UI behavior.
