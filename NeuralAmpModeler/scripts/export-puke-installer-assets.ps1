[CmdletBinding()]
param([ValidateSet('x64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Native tools must be checked explicitly in both Windows PowerShell and pwsh.
function Invoke-Checked {
    param([string]$Tool, [string[]]$ToolArguments)
    & $Tool @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "A required native tool failed: $Tool (exit $LASTEXITCODE)" }
}

function Assert-PE {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE executable: $Path" }
        $stream.Position = 0x3C
        $offset = $reader.ReadUInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Expected native x86_64 PE executable: $Path"
        }
    } finally { $reader.Dispose() }
}

function Get-PluginBinaries {
    param([string]$Bundle)
    @(Get-ChildItem -LiteralPath $Bundle -Recurse -File -Force | Where-Object {
        $_.Extension -in @('.dll', '.vst3', '.exe')
    })
}

function Assert-Bundle {
    param([string]$Bundle, [switch]$Signed)
    Invoke-Checked $python @($helper, 'validate', '--bundle', $Bundle)
    $main = Join-Path $Bundle 'Contents\x86_64-win\Puke Amp.vst3'
    Assert-PE $main
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($main)
    if ($info.ProductVersion -ne $version -or $info.FileVersion -ne $version -or
        "$($info.FileMajorPart).$($info.FileMinorPart).$($info.FileBuildPart)" -ne $version) {
        throw "Built plug-in version does not match config.h ($version). Rebuild with the current main.rc."
    }
    $binaries = @(Get-PluginBinaries $Bundle)
    foreach ($binary in $binaries) {
        Assert-PE $binary.FullName
        # MSVC runtime is statically linked by common-win.props. Resolve other
        # direct imports against bundled DLLs or the native Windows system DLLs.
        $dependencies = & dumpbin.exe /nologo /dependents $binary.FullName
        if ($LASTEXITCODE -ne 0) { throw 'dumpbin dependency inspection failed.' }
        foreach ($line in $dependencies) {
            if ($line -match '^\s+([^\s]+\.dll)\s*$') {
                $dll = $Matches[1]
                if ($dll -match '^(api-ms-win-|ext-ms-win-)') { continue }
                $local = Join-Path $binary.DirectoryName $dll
                $system = Join-Path $env:SystemRoot "System32\$dll"
                if (-not (Test-Path -LiteralPath $local) -and -not (Test-Path -LiteralPath $system)) {
                    throw "Missing dependency $dll required by $($binary.Name). Bundle it before signing."
                }
            }
        }
        if ($Signed) {
            Invoke-Checked $signTool @('verify', '/pa', '/all', $binary.FullName)
            $signature = Get-AuthenticodeSignature -LiteralPath $binary.FullName
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint -or
                $null -eq $signature.TimeStamperCertificate) {
                throw "Invalid, unexpected, or untimestamped Authenticode signature: $($binary.Name)"
            }
        }
    }
}

