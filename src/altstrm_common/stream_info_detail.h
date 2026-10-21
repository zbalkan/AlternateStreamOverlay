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
}
