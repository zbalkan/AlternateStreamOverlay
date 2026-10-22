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

#region Helpers

function ConvertTo-DisplayPath {
    param([string] $Path)
    # Display output doubles backslashes to distinguish literal paths
    # from escaped UTF-16 control sequences.
    return $Path.Replace('\', '\\')
}

function Invoke-AltStrm {
    param([string] $Path)
    # Windows PowerShell 5.1 wraps native stderr lines in ErrorRecords under
    # 2>&1, which terminate the script while $ErrorActionPreference is Stop.
    # Relax it in this function scope only and flatten records to text.
    $ErrorActionPreference = 'Continue'
    $output = & $exe $Path 2>&1 | ForEach-Object { "$_" } | Out-String
    return [pscustomobject]@{ Output = $output; ExitCode = $LASTEXITCODE }
}

function Invoke-AltStrmChecked {
    param([string] $Path, [string] $Context)
    $result = Invoke-AltStrm -Path $Path
    if ($result.ExitCode -ne 0) {
        throw "altstrm.exe returned $($result.ExitCode) for $Context. Output: $($result.Output)"
    }
    return $result.Output
}

function Invoke-AltStrmRedirected {
    param([string] $Argument)
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $exe
    $info.Arguments = '"' + $Argument + '"'
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # The CLI encodes both redirected pipes as UTF-8, regardless of the
    # Windows locale or console code page. Strict decoding rejects invalid bytes.
    $utf8 = [System.Text.UTF8Encoding]::new($false, $true)
    $info.StandardOutputEncoding = $utf8
    $info.StandardErrorEncoding = $utf8

    $process = [System.Diagnostics.Process]::Start($info)
    try {
        # Drain stderr concurrently so neither pipe can fill and block the child.
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $stdout = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()
        return [pscustomobject]@{
            Stdout   = $stdout
            Stderr   = $stderrTask.Result
            ExitCode = $process.ExitCode
        }
    }
    finally { $process.Dispose() }
}

function Assert-ContainsAll {
    param([string] $Text, [string[]] $Expected, [string] $Message)
    $missing = @($Expected | Where-Object { -not $Text.Contains($_) })
    if ($missing.Count -gt 0) {
        throw "$Message ($($missing -join ', ')): $Text"
    }
}

function Assert-NoResultLine {
    param([string] $Output, [string] $Path, [string] $Message)
    $pattern = '(?m)^' + [regex]::Escape((ConvertTo-DisplayPath $Path)) + '\r?$'
    if ($Output -match $pattern) {
        throw "${Message}: $Output"
    }
}

function Assert-NativeStream {
    param([string] $Path, [string] $StreamName, [string] $Message)
    $expected = ':' + $StreamName + ':$DATA'
    $hits = @([NativeAdsFixture]::Streams($Path) | Where-Object {
        [string]::Equals($_, $expected, [StringComparison]::Ordinal)
    })
    if ($hits.Count -ne 1) {
        throw $Message
    }
}

function Test-StreamLineAfterPath {
    param([string] $Output, [string] $Path, [string] $StreamLine)
    # Assert exact result-path lines, not substrings in the introductory banner.
    $displayPath = ConvertTo-DisplayPath $Path
    $lines = @($Output -split '\r?\n')
    for ($i = 0; $i + 1 -lt $lines.Count; $i++) {
        if ($lines[$i] -ceq $displayPath -and $lines[$i + 1] -ceq $StreamLine) {
            return $true
        }
    }
    return $false
}

function New-DanglingJunction {
    param([string] $Path, [string] $Target)
    # PowerShell requires the target to exist when creating a junction.
    # Remove it only after creation to obtain a dangling reparse point.
    [System.IO.Directory]::CreateDirectory($Target) | Out-Null
    $junction = New-Item -ItemType Junction -Path $Path -Target $Target
    [System.IO.Directory]::Delete($Target)
    return $junction.FullName
}

function Assert-PartialScan {
    param(
        [pscustomobject] $Result,
        [string] $BlockedPath,
        [string] $SucceededPath,
        [string] $StreamLine
    )
    if ($Result.ExitCode -eq 0) {
        throw "Partial wildcard scan should exit nonzero: $($Result.Stdout) $($Result.Stderr)"
    }
    $diagnostic = 'Stream enumeration failed for ' + (ConvertTo-DisplayPath $BlockedPath)
    if (-not $Result.Stderr.Contains($diagnostic)) {
        throw "Missing per-item stderr diagnostic: $($Result.Stderr)"
    }
    if ($Result.Stdout.Contains('Stream enumeration failed')) {
        throw "Operational error leaked into stdout: $($Result.Stdout)"
    }
    if (-not (Test-StreamLineAfterPath -Output $Result.Stdout -Path $SucceededPath -StreamLine $StreamLine)) {
        throw "Successful ADS result missing from stdout: $($Result.Stdout)"
    }
}

#endregion

#region Basic enumeration

function Test-NamedFileStreams {
    param([string] $Root)
    $file = Join-Path $Root 'fixture.txt'
    [System.IO.File]::WriteAllText($file, 'default stream')

    # Write named streams without relying on PowerShell-specific -Stream behavior.
    [System.IO.File]::WriteAllBytes($file + ':alpha', [byte[]](65, 66, 67))
    [System.IO.File]::WriteAllBytes($file + ':binary', [byte[]](0, 255, 127))

    $output = Invoke-AltStrmChecked -Path $file -Context 'named-stream fixture'
    Assert-ContainsAll -Text $output -Expected @(':alpha:$DATA', ':binary:$DATA') `
        -Message 'Named ADS entries are missing'

    # The unnamed stream is not a named ADS and must not be printed.
    if ($output -match '(?m)^\s+::\$DATA\r?$') {
        throw "Unnamed default stream was reported as ADS: $output"
    }
}

function Test-PlainFileHasNoStreams {
    param([string] $Root)
    $plain = Join-Path $Root 'plain.txt'
    [System.IO.File]::WriteAllText($plain, 'plain')
    $output = Invoke-AltStrmChecked -Path $plain -Context 'plain fixture'
    Assert-NoResultLine -Output $output -Path $plain `
        -Message 'Plain file was incorrectly reported as having named streams'
}

function Test-DirectoryStream {
    param([string] $Root)
    # A named stream on a directory must be reported as ADS.
    $adsDirectory = Join-Path $Root 'with-stream'
    New-Item -ItemType Directory -Path $adsDirectory | Out-Null
    [System.IO.File]::WriteAllBytes($adsDirectory + ':directoryads', [byte[]](68, 73, 82))
    $output = Invoke-AltStrmChecked -Path $adsDirectory -Context 'directory fixture'
    Assert-ContainsAll -Text $output -Expected @(':directoryads:$DATA') `
        -Message 'Directory ADS was not enumerated'
}

function New-HostileStreamFixture {
    param([string] $Path)
    # Byte contents are deliberately inert fixtures. altstrm enumerates stream
    # names, never executes or interprets stream data. These cases therefore
    # exercise discovery and error handling, not Explorer preview rendering.
    [System.IO.File]::WriteAllText($Path, 'ordinary file')
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
        [System.IO.File]::WriteAllBytes(($Path + ':' + $fixture.Name), $fixture.Bytes)
    }
    # Force the stream-information query to expand beyond its initial buffer.
    for ($i = 0; $i -lt 80; $i++) {
        [System.IO.File]::WriteAllBytes(($Path + ':many' + $i.ToString('D3')), [byte[]]@($i))
    }
    return $fixtures
}

function Test-HostileStreamContent {
    param([string] $Root)
    $hostile = Join-Path $Root 'hostile.txt'
    $fixtures = @(New-HostileStreamFixture -Path $hostile)
    $output = Invoke-AltStrmChecked -Path $hostile -Context 'hostile fixture'

    # Native enumeration preserves UTF-16 names independent of console decoding.
    foreach ($fixture in $fixtures) {
        Assert-NativeStream -Path $hostile -StreamName $fixture.Name `
            -Message "Stream missing from FileStreamInfo: $($fixture.Name)"
        $isAscii = [System.Text.RegularExpressions.Regex]::IsMatch($fixture.Name, '^[\x00-\x7F]+$')
        if ($isAscii -and -not $output.Contains(':' + $fixture.Name + ':$DATA')) {
            throw "ASCII stream missing from CLI output: $($fixture.Name)"
        }
    }

    $expanded = @(0, 40, 79) | ForEach-Object { ':many' + $_.ToString('D3') + ':$DATA' }
    Assert-ContainsAll -Text $output -Expected $expanded `
        -Message 'Expanded stream-info buffer lost a stream'
}

function Test-MissingPathFails {
    param([string] $Root)
    # A missing file must not crash the utility; its documented CLI exit
    # status is nonzero when FindFirstFile finds nothing.
    $result = Invoke-AltStrm -Path (Join-Path $Root 'does-not-exist.txt')
    if ($result.ExitCode -eq 0) {
        throw "Missing file unexpectedly succeeded: $($result.Output)"
    }
}

function Test-BasicEnumeration {
    param([string] $Root)
    Test-NamedFileStreams -Root $Root
    Test-PlainFileHasNoStreams -Root $Root
    Test-DirectoryStream -Root $Root
    Test-HostileStreamContent -Root $Root
    Test-MissingPathFails -Root $Root
    Write-Host 'PASS: basic, directory, hostile-content, and missing-path cases'
}

#endregion

#region CLI path forms

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
    $output = Invoke-AltStrmChecked -Path $Case.Argument -Context "CLI path case $($Case.Name)"

    $matchCount = [regex]::Matches($output, ':pathcheck:\$DATA').Count
    if ($matchCount -ne $Case.ExpectedPaths.Count) {
        throw "CLI path case $($Case.Name) expected $($Case.ExpectedPaths.Count) streams, found $matchCount; $output"
    }
    foreach ($expectedPath in $Case.ExpectedPaths) {
        if (-not (Test-StreamLineAfterPath -Output $output -Path $expectedPath -StreamLine ' :pathcheck:$DATA')) {
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

#endregion

#region Filesystem lifecycle

function Test-EmptyDirectoryHasNoStreams {
    param([string] $Root)
    $plainDirectory = Join-Path $Root 'empty-directory'
    [System.IO.Directory]::CreateDirectory($plainDirectory) | Out-Null
    $output = Invoke-AltStrmChecked -Path $plainDirectory -Context 'empty directory'
    Assert-NoResultLine -Output $output -Path $plainDirectory `
        -Message 'Empty directory was reported as having ADS'
}

