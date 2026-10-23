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
    // Write UTF-16 directly to a console; redirected output uses UTF-8.
    // Caller escapes unsafe code units before formatting untrusted paths.
    bool WriteOutput(const std::wstring& text, DWORD standardHandle = STD_OUTPUT_HANDLE)
    {
        const HANDLE output = GetStdHandle(standardHandle);
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

    bool WriteError(const std::wstring& text)
    {
        return WriteOutput(text, STD_ERROR_HANDLE);
    }

    // Escape UTF-16 code units that cannot be represented unambiguously
    // in terminal output. Keep native stream names unchanged for I/O.
    std::wstring EscapeDisplay(const std::wstring& name)
    {
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        std::wstring output;
        for (wchar_t ch : name)
        {
            const auto value = static_cast<unsigned int>(ch);
            const bool unsafe = value < 0x20 || (value >= 0x7F && value <= 0x9F) ||
                value == 0x061C || value == 0x200E || value == 0x200F ||
                (value >= 0x202A && value <= 0x202E) ||
                (value >= 0x2066 && value <= 0x2069) ||
                (value >= 0xD800 && value <= 0xDFFF);
            if (ch == L'\\')
            {
                output += LR"(\\)";
                continue;
            }
            if (!unsafe)
            {
                output += ch;
                continue;
            }
            output += L"\\u";
            output += hex[(value >> 12) & 15];
            output += hex[(value >> 8) & 15];
            output += hex[(value >> 4) & 15];
            output += hex[value & 15];
        }
        return output;
    }

    void PrintUsage(int argc, wchar_t* argv[])
    {
        const std::wstring program = argc > 0 ? argv[0] : L"altstrm";
        WriteOutput(EscapeDisplay(program) + L" [file path/spec]\r\n");
    }

    std::wstring SearchDirectory(const std::wstring& pattern)
    {
        const size_t separator = pattern.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
            return pattern.substr(0, separator + 1);

        // Drive-relative paths use the drive's current directory.
        if (pattern.size() >= 2 &&
            ((pattern[0] >= L'A' && pattern[0] <= L'Z') ||
             (pattern[0] >= L'a' && pattern[0] <= L'z')) &&
            pattern[1] == L':')
            return pattern.substr(0, 2);

        return L".\\";
    }

    bool IsDirectory(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    void WriteSearchError(DWORD error)
    {
        WriteError(L"File search failed (Win32 error " +
            std::to_wstring(error) + L")\r\n");
    }

    void ReportFindFirstFailure(DWORD error)
    {
        if (error == ERROR_FILE_NOT_FOUND)
            WriteOutput(L"No files to check.\r\n");
        else
            WriteSearchError(error);
    }

    bool ReportStreams(const AlternateStreamReader& reader, const std::wstring& fullPath)
    {
        auto enumeration = reader.EnumerateStreams(fullPath.c_str());
        if (enumeration.status != StreamStatus::Ok)
        {
            const std::wstring reason = enumeration.status == StreamStatus::Truncated
                ? L"Incomplete stream enumeration"
                : L"Stream enumeration failed";
            WriteError(reason + L" for " + EscapeDisplay(fullPath) + L" (Win32 error " +
                std::to_wstring(enumeration.error) + L")\r\n");
            return false;
        }

        // These raw names need not be usable as Win32 preview paths.
        // Enumeration is complete even when preview-path validation fails.

        auto& names = enumeration.streams;
        // The unnamed default stream is not an alternate data stream.
        names.erase(std::remove(names.begin(), names.end(), L"::$DATA"), names.end());
        if (names.empty())
            return true;

        std::wstring result = EscapeDisplay(fullPath) + L"\r\n";
        for (const auto& name : names)
            result += L" " + EscapeDisplay(name) + L"\r\n";
        return WriteOutput(result);
    }

    // Continue after individual enumeration failures, but retain a failing
    // exit status for an incomplete scan. The caller owns the search handle.
    bool ReportMatches(const AlternateStreamReader& reader, HANDLE search,
                       WIN32_FIND_DATAW& findData, const std::wstring& directory)
    {
        bool anyFailed = false;
        do
        {
            // Wildcard directory searches may include these synthetic entries.
            if (wcscmp(findData.cFileName, L".") == 0 ||
                wcscmp(findData.cFileName, L"..") == 0)
                continue;

            if (!ReportStreams(reader, directory + findData.cFileName))
                anyFailed = true;
        } while (FindNextFileW(search, &findData) != 0);

        const DWORD lastError = GetLastError();
        if (lastError != ERROR_NO_MORE_FILES)
        {
            WriteSearchError(lastError);
            anyFailed = true;
        }
        return !anyFailed;
    }
}

int Run(int argc, wchar_t* argv[])
{
    if (argc != 2)
    {
        PrintUsage(argc, argv);
        return 1;
    }

    // Exact directory arguments enumerate their own streams, not children.
    const std::wstring pattern(argv[1]);
    const std::wstring directory = SearchDirectory(pattern);
    if (!WriteOutput(L"Files with alternate streams matching " + EscapeDisplay(pattern) + L"\r\n"))
        return 1;

    AlternateStreamReader reader;
    // FindFirstFileW cannot enumerate a volume root (for example, C:\).
    if (IsDirectory(pattern))
        return ReportStreams(reader, pattern) ? 0 : 1;

    WIN32_FIND_DATAW findData = {};
    const HANDLE search = FindFirstFileW(pattern.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE)
    {
        ReportFindFirstFailure(GetLastError());
        return 1;
    }

    const bool success = ReportMatches(reader, search, findData, directory);
    FindClose(search);
    return success ? 0 : 1;
}


// Keep exceptions from escaping the C runtime entry point.
int wmain(int argc, wchar_t* argv[])
{
    try
    {
        return Run(argc, argv);
    }
    catch (...)
    {
        // Even diagnostics can allocate and throw during exception handling.
        try
        {
            WriteError(L"Unexpected failure while enumerating alternate streams.\r\n");
        }
        catch (...)
        {
            // Preserve the failure exit status if reporting also fails.
        }
        return 1;
    }
}
