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


namespace
{
    // Windows uses BYTE for ReadFile buffers. Keep that API boundary and
    // classify individual octets without introducing a C++17 requirement.
    bool IsContinuation(BYTE value)
    {
        return (value & 0xC0) == 0x80;
    }

    size_t Utf8SequenceLength(BYTE lead)
    {
        if (lead >= 0xC2 && lead <= 0xDF) return 2;
        if (lead >= 0xE0 && lead <= 0xEF) return 3;
        if (lead >= 0xF0 && lead <= 0xF4) return 4;
        return 0;
    }

    bool IsValidInitialContinuation(BYTE lead, BYTE second)
    {
        if (!IsContinuation(second)) return false;
        if (lead == 0xE0 && second < 0xA0) return false;
        if (lead == 0xED && second > 0x9F) return false;
        if (lead == 0xF0 && second < 0x90) return false;
        if (lead == 0xF4 && second > 0x8F) return false;
        return true;
    }

    size_t CompleteUtf8PrefixLength(const BYTE* data, size_t length)
    {
        // At most three trailing continuation bytes can belong to a UTF-8
        // sequence cut by the preview limit.
        size_t lead = length;
        while (lead > 0 && length - lead < 3 && IsContinuation(data[lead - 1]))
            --lead;
        if (lead == 0)
            return length;
        --lead;

        const size_t required = Utf8SequenceLength(data[lead]);
        const size_t available = length - lead;
        if (required == 0 || available >= required)
            return length;
        if (available >= 2 &&
            !IsValidInitialContinuation(data[lead], data[lead + 1]))
            return length;

        for (size_t i = lead + 2; i < length; ++i)
        {
            if (!IsContinuation(data[i]))
                return length;
        }
        return lead;
    }
}

bool TryDecodeUtf8Preview(const BYTE* data, size_t size, bool truncated,
                          std::wstring& text)
{
    text.clear();
    if (size == 0)
        return true;
    if (data == nullptr || size > static_cast<size_t>(INT_MAX))
        return false;

    const auto length = truncated ? CompleteUtf8PrefixLength(data, size) : size;
    if (std::find(data, data + length, static_cast<BYTE>(0)) != data + length)
        return false;
    if (length == 0)
        return true;

    // Win32 takes the input length in bytes (cbMultiByte), but the output
    // capacity in WCHAR elements (cchWideChar). Query the required character
    // count first, then allocate exactly that many wchar_t elements.
    const auto byteCount = static_cast<int>(length);
    // Flawfinder: ignore
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data), byteCount, nullptr, 0);
    if (count <= 0)
        return false;

    std::wstring decoded(static_cast<size_t>(count), L'\0');
    // Flawfinder: ignore
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(data), byteCount, &decoded[0], count) != count)
        return false;
    text.swap(decoded);
    return true;
}


std::wstring FormatHexPreview(const BYTE* data, size_t size)
{
    constexpr size_t BytesPerRow = 8;
    constexpr size_t MaxPreviewBytes = 65535;
    constexpr size_t CharactersPerRow = 47;
    constexpr WCHAR Digits[] = L"0123456789ABCDEF";

    if (data == nullptr || size == 0)
        return {};

    // The caller reads at most 65535 preview bytes; enforce the same
    // bound here so formatting cannot allocate unbounded Explorer memory.
    const size_t length = std::min(size, MaxPreviewBytes);
    const size_t rows = (length + BytesPerRow - 1) / BytesPerRow;
    std::wstring output;
    output.reserve(rows * CharactersPerRow);

    for (size_t offset = 0; offset < length; offset += BytesPerRow)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
            output.push_back(Digits[(offset >> shift) & 0xF]);
        output += L"  ";

        const size_t count = std::min(BytesPerRow, length - offset);
        for (size_t i = 0; i < BytesPerRow; ++i)
        {
            if (i < count)
            {
                const BYTE value = data[offset + i];
                output.push_back(Digits[value >> 4]);
                output.push_back(Digits[value & 0xF]);
                output.push_back(L' ');
            }
            else
                output += L"   ";
        }

        output += L" |";
        for (size_t i = 0; i < count; ++i)
        {
            const BYTE value = data[offset + i];
            output.push_back(value >= 32 && value <= 126
                ? static_cast<WCHAR>(value) : L'.');
        }
        output.append(BytesPerRow - count, L' ');
        output += L"|\r\n";
    }

    return output;
}
