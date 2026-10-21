//
// Command-line alternate data stream enumeration.
//

#include "stdafx.h"
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>
#include "altstrm_common.h"

namespace
{
    // Write UTF-16 directly to a console. Redirected output is UTF-8 without
    // a BOM, independent of the active Windows console code page. Unpaired
    // UTF-16 surrogates in NTFS names are replaced during UTF-8 encoding.
    bool WriteOutput(const std::wstring& text)
    {
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        if (output == nullptr || output == INVALID_HANDLE_VALUE)
            return false;

        DWORD mode = 0;
        if (GetConsoleMode(output, &mode))
        {
            size_t position = 0;
            while (position < text.size())
            {
                DWORD written = 0;
                const auto count = static_cast<DWORD>(text.size() - position);
                if (!WriteConsoleW(output, text.data() + position, count, &written, nullptr) ||
                    written == 0)
                    return false;
                position += written;
            }
            return true;
        }

        if (text.empty())
            return true;
        const auto characters = static_cast<int>(text.size());
        const int byteCount = WideCharToMultiByte(CP_UTF8, 0,
            text.c_str(), characters, nullptr, 0, nullptr, nullptr);
        if (byteCount == 0)
            return false;

        std::string encoded(static_cast<size_t>(byteCount), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0,
            text.c_str(), characters, &encoded[0], byteCount, nullptr, nullptr) != byteCount)
            return false;

        size_t position = 0;
        while (position < encoded.size())
        {
            DWORD written = 0;
            const auto count = static_cast<DWORD>(encoded.size() - position);
            if (!WriteFile(output, encoded.data() + position, count, &written, nullptr) ||
                written == 0)
                return false;
            position += written;
        }
        return true;
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2)
    {
        const std::wstring program = argc > 0 ? argv[0] : L"altstrm";
        WriteOutput(program + L" [file path/spec]\r\n");
        return 1;
    }

    // Treat a directory argument as the directory itself, so its named
    // streams are reported (rather than enumerating its children).
    const std::wstring pattern(argv[1]);

    const size_t separator = pattern.find_last_of(L"\\/");
    // A drive-relative spec such as C:sample.txt is resolved relative to
    // drive C's current directory, not the process current directory.
    const bool driveRelative = pattern.size() >= 2 &&
        ((pattern[0] >= L'A' && pattern[0] <= L'Z') ||
         (pattern[0] >= L'a' && pattern[0] <= L'z')) &&
        pattern[1] == L':' && separator == std::wstring::npos;
    const std::wstring directory = separator != std::wstring::npos
        ? pattern.substr(0, separator + 1)
        : (driveRelative ? pattern.substr(0, 2) : L".\\");

    if (!WriteOutput(L"Files with alternate streams matching " +
                     std::wstring(argv[1]) + L"\r\n"))
        return 1;

    AlternateStreamReader reader;
    auto reportStreams = [&](const std::wstring& fullPath) -> bool
    {
        const StreamEnumeration enumeration = reader.EnumerateStreams(fullPath.c_str());
        if (enumeration.status != StreamStatus::Ok)
        {
            const std::wstring reason = enumeration.status == StreamStatus::Truncated
                ? L"Incomplete stream enumeration"
                : L"Stream enumeration failed";
            WriteOutput(reason + L" for " + fullPath + L" (Win32 error " +
                std::to_wstring(enumeration.error) + L")\r\n");
            return false;
        }

        std::vector<std::wstring> names = enumeration.streams;
        // The unnamed default stream is not an alternate data stream.
        names.erase(std::remove(names.begin(), names.end(), L"::$DATA"), names.end());
        if (names.empty())
            return true;

        std::wstring result = fullPath + L"\r\n";
        for (const auto& name : names)
            result += L" " + name + L"\r\n";
        return WriteOutput(result);
    };

    // FindFirstFileW cannot enumerate a volume root (for example, C:\\).
    // Exact directory arguments should be inspected directly, including roots.
    const DWORD attributes = GetFileAttributesW(pattern.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return reportStreams(pattern) ? 0 : 1;

    WIN32_FIND_DATAW findData = {};
    const HANDLE search = FindFirstFileW(pattern.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            WriteOutput(L"No files to check.\r\n");
        else
            WriteOutput(L"File search failed (Win32 error " +
                std::to_wstring(error) + L")\r\n");
        return 1;
    }

    bool success = true;
    do
    {
        success = reportStreams(directory + findData.cFileName);
        if (!success)
            break;
    } while (FindNextFileW(search, &findData) != 0);

    const DWORD lastError = GetLastError();
    FindClose(search);
    if (success && lastError != ERROR_NO_MORE_FILES)
    {
        WriteOutput(L"File search failed (Win32 error " +
            std::to_wstring(lastError) + L")\r\n");
        success = false;
    }
    return success ? 0 : 1;
}
