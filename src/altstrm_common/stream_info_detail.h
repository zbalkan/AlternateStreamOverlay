#pragma once

#include <windows.h>
#include <cstddef>

// Internal FILE_STREAM_INFO validation for enumeration and synthetic-buffer tests.
// Returns false for malformed records, including invalid or out-of-range offsets.
namespace stream_info_detail
{
    struct Record
    {
        const WCHAR* name;
        size_t nameBytes;
        size_t nextOffset;
    };

    bool ReadRecord(const BYTE* buffer, size_t size, size_t offset, Record& record);

    // Only names that form a single named $DATA stream may be appended to
    // a filesystem path for preview. Length is in WCHARs, not bytes.
    bool IsNamedDataStreamName(const WCHAR* name, size_t length);

    // Classification is separate from Win32 calls so unsupported-volume
    // behavior can be tested with deterministic error and capability inputs.
    bool IsUnsupportedStreamQuery(DWORD queryError, bool volumeInfoAvailable,
                                  DWORD volumeFlags);
}
