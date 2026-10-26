#requires -Version 5.1
<#
.SYNOPSIS
    Reports Explorer shell icon overlay registrations without modifying them.
.DESCRIPTION
    Enumerates machine and per-user overlay identifier keys. Resolves each CLSID to its in-process COM server and
    identifies this extension's registration. Registry entries alone cannot
    prove which overlays Explorer has loaded or selected for display.
#>
[CmdletBinding()]
param([switch] $RequireRegistration)

$overlayPath = 'Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers'
$expectedOverlays = [ordered]@{
    AlternateStreamOverlayIcon = '{FBAB8C58-59C9-4522-B7E8-1A6AA23B318C}'
    AlternateStreamOverlayMOTW = '{0D5A924E-7498-4AAF-8D27-36D68A653F5D}'
}
$view = [Microsoft.Win32.RegistryView]::Registry64

function Get-OverlayRegistrations {
    param(
        [Microsoft.Win32.RegistryHive] $Hive,
        [string] $Scope
    )

    $root = [Microsoft.Win32.RegistryKey]::OpenBaseKey($Hive, $view)
    try {
        $base = $root.OpenSubKey($overlayPath)
        if ($null -eq $base) { return }
        try {
            foreach ($name in $base.GetSubKeyNames() | Sort-Object) {
                $sub = $base.OpenSubKey($name)
                if ($null -eq $sub) { continue }
                try {
                    $clsid = [string] $sub.GetValue('', '')
                    [pscustomobject]@{
                        Scope = $Scope
                        Name = $name
                        CLSID = $clsid
                        IsAlternateStreamOverlay = ($expectedOverlays.Contains($name) -and $clsid -ieq $expectedOverlays[$name])
                    }
                }
                finally { $sub.Dispose() }
            }
        }
        finally { $base.Dispose() }
    }
    finally { $root.Dispose() }
}

$registrations = @(
    Get-OverlayRegistrations -Hive LocalMachine -Scope 'HKLM'
    Get-OverlayRegistrations -Hive CurrentUser -Scope 'HKCU'
)
$classes = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::ClassesRoot, $view)
try {
    $entries = @(
        foreach ($item in $registrations) {
            $server = $null
            if ($item.CLSID -match '^\{[0-9a-fA-F-]{36}\}$') {
                $key = $classes.OpenSubKey("CLSID\$($item.CLSID)\InprocServer32")
                if ($null -ne $key) {
                    try { $server = [string] $key.GetValue('', '') }
                    finally { $key.Dispose() }
                }
            }
            [pscustomobject]@{
                Scope = $item.Scope
                Name = $item.Name
                CLSID = $item.CLSID
                InprocServer32 = $server
                IsAlternateStreamOverlay = $item.IsAlternateStreamOverlay
            }
        }
    )
}
finally { $classes.Dispose() }

$entries | Format-Table Scope, Name, CLSID, InprocServer32 -AutoSize | Out-Host
Write-Host "Overlay registrations: $($entries.Count)"
$mine = @($entries | Where-Object IsAlternateStreamOverlay)
$missing = @(
    foreach ($name in $expectedOverlays.Keys) {
        if (-not @($mine | Where-Object { $_.Scope -eq 'HKLM' -and $_.Name -eq $name }).Count) {
            $name
        }
    }
)
foreach ($name in $missing) {
    Write-Warning "Required overlay registration missing: $name"
}
foreach ($entry in $mine) {
    if ([string]::IsNullOrWhiteSpace($entry.InprocServer32)) {
        Write-Warning "$($entry.Scope)\$($entry.Name): CLSID has no effective InprocServer32 entry."
    }
    elseif (-not (Test-Path -LiteralPath ([Environment]::ExpandEnvironmentVariables($entry.InprocServer32)) -PathType Leaf)) {
        Write-Warning "$($entry.Scope)\$($entry.Name): COM server path does not exist: $($entry.InprocServer32)"
    }
}
if ($RequireRegistration -and ($missing.Count -gt 0 -or
    @($mine | Where-Object {
        [string]::IsNullOrWhiteSpace($_.InprocServer32) -or
        -not (Test-Path -LiteralPath ([Environment]::ExpandEnvironmentVariables($_.InprocServer32)) -PathType Leaf)
    }).Count -gt 0)) {
    throw 'ADS and MOTW overlay registrations must both have a valid COM server.'
}
if ($RequireRegistration) {
    # This shell extension is activated by Explorer using its CLSID.
    # Fresh installations must not publish unused ATL-generated ProgIDs.
    $legacyProgIds = @(
        'AlternateStreamOverlay.AlternateStrea.1',
        'AlternateStreamOverlay.AlternateStreamO'
    )
    $classes = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::ClassesRoot, $view)
    try {
        foreach ($progId in $legacyProgIds) {
            $key = $classes.OpenSubKey($progId)
            if ($null -ne $key) {
                $key.Dispose()
                throw "Unused overlay ProgID was registered: $progId"
            }
        }
        foreach ($expectedClsid in $expectedOverlays.Values) {
            $clsidKey = $classes.OpenSubKey("CLSID\$expectedClsid")
            if ($null -eq $clsidKey) { throw "Missing overlay CLSID: $expectedClsid" }
            try {
                foreach ($name in @('ProgID', 'VersionIndependentProgID')) {
                    $key = $clsidKey.OpenSubKey($name)
                    if ($null -ne $key) {
                        $key.Dispose()
                        throw "Unused CLSID\$expectedClsid\$name was registered"
                    }
                }
            }
            finally { $clsidKey.Dispose() }
        }
    }
    finally { $classes.Dispose() }
}
Write-Host 'Note: registration counts are not Explorer slot assignments. Loaded overlays depend on Explorer and other installed handlers.'
