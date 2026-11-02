# Removes Geode's extracted-mod cache for afterflow.
#
# Geode unpacks a .geode into geode/unzipped/<mod-id>/ and only re-extracts when
# the version string in mod.json changes. If you rebuild without bumping the
# version, the game keeps loading the OLD extracted DLL - the mod appears to
# "do nothing" even though the .geode on disk is current.
#
# Deleting the extracted folder forces a fresh unpack on next launch.

param(
    [string]$ModId = 'afterflow.prsevend'
)

$ErrorActionPreference = 'SilentlyContinue'

$roots = @()
if ($env:GEODE_GAME_DIR) { $roots += $env:GEODE_GAME_DIR }

# common install locations
$roots += @(
    'C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash',
    'C:\Program Files\Steam\steamapps\common\Geometry Dash',
    'D:\SteamLibrary\steamapps\common\Geometry Dash',
    'E:\SteamLibrary\steamapps\common\Geometry Dash'
)

# any steam library on other drives
Get-PSDrive -PSProvider FileSystem | ForEach-Object {
    $candidate = Join-Path $_.Root 'SteamLibrary\steamapps\common\Geometry Dash'
    if (Test-Path $candidate) { $roots += $candidate }
} | Out-Null

$seen = @{}
foreach ($root in $roots) {
    if ([string]::IsNullOrWhiteSpace($root)) { continue }
    $target = Join-Path $root "geode\unzipped\$ModId"
    if ($seen.ContainsKey($target)) { continue }
    $seen[$target] = $true

    if (Test-Path $target) {
        Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $target) {
            Write-Host "purge: could not remove $target (game running?)"
        }
        else {
            Write-Host "purge: removed $target"
        }
    }
}

Write-Host 'purge: done'
exit 0
