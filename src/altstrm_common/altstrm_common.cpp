#include "altstrm_common.h"
#include "stream_info_detail.h"

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
#include <array>
#include <climits>
#include <cstddef>
#include <cwchar>
#include <vector>
#include <utility>

namespace
{
    constexpr size_t InitialBufferSize = 4096;
    constexpr size_t InvalidParameterRetrySize = 65536;
    constexpr size_t MaxBufferSize = 4 * 1024 * 1024;
    constexpr size_t StreamInfoHeaderSize = offsetof(FILE_STREAM_INFO, StreamName);

    StreamStatus QueryStreamInfo(HANDLE file, BYTE*& data, size_t& size,
                                 std::vector<BYTE>& overflow, DWORD& error)
    {
        error = ERROR_SUCCESS;
        for (;;)
        {
            if (GetFileInformationByHandleEx(file, FileStreamInfo,
                data, static_cast<DWORD>(size)))
            {
                error = ERROR_SUCCESS;
                return StreamStatus::Ok;
            }

            error = GetLastError();
            if (error == ERROR_HANDLE_EOF)
            {
                size = 0;
                error = ERROR_SUCCESS;
                return StreamStatus::Ok;
            }
            if (error == ERROR_INVALID_PARAMETER && size < InvalidParameterRetrySize)
            {
                size = InvalidParameterRetrySize;
                overflow.resize(size);
                data = overflow.data();
                continue;
            }
            if (error == ERROR_INVALID_PARAMETER ||
                error == ERROR_INVALID_FUNCTION ||
                error == ERROR_NOT_SUPPORTED)
            {
                DWORD flags = 0;
                const bool volumeInfoAvailable =
                    GetVolumeInformationByHandleW(file, nullptr, 0, nullptr,
                        nullptr, &flags, nullptr, 0) != FALSE;
                if (stream_info_detail::IsUnsupportedStreamQuery(
                        error, volumeInfoAvailable, flags))
                {
                    size = 0;
                    error = ERROR_SUCCESS;
                    return StreamStatus::Ok;
                }
                return StreamStatus::Error;
            }
            if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER)
                return StreamStatus::Error;
            if (size >= MaxBufferSize)
            {
                size = 0;
                return StreamStatus::Truncated;
            }
            size *= 2;
            overflow.resize(size);
            data = overflow.data();
        }
    }
}

bool stream_info_detail::IsUnsupportedStreamQuery(
    DWORD queryError, bool volumeInfoAvailable, DWORD volumeFlags)
{
    return volumeInfoAvailable && (volumeFlags & FILE_NAMED_STREAMS) == 0 &&
        (queryError == ERROR_INVALID_PARAMETER ||
         queryError == ERROR_INVALID_FUNCTION ||
         queryError == ERROR_NOT_SUPPORTED);
}

bool stream_info_detail::ReadRecord(
    const BYTE* buffer, size_t size, size_t offset, Record& record)
{
    record = {};
    if (buffer == nullptr || offset > size ||
        offset % alignof(FILE_STREAM_INFO) != 0 ||
        size - offset < StreamInfoHeaderSize)
        return false;

    const auto* entry =
        reinterpret_cast<const FILE_STREAM_INFO*>(buffer + offset);
    const size_t remaining = size - offset;
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

    // Names are length-delimited UTF-16, but embedded NULs cannot be
    // represented safely by downstream NUL-terminated Win32 path APIs.
    if (wmemchr(entry->StreamName, L'\0', nameBytes / sizeof(WCHAR)) != nullptr)
        return false;

    record.name = entry->StreamName;
    record.nameBytes = nameBytes;
    record.nextOffset = next;
    return true;
}

bool stream_info_detail::IsNamedDataStreamName(const WCHAR* name, size_t length)
{
    constexpr WCHAR Suffix[] = L":$DATA";
    constexpr size_t SuffixLength = (sizeof(Suffix) / sizeof(WCHAR)) - 1;
    if (name == nullptr || length <= SuffixLength + 1 ||
        name[0] != L':' ||
        wmemcmp(name + length - SuffixLength, Suffix, SuffixLength) != 0)
        return false;

    // A second colon, path separator, or control character could change
    // how Win32 interprets the concatenated path.
    for (size_t i = 1; i < length - SuffixLength; ++i)
    {
        if (name[i] == L':' || name[i] == L'\\' ||
            name[i] == L'/' || name[i] < L' ')
            return false;
    }
    return true;
}

StreamEnumeration AlternateStreamReader::EnumerateStreams(HANDLE file) const
{
    return EnumerateStreams(file, true);
}

