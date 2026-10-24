#pragma once
#include "resource.h"       // main symbols

#include <shlobj.h>
#include <comdef.h>
#include <vector>
#include <string>
#include "SrwLock.h"
#include <Windows.h>

/* Responsible for the Explorer property tab */

class ATL_NO_VTABLE __declspec(uuid("4A26024B-5499-45BE-A23F-4A65E88888D0")) CAlternateStreamContext :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CAlternateStreamContext, &__uuidof(CAlternateStreamContext)>,
    public IShellExtInit,
    public IShellPropSheetExt
{
private:
    mutable SRWLOCK m_selectionLock = SRWLOCK_INIT;
    std::wstring m_path;
    bool m_enumerationIncomplete = false;
    std::vector<std::wstring> m_streams;
public:
    CAlternateStreamContext() = default;
    CAlternateStreamContext(const CAlternateStreamContext&) = delete;
    CAlternateStreamContext& operator=(const CAlternateStreamContext&) = delete;

    DECLARE_REGISTRY_RESOURCEID(IDR_ALTERNATESTREAMCONTEXT)

    DECLARE_NOT_AGGREGATABLE(CAlternateStreamContext)

    BEGIN_COM_MAP(CAlternateStreamContext)
        COM_INTERFACE_ENTRY(IShellExtInit)
        COM_INTERFACE_ENTRY(IShellPropSheetExt)
    END_COM_MAP()

    DECLARE_PROTECT_FINAL_CONSTRUCT()

    HRESULT FinalConstruct()
    {
        return S_OK;
    }

public:
    // Native
    void SnapshotSelection(std::wstring& path,
        std::vector<std::wstring>& streams, bool& incomplete) const;
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

OBJECT_ENTRY_AUTO(__uuidof(CAlternateStreamContext), CAlternateStreamContext)
