#include "altstrm_common.h"

/*
 * 08/2011 lee@benf.org
 *
 * Originally based on the excellent article
 * http://www.flexhex.com/docs/articles/alternate-streams.phtml
 *
 * Updated to use functions now natively avaiable in vs2008.
 *
 * Helper utilities to extract a vector of (or test for presence of)
 * alternate streams.
 *
 * I could check if the FS is NTFS, but that'd require caching to not be awful,
 * so I'd need to figure out removable drive notifications.... Manyana!
 *
 *
 */

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cwchar>
#include <vector>

namespace
{
    constexpr size_t InitialBufferSize = 4096;
    constexpr size_t MaxBufferSize = 4 * 1024 * 1024;
    constexpr size_t StreamInfoHeaderSize = offsetof(FILE_STREAM_INFO, StreamName);

    bool QueryStreamInfo(HANDLE file, std::vector<BYTE>& buffer)
    {
        buffer.resize(InitialBufferSize);

        for (;;)
        {
            if (GetFileInformationByHandleEx(file, FileStreamInfo,
                buffer.data(),
                static_cast<DWORD>(buffer.size())))
                return true;

            const DWORD error = GetLastError();
            if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER)
                return false;

            if (buffer.size() >= MaxBufferSize)
                return false;

            buffer.resize(buffer.size() * 2);
        }
    }
}

std::vector<std::wstring> AlternateStreamReader::ListAlternateStreams(HANDLE file) const
{
    std::vector<std::wstring> streams;
    if (file == nullptr || file == INVALID_HANDLE_VALUE)
        return streams;

    std::vector<BYTE> buffer;
    if (!QueryStreamInfo(file, buffer))
        return streams;

    size_t offset = 0;
    for (;;)
    {
        const size_t remaining = buffer.size() - offset;
        if (remaining < StreamInfoHeaderSize)
            return {};

        const auto* entry =
            reinterpret_cast<const FILE_STREAM_INFO*>(buffer.data() + offset);
        const size_t nameBytes = entry->StreamNameLength;
        const size_t next = entry->NextEntryOffset;

        if (nameBytes == 0 ||
            nameBytes % sizeof(WCHAR) != 0 ||
            nameBytes > remaining - StreamInfoHeaderSize)
            return {};

        if (next != 0 &&
            (next < StreamInfoHeaderSize ||
                next % alignof(FILE_STREAM_INFO) != 0 ||
                next > remaining ||
                nameBytes > next - StreamInfoHeaderSize))
            return {};

        streams.emplace_back(entry->StreamName, nameBytes / sizeof(WCHAR));

        if (next == 0)
            return streams;

        offset += next;
    }
}

std::vector<std::wstring> AlternateStreamReader::ListAlternateStreams(LPCWSTR filename) const
{
    if (filename == nullptr || *filename == L'\0')
        return {};

    HandleW file(CreateFileW(filename, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
        nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        return {};

    return ListAlternateStreams(file.get());
}

bool AlternateStreamReader::HasAlternateStreams(HANDLE file) const
{
    if (file == nullptr || file == INVALID_HANDLE_VALUE)
        return false;

    // Explorer asks about many files while painting a directory. Avoid
    // allocating a vector of stream names and querying attributes for each
    // overlay decision; only a named stream can qualify.
    std::vector<BYTE> buffer;
    if (!QueryStreamInfo(file, buffer))
        return false;

    size_t offset = 0;
    for (;;)
    {
        const size_t remaining = buffer.size() - offset;
        if (remaining < StreamInfoHeaderSize)
            return false;

        const auto* entry =
            reinterpret_cast<const FILE_STREAM_INFO*>(buffer.data() + offset);
        const size_t nameBytes = entry->StreamNameLength;
        const size_t next = entry->NextEntryOffset;

        if (nameBytes == 0 ||
            nameBytes % sizeof(WCHAR) != 0 ||
            nameBytes > remaining - StreamInfoHeaderSize)
            return false;

        if (next != 0 &&
            (next < StreamInfoHeaderSize ||
                next % alignof(FILE_STREAM_INFO) != 0 ||
                next > remaining ||
                nameBytes > next - StreamInfoHeaderSize))
            return false;

        // FileStreamInfo uses "::$DATA" for the unnamed default stream.
        // Directories may have no unnamed stream at all. Both cases are
        // handled by inspecting names rather than counting entries.
        constexpr WCHAR defaultName[] = L"::$DATA";
        if (nameBytes != (sizeof(defaultName) - sizeof(WCHAR)) ||
            wmemcmp(entry->StreamName, defaultName,
                (sizeof(defaultName) / sizeof(WCHAR)) - 1) != 0)
            return true;

        if (next == 0)
            return false;

        offset += next;
    }
}

bool AlternateStreamReader::HasAlternateStreams(LPCWSTR filename) const
{
    if (filename == nullptr || *filename == L'\0')
        return false;

    HandleW file(CreateFileW(filename, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
        nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        return false;

    return HasAlternateStreams(file.get());
}


bool TryDecodeUtf8Preview(const BYTE* data, size_t size, bool truncated,
                          std::wstring& text)
{
    text.clear();
    if (size == 0)
        return true;
    if (data == nullptr || size > static_cast<size_t>(INT_MAX))
        return false;

    size_t length = size;
    if (truncated)
    {
        // Only remove an incomplete final code point. Never forgive malformed
        // UTF-8 elsewhere in the preview (or a genuinely incomplete file).
        size_t lead = length;
        while (lead > 0 && length - lead < 3 &&
               (data[lead - 1] & 0xC0) == 0x80)
            --lead;
        if (lead > 0)
        {
            --lead;
            const BYTE first = data[lead];
            const size_t available = length - lead;
            const size_t required = first >= 0xF0 && first <= 0xF4 ? 4 :
                                    first >= 0xE0 && first <= 0xEF ? 3 :
                                    first >= 0xC2 && first <= 0xDF ? 2 : 0;
            bool validPrefix = required != 0 && available < required;
            for (size_t i = lead + 1; validPrefix && i < length; ++i)
                validPrefix = (data[i] & 0xC0) == 0x80;
            if (validPrefix && available >= 2)
            {
                const BYTE second = data[lead + 1];
                if ((first == 0xE0 && second < 0xA0) ||
                    (first == 0xED && second > 0x9F) ||
                    (first == 0xF0 && second < 0x90) ||
                    (first == 0xF4 && second > 0x8F))
                    validPrefix = false;
            }
            if (validPrefix)
                length = lead;
        }
    }

    if (std::find(data, data + length, static_cast<BYTE>(0)) != data + length)
        return false;
    if (length == 0)
        return true;

    const int byteCount = static_cast<int>(length);
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data), byteCount, nullptr, 0);
    if (count <= 0)
        return false;

    std::wstring decoded(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data), byteCount, &decoded[0], count) != count)
        return false;
    text.swap(decoded);
    return true;
}
