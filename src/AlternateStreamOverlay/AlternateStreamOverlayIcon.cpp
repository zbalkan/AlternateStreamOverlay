#include "stdafx.h"
#include "AlternateStreamOverlayIcon.h"
#include <string>
#include <sstream>
#include <iostream>
#include "Common.h"

CAlternateStreamOverlayIcon::CAlternateStreamOverlayIcon()
{
}

namespace
{
bool IsLocalDrivePath(LPCWSTR path)
{
    // Mapped drives look like local paths; classify their drive root before
    // opening any file. Skip unsupported path forms on this Explorer hot path.
    if (wcsncmp(path, L"\\\\?\\", 4) == 0)
        path += 4;

    if (!((path[0] >= L'A' && path[0] <= L'Z') ||
          (path[0] >= L'a' && path[0] <= L'z')) ||
        path[1] != L':' || path[2] != L'\\')
        return false;

    const WCHAR root[] = { path[0], L':', L'\\', L'\0' };
    const UINT type = GetDriveTypeW(root);
    return type != DRIVE_REMOTE && type != DRIVE_UNKNOWN &&
           type != DRIVE_NO_ROOT_DIR;
}
}

// Does this overlay apply?
STDMETHODIMP CAlternateStreamOverlayIcon::IsMemberOf(LPCWSTR pwszPath, DWORD dwAttrib)
{
    try
    {
        if (pwszPath == nullptr || *pwszPath == L'\0')
            return S_FALSE;

        // Explorer calls this for every displayed item. Do not open files that
        // are offline or may be recalled from remote/cloud storage.
        constexpr DWORD recallAttributes =
            FILE_ATTRIBUTE_OFFLINE |
            FILE_ATTRIBUTE_RECALL_ON_OPEN |
            FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
        if ((dwAttrib & recallAttributes) != 0)
            return S_FALSE;

        // Avoid synchronous SMB/UNC I/O on Explorer's overlay callback path.
        if (PathIsUNCW(pwszPath) || !IsLocalDrivePath(pwszPath))
            return S_FALSE;

        return hasAlternateStreams(pwszPath) ? S_OK : S_FALSE;
    }
    catch (...)
    {
        // Overlay detection must not propagate C++ exceptions into Explorer.
        return S_FALSE;
    }
}

STDMETHODIMP CAlternateStreamOverlayIcon::GetOverlayInfo(LPWSTR pwszIconFile, int cchMax, int* pIndex, DWORD* pdwFlags)
{
    if (pwszIconFile == nullptr || pIndex == nullptr || pdwFlags == nullptr)
        return E_POINTER;
    if (cchMax <= 0)
        return E_INVALIDARG;

    // Explorer needs the full DLL path. Never advertise a truncated icon path.
    pwszIconFile[0] = L'\0';
    *pIndex = 0;
    *pdwFlags = 0;

    const DWORD written = GetModuleFileNameW(
        _AtlBaseModule.GetModuleInstance(), pwszIconFile, static_cast<DWORD>(cchMax));
    if (written == 0)
        return HRESULT_FROM_WIN32(GetLastError());
    if (written >= static_cast<DWORD>(cchMax))
    {
        pwszIconFile[0] = L'\0';
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    *pdwFlags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
    return S_OK;
}

STDMETHODIMP CAlternateStreamOverlayIcon::GetPriority(int* pIPriority)
{
    if (pIPriority == nullptr)
        return E_POINTER;

    *pIPriority = 10;
    return S_OK;
}
