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
    std::vector<std::wstring> streams;
};

// Enumerate alternate streams using the documented Windows 10+ API.
class AlternateStreamReader
{
public:
    StreamEnumeration EnumerateStreams(HANDLE file) const;
    StreamEnumeration EnumerateStreams(LPCWSTR filename) const;
    bool HasAlternateStreams(HANDLE file) const;
    bool HasAlternateStreams(LPCWSTR filename) const;
    std::vector<std::wstring> ListAlternateStreams(HANDLE file) const;
    std::vector<std::wstring> ListAlternateStreams(LPCWSTR filename) const;

private:
    StreamEnumeration EnumerateStreams(HANDLE file, bool collectNames) const;
    StreamEnumeration EnumerateStreams(LPCWSTR filename, bool collectNames) const;
};

// Format bounded binary previews in eight-byte rows for the property page.
std::wstring FormatHexPreview(const BYTE* data, size_t size);

// Decode text previews, discarding only an incomplete trailing UTF-8 character
// when the read was confirmed truncated. Invalid content remains binary.
bool TryDecodeUtf8Preview(const BYTE* data, size_t size, bool truncated,
                          std::wstring& text);

#endif // _ALTSTRM_COMMON_H_
