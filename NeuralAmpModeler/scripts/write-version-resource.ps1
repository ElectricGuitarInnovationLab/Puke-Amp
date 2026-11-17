# Windows RC accepts fewer arithmetic operators than C++; emit literal version
# components rather than placing bit-shift expressions in FILEVERSION.
# https://devblogs.microsoft.com/oldnewthing/20230313-00/?p=107928
param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
try {
    $config = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\config.h') -Raw
    $match = [regex]::Match($config, '(?m)^#define PLUG_VERSION_STR "(\d+)\.(\d+)\.(\d+)"\r?$')
    $hex = [regex]::Match($config, '(?m)^#define PLUG_VERSION_HEX (0x[0-9A-Fa-f]+)\r?$')
    if (-not $match.Success -or -not $hex.Success) { throw 'Missing authoritative plug-in version in config.h.' }
    $major = [int]$match.Groups[1].Value
    $minor = [int]$match.Groups[2].Value
    $patch = [int]$match.Groups[3].Value
    $packed = ([long]$major -shl 16) -bor ($minor -shl 8) -bor $patch
    if ($major -gt 65535 -or $minor -gt 255 -or $patch -gt 255 -or
        [Convert]::ToInt64($hex.Groups[1].Value.Substring(2), 16) -ne $packed) {
        throw 'PLUG_VERSION_STR and PLUG_VERSION_HEX must agree and fit the packed iPlug version.'
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $path = Join-Path $OutputDirectory 'PukeAmpVersion.h'
    $text = "// Generated from config.h; do not edit.`r`n#define PUKE_FILE_VERSION $major,$minor,$patch,0`r`n"
    if (-not (Test-Path -LiteralPath $path) -or [IO.File]::ReadAllText($path) -ne $text) {
        [IO.File]::WriteAllText($path, $text, [Text.Encoding]::ASCII)
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
