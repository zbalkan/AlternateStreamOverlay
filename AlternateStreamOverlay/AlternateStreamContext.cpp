#include "stdafx.h"
#include "AlternateStreamContext.h"
#include <strsafe.h>
#include <vector>
#include <sstream>
#include <string>
#include <ios>
#include "Common.h"

constexpr auto START_OF_HEX_TEXT = 10;
constexpr auto START_OF_ASCII_TEXT = 36;

STDMETHODIMP CAlternateStreamContext::Initialize(LPCITEMIDLIST pidlFolder, IDataObject* pDataObj, HKEY hkeyProgId)
{
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg = { TYMED_HGLOBAL };
    if (pDataObj == nullptr) return E_INVALIDARG;

    HDROP hDrop = nullptr;

    if (FAILED(pDataObj->GetData(&fmt, &stg)))
    {
        return E_INVALIDARG;
    }

    hDrop = (HDROP)GlobalLock(stg.hGlobal);

    if (hDrop == nullptr)
    {
        ReleaseStgMedium(&stg);
        return E_INVALIDARG;
    }

    UINT uNumFiles = DragQueryFile(hDrop, 0xffffffff, NULL, 0);
    HRESULT hr = S_OK;
    // We only handle single file case.
    if (1 != uNumFiles)
    {
        GlobalUnlock(stg.hGlobal);
        ReleaseStgMedium(&stg);
        return E_INVALIDARG;
    }

    const UINT pathLength = DragQueryFileW(hDrop, 0, nullptr, 0);
    if (pathLength == 0)
        hr = E_INVALIDARG;

    std::wstring filePath;
    if (SUCCEEDED(hr))
    {
        std::vector<WCHAR> path(pathLength + 1);
        if (DragQueryFileW(hDrop, 0, path.data(), static_cast<UINT>(path.size())) != pathLength)
            hr = E_INVALIDARG;
        else
            filePath.assign(path.data(), pathLength);
    }

    GlobalUnlock(stg.hGlobal);
    ReleaseStgMedium(&stg);

    if (FAILED(hr)) return hr;

    const bool directory = PathIsDirectoryW(filePath.c_str()) != FALSE;
    m_path = filePath;
    m_isDirectory = directory;
    m_streams = listAlternateStreams(filePath.c_str());
    // We have the filename, do we want to present a menu item for it?
    if (m_streams.size() <= (directory ? 0u : 1u))
    {
        hr = E_FAIL;
    }

    return hr;
}

STDMETHODIMP CAlternateStreamContext::QueryContextMenu(
    HMENU hmenu,
    UINT indexMenu,
    UINT idCmdFirst,
    UINT idCmdLast,
    UINT uFlags)
{
    if (uFlags & CMF_DEFAULTONLY || m_streams.size() <= (m_isDirectory ? 0u : 1u))
    {
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);
    }

    UINT uId = idCmdFirst;

    //	InsertMenu(hmenu, uId, MF_BYPOSITION | MF_SEPARATOR, uId, L"");
    //	uId++;
    HMENU subMenu = CreatePopupMenu();

    UINT id = 0;
    for (std::vector<std::wstring>::const_iterator vit = m_streams.begin(); vit != m_streams.end(); ++vit)
    {
        InsertMenu(subMenu, id++, MF_BYPOSITION, uId++, vit->c_str());
    }

    MENUITEMINFO mii = { sizeof(MENUITEMINFO) };

    mii.fMask = MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
    mii.wID = uId++;
    mii.hSubMenu = subMenu;
    mii.dwTypeData = _T("Alternate Streams");

    InsertMenuItem(hmenu, indexMenu, TRUE, &mii);

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, uId - idCmdFirst);
}

// Don't actually do anything - it's available in the property page.
STDMETHODIMP CAlternateStreamContext::InvokeCommand(
    CMINVOKECOMMANDINFO* pCmdInfo)
{
    return S_OK;
}

