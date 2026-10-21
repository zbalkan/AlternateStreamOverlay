#include "stdafx.h"
#include "AlternateStreamContext.h"
#include <algorithm>
#include <strsafe.h>
#include <vector>
#include <sstream>
#include <string>
#include <ios>
#include "Common.h"

// IShellExtInit fixes this signature. Keep unused arguments unnamed to avoid C4100.
STDMETHODIMP CAlternateStreamContext::Initialize(LPCITEMIDLIST, IDataObject* pDataObj, HKEY)
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
    // FILE_STREAM_INFO includes the unnamed default stream for ordinary files.
    // The property page should present only named alternate data streams.
    m_streams.erase(
        std::remove(m_streams.begin(), m_streams.end(), L"::$DATA"),
        m_streams.end());
    if (m_streams.empty())
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
    if ((uFlags & CMF_DEFAULTONLY) || m_streams.empty())
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

    if (hmenu == nullptr)
        return E_INVALIDARG;
    if (idCmdFirst > idCmdLast)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);

    MENUITEMINFOW item = { sizeof(item) };
    item.fMask = MIIM_ID | MIIM_STRING;
    item.wID = idCmdFirst;
    // Alt+A when the classic context menu is open. Explorer chooses
    // whether a mnemonic is available when other commands use the same key.
    item.dwTypeData = const_cast<LPWSTR>(L"&Alternate Streams...");
    if (!InsertMenuItemW(hmenu, indexMenu, TRUE, &item))
        return HRESULT_FROM_WIN32(GetLastError());

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 1);
}

STDMETHODIMP CAlternateStreamContext::InvokeCommand(
    CMINVOKECOMMANDINFO* pCmdInfo)
{
    if (pCmdInfo == nullptr)
        return E_INVALIDARG;
    if (m_path.empty() || m_streams.empty())
        return E_FAIL;

    bool invoked = false;
    if (HIWORD(pCmdInfo->lpVerb) == 0)
    {
        invoked = LOWORD(pCmdInfo->lpVerb) == 0;
    }
    else if (pCmdInfo->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) &&
             (pCmdInfo->fMask & CMIC_MASK_UNICODE))
    {
        const auto* info = reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(pCmdInfo);
        if (info->lpVerbW != nullptr && HIWORD(info->lpVerbW) != 0)
            invoked = _wcsicmp(info->lpVerbW, L"alternatestreams") == 0;
    }
    if (!invoked && HIWORD(pCmdInfo->lpVerb) != 0)
        invoked = _stricmp(pCmdInfo->lpVerb, "alternatestreams") == 0;
    if (!invoked)
        return E_INVALIDARG;

    // Request our property page by its PSP_USETITLE name. Shell versions
    // may fall back to the default tab if they cannot activate the page.
    if (!SHObjectProperties(pCmdInfo->hwnd, SHOP_FILEPATH,
                            m_path.c_str(), L"AlternateStreams"))
        return HRESULT_FROM_WIN32(GetLastError());

    return S_OK;
}

// IContextMenu fixes this signature; the reserved argument is intentionally unnamed.
STDMETHODIMP CAlternateStreamContext::GetCommandString(
    UINT_PTR idCmd,
    UINT uFlags,
    UINT*,
    LPSTR pszName,
    UINT cchMax)
{
    if (idCmd != 0)
        return E_INVALIDARG;

    const UINT kind = uFlags & 0x00000007;
    if (kind == GCS_VALIDATEA || kind == GCS_VALIDATEW)
        return S_OK;
    if (pszName == nullptr || cchMax == 0)
        return E_INVALIDARG;

    if (kind == GCS_HELPTEXTA)
        return StringCchCopyA(pszName, cchMax, "Inspect alternate data streams");
    if (kind == GCS_HELPTEXTW)
        return StringCchCopyW(reinterpret_cast<LPWSTR>(pszName), cchMax,
                              L"Inspect alternate data streams");
    if (kind == GCS_VERBA)
        return StringCchCopyA(pszName, cchMax, "alternatestreams");
    if (kind == GCS_VERBW)
        return StringCchCopyW(reinterpret_cast<LPWSTR>(pszName), cchMax,
                              L"alternatestreams");
    return E_INVALIDARG;
}

