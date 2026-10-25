[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version,
    [string]$ChangelogPath = 'CHANGELOG.md',
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$content = Get-Content -LiteralPath $ChangelogPath -Raw -Encoding utf8
$releases = [regex]::Matches($content, '(?m)^## \[(\d+\.\d+\.\d+)\](?:[ \t]+-[ \t]+\d{4}-\d{2}-\d{2})?[ \t]*\r?$')
if ($releases.Count -eq 0 -or $releases[0].Groups[1].Value -ne $Version) {
    throw "Latest versioned changelog section must be [$Version]."
}
$heading = $releases[0]
if ($heading.Value -notmatch '[ \t]+-[ \t]+\d{4}-\d{2}-\d{2}[ \t]*\r?$') {
    throw "Changelog section [$Version] must have a release date."
}
$start = $heading.Index + $heading.Length
$next = [regex]::Match($content.Substring($start), '(?m)^## ')
$end = if ($next.Success) { $start + $next.Index } else { $content.Length }
$notes = $content.Substring($start, $end - $start).Trim()
if ([string]::IsNullOrWhiteSpace($notes) -or $notes -notmatch '(?m)^### (Added|Changed|Deprecated|Removed|Fixed|Security)[ \t]*\r?$') {
    throw "Changelog section [$Version] has no categorized release notes."
}
$destination = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.File]::WriteAllText($destination, "# AlternateStreamOverlay v$Version`n`n$notes`n", [System.Text.UTF8Encoding]::new($false))
Write-Host "Prepared release notes from CHANGELOG.md [$Version] at $destination"
