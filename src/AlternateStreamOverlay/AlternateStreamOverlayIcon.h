#pragma once
#include "resource.h"       // main symbols

#include <shlobj.h>

/* Like the name implies, responsible for overlaying icon ... */

class ATL_NO_VTABLE __declspec(uuid("FBAB8C58-59C9-4522-B7E8-1A6AA23B318C")) CAlternateStreamOverlayIcon :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CAlternateStreamOverlayIcon, &__uuidof(CAlternateStreamOverlayIcon)>,
    public IShellIconOverlayIdentifier
{
public:
    CAlternateStreamOverlayIcon() = default;

    DECLARE_REGISTRY_RESOURCEID(IDR_ALTERNATESTREAMOVERLAYICON)

    DECLARE_NOT_AGGREGATABLE(CAlternateStreamOverlayIcon)

    BEGIN_COM_MAP(CAlternateStreamOverlayIcon)
        COM_INTERFACE_ENTRY(IShellIconOverlayIdentifier)
    END_COM_MAP()

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    HRESULT FinalConstruct()
    {
        return S_OK;
    }

public:
    // IShellIconOverlayIdentifier
    STDMETHOD(IsMemberOf)(LPCWSTR pwszPath, DWORD dwAttrib);
    STDMETHOD(GetOverlayInfo)(LPWSTR pwszIconFile, int cchMax, int* pIndex, DWORD* pdwFlags);
    STDMETHOD(GetPriority)(int* pIPriority);
};

OBJECT_ENTRY_AUTO(__uuidof(CAlternateStreamOverlayIcon), CAlternateStreamOverlayIcon)
