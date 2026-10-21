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
    operator HANDLE() const { return handle_; }

private:
    HANDLE handle_;
};

// Internal record validator shared by enumeration and synthetic-buffer tests.
// The supplied offset must point to a FILE_STREAM_INFO record within the buffer.
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

// Enumerate alternate streams using the documented Windows 10+ API.
class AlternateStreamReader
{
public:
    bool HasAlternateStreams(HANDLE file) const;
    bool HasAlternateStreams(LPCWSTR filename) const;
    std::vector<std::wstring> ListAlternateStreams(HANDLE file) const;
    std::vector<std::wstring> ListAlternateStreams(LPCWSTR filename) const;
};

// Format bounded binary previews in eight-byte rows for the property page.
std::wstring FormatHexPreview(const BYTE* data, size_t size);

// Decode text previews, discarding only an incomplete trailing UTF-8 character
// when the read was confirmed truncated. Invalid content remains binary.
bool TryDecodeUtf8Preview(const BYTE* data, size_t size, bool truncated,
                          std::wstring& text);

#endif // _ALTSTRM_COMMON_H_