function Test-StreamLifecycle {
    param([string] $Path)
    # Deleting and recreating a named stream must update enumeration.
    $stream = $Path + ':cycle'
    [System.IO.File]::WriteAllBytes($stream, [byte[]](1))
    $before = Invoke-AltStrmChecked -Path $Path -Context 'new stream'
    Assert-ContainsAll -Text $before -Expected @(':cycle:$DATA') `
        -Message 'New stream was not discovered'

    [System.IO.File]::Delete($stream)
    $removed = Invoke-AltStrmChecked -Path $Path -Context 'deleted stream'
    if ($removed.Contains(':cycle:$DATA')) {
        throw "Deleted stream is still reported: $removed"
    }

    [System.IO.File]::WriteAllBytes($stream, [byte[]](2))
    $again = Invoke-AltStrmChecked -Path $Path -Context 'recreated stream'
    Assert-ContainsAll -Text $again -Expected @(':cycle:$DATA') `
        -Message 'Recreated stream was not discovered'
}

function Test-LargeStreamRepeatability {
    param([string] $Path)
    # Content size should not affect metadata enumeration.
    [System.IO.File]::WriteAllBytes($Path + ':large', ([byte[]]::new(2 * 1024 * 1024)))
    Assert-NativeStream -Path $Path -StreamName 'large' `
        -Message 'Large stream missing from native enumeration'

    # Separate CLI invocations check repeatability, not persistent handle leaks.
    # Relies on :cycle created by Test-StreamLifecycle.
    for ($i = 0; $i -lt 20; $i++) {
        $result = Invoke-AltStrmChecked -Path $Path -Context "repeat iteration $i"
        Assert-ContainsAll -Text $result -Expected @(':cycle:$DATA', ':large:$DATA') `
            -Message "Repeated enumeration failed on iteration $i"
    }
}

