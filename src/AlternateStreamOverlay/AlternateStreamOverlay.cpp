#include "stdafx.h"
#include "resource.h"
#include "AlternateStreamOverlay_i.h"
#include "dllmain.h"
#include "dlldatax.h"
#include "RegistrationPolicy.h"
#include <array>
#include <ktmw32.h>
#pragma comment(lib, "KtmW32.lib")

namespace
{
    constexpr LPCWSTR OverlayKey =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
        L"ShellIconOverlayIdentifiers\\AlternateStreamOverlayIcon";
    constexpr WCHAR OverlayClsid[] = L"{FBAB8C58-59C9-4522-B7E8-1A6AA23B318C}";

    // Scoped Win32 ownership keeps aborted registry transactions uncommitted.
    class TransactionHandle
    {
    public:
        TransactionHandle()
            : handle_(CreateTransaction(nullptr, nullptr, 0, 0, 0, 0, nullptr)) {}
        ~TransactionHandle()
        {
            if (handle_ != INVALID_HANDLE_VALUE)
                CloseHandle(handle_);
        }
        TransactionHandle(const TransactionHandle&) = delete;
        TransactionHandle& operator=(const TransactionHandle&) = delete;
        HANDLE get() const { return handle_; }

    private:
        HANDLE handle_;
    };

    class RegistryKey
    {
    public:
        ~RegistryKey() { Close(); }
        void Close()
        {
            if (handle_ != nullptr)
            {
                RegCloseKey(handle_);
                handle_ = nullptr;
            }
        }
        RegistryKey(const RegistryKey&) = delete;
        RegistryKey& operator=(const RegistryKey&) = delete;
        RegistryKey() = default;
        HKEY* put() { return &handle_; }
        HKEY get() const { return handle_; }

    private:
        HKEY handle_ = nullptr;
    };

    LONG ReadRegisteredClsid(HKEY key, std::array<WCHAR, 64>& clsid)
    {
        auto bytes = static_cast<DWORD>(clsid.size() * sizeof(WCHAR));
        return RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ,
            nullptr, clsid.data(), &bytes);
    }

    HRESULT RemoveOverlayRegistration(HKEY root)
    {
        // Keep the ownership read and deletion in one registry transaction.
        TransactionHandle transaction;
        if (transaction.get() == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());

        RegistryKey key;
        LONG status = RegOpenKeyTransactedW(root, OverlayKey, 0,
            KEY_QUERY_VALUE, key.put(), transaction.get(), nullptr);
        if (status == ERROR_FILE_NOT_FOUND)
            return S_OK;
        if (status != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(status);

        std::array<WCHAR, 64> registeredClsid = {};
        status = ReadRegisteredClsid(key.get(), registeredClsid);

        // An absent, different, or unrecognizable value is not ours.
        if (status == ERROR_FILE_NOT_FOUND ||
            status == ERROR_MORE_DATA ||
            status == ERROR_UNSUPPORTED_TYPE ||
            (status == ERROR_SUCCESS &&
             _wcsicmp(registeredClsid.data(), OverlayClsid) != 0))
            return S_OK;

        if (status != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(status);

        key.Close(); // Preserve the original close-before-delete sequence.
        status = RegDeleteKeyTransactedW(root, OverlayKey, 0, 0,
            transaction.get(), nullptr);
        // Another remover may already have deleted the key.
        // Do not mask transactional conflicts or commit failures.
        if (status == ERROR_FILE_NOT_FOUND)
            return S_OK;
        if (status == ERROR_SUCCESS && !CommitTransaction(transaction.get()))
            status = GetLastError();
        return HRESULT_FROM_WIN32(status);
    }

    HRESULT InstallOverlayRegistration(HKEY root)
    {
        // Ownership validation and the write share one transaction.
        TransactionHandle transaction;
        if (transaction.get() == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(GetLastError());

        RegistryKey key;
        DWORD disposition = 0;
        LONG status = RegCreateKeyTransactedW(root, OverlayKey, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE,
            nullptr, key.put(), &disposition, transaction.get(), nullptr);
        if (status != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(status);

        std::array<WCHAR, 64> registeredClsid = {};
        const LONG readStatus = ReadRegisteredClsid(key.get(), registeredClsid);
        if (!CanReplaceOverlayRegistration(readStatus, registeredClsid.data(),
                                            OverlayClsid, disposition))
        {
            // An uncommitted transaction rolls back newly created keys.
            return HRESULT_FROM_WIN32(readStatus == ERROR_SUCCESS ||
                readStatus == ERROR_FILE_NOT_FOUND
                ? ERROR_ALREADY_EXISTS : readStatus);
        }

        status = RegSetValueExW(key.get(), nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(OverlayClsid), sizeof(OverlayClsid));
        key.Close(); // Preserve the original close-before-commit sequence.
        if (status == ERROR_SUCCESS && !CommitTransaction(transaction.get()))
            status = GetLastError();
        return HRESULT_FROM_WIN32(status);
    }

    HRESULT UpdateOverlayRegistration(bool install)
    {
        bool perUser = false;
        const HRESULT scopeResult = AtlGetPerUserRegistration(&perUser);
        if (FAILED(scopeResult))
            return scopeResult;

        const HKEY root = perUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        return install ? InstallOverlayRegistration(root) :
                         RemoveOverlayRegistration(root);
    }
}

// Boilerplate guff

// Used to determine whether the DLL can be unloaded by OLE
_Check_return_
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
    {
        hr = UpdateOverlayRegistration(true);
        if (FAILED(hr))
        {
            // Plain regsvr32 invokes DllRegisterServer directly. Roll back
            // the COM registration without removing a foreign overlay key.
            _AtlModule.DllUnregisterServer();
#ifdef _MERGE_PROXYSTUB
            PrxDllUnregisterServer();
#endif
        }
    }
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
    auto hr = E_FAIL;
    static const wchar_t szUserSwitch[] = _T("user");

    if (pszCmdLine != nullptr)
    {
        if (_wcsnicmp(pszCmdLine, szUserSwitch, _countof(szUserSwitch)) == 0)
        {
            AtlSetPerUserRegistration(true);
        }
    }

    // DllRegisterServer owns its rollback on overlay registration failure.
    // Calling DllUnregisterServer here would run another overlay removal,
    // potentially deleting an existing registration after a failed install.
    hr = bInstall ? DllRegisterServer() : DllUnregisterServer();

    return hr;
}