$work = $null
$certificate = $null
try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'Run this command on native Windows in a Visual Studio Developer PowerShell.'
    }
    foreach ($tool in @('python.exe', 'msbuild.exe', 'dumpbin.exe')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Missing $tool. Install Python 3.9+ and Visual Studio 2022 C++ desktop tools; open Developer PowerShell." }
    }
    $python = (Get-Command python.exe).Source
    $project = Split-Path $PSScriptRoot -Parent
    $repo = Split-Path $project -Parent
    $helper = Join-Path $PSScriptRoot 'installer_assets.py'
    $versionOutput = & $python $helper version
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read authoritative plug-in version.' }
    $version = "$versionOutput".Trim()
    if (-not $env:WINDOWS_CERTIFICATE_PATH -or -not (Test-Path -LiteralPath $env:WINDOWS_CERTIFICATE_PATH) -or
        -not $env:WINDOWS_CERTIFICATE_PASSWORD) {
        throw 'Release signing is required. Set WINDOWS_CERTIFICATE_PATH to your code-signing PFX and WINDOWS_CERTIFICATE_PASSWORD securely in this process environment. No unsigned export is supported.'
    }
    $signTool = $env:WINDOWS_SIGNTOOL_PATH
    if (-not $signTool) {
        $candidate = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($candidate) { $signTool = $candidate.FullName }
    }
    if (-not $signTool -or -not (Test-Path -LiteralPath $signTool)) {
        throw 'Install Windows SDK Signing Tools and set WINDOWS_SIGNTOOL_PATH to signtool.exe.'
    }
    # Sign through the Windows certificate store so the PFX password is never
    # passed on a child process command line or printed in a native-tool error.
    $password = ConvertTo-SecureString $env:WINDOWS_CERTIFICATE_PASSWORD -AsPlainText -Force
    $existing = @(Get-ChildItem Cert:\CurrentUser\My | ForEach-Object { $_.Thumbprint })
    try {
        $imported = @(Import-PfxCertificate -FilePath $env:WINDOWS_CERTIFICATE_PATH -CertStoreLocation Cert:\CurrentUser\My -Password $password)
    } catch { throw 'Cannot import code-signing PFX. Check WINDOWS_CERTIFICATE_PATH and WINDOWS_CERTIFICATE_PASSWORD.' }
    $certificate = $imported | Where-Object { $_.HasPrivateKey -and ($_.EnhancedKeyUsageList.ObjectId -contains '1.3.6.1.5.5.7.3.3') } | Select-Object -First 1
    if (-not $certificate) { throw 'The PFX must contain a code-signing certificate and its private key.' }
    if ($certificate.NotAfter -le (Get-Date) -or $certificate.NotBefore -gt (Get-Date)) { throw 'The code-signing certificate is outside its validity period.' }

    $output = Join-Path $repo 'dist\puke-installer-assets\packaging\vendor\puke-amp'
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    $work = Join-Path $output ('.windows-x64.' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    $bundle = Join-Path $work 'staging\Puke Amp.vst3'
    $platformOutput = Join-Path $work 'windows-x64'
    $extracted = Join-Path $work 'extracted'
    New-Item -ItemType Directory -Path (Split-Path $bundle), $platformOutput, $extracted | Out-Null
    # Force a fresh bundle so a previous Win32 build cannot leak into this ZIP.
    $built = Join-Path $project 'build-win\Puke Amp.vst3'
    if (Test-Path -LiteralPath $built) { Remove-Item -LiteralPath $built -Recurse -Force }
    Invoke-Checked (Join-Path $PSScriptRoot 'make-vst3-win.bat') @('--bundle-only')
    Invoke-Checked $python @($helper, 'copy', '--source', $built, '--bundle', $bundle)
    Invoke-Checked $python @($helper, 'stage', '--bundle', $bundle)
    Assert-Bundle $bundle
    foreach ($binary in @(Get-PluginBinaries $bundle)) {
        Invoke-Checked $signTool @('sign', '/sha1', $certificate.Thumbprint, '/s', 'My', '/fd', 'SHA256',
            '/tr', 'https://timestamp.digicert.com', '/td', 'SHA256', $binary.FullName)
    }
    Assert-Bundle $bundle -Signed
    $archive = Join-Path $platformOutput "Puke-Amp-$version-Windows-x64.zip"
    Invoke-Checked $python @($helper, 'windows-pack', '--bundle', $bundle, '--archive', $archive)
    Invoke-Checked $python @($helper, 'windows-unpack', '--archive', $archive, '--extracted', $extracted)
    Invoke-Checked $python @($helper, 'archive-check', '--bundle', $bundle, '--archive', $archive, '--extracted', $extracted)
    $extractedBundle = Join-Path $extracted 'Puke Amp.vst3'
    Assert-Bundle $extractedBundle -Signed
    Invoke-Checked $python @($helper, 'manifest', '--bundle', $extractedBundle, '--archive', $archive, '--platform', 'windows-x64')
    $destination = Join-Path $output 'windows-x64'
    $previous = Join-Path $work 'previous'
    if (Test-Path -LiteralPath $destination) { Move-Item -LiteralPath $destination -Destination $previous }
    try { Move-Item -LiteralPath $platformOutput -Destination $destination }
    catch {
        if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $destination }
        throw
    }
    Write-Host "Exported $destination"
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally {
    if ($work -and (Test-Path -LiteralPath $work)) { Remove-Item -LiteralPath $work -Recurse -Force }
    if (Get-Variable imported -ErrorAction SilentlyContinue) {
        foreach ($cert in $imported) {
            if ($cert.Thumbprint -notin $existing) { Remove-Item -LiteralPath "Cert:\CurrentUser\My\$($cert.Thumbprint)" -Force }
        }
    }
}
