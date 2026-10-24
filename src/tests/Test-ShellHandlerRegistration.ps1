#requires -Version 5.1
<#
.SYNOPSIS
    Checks AlternateStreamOverlay context-menu and property-sheet registrations.
.DESCRIPTION
    Inspects effective HKCR shell handler registrations and the associated COM
    server without changing registry state. This does not establish whether
    Explorer has loaded or activated the handlers.
#>
[CmdletBinding()]
param([switch] $FailOnIssues)

$expectedClsid = '{4A26024B-5499-45BE-A23F-4A65E88888D0}'
$view = [Microsoft.Win32.RegistryView]::Registry64
$checks = @(
    @{ Kind = 'Context menu'; Key = '*\shellex\ContextMenuHandlers\AlternateStreams' }
    @{ Kind = 'Property sheet'; Key = '*\shellex\PropertySheetHandlers\{4A26024B-5499-45BE-A23F-4A65E88888D0}' }
    @{ Kind = 'Context menu'; Key = 'Directory\shellex\ContextMenuHandlers\AlternateStreams' }
    @{ Kind = 'Property sheet'; Key = 'Directory\shellex\PropertySheetHandlers\{4A26024B-5499-45BE-A23F-4A65E88888D0}' }
)

$root = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::ClassesRoot, $view)
try {
    $results = @(
        foreach ($check in $checks) {
            $key = $root.OpenSubKey($check.Key)
            $registeredClsid = $null
            $exists = $null -ne $key
            if ($exists) {
                try {
                    if ($check.Kind -eq 'Property sheet') {
                        # PropertySheetHandlers uses a CLSID-named subkey.
                        $registeredClsid = $check.Key.Split([char]92)[-1]
                    }
                    else {
                        $registeredClsid = [string] $key.GetValue('', '')
                    }
                }
                finally { $key.Dispose() }
            }
            [pscustomobject]@{
                Kind = $check.Kind
                RegistryKey = "HKCR\$($check.Key)"
                CLSID = $registeredClsid
                Status = if (-not $exists) { 'Missing' }
                         elseif ($registeredClsid -ieq $expectedClsid) { 'OK' }
                         else { 'Unexpected CLSID' }
            }
        }
    )

    $serverPath = "CLSID\$expectedClsid\InprocServer32"
    $serverKey = $root.OpenSubKey($serverPath)
    $server = $null
    $threading = $null
    if ($null -ne $serverKey) {
        try {
            $server = [string] $serverKey.GetValue('', '')
            $threading = [string] $serverKey.GetValue('ThreadingModel', '')
        }
        finally { $serverKey.Dispose() }
    }

    $expandedServer = if ([string]::IsNullOrWhiteSpace($server)) {
        $null
    } else {
        [Environment]::ExpandEnvironmentVariables($server)
    }
    $serverStatus = if ([string]::IsNullOrWhiteSpace($server)) { 'Missing COM server' }
                    elseif (-not (Test-Path -LiteralPath $expandedServer -PathType Leaf)) { 'Server file missing' }
                    elseif ($threading -ine 'Apartment') { 'Unexpected threading model' }
                    else { 'OK' }

    $results | Format-Table Kind, RegistryKey, CLSID, Status -AutoSize | Out-Host
    [pscustomobject]@{
        Kind = 'COM server'
        RegistryKey = "HKCR\$serverPath"
        CLSID = $expectedClsid
        InprocServer32 = $server
        ThreadingModel = $threading
        Status = $serverStatus
    } | Format-List | Out-Host

    $overlayClsid = '{FBAB8C58-59C9-4522-B7E8-1A6AA23B318C}'
    $overlayKey = $root.OpenSubKey("CLSID\$overlayClsid\InprocServer32")
    $overlayThreading = $null
    $overlayServer = $null
    if ($null -ne $overlayKey) {
        try {
            $overlayThreading = [string]$overlayKey.GetValue('ThreadingModel', '')
            $overlayServer = [string]$overlayKey.GetValue('', '')
        }
        finally { $overlayKey.Dispose() }
    }
    $overlayStatus = if ([string]::IsNullOrWhiteSpace($overlayServer)) { 'Missing COM server' }
                     elseif (-not (Test-Path -LiteralPath ([Environment]::ExpandEnvironmentVariables($overlayServer)) -PathType Leaf)) { 'Server file missing' }
                     elseif ($overlayThreading -ine 'Apartment') { 'Unexpected threading model' }
                     else { 'OK' }
    Write-Host "Overlay COM threading model: $overlayThreading ($overlayStatus)"

    $failures = @($results | Where-Object Status -ne 'OK')
    if ($failures.Count -gt 0 -or $serverStatus -ne 'OK' -or $overlayStatus -ne 'OK') {
        $message = "Shell handler registration has $($failures.Count) handler issue(s); COM server status: $serverStatus."
        if ($FailOnIssues) { throw $message }
        Write-Warning $message
    }
    else {
        Write-Host 'All expected shell handler registrations are present.'
    }
    Write-Host 'Note: this checks effective registration, not Explorer activation or menu visibility.'
}
finally { $root.Dispose() }
