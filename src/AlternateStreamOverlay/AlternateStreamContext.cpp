#include "stdafx.h"
#include "AlternateStreamContext.h"
#include <algorithm>
#include <strsafe.h>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <new>
#include "Common.h"

// IShellExtInit fixes this signature. Keep unused arguments unnamed to avoid C4100.
STDMETHODIMP CAlternateStreamContext::Initialize(LPCITEMIDLIST, IDataObject* pDataObj, HKEY)
{
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (pDataObj == nullptr)
        return E_INVALIDARG;

    STGMEDIUM stg = {};
    if (FAILED(pDataObj->GetData(&fmt, &stg)))
        return E_INVALIDARG;

    // IDataObject::GetData must return a usable CF_HDROP HGLOBAL.
    // Always release a successfully returned medium, including malformed ones.
    if (stg.tymed != TYMED_HGLOBAL || stg.hGlobal == nullptr)
    {
        // A malformed data object may return an interface/file medium with
        // a null payload. Do not let ReleaseStgMedium dereference it; retain
        // pUnkForRelease so the provider's release hook is still invoked.
        // tymed selects one storage representation. Exact matching is
        // intentional; testing individual bits would accept malformed combinations.
        bool missingPayload = false;
        switch (stg.tymed)
        {
        case TYMED_ISTREAM:
            missingPayload = stg.pstm == nullptr;
            break;
        case TYMED_ISTORAGE:
            missingPayload = stg.pstg == nullptr;
            break;
        case TYMED_FILE:
            missingPayload = stg.lpszFileName == nullptr;
            break;
        case TYMED_MFPICT:
            missingPayload = stg.hMetaFilePict == nullptr;
            break;
        default:
            break;
        }
        if (missingPayload)
            stg.tymed = TYMED_NULL;

        ReleaseStgMedium(&stg);
        return E_INVALIDARG;
    }

    auto hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));

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

    MENUITEMINFOW item = {};
    item.cbSize = sizeof(item);
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
constexpr UINT WM_PREVIEW_READY = WM_APP + 42;

struct PreviewGate
{
    std::mutex mutex;
    bool alive = true;
    HWND window = nullptr;
};

struct DialogState
{
    CAlternateStreamContext* context;
    std::shared_ptr<PreviewGate> gate;
    size_t generation = 0;
};

struct PreviewRequest
{
    std::shared_ptr<PreviewGate> gate;
    std::wstring path;
    size_t generation;
    bool asHex;
};

struct PreviewResult
{
    size_t generation;
    std::wstring text;
};

std::wstring ReadPreview(const std::wstring& path, bool asHex)
{
    HandleW hFile(::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (hFile == INVALID_HANDLE_VALUE)
        return L"Unable to open alternate stream.";

    constexpr DWORD MaxPreviewBytes = 64 * 1024 - 1;
    std::vector<BYTE> buffer(MaxPreviewBytes + 1);
    DWORD bytesRead = 0;
    while (bytesRead < buffer.size())
    {
        DWORD count = 0;
        const DWORD remaining = static_cast<DWORD>(buffer.size()) - bytesRead;
        if (!ReadFile(hFile, buffer.data() + bytesRead, remaining, &count, nullptr))
            return L"Unable to read alternate stream.";
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
            return text;
        }
    }

    std::wstring formatted = FormatHexPreview(buffer.data(), bytesRead);
    if (truncated)
        formatted += L"\r\n[Preview truncated after 65535 bytes]";
    return formatted;
}

DWORD WINAPI PreviewWorker(LPVOID parameter)
{
    std::unique_ptr<PreviewRequest> request(static_cast<PreviewRequest*>(parameter));
    std::unique_ptr<PreviewResult> result(new (std::nothrow) PreviewResult);
    if (!result)
        return 0;
    result->generation = request->generation;
    try
    {
        result->text = ReadPreview(request->path, request->asHex);
    }
    catch (...)
    {
        result->text = L"Unable to read alternate stream.";
    }

    // The Windows thread-pool callback must not allow exceptions to escape.
    // Serialize posting with destruction. WM_DESTROY drains posted completions.
    std::lock_guard<std::mutex> lock(request->gate->mutex);
    if (request->gate->alive &&
        PostMessageW(request->gate->window, WM_PREVIEW_READY, 0,
                     reinterpret_cast<LPARAM>(result.get())))
        result.release();
    return 0;
}

void SelectText(DialogState* state, size_t idx, bool asHex, HWND textBox)
{
    if (textBox == nullptr || state == nullptr)
        return;
    const auto& streams = state->context->get_streams();
    ++state->generation; // Invalidate all earlier requests, including invalid selections.
    if (idx >= streams.size())
    {
        SetWindowTextW(textBox, L"");
        return;
    }

    SetWindowTextW(textBox, L"Loading alternate stream...");
    std::unique_ptr<PreviewRequest> request(new (std::nothrow) PreviewRequest);
    if (!request)
    {
        SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
        return;
    }
    request->gate = state->gate;
    request->path = state->context->get_path() + streams[idx];
    request->generation = state->generation;
    request->asHex = asHex;
    if (!QueueUserWorkItem(PreviewWorker, request.get(), WT_EXECUTEDEFAULT))
    {
        SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
        return;
    }
    request.release(); // Worker owns the request from this point forward.
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

    std::unique_ptr<DialogState> state(new (std::nothrow) DialogState);
    if (!state)
        return FALSE;
    state->context = pthis;
    state->gate = std::make_shared<PreviewGate>();
    state->gate->window = hWnd;
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
    state.release();

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
        SelectText(state.get(), 0, false, textBox);
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

    if (uMsg == WM_PREVIEW_READY)
    {
        std::unique_ptr<PreviewResult> result(reinterpret_cast<PreviewResult*>(lParam));
        auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        if (state != nullptr && result && result->generation == state->generation)
            SetWindowTextW(GetDlgItem(hWnd, IDC_EDIT1), result->text.c_str());
        return TRUE;
    }

    if (uMsg == WM_DESTROY)
    {
        std::unique_ptr<DialogState> state(reinterpret_cast<DialogState*>(
            GetWindowLongPtr(hWnd, GWLP_USERDATA)));
        if (state != nullptr)
        {
            std::lock_guard<std::mutex> lock(state->gate->mutex);
            state->gate->alive = false;
            MSG pending = {};
            while (PeekMessageW(&pending, hWnd, WM_PREVIEW_READY,
                                WM_PREVIEW_READY, PM_REMOVE))
            {
                // Each removed message owns one completion result.
                std::unique_ptr<PreviewResult> discarded(
                    reinterpret_cast<PreviewResult*>(pending.lParam));
            }
        }
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

    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (state == nullptr)
        return FALSE;

    HWND comboBox = GetDlgItem(hWnd, IDC_COMBO1);
    HWND checkBox = GetDlgItem(hWnd, IDC_CHECK1);
    HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);
    const LRESULT selected = SendMessage(comboBox, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR)
        return TRUE;

    const bool hex = SendMessage(checkBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SelectText(state, static_cast<size_t>(selected), hex, textBox);
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