STDMETHODIMP CAlternateStreamContext::GetCommandString(
    UINT_PTR idCmd,
    UINT uFlags,
    UINT* pReserved,
    LPSTR pszName,
    UINT cchMax)
{
    if (idCmd != 0 || !(uFlags & GCS_HELPTEXT) || pszName == nullptr || cchMax == 0)
        return E_INVALIDARG;

    constexpr wchar_t helpW[] = L"AlternateStreamOverlay - www.benf.org";
    constexpr char helpA[] = "AlternateStreamOverlay - www.benf.org";
    if (uFlags & GCS_UNICODE)
        return StringCchCopyW(reinterpret_cast<LPWSTR>(pszName), cchMax, helpW);
    return StringCchCopyA(pszName, cchMax, helpA);
}

const std::vector<std::wstring>& CAlternateStreamContext::get_streams() const
{
    return m_streams;
}

const std::wstring& CAlternateStreamContext::get_path() const
{
    return m_path;
}

char asciificate(BYTE b)
{
    if (b >= 32 && b <= 127) return (char)b;
    return '.';
}

#pragma warning(push)
#pragma warning(disable:4996)
// I don't really know why, but I still prefer sprintf(X) to ios::hex. ;)
void hexinto(char* tgt, unsigned char val)
{
    sprintf(tgt, "%2.2X", val);
    tgt[2] = ' ';
}

void hexinto(char* tgt, unsigned int val)
{
    sprintf(tgt, "%8.8X", val);
    tgt[8] = ' ';
}
#pragma warning(pop)

void setuphexline(char* tgt, const int len, char*& hexo, char*& txto)
{
    memset(tgt, ' ', len);
    hexo = &tgt[START_OF_HEX_TEXT];
    txto = &tgt[START_OF_ASCII_TEXT];
}

void DumpHex(BYTE* buf, DWORD size, std::stringstream& tgt)
{
    const int LINESIZE = 46;
    char tmp[LINESIZE + 4];
    tmp[LINESIZE] = ' ';
    tmp[LINESIZE + 1] = '\r';
    tmp[LINESIZE + 2] = '\n';
    tmp[LINESIZE + 3] = 0;
    char* lineo = &tmp[0];
    char* hexo = nullptr;
    char* charo = nullptr;
    setuphexline(lineo, LINESIZE, hexo, charo);
    size_t cnt = 0;
    for (unsigned int x = 0;x < size;++x, ++buf)
    {
        if (cnt == 0)
        {
            hexinto(lineo, x);
        }
        hexinto(hexo, (unsigned char)(*buf));
        *charo = asciificate(*buf);
        cnt++;
        hexo += 3;
        charo++;
        if (cnt == 8)
        {
            tgt << tmp;
            setuphexline(lineo, LINESIZE, hexo, charo);
            cnt = 0;
        }
    }
    if (cnt != 0)
        tgt << tmp;
}

void SelectText(CAlternateStreamContext* pthis, size_t idx, bool asHex, HWND textBox)
{
    const std::vector<std::wstring>& streams = pthis->get_streams();
    const std::wstring& fname = pthis->get_path();

    if (idx >= streams.size())
    {
        OutputDebugStringA("Invalid index");
        return;
    }

    std::wstringstream ss;
    ss << fname << streams[idx];
    std::wstring full_path = ss.str();

    HandleW hFile(::CreateFile(full_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));

    if (hFile == INVALID_HANDLE_VALUE)
    {
        // Shouldn't be called for this file, so dbgerror
        OutputDebugStringA("Asked to get properties for invalid path");
        OutputDebugString(full_path.c_str());
        return;
    }

    // Large buffers belong on the heap, not on Explorer's callback stack.
    std::vector<BYTE> buffer(64 * 1024);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size() - 1),
                  &bytesRead, nullptr))
    {
        SetWindowTextW(textBox, L"Unable to read alternate stream.");
        return;
    }

    if (asHex)
    {
        std::stringstream output;
        DumpHex(buffer.data(), bytesRead, output);
        const std::string formatted = output.str();
        SetWindowTextA(textBox, formatted.c_str());
    }
    else
    {
        buffer[bytesRead] = 0;
        SetWindowTextA(textBox, reinterpret_cast<const char*>(buffer.data()));
    }
}

void CAlternateStreamContext::attach_font_resource(HFONT font)
{
    this->font = font;
}

