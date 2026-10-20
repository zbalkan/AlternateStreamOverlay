#pragma once
#include "resource.h"       // main symbols

#include "AlternateStreamOverlay_i.h"
#include <shlobj.h>
#include <comdef.h>
#include <vector>
#include <string>
#include <windows.h>

/* Provides the alternate-stream property tab in Explorer. */

class ATL_NO_VTABLE CAlternateStreamContext :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CAlternateStreamContext, &CLSID_AlternateStreamContext>,
    public IAlternateStreamContext,
    public IShellExtInit,
    public IShellPropSheetExt
{
private:
    std::wstring m_path;
    bool m_isDirectory;
    std::vector<std::wstring> m_streams;
public:
    CAlternateStreamContext() : m_isDirectory(false)
    {
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_ALTERNATESTREAMCONTEXT)

    DECLARE_NOT_AGGREGATABLE(CAlternateStreamContext)

    BEGIN_COM_MAP(CAlternateStreamContext)
        COM_INTERFACE_ENTRY(IAlternateStreamContext)
        COM_INTERFACE_ENTRY(IShellExtInit)
        COM_INTERFACE_ENTRY(IShellPropSheetExt)
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
    // Native
    const std::vector<std::wstring>& get_streams() const;
    const std::wstring& get_path() const;
    // IShellExtInit
    STDMETHODIMP Initialize(LPCITEMIDLIST pidlFolder, IDataObject* pdtobj, HKEY hkeyProgId);
    // IShellPropSheetExt
    STDMETHODIMP AddPages(
        LPFNSVADDPROPSHEETPAGE pfnAddPage,
        LPARAM lParam);
    STDMETHODIMP ReplacePage(
        EXPPS uPageID,
        LPFNSVADDPROPSHEETPAGE pfnReplaceWith,
        LPARAM lParam);
};

OBJECT_ENTRY_AUTO(__uuidof(AlternateStreamContext), CAlternateStreamContext)
