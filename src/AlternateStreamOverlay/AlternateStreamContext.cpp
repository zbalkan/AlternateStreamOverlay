#include "stdafx.h"
#include "AlternateStreamContext.h"
#include <algorithm>
#include <strsafe.h>
#include <vector>
#include <string>
#include <memory>
#include <new>
#include <exception>
#include "Common.h"
#include "SrwLock.h"
#include "stream_info_detail.h"
#include "LocalPathPolicy.h"

namespace
{
// Scope ownership after successful GetData, including exceptions.
class ScopedDropMedium
{
public:
    explicit ScopedDropMedium(STGMEDIUM& medium) : medium_(medium) {}
    ~ScopedDropMedium() { Release(); }

    void Release()
    {
        if (released_)
            return;
        if (locked_)
            GlobalUnlock(medium_.hGlobal);
        ReleaseStgMedium(&medium_);
        released_ = true;
    }

    ScopedDropMedium(const ScopedDropMedium&) = delete;
    ScopedDropMedium& operator=(const ScopedDropMedium&) = delete;

    void SetLocked() { locked_ = true; }

private:
    STGMEDIUM& medium_;
    bool locked_ = false;
    bool released_ = false;
};
}

// IShellExtInit fixes this signature. Keep unused arguments unnamed to avoid C4100.
STDMETHODIMP CAlternateStreamContext::Initialize(LPCITEMIDLIST, IDataObject* pDataObj, HKEY)
{
    try
    {
        // A failed reinitialization must not retain the previous shell selection.
        {
            ExclusiveSrwLock lock(m_selectionLock);
            m_path.clear();
            m_streams.clear();
            m_enumerationIncomplete = false;
        }

        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (pDataObj == nullptr)
            return E_INVALIDARG;

        STGMEDIUM stg = {};
        if (FAILED(pDataObj->GetData(&fmt, &stg)))
            return E_INVALIDARG;
        ScopedDropMedium mediumGuard(stg);

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

            return E_INVALIDARG;
        }

        auto hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal));

        if (hDrop == nullptr)
            return E_INVALIDARG;
        mediumGuard.SetLocked();

        // We only handle a single selected file.
        if (DragQueryFileW(hDrop, 0xffffffff, nullptr, 0) != 1)
            return E_INVALIDARG;

        const UINT pathLength = DragQueryFileW(hDrop, 0, nullptr, 0);
        if (pathLength == 0)
            return E_INVALIDARG;

        std::wstring filePath(pathLength + 1, L'\0');
        if (DragQueryFileW(hDrop, 0, &filePath[0], static_cast<UINT>(filePath.size())) != pathLength)
            return E_INVALIDARG;
        filePath.resize(pathLength);

        // Do not hold the shell data object's storage during filesystem I/O.
        mediumGuard.Release();
        if (!CanEnumerateExplorerPath(filePath))
            return E_FAIL;

        AlternateStreamReader reader;
        auto enumeration = reader.EnumerateStreams(filePath.c_str());
        // Even failed enumeration must not appear as a clean file. A notice
        // is preferable to silently omitting the Explorer inspection UI.
        // An incomplete response cannot be interpreted as an empty ADS list.
        // The Win32 call did not supply trustworthy records to preview, so
        // expose a notice instead of inventing stream names.
        const bool incomplete = enumeration.NeedsNotice();
        auto& streams = enumeration.streams;
        streams.erase(std::remove_if(streams.begin(), streams.end(),
            [](const std::wstring& name)
            {
                return name == L"::$DATA" ||
                    !stream_info_detail::IsNamedDataStreamName(name.data(), name.size());
            }), streams.end());
        if (streams.empty() && !incomplete)
            return E_FAIL;

        // Commit selection only after validation; avoid allocations on commit.
        {
            ExclusiveSrwLock lock(m_selectionLock);
            m_path.swap(filePath);
            m_streams.swap(streams);
            m_enumerationIncomplete = incomplete;
        }
        return S_OK;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP CAlternateStreamContext::QueryContextMenu(
    HMENU hmenu,
    UINT indexMenu,
    UINT idCmdFirst,
    UINT idCmdLast,
    UINT uFlags)
{
    if (uFlags & CMF_DEFAULTONLY)
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

    bool incomplete = false;
    {
        ExclusiveSrwLock lock(m_selectionLock);
        if (m_streams.empty() && !m_enumerationIncomplete)
            return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);
        incomplete = m_enumerationIncomplete;
    }

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
    wchar_t menuText[] = L"&Alternate Streams...";
    wchar_t incompleteText[] = L"&Alternate Streams (incomplete)...";
    item.dwTypeData = incomplete ? incompleteText : menuText;
    if (!InsertMenuItemW(hmenu, indexMenu, TRUE, &item))
        return HRESULT_FROM_WIN32(GetLastError());

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 1);
}