StreamEnumeration AlternateStreamReader::EnumerateStreams(HANDLE file, bool collectNames) const
{
    StreamEnumeration result;
    if (file == nullptr || file == INVALID_HANDLE_VALUE)
    {
        result.error = ERROR_INVALID_HANDLE;
        return result;
    }

    // Most files fit the initial query: avoid allocating a heap buffer on
    // every Explorer icon overlay callback. Grow on demand for large records.
    alignas(FILE_STREAM_INFO) std::array<BYTE, InitialBufferSize> initial = {};
    std::vector<BYTE> overflow;
    BYTE* data = initial.data();
    size_t bufferSize = initial.size();
    result.status = QueryStreamInfo(file, data, bufferSize, overflow, result.error);
    if (result.status == StreamStatus::Error || bufferSize == 0)
        return result;

    size_t offset = 0;
    for (;;)
    {
        stream_info_detail::Record record = {};
        if (!stream_info_detail::ReadRecord(data, bufferSize, offset, record))
        {
            result.status = StreamStatus::Error;
            result.error = ERROR_INVALID_DATA;
            return result;
        }

        constexpr WCHAR DefaultStream[] = L"::$DATA";
        const bool defaultStream =
            record.nameBytes == sizeof(DefaultStream) - sizeof(WCHAR) &&
            wmemcmp(record.name, DefaultStream,
                    (sizeof(DefaultStream) / sizeof(WCHAR)) - 1) == 0;
        // Existence is independent of whether the name can be appended to a
        // Win32 path. Skip name classification on the Explorer overlay path.
        if (!defaultStream)
            result.hasNamedStreams = true;

        // Inventory is lossless. Path safety belongs to the preview consumer.
        if (collectNames)
        {
            if (!defaultStream && !stream_info_detail::IsNamedDataStreamName(
                record.name, record.nameBytes / sizeof(WCHAR)))
                ++result.undisplayableNames;
            result.streams.emplace_back(record.name, record.nameBytes / sizeof(WCHAR));
        }

        if (record.nextOffset == 0)
            return result;
        offset += record.nextOffset;
    }
}

StreamEnumeration AlternateStreamReader::EnumerateStreams(LPCWSTR filename) const
{
    return EnumerateStreams(filename, true);
}

StreamEnumeration AlternateStreamReader::EnumerateStreams(LPCWSTR filename, bool collectNames) const
{
    if (filename == nullptr || *filename == L'\0')
    {
        StreamEnumeration result;
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }

    HandleW file(CreateFileW(filename, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        StreamEnumeration result;
        result.error = GetLastError();
        return result;
    }
    return EnumerateStreams(file.get(), collectNames);
}

std::vector<std::wstring> AlternateStreamReader::ListAlternateStreams(HANDLE file) const
{
    auto result = EnumerateStreams(file);
    // The vector-only API cannot report partial or failed enumeration.
    if (result.NeedsNotice())
        return {};
    return std::move(result.streams);
}

std::vector<std::wstring> AlternateStreamReader::ListAlternateStreams(LPCWSTR filename) const
{
    auto result = EnumerateStreams(filename);
    // The vector-only API cannot report partial or failed enumeration.
    if (result.NeedsNotice())
        return {};
    return std::move(result.streams);
}

bool AlternateStreamReader::HasAlternateStreams(HANDLE file) const
{
    const auto result = EnumerateStreams(file, false);
    return result.status == StreamStatus::Ok && result.hasNamedStreams;
}

bool AlternateStreamReader::HasAlternateStreams(LPCWSTR filename) const
{
    const auto result = EnumerateStreams(filename, false);
    return result.status == StreamStatus::Ok && result.hasNamedStreams;
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
    // BYTE matches the Win32 stream API; these values are octets used for
    // numeric indexing and ASCII/hex rendering, not typed C++ byte storage.
    constexpr size_t BytesPerRow = 8;
    constexpr size_t MaxPreviewBytes = 65535;
    constexpr size_t CharactersPerRow = 47;
    constexpr WCHAR Digits[] = L"0123456789ABCDEF";

    if (data == nullptr || size == 0)
        return {};

    // The caller reads at most 65535 preview bytes; enforce the same
    // bound here so formatting cannot allocate unbounded Explorer memory.
    const size_t length = (std::min)(size, MaxPreviewBytes);
    const size_t rows = (length + BytesPerRow - 1) / BytesPerRow;
    std::wstring output;
    output.reserve(rows * CharactersPerRow);

    for (size_t offset = 0; offset < length; offset += BytesPerRow)
    {
        for (int shift = 28; shift >= 0; shift -= 4)
            output.push_back(Digits[(offset >> shift) & 0xF]);
        output += L"  ";

        const size_t count = (std::min)(BytesPerRow, length - offset);
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
