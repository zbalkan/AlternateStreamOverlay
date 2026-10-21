#requires -Version 5.1
<#
    NTFS ADS integration and adversarial regression tests (Windows PowerShell 5.1+).
    Covers named streams on files/directories, inert hostile bytes, large stream
    inventories, and exact UTF-16 names including isolated surrogates.
    This tests enumeration, not the Explorer property-page preview.
#>
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string] $ExePath)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$directory = Join-Path $env:TEMP ('ASO-test-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($directory) | Out-Null

# Win32 interop: avoids managed path normalization/replacement for malformed
# UTF-16 names and compares raw stream names returned by FileStreamInfo.
$nativeSource = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class NativeAdsFixture
{
    private const uint GenericWrite = 0x40000000;
    private const uint ShareAll = 7;
    private const uint OpenAlways = 4;
    private const uint OpenExisting = 3;
    private const uint BackupSemantics = 0x02000000;

    [DllImport("kernel32.dll", EntryPoint = "CreateFileW", CharSet = CharSet.Unicode,
        ExactSpelling = true, SetLastError = true)]
    private static extern SafeFileHandle CreateFile(string path, uint access, uint share,
        IntPtr security, uint disposition, uint flags, IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandleEx(
        SafeFileHandle file, int infoClass, IntPtr buffer, uint size);

    public static void Create(string path)
    {
        using (SafeFileHandle file = CreateFile(path, GenericWrite, ShareAll,
            IntPtr.Zero, OpenAlways, 0, IntPtr.Zero))
        {
            if (file.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateFileW failed");
        }
    }

    public static string[] Streams(string path)
    {
        using (SafeFileHandle file = CreateFile(path, 0, ShareAll,
            IntPtr.Zero, OpenExisting, BackupSemantics, IntPtr.Zero))
        {
            if (file.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Open file failed");
            int size = 65536;
            IntPtr buffer = Marshal.AllocHGlobal(size);
            try
            {
                if (!GetFileInformationByHandleEx(file, 7, buffer, (uint)size))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "FileStreamInfo failed");
                List<string> names = new List<string>();
                int offset = 0;
                while (true)
                {
                    if (offset > size - 24) throw new Exception("Invalid stream record header");
                    uint next = unchecked((uint)Marshal.ReadInt32(buffer, offset));
                    uint nameBytes = unchecked((uint)Marshal.ReadInt32(buffer, offset + 4));
                    if ((nameBytes & 1) != 0 || nameBytes > size - offset - 24)
                        throw new Exception("Invalid stream name length");
                    names.Add(Marshal.PtrToStringUni(IntPtr.Add(buffer, offset + 24), (int)nameBytes / 2));
                    if (next == 0) return names.ToArray();
                    if (next < 24 || next > size - offset || (next & 7) != 0)
                        throw new Exception("Invalid stream record offset");
                    offset += (int)next;
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }
    }
}
'@


function Test-BasicEnumeration {
    param([string] $Root)
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
    if ($plainOutput -match ('(?m)^' + [regex]::Escape($noStreams) + '\r?$')) {
        throw "Plain file was incorrectly reported as having named streams: $plainOutput"
    }

    # A named stream on a directory must be reported as ADS.
    $adsDirectory = Join-Path $directory 'with-stream'
    New-Item -ItemType Directory -Path $adsDirectory | Out-Null
    [System.IO.File]::WriteAllBytes($adsDirectory + ':directoryads', [byte[]](68, 73, 82))
    $dirOutput = & $exe $adsDirectory 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $dirOutput -notmatch ':directoryads:\$DATA') {
        throw "Directory ADS was not enumerated: $dirOutput"
    }

    # The unnamed stream is not a named ADS and must not be printed.
    if ($output -match '(?m)^\s+::\$DATA\r?$') {
        throw "Unnamed default stream was reported as ADS: $output"
    }

    # Byte contents are deliberately inert fixtures. altstrm enumerates stream
    # names, never executes or interprets stream data. These cases therefore
    # exercise discovery and error handling, not Explorer preview rendering.
    $hostile = Join-Path $directory 'hostile.txt'
    [System.IO.File]::WriteAllText($hostile, 'ordinary file')
    $fixtures = @(
        @{ Name = 'empty'; Bytes = [byte[]]@() },
        @{ Name = 'nulbytes'; Bytes = [byte[]](0, 65, 0, 66, 255) },
        @{ Name = 'invalidutf8'; Bytes = [byte[]](0xC3, 0x28, 0xFF) },
        @{ Name = 'escapes'; Bytes = [byte[]](27, 91, 50, 74, 13, 10, 9) },
        @{ Name = 'scripttext'; Bytes = [System.Text.Encoding]::UTF8.GetBytes('<script>alert("inert")</script>') },
        @{ Name = 'powershelltext'; Bytes = [System.Text.Encoding]::UTF8.GetBytes('Write-Output "INERT_TEST_DATA"') },
        @{ Name = ('unicode_' + [char]0x00FC); Bytes = [System.Text.Encoding]::UTF8.GetBytes('Unicode stream content') },
        @{ Name = 'spaced name'; Bytes = [byte[]](1, 2, 3) }
    )
    foreach ($fixture in $fixtures) {
        [System.IO.File]::WriteAllBytes(($hostile + ':' + $fixture.Name), $fixture.Bytes)
    }
    # Force the stream-information query to expand beyond its initial buffer.
    for ($i = 0; $i -lt 80; $i++) {
        [System.IO.File]::WriteAllBytes(($hostile + ':many' + $i.ToString('D3')), [byte[]]@($i))
    }

    $hostileOutput = & $exe $hostile 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to enumerate hostile fixture: $hostileOutput"
    }
    # Native enumeration preserves UTF-16 names independent of console decoding.
    $nativeNames = [NativeAdsFixture]::Streams($hostile)
    foreach ($fixture in $fixtures) {
        $expected = ':' + $fixture.Name + ':$DATA'
        if (-not $nativeNames.Contains($expected)) {
            throw "Stream missing from FileStreamInfo: $($fixture.Name)"
        }
        if ([System.Text.RegularExpressions.Regex]::IsMatch($fixture.Name, '^[\x00-\x7F]+$') -and -not $hostileOutput.Contains($expected)) {
            throw "ASCII stream missing from CLI output: $($fixture.Name)"
        }
    }
    foreach ($i in @(0, 40, 79)) {
        if (-not $hostileOutput.Contains(':many' + $i.ToString('D3') + ':$DATA')) {
            throw "Expanded stream-info buffer lost stream $i"
        }
    }

    # A missing file must not crash the utility; its documented CLI exit
    # status is nonzero when FindFirstFile finds nothing.
    $missingOutput = & $exe (Join-Path $directory 'does-not-exist.txt') 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        throw "Missing file unexpectedly succeeded: $missingOutput"
    }


    Write-Host 'PASS: basic, directory, hostile-content, and missing-path cases'
}

function New-CliPathFixture {
    param([string] $Directory)

    [System.IO.Directory]::CreateDirectory($Directory) | Out-Null
    foreach ($name in @('first.txt', 'second.txt')) {
        $file = Join-Path $Directory $name
        [System.IO.File]::WriteAllText($file, 'base')
        [System.IO.File]::WriteAllBytes($file + ':pathcheck', [byte[]](1))
    }
}

function Assert-CliPathCase {
    param([hashtable] $Case)

    $output = & $exe $Case.Argument 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "CLI path case $($Case.Name) failed: $output"
    }

    $matchCount = [regex]::Matches($output, ':pathcheck:\$DATA').Count
    if ($matchCount -ne $Case.ExpectedPaths.Count) {
        throw "CLI path case $($Case.Name) expected $($Case.ExpectedPaths.Count) streams, found $matchCount; $output"
    }

    # Assert exact result-path lines, not substrings in the introductory banner.
    $lines = $output -split '\r?\n'
    foreach ($expectedPath in $Case.ExpectedPaths) {
        $found = $false
        for ($index = 0; $index -lt $lines.Length - 1; $index++) {
            if ($lines[$index] -ceq $expectedPath -and
                $lines[$index + 1] -ceq ' :pathcheck:$DATA') {
                $found = $true
                break
            }
        }
        if (-not $found) {
            throw "CLI path case $($Case.Name) omitted exact result $expectedPath; $output"
        }
    }
}

function Test-CliPathForms {
    param([string] $Root)

    $pathDirectory = Join-Path $Root 'path-forms'
    New-CliPathFixture -Directory $pathDirectory

    Push-Location -LiteralPath $pathDirectory
    try {
        $drive = [System.IO.Path]::GetPathRoot($pathDirectory).Substring(0, 2)
        $cases = @(
            @{ Name = 'bare relative file'; Argument = 'first.txt'; ExpectedPaths = @('.\first.txt') },
            @{ Name = 'explicit relative file'; Argument = '.\first.txt'; ExpectedPaths = @('.\first.txt') },
            @{ Name = 'drive-relative file'; Argument = ($drive + 'first.txt'); ExpectedPaths = @(($drive + 'first.txt')) },
            @{ Name = 'relative wildcard'; Argument = '*.txt'; ExpectedPaths = @('.\first.txt', '.\second.txt') },
            @{ Name = 'drive-relative wildcard'; Argument = ($drive + '*.txt'); ExpectedPaths = @(($drive + 'first.txt'), ($drive + 'second.txt')) },
            @{ Name = 'absolute wildcard'; Argument = (Join-Path $pathDirectory '*.txt'); ExpectedPaths = @((Join-Path $pathDirectory 'first.txt'), (Join-Path $pathDirectory 'second.txt')) }
        )
        foreach ($case in $cases) {
            Assert-CliPathCase -Case $case
        }
    }
    finally {
        Pop-Location
    }
    Write-Host 'PASS: exact CLI result paths for relative, drive-relative, and wildcard inputs'
}

function Test-FilesystemLifecycle {
    param([string] $Root)

    $plainDirectory = Join-Path $Root 'empty-directory'
    [System.IO.Directory]::CreateDirectory($plainDirectory) | Out-Null
    $plainDirectoryOutput = & $exe $plainDirectory 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $plainDirectoryOutput -match ('(?m)^' + [regex]::Escape($plainDirectory) + '\r?$')) {
        throw "Empty directory was reported as having ADS: $plainDirectoryOutput"
    }

    # Deleting and recreating a named stream must update enumeration.
    $path = Join-Path $Root 'lifecycle.txt'
    [System.IO.File]::WriteAllText($path, 'base')
    $stream = $path + ':cycle'
    [System.IO.File]::WriteAllBytes($stream, [byte[]](1))
    $before = & $exe $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or -not $before.Contains(':cycle:$DATA')) {
        throw "New stream was not discovered: $before"
    }
    [System.IO.File]::Delete($stream)
    $removed = & $exe $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $removed.Contains(':cycle:$DATA')) {
        throw "Deleted stream is still reported: $removed"
    }
    [System.IO.File]::WriteAllBytes($stream, [byte[]](2))
    $again = & $exe $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or -not $again.Contains(':cycle:$DATA')) {
        throw "Recreated stream was not discovered: $again"
    }

    # Content size should not affect metadata enumeration.
    $large = $path + ':large'
    [System.IO.File]::WriteAllBytes($large, ([byte[]]::new(2 * 1024 * 1024)))
    $names = [NativeAdsFixture]::Streams($path)
    if (-not $names.Contains(':large:$DATA')) {
        throw "Large stream missing from native enumeration"
    }

    # Separate CLI invocations check repeatability, not persistent handle leaks.
    for ($i = 0; $i -lt 20; $i++) {
        $result = & $exe $path 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or -not $result.Contains(':cycle:$DATA') -or -not $result.Contains(':large:$DATA')) {
            throw ("Repeated enumeration failed on iteration {0}: {1}" -f $i, $result)
        }
    }

    # An exclusive handle may prevent stream queries. Failure to enumerate
    # is acceptable, but the CLI must remain operational and recover.
    $lock = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    try {
        $lockedOutput = & $exe $path 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            throw "CLI returned failure under exclusive lock: $lockedOutput"
        }
    }
    finally {
        $lock.Dispose()
    }
    $afterUnlock = & $exe $path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or -not $afterUnlock.Contains(':cycle:$DATA')) {
        throw "Stream discovery did not recover after unlock: $afterUnlock"
    }
    Write-Host 'PASS: directory negatives, ADS lifecycle, large stream, repeatability and sharing'
}