BOOL OnInitDialog(HWND hWnd, LPARAM lParam)
{
    PROPSHEETPAGE* ppsp = (PROPSHEETPAGE*)lParam;
    CAlternateStreamContext* pthis = reinterpret_cast<CAlternateStreamContext*>(ppsp->lParam);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pthis));

    HWND comboBox = GetDlgItem(hWnd, IDC_COMBO1);
    HWND checkBox = GetDlgItem(hWnd, IDC_CHECK1);
    HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);

    const std::vector<std::wstring>& streams = pthis->get_streams();
    for (std::vector<std::wstring>::const_iterator vit = streams.begin(); vit != streams.end(); ++vit)
    {
        SendMessage(comboBox, CB_ADDSTRING, 0, (LPARAM)vit->c_str());
    }

    HFONT font = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, L"Courier New");
    pthis->attach_font_resource(font);
    SendMessage(textBox, WM_SETFONT, (WPARAM)font, MAKELPARAM(TRUE, 0));

    SendMessage(comboBox, CB_SETCURSEL, 0, 0);
    SendMessage(checkBox, BM_SETCHECK, 0, 0);
    SelectText(pthis, 0, false, textBox);
    return FALSE;
}

UINT CALLBACK PropPageCallbackProc(HWND hWnd, UINT uMsg, LPPROPSHEETPAGE ppsp)
{
    CAlternateStreamContext* pthis = reinterpret_cast<CAlternateStreamContext*>(ppsp->lParam);

    if (uMsg == PSPCB_RELEASE)
    {
        // Releasing the CAlternateStreamContext will delete the font resource.
        pthis->Release();
        ppsp->lParam = 0;
    }

    return 1;
}

typedef INT_PTR DLGRETURN;

DLGRETURN CALLBACK PropPageDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    DLGRETURN bRet = FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        bRet = OnInitDialog(hWnd, lParam);
        break;
    case WM_NOTIFY:
        break;
    case WM_COMMAND:
    {
        HWND comboBox = GetDlgItem(hWnd, IDC_COMBO1);
        HWND checkBox = GetDlgItem(hWnd, IDC_CHECK1);
        HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);

        int wmid = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);
        switch (wmid)
        {
        case IDC_COMBO1:
            if (wmEvent == CBN_SELCHANGE)
            { // sue me ;)
        case IDC_CHECK1:
        {
            CAlternateStreamContext* pthis = reinterpret_cast<CAlternateStreamContext*>(
                GetWindowLongPtr(hWnd, GWLP_USERDATA));
            LRESULT idx = SendMessage(comboBox, CB_GETCURSEL, 0, 0);
            bool checked = (SendMessage(checkBox, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (idx >= 0)
            {
                SelectText(pthis, (size_t)idx, checked, textBox);
            }
        }
            }
            break;
        }
    }
    break;
    }
    return bRet;
}

STDMETHODIMP CAlternateStreamContext::AddPages(
    LPFNSVADDPROPSHEETPAGE pfnAddPage,
    LPARAM lParam)
{
    PROPSHEETPAGE psp = {};
    HPROPSHEETPAGE hPage = nullptr;
    LPCWSTR szPageTitle = L"AlternateStreams";
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = PSP_USETITLE | PSP_USECALLBACK;
    psp.hInstance = _AtlBaseModule.GetResourceInstance();
    psp.pszTemplate = MAKEINTRESOURCE(IDD_PROPPAGE_MEDIUM);
    psp.pfnCallback = PropPageCallbackProc;
    psp.pfnDlgProc = PropPageDlgProc;
    psp.lParam = (LPARAM)this;
    psp.pszTitle = szPageTitle;

    // The property-page release callback owns a reference once the page exists.
    // Acquire it first; otherwise a page destroyed synchronously can release
    // an object whose reference count has not yet been incremented.
    this->AddRef();
    hPage = CreatePropertySheetPage(&psp);
    if (hPage == nullptr)
    {
        this->Release();
        return E_OUTOFMEMORY;
    }

    if (!pfnAddPage(hPage, lParam))
    {
        // PSPCB_RELEASE balances AddRef when this page is destroyed.
        DestroyPropertySheetPage(hPage);
        return E_FAIL;
    }
    return S_OK;
}

STDMETHODIMP CAlternateStreamContext::ReplacePage(
    EXPPS uPageID,
    LPFNSVADDPROPSHEETPAGE pfnReplaceWith,
    LPARAM lParam)
{
    return E_NOTIMPL;
}