const std::vector<std::wstring>& CAlternateStreamContext::get_streams() const
{
    return m_streams;
}

const std::wstring& CAlternateStreamContext::get_path() const
{
    return m_path;
}

namespace
{
char asciificate(BYTE b)
{
    if (b >= 32 && b <= 126) return static_cast<char>(b);
    return '.';
}

// Eight-byte rows keep the ASCII column visible in the narrow properties tab.
void DumpHex(const BYTE* bytes, DWORD size, std::stringstream& output)
{
    constexpr DWORD BytesPerLine = 8;
    constexpr char Digits[] = "0123456789ABCDEF";

    for (DWORD offset = 0; offset < size; offset += BytesPerLine)
    {
        // Each offset covers the first 64 KiB, so eight hex digits suffice.
        for (int shift = 28; shift >= 0; shift -= 4)
            output << Digits[(offset >> shift) & 0xF];
        output << "  ";

        const DWORD count = (size - offset < BytesPerLine)
            ? size - offset : BytesPerLine;
        for (DWORD i = 0; i < BytesPerLine; ++i)
        {
            if (i < count)
            {
                const BYTE b = bytes[offset + i];
                output << Digits[b >> 4] << Digits[b & 0xF] << ' ';
            }
            else
                output << "   ";
        }

        output << " |";
        for (DWORD i = 0; i < count; ++i)
            output << asciificate(bytes[offset + i]);
        for (DWORD i = count; i < BytesPerLine; ++i)
            output << ' ';
        output << "|\r\n";
    }
}

void SelectText(CAlternateStreamContext* pthis, size_t idx, bool asHex, HWND textBox)
{
    if (textBox == nullptr || pthis == nullptr)
        return;

    SetWindowTextW(textBox, L"");
    const std::vector<std::wstring>& streams = pthis->get_streams();
    if (idx >= streams.size())
        return;

    const std::wstring& fname = pthis->get_path();

    const std::wstring full_path = fname + streams[idx];

    HandleW hFile(::CreateFile(full_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));

    if (hFile == INVALID_HANDLE_VALUE)
    {
        SetWindowTextW(textBox, L"Unable to open alternate stream.");
        return;
    }

    // Read one byte beyond the display limit to detect truncated previews
    // without loading an unbounded stream into the Explorer process.
    constexpr DWORD MaxPreviewBytes = 64 * 1024 - 1;
    std::vector<BYTE> buffer(MaxPreviewBytes + 1);
    DWORD bytesRead = 0;
    while (bytesRead < buffer.size())
    {
        DWORD count = 0;
        const DWORD remaining = static_cast<DWORD>(buffer.size()) - bytesRead;
        if (!ReadFile(hFile, buffer.data() + bytesRead, remaining,
                      &count, nullptr))
        {
            SetWindowTextW(textBox, L"Unable to read alternate stream.");
            return;
        }
        if (count == 0)
            break;
        bytesRead += count;
    }
    const bool truncated = bytesRead > MaxPreviewBytes;
    if (truncated)
        bytesRead = MaxPreviewBytes;

    if (!asHex)
    {
        std::wstring text;
        if (TryDecodeUtf8Preview(buffer.data(), bytesRead, truncated, text))
        {
            if (truncated)
                text += L"\r\n[Preview truncated after 65535 bytes]";
            SetWindowTextW(textBox, text.c_str());
            return;
        }
    }

    // Binary, non-UTF-8, or explicitly requested hexadecimal preview.
    std::stringstream output;
    DumpHex(buffer.data(), bytesRead, output);
    std::string formatted = output.str();
    if (truncated)
        formatted += "\r\n[Preview truncated after 65535 bytes]";
    const std::wstring wide(formatted.begin(), formatted.end());
    SetWindowTextW(textBox, wide.c_str());
}
} // namespace

