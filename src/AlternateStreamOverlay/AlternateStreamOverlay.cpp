#include "stdafx.h"
#include "resource.h"
#include "AlternateStreamOverlay_i.h"
#include "dllmain.h"
#include "dlldatax.h"
#include <array>
#include <ktmw32.h>
#pragma comment(lib, "KtmW32.lib")

namespace
{
    constexpr LPCWSTR OverlayKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
        L"ShellIconOverlayIdentifiers\\AlternateStreamOverlayIcon";
    constexpr WCHAR OverlayClsid[] = L"{FBAB8C58-59C9-4522-B7E8-1A6AA23B318C}";

    HRESULT UpdateOverlayRegistration(bool install)
    {
        bool perUser = false;
        const HRESULT scopeResult = AtlGetPerUserRegistration(&perUser);
        if (FAILED(scopeResult))
            return scopeResult;

        const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        if (!install)
        {
            // Keep the ownership read and deletion in one registry transaction.
            // A conflicting non-transacted modification aborts the transaction
            // rather than allowing an unrelated handler to be deleted.
            const HANDLE transaction =
                CreateTransaction(nullptr, nullptr, 0, 0, 0, 0, nullptr);
            if (transaction == INVALID_HANDLE_VALUE)
                return HRESULT_FROM_WIN32(GetLastError());

            HKEY key = nullptr;
            LONG status = RegOpenKeyTransactedW(root, OverlayKey, 0,
                KEY_QUERY_VALUE, &key, transaction, nullptr);
            if (status == ERROR_FILE_NOT_FOUND)
            {
                CloseHandle(transaction);
                return S_OK;
            }
            if (status != ERROR_SUCCESS)
            {
                CloseHandle(transaction);
                return HRESULT_FROM_WIN32(status);
            }

            auto registeredClsid = std::array<WCHAR, 64>{};
            auto bytes = static_cast<DWORD>(
                registeredClsid.size() * sizeof(WCHAR));
            status = RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ,
                nullptr, registeredClsid.data(), &bytes);
            RegCloseKey(key);

            // An absent, different, or unrecognizable value is not ours.
            if (status == ERROR_FILE_NOT_FOUND ||
                status == ERROR_MORE_DATA ||
                status == ERROR_UNSUPPORTED_TYPE ||
                (status == ERROR_SUCCESS &&
                 _wcsicmp(registeredClsid.data(), OverlayClsid) != 0))
            {
                CloseHandle(transaction); // Uncommitted transaction rolls back.
                return S_OK;
            }
            if (status == ERROR_SUCCESS)
            {
                status = RegDeleteKeyTransactedW(root, OverlayKey, 0, 0,
                    transaction, nullptr);
                // Another remover may already have deleted the key.
                // Do not mask transactional conflicts or commit failures.
                if (status == ERROR_FILE_NOT_FOUND)
                    status = ERROR_SUCCESS;
                else if (status == ERROR_SUCCESS && !CommitTransaction(transaction))
                    status = GetLastError();
            }

            CloseHandle(transaction);
            return HRESULT_FROM_WIN32(status);
        }

        HKEY key = nullptr;
        const LONG status = RegCreateKeyExW(root, OverlayKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(status);

        const LONG valueStatus = RegSetValueExW(key, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(OverlayClsid), sizeof(OverlayClsid));
        RegCloseKey(key);
        return HRESULT_FROM_WIN32(valueStatus);
    }
}

// Boilerplate guff

// Used to determine whether the DLL can be unloaded by OLE
STDAPI DllCanUnloadNow(void)
{
#ifdef _MERGE_PROXYSTUB
    HRESULT hr = PrxDllCanUnloadNow();
    if (hr != S_OK)
        return hr;
#endif
    return _AtlModule.DllCanUnloadNow();
}

// Returns a class factory to create an object of the requested type
_Check_return_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
#ifdef _MERGE_PROXYSTUB
    if (PrxDllGetClassObject(rclsid, riid, ppv) == S_OK)
        return S_OK;
#endif
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

// DllRegisterServer - Adds entries to the system registry
STDAPI DllRegisterServer(void)
{
    // registers object, typelib and all interfaces in typelib
    HRESULT hr = _AtlModule.DllRegisterServer();
#ifdef _MERGE_PROXYSTUB
    if (FAILED(hr))
        return hr;
    hr = PrxDllRegisterServer();
#endif
    if (SUCCEEDED(hr))
        hr = UpdateOverlayRegistration(true);
    return hr;
}

// DllUnregisterServer - Removes entries from the system registry
STDAPI DllUnregisterServer(void)
{
    const HRESULT overlayResult = UpdateOverlayRegistration(false);
    HRESULT hr = _AtlModule.DllUnregisterServer();
#ifdef _MERGE_PROXYSTUB
    if (SUCCEEDED(hr))
        hr = PrxDllUnregisterServer();
#endif
    return FAILED(hr) ? hr : overlayResult;
}

// DllInstall - Adds/Removes entries to the system registry per user
//              per machine.
STDAPI DllInstall(BOOL bInstall, _In_opt_ LPCWSTR pszCmdLine)
{
    HRESULT hr = E_FAIL;
    static const wchar_t szUserSwitch[] = _T("user");

    if (pszCmdLine != nullptr)
    {
        if (_wcsnicmp(pszCmdLine, szUserSwitch, _countof(szUserSwitch)) == 0)
        {
            AtlSetPerUserRegistration(true);
        }
    }

    if (bInstall)
    {
        hr = DllRegisterServer();
        if (FAILED(hr))
        {
            DllUnregisterServer();
        }
    }
    else
    {
        hr = DllUnregisterServer();
    }

    return hr;
}
