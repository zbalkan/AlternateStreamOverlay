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

 // Roll back only a fresh registration; existing COM classes may belong to
 // an earlier successful installation and must not be deleted on failure.
inline bool ShouldRollbackComRegistration(bool alreadyRegistered)
{
    return !alreadyRegistered;
}

// Access failures conservatively count as existing registration: never
// remove COM keys whose previous state we cannot establish.
template <size_t N>
bool HasExistingComClassRegistration(HKEY root, const LPCWSTR (&keys)[N])
{
    for (const auto* path : keys)
    {
        HKEY key = nullptr;
        const LONG status = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
        if (status == ERROR_SUCCESS)
        {
            RegCloseKey(key);
            return true;
        }
        if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
            return true;
    }
    return false;
}
