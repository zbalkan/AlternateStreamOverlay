#requires -Version 5.1
<#
  Integration smoke test for the Windows-only ADS enumeration utility.
  Run on an NTFS volume after building Debug|x64 or Release|x64.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ExePath
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$directory = Join-Path $env:TEMP ('ASO-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null

try {
    $file = Join-Path $directory 'fixture.txt'
    [System.IO.File]::WriteAllText($file, 'default stream')

    # Write named streams without relying on PowerShell-specific -Stream behavior.
    [System.IO.File]::WriteAllBytes($file + ':alpha', [byte[]](65, 66, 67))
    [System.IO.File]::WriteAllBytes($file + ':binary', [byte[]](0, 255, 127))

    $output = & $exe $file 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "altstrm.exe returned $LASTEXITCODE. Output: $output"
    }
    if ($output -notmatch ':alpha:\$DATA' -or $output -notmatch ':binary:\$DATA') {
        throw "Named ADS entries are missing: $output"
    }

    $noStreams = Join-Path $directory 'plain.txt'
    [System.IO.File]::WriteAllText($noStreams, 'plain')
    $plainOutput = & $exe $noStreams 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "altstrm.exe returned $LASTEXITCODE on plain fixture."
    }
    if ($plainOutput -match ':alpha:\$DATA|:binary:\$DATA') {
        throw "ADS from other file appeared in plain-file output: $plainOutput"
    }

    Write-Host 'PASS: ADS enumeration and negative fixture'
}
finally {
    Remove-Item -LiteralPath $directory -Recurse -Force -ErrorAction SilentlyContinue
}
