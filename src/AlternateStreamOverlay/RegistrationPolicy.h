#pragma once
#include <Windows.h>
#include <cwchar>

// Only a newly created key may have no default value. An existing key
// must already contain our CLSID before registration can modify it.
inline bool CanReplaceOverlayRegistration(LONG status, LPCWSTR registeredClsid,
                                          LPCWSTR expectedClsid, DWORD disposition)
{
    if (status == ERROR_FILE_NOT_FOUND)
        return disposition == REG_CREATED_NEW_KEY;
    return status == ERROR_SUCCESS && registeredClsid != nullptr &&
        expectedClsid != nullptr &&
        _wcsicmp(registeredClsid, expectedClsid) == 0;
}
