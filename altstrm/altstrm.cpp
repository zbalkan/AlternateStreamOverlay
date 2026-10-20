//
// A very simple command line test of altstrm_common
//

#include "stdafx.h"
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <string>
#include <sstream>
#include <iostream>
#include "altstrm_common.h"

AlternateStreamReader g_streamReader;

static int _tmain(int argc, _TCHAR* argv[])
{
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind;

    if (argc != 2)
    {
        std::wcout << argv[0] << L" [file path/spec]\n";
        return 1;
    }

    std::wstring arg(argv[1]);

    // if this is a directory, postfix '\*', if not carry on.

    std::wstring::size_type pos = arg.find_last_of(L'\\');
    std::wstring directory = L".\\";
    if (pos != std::wstring::npos)
    {
        directory = arg.substr(0, pos + 1);
    }

    std::wcout << L"Files with alternate streams matching " << argv[1] << L"\n";
    hFind = FindFirstFile(arg.c_str(), &FindFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        std::wcout << L"No files to check.\n";
        return 1;
    }

    do
    {
        bool isDir = (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        std::wstring fullPath = directory + FindFileData.cFileName;
        std::vector<std::wstring> subnames = g_streamReader.ListAlternateStreams(fullPath.c_str());
        if (subnames.size() > (isDir ? 0u : 1u))
        {
            std::wcout << fullPath << L"\n";
            for (std::vector<std::wstring>::const_iterator vit = subnames.begin(); vit != subnames.end(); ++vit)
            {
                std::wcout << L" " << *vit << L"\n";
            }
        }
    } while (FindNextFile(hFind, &FindFileData) != 0);
    FindClose(hFind);

    return 0;
}