function Test-EnumerationUnderExclusiveLock {
    param([string] $Path)
    # Metadata-only opens can succeed despite an exclusive data-file lock.
    # Verify that both outcomes are handled and subsequent reads still work.
    $lock = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
    try {
        $locked = Invoke-AltStrm -Path $Path
        if ($locked.ExitCode -eq 0) {
            if (-not $locked.Output.Contains(':cycle:$DATA')) {
                throw "Metadata-only stream discovery failed under lock: $($locked.Output)"
            }
        }
        elseif ($locked.Output -notmatch 'Stream enumeration failed') {
            throw "Exclusive-lock error was not reported: $($locked.Output)"
        }
    }
    finally {
        $lock.Dispose()
    }

    $afterUnlock = Invoke-AltStrmChecked -Path $Path -Context 'after unlock'
    Assert-ContainsAll -Text $afterUnlock -Expected @(':cycle:$DATA') `
        -Message 'Stream discovery did not recover after unlock'
}

function Test-FilesystemLifecycle {
    param([string] $Root)
    Test-EmptyDirectoryHasNoStreams -Root $Root

    $path = Join-Path $Root 'lifecycle.txt'
    [System.IO.File]::WriteAllText($path, 'base')
    Test-StreamLifecycle -Path $path
    Test-LargeStreamRepeatability -Path $path
    Test-EnumerationUnderExclusiveLock -Path $path
    Write-Host 'PASS: directory negatives, ADS lifecycle, large stream, repeatability and sharing'
}

#endregion

#region Partial failures and redirected output

function Test-WildcardContinuesAfterFailure {
    param([string] $Root)
    $scanDirectory = Join-Path $Root 'mixed-results'
    [System.IO.Directory]::CreateDirectory($scanDirectory) | Out-Null
    $files = @(
        (Join-Path $scanDirectory '00-first.txt'),
        (Join-Path $scanDirectory 'zz-second.txt')
    )

    # Use a dangling directory junction as a deterministic per-item error.
    # Unlike an exclusive data-file lock, opening a missing junction target
    # cannot succeed through a metadata-only CreateFileW request.
    # Both orderings check that the scan continues whether the failure
    # comes first or last.
    for ($iteration = 0; $iteration -lt $files.Count; $iteration++) {
        $blocked = $files[$iteration]
        $following = $files[1 - $iteration]
        [System.IO.File]::WriteAllText($following, 'valid')
        [System.IO.File]::WriteAllBytes($following + ':afterfailure', [byte[]](65))

        $junction = New-DanglingJunction -Path $blocked `
            -Target (Join-Path $Root ('junction-target-' + $iteration))
        try {
            $result = Invoke-AltStrmRedirected -Argument (Join-Path $scanDirectory '*.txt')
            Assert-PartialScan -Result $result -BlockedPath $blocked `
                -SucceededPath $following -StreamLine ' :afterfailure:$DATA'
        }
        finally {
            # Remove the junction itself, never recurse into its target.
            [System.IO.Directory]::Delete($junction)
            [System.IO.File]::Delete($following)
        }
    }
    Write-Host 'PASS: wildcard scan continues after an item failure and exits nonzero'
}

function Test-RedirectedErrorDiagnostics {
    param([string] $Root)
    $plain = Join-Path $Root 'stderr-valid.txt'
    [System.IO.File]::WriteAllText($plain, 'base')
    [System.IO.File]::WriteAllBytes($plain + ':diagnostic', [byte[]](65))

    $broken = Join-Path $Root 'stderr-broken.txt'
    $junction = New-DanglingJunction -Path $broken `
        -Target (Join-Path $Root 'stderr-junction-target')
    try {
        $result = Invoke-AltStrmRedirected -Argument (Join-Path $Root 'stderr-*.txt')
        Assert-PartialScan -Result $result -BlockedPath $broken `
            -SucceededPath $plain -StreamLine ' :diagnostic:$DATA'
    }
    finally {
        [System.IO.Directory]::Delete($junction)
        [System.IO.File]::Delete($plain)
    }
    Write-Host 'PASS: results on stdout and enumeration errors on stderr'
}

function Test-RedirectedUnicodeOutput {
    param([string] $Root)
    $path = Join-Path $Root ('cli_' + [char]0x00E9 + '.txt')
    [NativeAdsFixture]::Create($path)
    $streamName = 'str' + [char]0x00F6 + 'm'
    [NativeAdsFixture]::Create($path + ':' + $streamName)

    $result = Invoke-AltStrmRedirected -Argument $path
    if ($result.ExitCode -ne 0) {
        throw "Unicode CLI exited $($result.ExitCode): $($result.Stderr)"
    }
    Assert-ContainsAll -Text $result.Stdout `
        -Expected @((ConvertTo-DisplayPath $path), (':' + $streamName + ':$DATA')) `
        -Message 'Redirected UTF-8 output lost Unicode path or ADS name'
    Write-Host 'PASS: redirected UTF-8 Unicode path and ADS name'
}

#endregion

#region UTF-16 names and escaping

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

        # CI's TEMP root can be longer than a developer's. The long ADS
        # fixture may exceed legacy MAX_PATH despite a valid 255-unit
        # NTFS stream-name limit; use Win32's extended-length path syntax.
        if (($path + ':' + $case.Stream).Length -ge 248) {
            $path = '\\?\' + $path
        }

        [NativeAdsFixture]::Create($path)
        [NativeAdsFixture]::Create($path + ':' + $case.Stream)
        Assert-NativeStream -Path $path -StreamName $case.Stream `
            -Message "Native FileStreamInfo failed exact UTF-16 match for $($case.Name)"

        # Invoke the utility with the actual UTF-16 filename. A console
        # encoder may replace lone surrogates in output; do not compare
        # displayed glyphs as proof of exact code-unit preservation.
        Invoke-AltStrmChecked -Path $path -Context $case.Name | Out-Null
        Write-Host "PASS: $($case.Name)"
    }
}

function Test-NormalizationForms {
    param([string] $Root)
    # Distinct normalization forms should remain individually discoverable.
    $path = Join-Path $Root 'normalization.txt'
    [NativeAdsFixture]::Create($path)
    $forms = @([string][char]0x00E9, ('e' + [string][char]0x0301))
    foreach ($form in $forms) {
        [NativeAdsFixture]::Create($path + ':norm_' + $form)
    }
    foreach ($form in $forms) {
        Assert-NativeStream -Path $path -StreamName ('norm_' + $form) `
            -Message 'Missing exact normalization-form stream'
    }
    Write-Host 'PASS: UTF-16 ADS and normalization cases'
}

function Test-EscapedBidiStreamName {
    param([string] $Root)
    $path = Join-Path $Root 'bidi-stream.txt'
    [NativeAdsFixture]::Create($path)
    $rawName = 'prefix' + [char]0x202E + 'suffix'
    [NativeAdsFixture]::Create($path + ':' + $rawName)
    Assert-NativeStream -Path $path -StreamName $rawName `
        -Message 'Native API did not preserve bidi stream fixture'

    $output = Invoke-AltStrmChecked -Path $path -Context 'bidi stream'
    if (-not $output.Contains('prefix\u202Esuffix') -or $output.Contains([string][char]0x202E)) {
        throw "CLI did not escape bidi formatting control: $output"
    }
    Write-Host 'PASS: ADS bidi control is escaped'
}

function Test-EscapedLiteralBackslash {
    param([string] $Root)
    # Backslash is a Win32 path separator, not a valid ADS-name character.
    # Place the literal "\u202E" sequence across a directory boundary instead.
    $folder = Join-Path $Root 'u202E'
    [System.IO.Directory]::CreateDirectory($folder) | Out-Null
    $path = Join-Path $folder 'literal-escape.txt'
    [NativeAdsFixture]::Create($path)
    [NativeAdsFixture]::Create($path + ':literal')

    $output = Invoke-AltStrmChecked -Path $path -Context 'literal backslash'
    Assert-ContainsAll -Text $output -Expected @((ConvertTo-DisplayPath $path), ':literal:$DATA') `
        -Message 'Literal backslash sequence was not preserved unambiguously'
    if ($output.Contains([string][char]0x202E)) {
        throw "Literal backslash sequence was decoded into a bidi control: $output"
    }
    Write-Host 'PASS: literal backslash sequence is escaped'
}

function Test-EscapedSurrogateOutput {
    param([string] $Root)
    $path = Join-Path $Root 'surrogate-output.txt'
    [NativeAdsFixture]::Create($path)
    [NativeAdsFixture]::Create($path + ':unit_' + [string][char]0xD800)

    $output = Invoke-AltStrmChecked -Path $path -Context 'isolated surrogate'
    Assert-ContainsAll -Text $output -Expected @('unit_\uD800') `
        -Message 'Isolated UTF-16 surrogate was lost in CLI output'
    Write-Host 'PASS: isolated surrogate is escaped'
}

function Test-EscapedBanner {
    param([string] $Root)
    $pattern = Join-Path $Root ('missing' + [char]0x202E + '.txt')
    $output = (Invoke-AltStrm -Path $pattern).Output
    if (-not $output.Contains('missing\u202E.txt') -or $output.Contains([string][char]0x202E)) {
        throw "File pattern printed a raw bidi control: $output"
    }
    Write-Host 'PASS: file pattern banner is escaped'
}

#endregion

try {
    Add-Type -TypeDefinition $nativeSource -ErrorAction Stop
    Test-BasicEnumeration -Root $directory
    Test-CliPathForms -Root $directory
    Test-FilesystemLifecycle -Root $directory
    Test-WildcardContinuesAfterFailure -Root $directory
    Test-RedirectedErrorDiagnostics -Root $directory
    Test-RedirectedUnicodeOutput -Root $directory
    Test-UnicodeNames -Root $directory
    Test-NormalizationForms -Root $directory
    Test-EscapedBidiStreamName -Root $directory
    Test-EscapedLiteralBackslash -Root $directory
    Test-EscapedSurrogateOutput -Root $directory
    Test-EscapedBanner -Root $directory
    Write-Host 'PASS: all NTFS ADS integration tests'
}
finally {
    # Malformed native filenames may not be removable via managed APIs;
    # cleanup is best-effort.
    Remove-Item -LiteralPath $directory -Recurse -Force -ErrorAction SilentlyContinue
}

# Earlier negative test cases intentionally invoke the CLI with nonzero exit
# codes. Report success explicitly after all assertions and cleanup complete.
exit 0
