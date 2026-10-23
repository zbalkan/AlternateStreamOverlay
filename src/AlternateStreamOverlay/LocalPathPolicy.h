#pragma once
#include <Windows.h>
#include <array>
#include <string>

// Reuse recently validated ancestors for sibling Explorer callbacks.
inline bool HasEligibleExplorerAncestors(const std::wstring& path, size_t offset,
                                         DWORD excludedAttributes)
{
    // Sibling overlay callbacks repeatedly inspect the same ancestors.
    // Cache only a successful directory walk, briefly and per thread.
    // The leaf is always rechecked; this is not a security boundary.
    const size_t lastSeparator = path.find_last_of(L"\\/");
    if (lastSeparator == std::wstring::npos || lastSeparator < offset + 2)
        return false;
#ifndef ASO_BENCHMARK_NO_CACHE
    const std::wstring directory = path.substr(0, lastSeparator);
    static thread_local std::wstring cachedDirectory;
    static thread_local ULONGLONG cachedTick = 0;
    const ULONGLONG now = GetTickCount64();
    if (directory != cachedDirectory || now - cachedTick > 250)
#endif
    {
#ifndef ASO_BENCHMARK_NO_CACHE
        cachedDirectory.clear();
#endif
        for (size_t end = offset + 3; end < path.size(); ++end)
        {
            if (path[end] != L'\\' && path[end] != L'/')
                continue;
            const std::wstring parent = path.substr(0, end);
            const DWORD attributes = GetFileAttributesW(parent.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & excludedAttributes) != 0)
                return false;
        }
#ifndef ASO_BENCHMARK_NO_CACHE
        cachedDirectory = directory;
        cachedTick = now;
#endif
    }

    return true;
}

// Avoid potentially blocking network/cloud I/O from Explorer callbacks.
// Reject any reparse component: its destination could be remote even when
// the drive itself is local. This is a best-effort, fail-closed guard.
inline bool CanEnumerateExplorerPath(const std::wstring& path)
{
    if (path.compare(0, 2, LR"(\\)") == 0 && path.compare(0, 4, LR"(\\?\)") != 0)
        return false;

    // CF_HDROP supplies absolute paths. Reject unrecognized path forms rather
    // than risk synchronous remote I/O in Explorer's initialization callback.
    size_t offset = 0;
    if (path.compare(0, 4, LR"(\\?\)") == 0)
        offset = 4;
    if (path.size() < offset + 3)
        return false;

    const WCHAR letter = path[offset];
    if (!((letter >= L'A' && letter <= L'Z') ||
          (letter >= L'a' && letter <= L'z')) ||
        path[offset + 1] != L':' || path[offset + 2] != L'\\')
        return false;

    const WCHAR root[] = { letter, L':', L'\\', L'\0' };
    const UINT driveType = GetDriveTypeW(root);
    if (driveType == DRIVE_REMOTE || driveType == DRIVE_UNKNOWN ||
        driveType == DRIVE_NO_ROOT_DIR)
        return false;

#if !defined(ASO_BENCHMARK_NO_CACHE) && !defined(ASO_BENCHMARK_NO_SUBST)
    // Reject SUBST/alias drive mappings, which may lead outside the local
    // volume even though their drive type is reported as local.
    const std::wstring drive = { letter, L':' };
    std::array<WCHAR, 1024> target = {};
    const DWORD targetLength = QueryDosDeviceW(drive.c_str(), target.data(),
        static_cast<DWORD>(target.size()));
    if (targetLength == 0 || wcsncmp(target.data(), LR"(\??\)", 4) == 0)
        return false;
#endif

    constexpr DWORD excludedAttributes = FILE_ATTRIBUTE_REPARSE_POINT |
        FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
        FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
    const DWORD rootAttributes = GetFileAttributesW(root);
    if (rootAttributes == INVALID_FILE_ATTRIBUTES ||
        (rootAttributes & excludedAttributes) != 0)
        return false;

    if (!HasEligibleExplorerAncestors(path, offset, excludedAttributes))
        return false;

    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & excludedAttributes) == 0;
}

// Explorer passes a pointer to a terminated path; avoid duplicate policy.
inline bool CanEnumerateExplorerPath(LPCWSTR path)
{
    return path != nullptr && *path != L'\0' &&
        CanEnumerateExplorerPath(std::wstring(path));
}
