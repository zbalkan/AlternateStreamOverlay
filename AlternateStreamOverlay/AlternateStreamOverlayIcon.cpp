#include "stdafx.h"
#include "AlternateStreamOverlayIcon.h"
#include <string>
#include <sstream>
#include <iostream>
#include "Common.h"

CAlternateStreamOverlayIcon::CAlternateStreamOverlayIcon()
{
}

// Does this overlay apply?
STDMETHODIMP CAlternateStreamOverlayIcon::IsMemberOf(LPCWSTR pwszPath, DWORD dwAttrib)
{
    if (pwszPath == nullptr || *pwszPath == L'\0')
        return S_FALSE;

    return hasAlternateStreams(pwszPath) ? S_OK : S_FALSE;
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
