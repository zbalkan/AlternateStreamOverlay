#pragma once
#include <Windows.h>
#include <cwchar>

// Registration must not replace an overlay key belonging to another CLSID.
// An absent default value on a newly created key is safe to populate.
inline bool CanReplaceOverlayRegistration(LONG status, LPCWSTR registeredClsid)
{
    if (status == ERROR_FILE_NOT_FOUND)
        return true;
    return status == ERROR_SUCCESS && registeredClsid != nullptr &&
        _wcsicmp(registeredClsid,
            L"{FBAB8C58-59C9-4522-B7E8-1A6AA23B318C}") == 0;
}
