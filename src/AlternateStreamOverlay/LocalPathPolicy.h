#pragma once
#include <Windows.h>
#include <string>

// Avoid potentially blocking network/cloud I/O from Explorer callbacks.
// Reject any reparse component: its destination could be remote even when
// the drive itself is local. This is a best-effort, fail-closed guard.
inline bool CanEnumerateExplorerPath(const std::wstring& path)
{
    if (path.compare(0, 2, L"\\\\") == 0 && path.compare(0, 4, L"\\\\?\\") != 0)
        return false;

    // CF_HDROP supplies absolute paths. Reject unrecognized path forms rather
    // than risk synchronous remote I/O in Explorer's initialization callback.
    size_t offset = 0;
    if (path.compare(0, 4, L"\\\\?\\") == 0)
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

    // A drive-letter path may cross a junction or symbolic link into a
    // remote location. Check each existing component before querying the
    // selected item; reject reparse points instead of following them.
    // This is a best-effort Explorer responsiveness guard, not a defense
    // against concurrent path replacement.
    constexpr DWORD excludedAttributes = FILE_ATTRIBUTE_REPARSE_POINT |
        FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
        FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
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
