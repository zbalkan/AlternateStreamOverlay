#include "stdafx.h"
#include "resource.h"
#include "AlternateStreamOverlay_i.h"
#include "dllmain.h"
#include "dlldatax.h"

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

// ATL registry scripts own the COM classes and machine-wide overlay key.
STDAPI DllRegisterServer(void)
{
    auto hr = _AtlModule.DllRegisterServer();
#ifdef _MERGE_PROXYSTUB
    if (SUCCEEDED(hr))
        hr = PrxDllRegisterServer();
#endif
    return hr;
}

STDAPI DllUnregisterServer(void)
{
    auto hr = _AtlModule.DllUnregisterServer();
#ifdef _MERGE_PROXYSTUB
    if (SUCCEEDED(hr))
        hr = PrxDllUnregisterServer();
#endif
    return hr;
}
