$ErrorActionPreference = 'Stop'
$folder = Join-Path ([System.IO.Path]::GetTempPath()) ("aso-notes-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder | Out-Null
try {
    $changelog = Join-Path $folder 'CHANGELOG.md'
    $notes = Join-Path $folder 'release-notes.md'
    $valid = @'
# Changelog
## [Unreleased]
### Added
- Pending work

## [2.0.0] - 2026-10-22
### Added
- Visible feature
### Fixed
- Visible bug

## Original changelog (1.x)
Minor rev - preserved
'@
    Set-Content -LiteralPath $changelog -Value $valid -Encoding utf8
    & "$PSScriptRoot/New-ReleaseNotes.ps1" -Version '2.0.0' -ChangelogPath $changelog -OutputPath $notes
    $actual = Get-Content -LiteralPath $notes -Raw
    if ($actual -notmatch 'Visible feature' -or $actual -notmatch 'Visible bug' -or
        $actual -match 'Pending work|Minor rev') { throw 'Release notes content regression.' }

    foreach ($bad in @(
        @{ Version = '2.0.1'; Body = $valid },
        @{ Version = '2.0.0'; Body = $valid.Replace('## [2.0.0] - 2026-10-22', '## [2.0.0]') },
        @{ Version = '2.0.0'; Body = $valid.Replace('### Added' + "`n" + '- Visible feature', 'Plain paragraph').Replace('### Fixed' + "`n" + '- Visible bug', '') }
    )) {
        Set-Content -LiteralPath $changelog -Value $bad.Body -Encoding utf8
        $failed = $false
        try {
            & "$PSScriptRoot/New-ReleaseNotes.ps1" -Version $bad.Version -ChangelogPath $changelog -OutputPath $notes 2>$null | Out-Null
        }
        catch { $failed = $true }
        if (-not $failed) { throw "Invalid changelog was accepted for $($bad.Version)." }
    }
    Write-Host 'Release notes extraction tests passed.'
}
finally {
    Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
}
