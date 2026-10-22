#pragma once
#include "resource.h"       // main symbols

#include <shlobj.h>
#include <comdef.h>
#include "AlternateStreamOverlay_i.h"

/* Like the name implies, responsible for overlaying icon ... */

class ATL_NO_VTABLE CAlternateStreamOverlayIcon :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CAlternateStreamOverlayIcon, &CLSID_AlternateStreamOverlayIcon>,
    public IAlternateStreamOverlayIcon,
    public IShellIconOverlayIdentifier
{
public:
    CAlternateStreamOverlayIcon() = default;

    DECLARE_REGISTRY_RESOURCEID(IDR_ALTERNATESTREAMOVERLAYICON)

    DECLARE_NOT_AGGREGATABLE(CAlternateStreamOverlayIcon)

    BEGIN_COM_MAP(CAlternateStreamOverlayIcon)
        COM_INTERFACE_ENTRY(IAlternateStreamOverlayIcon)
        COM_INTERFACE_ENTRY(IShellIconOverlayIdentifier)
    END_COM_MAP()

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    HRESULT FinalConstruct()
    {
        return S_OK;
    }

    void FinalRelease()
    {
    }

public:
    // IShellIconOverlayIdentifier
    STDMETHOD(IsMemberOf)(LPCWSTR pwszPath, DWORD dwAttrib);
    STDMETHOD(GetOverlayInfo)(LPWSTR pwszIconFile, int cchMax, int* pIndex, DWORD* pdwFlags);
    STDMETHOD(GetPriority)(int* pIPriority);
};

OBJECT_ENTRY_AUTO(__uuidof(AlternateStreamOverlayIcon), CAlternateStreamOverlayIcon)
