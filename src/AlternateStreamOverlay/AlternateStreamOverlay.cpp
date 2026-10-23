#include "stdafx.h"
#include "dllmain.h"

// Used to determine whether the DLL can be unloaded by COM.
_Check_return_
STDAPI DllCanUnloadNow(void)
{
    return _AtlModule.DllCanUnloadNow();
}

// Returns a class factory to create the requested object.
_Check_return_
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

// ATL scripts register both COM classes and the machine-wide overlay key.
// No type library is generated or registered.
STDAPI DllRegisterServer(void)
{
    return _AtlModule.DllRegisterServer(FALSE);
}

STDAPI DllUnregisterServer(void)
{
    return _AtlModule.DllUnregisterServer(FALSE);
}