STDMETHODIMP CAlternateStreamContext::InvokeCommand(
    CMINVOKECOMMANDINFO* pCmdInfo)
{
    if (pCmdInfo == nullptr)
        return E_INVALIDARG;
    std::wstring selectedPath;
    try
    {
        ExclusiveSrwLock lock(m_selectionLock);
        if (m_path.empty() || (m_streams.empty() && !m_enumerationIncomplete))
            return E_FAIL;
        selectedPath = m_path;
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }

    bool invoked = false;
    // Numeric command identifiers use MAKEINTRESOURCE-style pointer values.
    // IS_INTRESOURCE examines the full pointer width (including on x64).
    if (IS_INTRESOURCE(pCmdInfo->lpVerb))
    {
        invoked = LOWORD(pCmdInfo->lpVerb) == 0;
    }
    else if (pCmdInfo->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) &&
             (pCmdInfo->fMask & CMIC_MASK_UNICODE))
    {
        const auto* info = reinterpret_cast<const CMINVOKECOMMANDINFOEX*>(pCmdInfo);
        if (info->lpVerbW != nullptr && !IS_INTRESOURCE(info->lpVerbW))
            invoked = _wcsicmp(info->lpVerbW, L"alternatestreams") == 0;
    }
    if (!invoked && !IS_INTRESOURCE(pCmdInfo->lpVerb))
        invoked = _stricmp(pCmdInfo->lpVerb, "alternatestreams") == 0;
    if (!invoked)
        return E_INVALIDARG;

    // Request our property page by its PSP_USETITLE name. Shell versions
    // may fall back to the default tab if they cannot activate the page.
    if (!SHObjectProperties(pCmdInfo->hwnd, SHOP_FILEPATH,
                            selectedPath.c_str(), L"AlternateStreams"))
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

    // Match exact documented requests. Masking to the low three bits aliases
    // GCS_VERBICONW (0x14) to GCS_VERBW (0x04), returning the wrong data.
    const UINT kind = uFlags;
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

void CAlternateStreamContext::SnapshotSelection(
    std::wstring& path, std::vector<std::wstring>& streams, bool& incomplete) const
{
    // Copy all fields under the same lock, then publish them to the dialog
    // only after both potentially throwing allocations have completed.
    ExclusiveSrwLock lock(m_selectionLock);
    std::wstring pathCopy = m_path;
    std::vector<std::wstring> streamCopy = m_streams;
    path.swap(pathCopy);
    streams.swap(streamCopy);
    incomplete = m_enumerationIncomplete;
}