BOOL OnInitDialog(HWND hWnd, LPARAM lParam)
{
    if (lParam == 0)
        return FALSE;

    PROPSHEETPAGE* ppsp = reinterpret_cast<PROPSHEETPAGE*>(lParam);
    CAlternateStreamContext* pthis = reinterpret_cast<CAlternateStreamContext*>(ppsp->lParam);
    if (pthis == nullptr)
        return FALSE;

    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pthis));

    HWND comboBox = GetDlgItem(hWnd, IDC_COMBO1);
    HWND checkBox = GetDlgItem(hWnd, IDC_CHECK1);
    HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);

    const std::vector<std::wstring>& streams = pthis->get_streams();
    for (std::vector<std::wstring>::const_iterator vit = streams.begin(); vit != streams.end(); ++vit)
    {
        SendMessage(comboBox, CB_ADDSTRING, 0, (LPARAM)vit->c_str());
    }

    // Each dialog owns its preview font. The COM object may outlive the page
    // or own more than one page, so it must not own the dialog's GDI objects.
    HFONT font = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, L"Courier New");
    if (font != nullptr && textBox != nullptr)
    {
        SetWindowLongPtr(textBox, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(font));
        SendMessage(textBox, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    else if (font != nullptr)
    {
        DeleteObject(font);
    }

    SendMessage(checkBox, BM_SETCHECK, BST_UNCHECKED, 0);
    if (!streams.empty())
    {
        SendMessage(comboBox, CB_SETCURSEL, 0, 0);
        SelectText(pthis, 0, false, textBox);
    }
    return TRUE;
}

// PSP callback signature requires HWND even though only the release notification is used.
UINT CALLBACK PropPageCallbackProc(HWND, UINT uMsg, LPPROPSHEETPAGE ppsp)
{
    if (ppsp == nullptr)
        return 0;

    if (uMsg == PSPCB_RELEASE)
    {
        CAlternateStreamContext* pthis = reinterpret_cast<CAlternateStreamContext*>(ppsp->lParam);
        ppsp->lParam = 0;
        if (pthis != nullptr)
        {
            // The dialog releases its own font in WM_DESTROY.
            pthis->Release();
        }
    }

    return 1;
}

typedef INT_PTR DLGRETURN;

DLGRETURN CALLBACK PropPageDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
        return OnInitDialog(hWnd, lParam);

    if (uMsg == WM_DESTROY)
    {
        HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);
        if (textBox != nullptr)
        {
            HFONT font = reinterpret_cast<HFONT>(
                GetWindowLongPtr(textBox, GWLP_USERDATA));
            if (font != nullptr)
            {
                // Stop the control from referencing the font before deleting it.
                SendMessage(textBox, WM_SETFONT, 0, FALSE);
                SetWindowLongPtr(textBox, GWLP_USERDATA, 0);
                DeleteObject(font);
            }
        }
        SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        return FALSE;
    }

    if (uMsg != WM_COMMAND)
        return FALSE;

    const UINT controlId = LOWORD(wParam);
    const UINT notification = HIWORD(wParam);
    if ((controlId != IDC_COMBO1 || notification != CBN_SELCHANGE) &&
        (controlId != IDC_CHECK1 || notification != BN_CLICKED))
        return FALSE;

    auto* context = reinterpret_cast<CAlternateStreamContext*>(
        GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (context == nullptr)
        return FALSE;

    HWND comboBox = GetDlgItem(hWnd, IDC_COMBO1);
    HWND checkBox = GetDlgItem(hWnd, IDC_CHECK1);
    HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);
    const LRESULT selected = SendMessage(comboBox, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR)
        return TRUE;

    const bool hex = SendMessage(checkBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SelectText(context, static_cast<size_t>(selected), hex, textBox);
    return TRUE;
}

STDMETHODIMP CAlternateStreamContext::AddPages(
    LPFNSVADDPROPSHEETPAGE pfnAddPage,
    LPARAM lParam)
{
    if (pfnAddPage == nullptr)
        return E_POINTER;

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

// IShellPropSheetExt requires this method; it is unsupported, so all arguments are unnamed.
STDMETHODIMP CAlternateStreamContext::ReplacePage(
    EXPPS,
    LPFNSVADDPROPSHEETPAGE,
    LPARAM)
{
    return E_NOTIMPL;
}
