# Upgrading and legacy registration cleanup

Current builds require an x64 installation of Windows 10 version 2004 (build 19041) or later, or Windows 11 x64. They cannot load into a 32-bit Explorer process and no ARM64 build is supplied.

For normal installation and uninstallation of the current build, use the [user README](../README.md). This page addresses migrations from older installations of the original project or previous fork revisions.

## Recommended upgrade

1. Locate the **currently installed** `AlternateStreamOverlay.dll`.
2. In an elevated terminal, unregister that specific DLL before moving or deleting it:

   ```powershell
   regsvr32.exe /u "C:\path\to\old\AlternateStreamOverlay.dll"
   ```

3. Move the new x64 DLL into a permanent local folder and register it:

   ```powershell
   regsvr32.exe "C:\path\to\new\AlternateStreamOverlay.dll"
   ```

4. Reopen Explorer or sign out and back in if the extension has not refreshed. Check the property page on a file with a named alternate stream.

## Why older installations need care

Current builds use machine-wide registration and expose a property-sheet handler and an icon-overlay handler. The former context-menu integration has been removed.

Some earlier fork builds registered a type library (`{C0034A83-7FA3-43A7-82DD-3ADF66F0086C}`), custom interfaces (`{CFC72A5F-E58B-43BD-8968-BA89AFB2240D}` and `{96296677-6A18-4F22-AC2E-84A650C4081D}`), and an AppID (`{869100FF-B8AB-406F-A8E2-C8B561E7CC81}`). Current builds no longer manage those historical registrations; simply overwriting the old DLL may leave obsolete entries. Exact registrations depend on the version originally installed.

Earlier per-user installations may also leave registrations beneath `HKCU\Software\Classes` and `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers`; an elevated machine-wide unregister does not necessarily remove them.

## Diagnostic approach

Inspect first, using:

```powershell
.\src\tests\Test-ShellHandlerRegistration.ps1
.\src\tests\Get-OverlayRegistration.ps1
```

These scripts report current registration state. They do not remove legacy entries.

If the old DLL is unavailable, registry inspection may be necessary. Examine relevant `CLSID`, `Interface`, `TypeLib`, `AppID`, `*\ShellEx`, and `Directory\ShellEx` registrations under both `HKLM\Software\Classes` and `HKCU\Software\Classes`. The `HKCR` view merges registration scopes.

**Do not bulk-delete registry keys.** Export any relevant keys first and remove only entries positively identified as belonging to this extension. Never remove shared parent keys, unrelated COM classes, or handlers registered by other applications.
