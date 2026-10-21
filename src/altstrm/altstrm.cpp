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
    const std::wstring directory = separator == std::wstring::npos
        ? L".\\" : pattern.substr(0, separator + 1);

    if (!WriteOutput(L"Files with alternate streams matching " +
                     std::wstring(argv[1]) + L"\r\n"))
        return 1;

    WIN32_FIND_DATAW findData = {};
    const HANDLE search = FindFirstFileW(pattern.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE)
    {
        WriteOutput(L"No files to check.\r\n");
        return 1;
    }

    AlternateStreamReader reader;
    bool success = true;
    do
    {
        const std::wstring fullPath = directory + findData.cFileName;
        std::vector<std::wstring> names = reader.ListAlternateStreams(fullPath.c_str());
        // The unnamed default stream is not an alternate data stream.
        names.erase(std::remove(names.begin(), names.end(), L"::$DATA"), names.end());
        if (!names.empty())
        {
            std::wstring result = fullPath + L"\r\n";
            for (const auto& name : names)
                result += L" " + name + L"\r\n";
            if (!WriteOutput(result))
            {
                success = false;
                break;
            }
        }
    } while (FindNextFileW(search, &findData) != 0);

    const DWORD lastError = GetLastError();
    FindClose(search);
    if (success && lastError != ERROR_NO_MORE_FILES)
        success = false;
    return success ? 0 : 1;
}
