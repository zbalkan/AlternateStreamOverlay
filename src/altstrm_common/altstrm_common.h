#ifndef _ALTSTRM_COMMON_H_
#define _ALTSTRM_COMMON_H_

#include <windows.h>
#include <string>
#include <vector>

// Owns a Win32 file handle. It must not be copied.
class HandleW
{
public:
    explicit HandleW(HANDLE handle) : handle_(handle) {}
    ~HandleW()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }

    HandleW(const HandleW&) = delete;
    HandleW& operator=(const HandleW&) = delete;

    HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

// Preserve incomplete enumeration and native failures separately from no streams.
enum class StreamStatus { Ok, Truncated, Error };

struct StreamEnumeration
{
    StreamStatus status = StreamStatus::Error;
    DWORD error = ERROR_SUCCESS;
    bool hasNamedStreams = false;
    size_t undisplayableNames = 0;
    std::vector<std::wstring> streams;

    bool NeedsNotice() const
    {
        return status != StreamStatus::Ok || undisplayableNames != 0;
    }
};

// Interpret bounded native enumeration without requiring filesystem fixtures.
bool IndicatesAlternateStreams(const StreamEnumeration& result);

// Enumerate alternate streams using the documented Windows 10+ API.
class AlternateStreamReader
{
public:
    StreamEnumeration EnumerateStreams(LPCWSTR filename) const;
    bool HasAlternateStreams(LPCWSTR filename) const;

private:
    StreamEnumeration EnumerateStreams(LPCWSTR filename, bool collectNames) const;
    StreamEnumeration EnumerateStreams(HANDLE file, bool collectNames) const;
};

// Format bounded binary previews in eight-byte rows for the property page.
std::wstring FormatHexPreview(const BYTE* data, size_t size);

// The checkbox selects the display format. In text mode, malformed UTF-8
// is replaced rather than silently rendering a hex dump.
std::wstring FormatStreamPreview(const BYTE* data, size_t size,
                                 bool asHex);

#endif // _ALTSTRM_COMMON_H_
