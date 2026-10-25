# Regression tests for the release-notes generator; never creates a GitHub release.
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'New-ReleaseNotes.ps1'
$root = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString('N'))
New-Item -Path $root -ItemType Directory | Out-Null
try {
    $changelog = Join-Path $root 'CHANGELOG.md'
    $notes = Join-Path $root 'notes.txt'
    $valid = @'
## [Unreleased]

## [2.0.0] - 2026-10-25

### Added

- Test note

## Original changelog (1.x)

Historical text.
'@
    Set-Content -LiteralPath $changelog -Value $valid -Encoding utf8
    & $script -Version '2.0.0' -ChangelogPath $changelog -OutputPath $notes
    $actual = Get-Content -LiteralPath $notes -Raw -Encoding utf8
    if ($actual -notmatch 'Test note' -or $actual -match 'Historical text') {
        throw 'Expected only the 2.0.0 release notes.'
    }

    foreach ($case in @(
        @{ Name = 'Wrong version'; Content = $valid; Version = '2.0.1' },
        @{ Name = 'Missing version section'; Content = '## [Unreleased]'; Version = '2.0.0' },
        @{ Name = 'Undated version'; Content = $valid.Replace(' - 2026-10-25', ''); Version = '2.0.0' }
    )) {
        Set-Content -LiteralPath $changelog -Value $case.Content -Encoding utf8
        $failed = $false
        try {
            & $script -Version $case.Version -ChangelogPath $changelog -OutputPath $notes
        }
        catch {
            $failed = $true
        }
        if (-not $failed) { throw "Expected failure: $($case.Name)" }
    }
    Write-Host 'Release notes generator regression tests passed.'
}
finally {
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