function Test-RedirectedUnicodeOutput {
    param([string] $Root)

    $path = Join-Path $Root ('cli_' + [char]0x00E9 + '.txt')
    [NativeAdsFixture]::Create($path)
    $streamName = 'str' + [char]0x00F6 + 'm'
    [NativeAdsFixture]::Create($path + ':' + $streamName)

    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.Arguments = '"' + $path + '"'
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $bytes = New-Object System.IO.MemoryStream
        $process.StandardOutput.BaseStream.CopyTo($bytes)
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "Unicode CLI exited $($process.ExitCode)" }
        $strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
        $output = $strictUtf8.GetString($bytes.ToArray())
        if (-not $output.Contains($path) -or -not $output.Contains(':' + $streamName + ':$DATA')) {
            throw "Redirected UTF-8 output lost Unicode path or ADS name: $output"
        }
    }
    finally {
        $process.Dispose()
    }
    Write-Host 'PASS: redirected UTF-8 Unicode path and ADS name'
}

function Test-UnicodeNames {
    param([string] $Root)
    $high = [string][char]0xD800
    $low = [string][char]0xDC00
    $pair = [string]([char]0xD83E) + [string]([char]0xDD26)
    $cases = @(
        @{ Name = 'supplementary'; File = 'f_' + $pair + '.txt'; Stream = 's_' + $pair },
        @{ Name = 'isolated-high-file'; File = 'f_' + $high + '.txt'; Stream = 'ordinary' },
        @{ Name = 'isolated-low-file'; File = 'f_' + $low + '.txt'; Stream = 'ordinary' },
        @{ Name = 'isolated-high-ads'; File = 'high.txt'; Stream = 's_' + $high },
        @{ Name = 'isolated-low-ads'; File = 'low.txt'; Stream = 's_' + $low },
        @{ Name = 'long-unicode-stream'; File = 'long.txt'; Stream = ('z' * 170) + $pair }
    )

    foreach ($case in $cases) {
        $path = Join-Path $Root $case.File
        $streamPath = $path + ':' + $case.Stream

        # CI's TEMP root can be longer than a developer's. The long ADS
        # fixture may exceed legacy MAX_PATH despite a valid 255-unit
        # NTFS stream-name limit; use Win32's extended-length path syntax.
        if ($streamPath.Length -ge 248) {
            $path = '\\?\' + $path
            $streamPath = $path + ':' + $case.Stream
        }

        [NativeAdsFixture]::Create($path)
        [NativeAdsFixture]::Create($streamPath)
        $expected = ':' + $case.Stream + ':$DATA'
        $names = [NativeAdsFixture]::Streams($path)
        $found = $false
        foreach ($name in $names) {
            if ([string]::Equals($name, $expected, [StringComparison]::Ordinal)) { $found = $true }
        }
        if (-not $found) {
            throw "Native FileStreamInfo failed exact UTF-16 match for $($case.Name)"
        }

        # Invoke the utility with the actual UTF-16 filename. A console
        # encoder may replace lone surrogates in output; do not compare
        # displayed glyphs as proof of exact code-unit preservation.
        $cliOutput = & $exe $path 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            throw "altstrm failed on $($case.Name): $cliOutput"
        }
        Write-Host "PASS: $($case.Name)"
    }

    # Distinct normalization forms should remain individually discoverable.
    $normalization = Join-Path $Root 'normalization.txt'
    [NativeAdsFixture]::Create($normalization)
    $composed = [string][char]0x00E9
    $decomposed = 'e' + [string][char]0x0301
    [NativeAdsFixture]::Create($normalization + ':norm_' + $composed)
    [NativeAdsFixture]::Create($normalization + ':norm_' + $decomposed)
    $names = [NativeAdsFixture]::Streams($normalization)
    foreach ($form in @($composed, $decomposed)) {
        $expected = ':norm_' + $form + ':$DATA'
        if (@($names | Where-Object { [string]::Equals($_, $expected, [StringComparison]::Ordinal) }).Count -ne 1) {
            throw "Missing exact normalization-form stream"
        }
    }
    Write-Host 'PASS: UTF-16 ADS and normalization cases'
}

try {
    Add-Type -TypeDefinition $nativeSource -ErrorAction Stop
    Test-BasicEnumeration -Root $directory
    Test-CliPathForms -Root $directory
    Test-FilesystemLifecycle -Root $directory
    Test-RedirectedUnicodeOutput -Root $directory
    Test-UnicodeNames -Root $directory
    Write-Host 'PASS: all NTFS ADS integration tests'
}
finally {
    # Malformed native filenames may not be removable via managed APIs;
    # cleanup is best-effort.
    Remove-Item -LiteralPath $directory -Recurse -Force -ErrorAction SilentlyContinue
}
