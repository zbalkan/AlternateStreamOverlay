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
    return hasAlternateStreams(pwszPath) ? S_OK : S_FALSE;
}

STDMETHODIMP CAlternateStreamOverlayIcon::GetOverlayInfo(LPWSTR pwszIconFile, int cchMax, int* pIndex, DWORD* pdwFlags)
{
    // Use the first icon from our file.
    GetModuleFileNameW(_AtlBaseModule.GetModuleInstance(), pwszIconFile, cchMax);
    *pIndex = 0;
    *pdwFlags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
    return S_OK;
}

STDMETHODIMP CAlternateStreamOverlayIcon::GetPriority(int* pIPriority)
{
    *pIPriority = 10;
    return S_OK;
}
