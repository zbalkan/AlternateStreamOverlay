#pragma once
#include "resource.h"       // main symbols

#include "AlternateStreamOverlay_i.h"
#include <shlobj.h>
#include <comdef.h>
#include <vector>
#include <string>
#include <windows.h>

/* Responsible for context menu item, and property tab */

class ATL_NO_VTABLE CAlternateStreamContext :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CAlternateStreamContext, &CLSID_AlternateStreamContext>,
    public IAlternateStreamContext,
    public IShellExtInit,
    public IContextMenu,
    public IShellPropSheetExt
{
private:
    HFONT font;
    std::wstring m_path;
    bool m_isDirectory;
    std::vector<std::wstring> m_streams;
public:
    CAlternateStreamContext() : font(NULL), m_isDirectory(false)
    {
    }

    ~CAlternateStreamContext()
    {
        if (font != NULL)
        {
            DeleteObject(font);
        }
    }

    DECLARE_REGISTRY_RESOURCEID(IDR_ALTERNATESTREAMCONTEXT)

    DECLARE_NOT_AGGREGATABLE(CAlternateStreamContext)

    BEGIN_COM_MAP(CAlternateStreamContext)
        COM_INTERFACE_ENTRY(IAlternateStreamContext)
        COM_INTERFACE_ENTRY(IShellExtInit)
        COM_INTERFACE_ENTRY(IContextMenu)
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
    void attach_font_resource(HFONT font);
    // IShellExtInit
    STDMETHODIMP Initialize(LPCITEMIDLIST pidlFolder, IDataObject* pdtobj, HKEY hkeyProgId);
    // IContextMenu
    STDMETHODIMP QueryContextMenu(
        HMENU hmenu,
        UINT indexMenu,
        UINT idCmdFirst,
        UINT idCmdLast,
        UINT uFlags);
    STDMETHODIMP InvokeCommand(
        CMINVOKECOMMANDINFO* pici);
    STDMETHODIMP GetCommandString(
        UINT_PTR idCmd,
        UINT uType,
        UINT* pReserved,
        LPSTR pszName,
        UINT cchMax);
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