namespace
{
// Registered to avoid collisions with other dialog-specific application messages.
// The message is a notification only; no message parameter owns memory.
UINT PreviewMessageId()
{
    static const UINT id = RegisterWindowMessageW(
        L"AlternateStreamOverlay.PreviewReady.{9AC93D79-3D54-461E-A4E7-D855D24C24E3}");
    return id;
}

struct PreviewResult
{
    size_t generation = 0;
    std::wstring text;
};

struct PreviewGate
{
    SRWLOCK lock = SRWLOCK_INIT;
    bool alive = true;
    HWND window = nullptr;
    bool workerActive = false;
    size_t generation = 0;
    std::wstring path;
    bool asHex = false;
    std::unique_ptr<PreviewResult> pending;
    size_t failedGeneration = 0;
    bool pendingFailure = false;
};

struct DialogState
{
    // Freeze the selection while this property page is open. Initialize()
    // can replace the COM object's path and stream inventory independently.
    std::wstring path;
    std::vector<std::wstring> streams;
    bool enumerationIncomplete = false;
    std::shared_ptr<PreviewGate> gate;
    size_t generation = 0;
};

struct PreviewRequest
{
    std::shared_ptr<PreviewGate> gate;
    HMODULE moduleHandle = nullptr;
};

std::wstring ReadPreview(const std::wstring& path, bool asHex)
{
    HandleW hFile(::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (hFile.get() == INVALID_HANDLE_VALUE)
        return L"Unable to open alternate stream.";

    constexpr DWORD MaxPreviewBytes = 64 * 1024 - 1;
    std::vector<BYTE> buffer(MaxPreviewBytes + 1);
    DWORD bytesRead = 0;
    while (bytesRead < buffer.size())
    {
        DWORD count = 0;
        const DWORD remaining = static_cast<DWORD>(buffer.size()) - bytesRead;
        if (!ReadFile(hFile.get(), buffer.data() + bytesRead, remaining, &count, nullptr))
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

// Called with the gate lock held; publish a failure without allocating.
void PostPreviewFailure(PreviewGate& gate, size_t generation)
{
    if (!gate.alive || gate.generation != generation)
        return;
    gate.pending.reset();
    gate.failedGeneration = generation;
    gate.pendingFailure = true;
    const UINT messageId = PreviewMessageId();
    if (messageId == 0 || !PostMessageW(gate.window, messageId,
                                        static_cast<WPARAM>(generation), 0))
        gate.pendingFailure = false;
}

VOID CALLBACK PreviewWorker(PTP_CALLBACK_INSTANCE instance, PVOID parameter, PTP_WORK work)
{
    std::unique_ptr<PreviewRequest> request(static_cast<PreviewRequest*>(parameter));
    // Windows releases the DLL only after this callback has completely returned.
    FreeLibraryWhenCallbackReturns(instance, request->moduleHandle);
    CloseThreadpoolWork(work);

    for (;;)
    {
        std::wstring path;
        size_t generation = 0;
        bool asHex = false;
        {
            ExclusiveSrwLock lock(request->gate->lock);
            if (!request->gate->alive)
            {
                request->gate->workerActive = false;
                return;
            }
            generation = request->gate->generation;
            asHex = request->gate->asHex;
            try
            {
                path = request->gate->path;
            }
            catch (const std::bad_alloc&)
            {
                // Report a terminal error for the currently displayed request.
                PostPreviewFailure(*request->gate, generation);
                request->gate->workerActive = false;
                return;
            }
        }

        std::unique_ptr<PreviewResult> result(new (std::nothrow) PreviewResult);
        if (!result)
        {
            ExclusiveSrwLock lock(request->gate->lock);
            // A newer selection must still be serviced after this failed read.
            if (request->gate->alive && generation != request->gate->generation)
                continue;
            PostPreviewFailure(*request->gate, generation);
            request->gate->workerActive = false;
            return;
        }
        result->generation = generation;
        try
        {
            result->text = ReadPreview(path, asHex);
        }
        catch (...)
        {
            // An error-string allocation can also fail. Use an allocation-free
            // completion message instead of attempting another string write.
            ExclusiveSrwLock lock(request->gate->lock);
            // Do not abandon a newer selection because an older read failed.
            if (request->gate->alive && generation != request->gate->generation)
                continue;
            PostPreviewFailure(*request->gate, generation);
            request->gate->workerActive = false;
            return;
        }

        ExclusiveSrwLock lock(request->gate->lock);
        if (!request->gate->alive)
        {
            request->gate->workerActive = false;
            return;
        }
        if (generation != request->gate->generation)
            continue; // Skip superseded results; read only the latest selection.

        // The worker hands the result to shared state, not to a message's
        // untrusted lParam. A failed post relinquishes the result immediately.
        request->gate->pending.swap(result);
        request->gate->pendingFailure = false;
        const UINT messageId = PreviewMessageId();
        if (messageId == 0 ||
            !PostMessageW(request->gate->window, messageId,
                          static_cast<WPARAM>(generation), 0))
            request->gate->pending.reset();
        // A failed post usually means the dialog is gone; do not keep a
        // worker marked active when delivery was unsuccessful.
        request->gate->workerActive = false;
        return;
    }
}

void SelectTextImpl(DialogState* state, size_t idx, bool asHex, HWND textBox)
{
    if (textBox == nullptr || state == nullptr)
        return;
    const auto& streams = state->streams;
    if (idx >= streams.size())
    {
        ++state->generation;
        SetWindowTextW(textBox, L"");
        return;
    }

    // Construct before changing the generation: allocation failure must not
    // make an existing worker's previous path appear to be a new selection.
    std::wstring path = state->path + streams[idx];
    if (PreviewMessageId() == 0)
    {
        // No completion notification can be delivered without registration.
        SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
        return;
    }
    SetWindowTextW(textBox, L"Loading alternate stream...");
    ExclusiveSrwLock lock(state->gate->lock);
    state->gate->path.swap(path);
    ++state->generation;
    state->gate->generation = state->generation;
    state->gate->asHex = asHex;
    if (state->gate->workerActive)
        return; // Coalesce changes into the most recent pending selection.

    std::unique_ptr<PreviewRequest> request(new (std::nothrow) PreviewRequest);
    if (!request)
    {
        SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
        return;
    }
    request->gate = state->gate;
    HMODULE moduleHandle = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&PreviewWorker), &moduleHandle))
    {
        SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
        return;
    }
    request->moduleHandle = moduleHandle;
    PTP_WORK work = CreateThreadpoolWork(PreviewWorker, request.get(), nullptr);
    if (work == nullptr)
    {
        FreeLibrary(moduleHandle);
        SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
        return;
    }
    state->gate->workerActive = true;
    SubmitThreadpoolWork(work);
    (void)request.release(); // Ownership passes to PreviewWorker.
}

void SelectText(DialogState* state, size_t idx, bool asHex, HWND textBox)
{
    try
    {
        SelectTextImpl(state, idx, asHex, textBox);
    }
    catch (const std::exception&)
    {
        // Never propagate allocation or string construction failures through
        // the Win32 dialog procedure. The scoped SRW guard releases on unwind.
        if (textBox != nullptr)
            SetWindowTextW(textBox, L"Unable to start alternate stream preview.");
    }
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
    try
    {
        // Snapshot before publishing dialog state. A failed allocation leaves
        // no half-initialized state attached to the window.
        pthis->SnapshotSelection(state->path, state->streams,
                                 state->enumerationIncomplete);
        state->gate = std::make_shared<PreviewGate>();
    }
    catch (const std::bad_alloc&)
    {
        return FALSE;
    }
    state->gate->window = hWnd;
    DialogState* dialogState = state.get();
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialogState));
    (void)state.release(); // Ownership passes to the dialog window.

    HWND comboBox = GetDlgItem(hWnd, IDC_COMBO1);
    HWND checkBox = GetDlgItem(hWnd, IDC_CHECK1);
    HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);

    const std::vector<std::wstring>& streams = dialogState->streams;
    for (const auto& stream : streams)
    {
        SendMessage(comboBox, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(stream.c_str()));
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
    if (dialogState->enumerationIncomplete && textBox != nullptr)
    {
        SetWindowTextW(textBox,
            L"Stream enumeration is incomplete or failed.\r\n"
            L"Some stream names may be unavailable for preview.\r\n"
            L"Do not interpret this inventory as a clean result.");
    }
    else if (!streams.empty())
    {
        SendMessage(comboBox, CB_SETCURSEL, 0, 0);
        SelectText(dialogState, 0, false, textBox);
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

namespace
{
void OnPreviewReady(HWND hWnd, WPARAM wParam)
{
    const auto* state = reinterpret_cast<const DialogState*>(
        GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (state == nullptr)
        return;

    const auto notificationGeneration = static_cast<size_t>(wParam);
    std::unique_ptr<PreviewResult> result;
    bool failed = false;
    {
        ExclusiveSrwLock lock(state->gate->lock);
        if (!state->gate->alive)
            return;

        if (notificationGeneration != state->generation ||
            state->gate->generation != state->generation)
        {
            // A stale notification can only discard its own completion.
            // Never clear a newer generation's pending result or failure.
            if (state->gate->pending &&
                state->gate->pending->generation == notificationGeneration)
                state->gate->pending.reset();
            if (state->gate->pendingFailure &&
                state->gate->failedGeneration == notificationGeneration)
                state->gate->pendingFailure = false;
            return;
        }

        if (state->gate->pending &&
            state->gate->pending->generation == notificationGeneration)
            result.swap(state->gate->pending);
        else if (state->gate->pendingFailure &&
                 state->gate->failedGeneration == notificationGeneration)
        {
            failed = true;
            state->gate->pendingFailure = false;
        }
    }

    // Win32 control messages must not be sent while holding the SRW lock.
    if (result)
        SetWindowTextW(GetDlgItem(hWnd, IDC_EDIT1), result->text.c_str());
    else if (failed)
        SetWindowTextW(GetDlgItem(hWnd, IDC_EDIT1),
                       L"Unable to read alternate stream.");
}

void CloseGateAndDrain(HWND hWnd, DialogState& state)
{
    {
        ExclusiveSrwLock lock(state.gate->lock);
        state.gate->alive = false;
        state.gate->pending.reset();
        state.gate->pendingFailure = false;
    }

    // Queued notifications carry no ownership; removing them is optional for
    // memory safety, but avoids needless messages during dialog destruction.
    MSG pending = {};
    const UINT messageId = PreviewMessageId();
    while (messageId != 0 && PeekMessageW(&pending, hWnd, messageId,
                                         messageId, PM_REMOVE))
    {
        // PM_REMOVE discards each notification; no payload needs cleanup.
    }
}

void ReleaseEditFont(HWND hWnd)
{
    HWND textBox = GetDlgItem(hWnd, IDC_EDIT1);
    if (textBox == nullptr)
        return;

    auto font = reinterpret_cast<HFONT>(GetWindowLongPtr(textBox, GWLP_USERDATA));
    if (font == nullptr)
        return;

    // Stop the control from referencing the font before deleting it.
    SendMessage(textBox, WM_SETFONT, 0, FALSE);
    SetWindowLongPtr(textBox, GWLP_USERDATA, 0);
    DeleteObject(font);
}

void OnDestroy(HWND hWnd)
{
    // Detach before cleanup so reentrant messages cannot access the state
    // while its preview gate and dialog-owned resources are being released.
    std::unique_ptr<DialogState> state(reinterpret_cast<DialogState*>(
        GetWindowLongPtr(hWnd, GWLP_USERDATA)));
    SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
    if (state != nullptr)
        CloseGateAndDrain(hWnd, *state);

    ReleaseEditFont(hWnd);
}

bool IsSelectionInput(UINT controlId, UINT notification)
{
    return (controlId == IDC_COMBO1 && notification == CBN_SELCHANGE) ||
           (controlId == IDC_CHECK1 && notification == BN_CLICKED);
}

INT_PTR OnCommand(HWND hWnd, WPARAM wParam)
{
    if (!IsSelectionInput(LOWORD(wParam), HIWORD(wParam)))
        return FALSE;

    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
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
} // namespace

INT_PTR CALLBACK PropPageDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    const UINT previewMessage = PreviewMessageId();
    if (previewMessage != 0 && uMsg == previewMessage)
    {
        OnPreviewReady(hWnd, wParam);
        return TRUE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInitDialog(hWnd, lParam);
    case WM_DESTROY:
        OnDestroy(hWnd);
        return FALSE;
    case WM_COMMAND:
        return OnCommand(hWnd, wParam);
    default:
        return FALSE;
    }
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
