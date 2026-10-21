// dllmain.cpp : Implementation of DllMain.

#include "stdafx.h"
#include "resource.h"
#include "AlternateStreamOverlay_i.h"
#include "dllmain.h"
#include "dlldatax.h"

CAlternateStreamOverlayModule _AtlModule;

// DLL Entry Point
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
#ifdef _MERGE_PROXYSTUB
    if (!PrxDllMain(hInstance, dwReason, lpReserved))
        return FALSE;
#endif
    static_cast<void>(hInstance);
    return _AtlModule.DllMain(dwReason, lpReserved);
}
