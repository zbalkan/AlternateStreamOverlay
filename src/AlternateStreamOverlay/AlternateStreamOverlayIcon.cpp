#include "stdafx.h"
#include "AlternateStreamOverlayIcon.h"
#include "altstrm_common.h"
#include "LocalPathPolicy.h"

namespace
{
    bool CanCheckOverlay(LPCWSTR path, DWORD attributes)
    {
        if (path == nullptr || *path == L'\0')
            return false;
        constexpr DWORD recallAttributes =
            FILE_ATTRIBUTE_OFFLINE |
            FILE_ATTRIBUTE_RECALL_ON_OPEN |
            FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
        return (attributes & recallAttributes) == 0 &&
            CanEnumerateExplorerPath(path);
    }

    HRESULT ReadOverlayIconInfo(LPWSTR iconFile, int maxCharacters,
                                int* iconIndex, DWORD* flags, int index)
    {
        if (iconFile == nullptr || iconIndex == nullptr || flags == nullptr)
            return E_POINTER;
        if (maxCharacters <= 0)
            return E_INVALIDARG;
        iconFile[0] = L'\0';
        *iconIndex = 0;
        *flags = 0;
        const DWORD written = GetModuleFileNameW(
            _AtlBaseModule.GetModuleInstance(), iconFile,
            static_cast<DWORD>(maxCharacters));
        if (written == 0)
            return HRESULT_FROM_WIN32(GetLastError());
        if (written >= static_cast<DWORD>(maxCharacters))
        {
            iconFile[0] = L'\0';
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        *iconIndex = index;
        *flags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
        return S_OK;
    }

    HRESULT CheckOverlayMembership(LPCWSTR path, DWORD attributes, OverlayKind kind)
    {
        try
        {
            if (!CanCheckOverlay(path, attributes))
                return S_FALSE;
            return ClassifyFileOverlay(path) == kind ? S_OK : S_FALSE;
        }
        catch (...)
        {
            return S_FALSE;
        }
    }
}

STDMETHODIMP CAlternateStreamOverlayIcon::IsMemberOf(LPCWSTR path, DWORD attributes)
{
    return CheckOverlayMembership(path, attributes, OverlayKind::AlternateStreams);
}

STDMETHODIMP CMarkOfTheWebOverlayIcon::IsMemberOf(LPCWSTR path, DWORD attributes)
{
    return CheckOverlayMembership(path, attributes, OverlayKind::MarkOfTheWeb);
}

STDMETHODIMP CAlternateStreamOverlayIcon::GetOverlayInfo(LPWSTR file, int maxCharacters,
                                                         int* index, DWORD* flags)
{
    return ReadOverlayIconInfo(file, maxCharacters, index, flags, 0);
}

STDMETHODIMP CMarkOfTheWebOverlayIcon::GetOverlayInfo(LPWSTR file, int maxCharacters,
                                                       int* index, DWORD* flags)
{
    return ReadOverlayIconInfo(file, maxCharacters, index, flags, 1);
}

STDMETHODIMP CAlternateStreamOverlayIcon::GetPriority(int* priority)
{
    if (priority == nullptr)
        return E_POINTER;
    *priority = 10;
    return S_OK;
}

STDMETHODIMP CMarkOfTheWebOverlayIcon::GetPriority(int* priority)
{
    if (priority == nullptr)
        return E_POINTER;
    *priority = 5;
    return S_OK;
}
