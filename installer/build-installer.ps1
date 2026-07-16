[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '1.0.0',
    [switch]$SkipBuild,
    [switch]$InstallInnoSetup,
    [switch]$KeepStage,
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $PSScriptRoot 'stage'
$appStage = Join-Path $stage 'app'
$prereqStage = Join-Path $stage 'prerequisites'
$cache = Join-Path $PSScriptRoot 'cache'
$binaryOutput = Join-Path $PSScriptRoot 'build-bin'

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory = $root)
    Push-Location $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$FilePath failed with exit code $LASTEXITCODE." }
    } finally { Pop-Location }
}

function Require-File {
    param([string]$Path)
    if (-not (Test-Path $Path -PathType Leaf)) { throw "Required installer input is missing: $Path" }
}

function Get-MicrosoftPrerequisite {
    param([string]$Uri, [string]$Destination)
    if (-not (Test-Path $Destination)) {
        Write-Host "Downloading $(Split-Path $Destination -Leaf)..."
        Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $Destination
    }
    $signature = Get-AuthenticodeSignature $Destination
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft') {
        throw "Microsoft signature validation failed for $Destination."
    }
}

$exitCode = 0
try {
    Write-Host "Building MCDeploy $Version single-file installer..." -ForegroundColor Cyan

    if (-not $SkipBuild) {
        Write-Host 'Building the native dashboard (public WEBSITE is intentionally excluded)...'
        Invoke-Checked 'npm.cmd' @('ci') (Join-Path $root 'frontend')
        Invoke-Checked 'npm.cmd' @('run', 'build') (Join-Path $root 'frontend')

        if (Test-Path $binaryOutput) { Remove-Item $binaryOutput -Recurse -Force }
        New-Item -ItemType Directory -Path $binaryOutput -Force | Out-Null
        $outDirArgument = "/p:OutDir=$binaryOutput\"
        Invoke-Checked 'cmake.exe' @('--build', 'build', '--config', 'Release', '--', $outDirArgument) $root
    }

    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $appStage, $prereqStage, $cache -Force | Out-Null

    $builtExe = Join-Path $binaryOutput 'mcdeploy.exe'
    Require-File $builtExe
    Copy-Item $builtExe (Join-Path $appStage 'mcdeploy.exe')

    $runtimeDlls = @(
        'brotlicommon.dll', 'brotlidec.dll', 'brotlienc.dll', 'cares.dll',
        'drogon.dll', 'jsoncpp.dll', 'libcrypto-3-x64.dll', 'libcurl.dll',
        'libssl-3-x64.dll', 'sqlite3.dll', 'trantor.dll',
        'WebView2Loader.dll', 'zlib1.dll'
    )
    foreach ($dll in $runtimeDlls) {
        $sourceCandidates = @(
            (Join-Path $binaryOutput $dll),
            (Join-Path $root "build\Release\$dll")
        )
        $source = $sourceCandidates | Where-Object { Test-Path $_ -PathType Leaf } | Select-Object -First 1
        if (-not $source) { throw "Required runtime DLL is missing from build output: $dll" }
        Copy-Item $source (Join-Path $appStage $dll)
    }

    $bore = Join-Path $root 'bore.exe'
    Require-File $bore
    Copy-Item $bore (Join-Path $appStage 'bore.exe')

    $nativeDist = Join-Path $root 'dist'
    Require-File (Join-Path $nativeDist 'index.html')
    Copy-Item $nativeDist (Join-Path $appStage 'dist') -Recurse
    if (Test-Path (Join-Path $appStage 'WEBSITE')) {
        throw 'Public WEBSITE content was found in staging. Packaging was stopped.'
    }

    $vcCache = Join-Path $cache 'vc_redist.x64.exe'
    $webViewCache = Join-Path $cache 'MicrosoftEdgeWebView2Setup.exe'
    Get-MicrosoftPrerequisite 'https://aka.ms/vs/17/release/vc_redist.x64.exe' $vcCache
    Get-MicrosoftPrerequisite 'https://go.microsoft.com/fwlink/p/?LinkId=2124703' $webViewCache
    Copy-Item $vcCache (Join-Path $prereqStage 'vc_redist.x64.exe')
    Copy-Item $webViewCache (Join-Path $prereqStage 'MicrosoftEdgeWebView2Setup.exe')

    $isccCandidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) {
        Write-Host 'Inno Setup is required to create the EXE. Installing it with winget...' -ForegroundColor Yellow
        $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
        if (-not $winget) { throw 'Inno Setup is missing and winget is unavailable. Install Inno Setup 6, then run this script again.' }
        Invoke-Checked $winget.Source @(
            'install', '--id', 'JRSoftware.InnoSetup', '--exact', '--silent',
            '--accept-source-agreements', '--accept-package-agreements'
        ) $root
        $iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $iscc) { throw 'Inno Setup installation completed, but ISCC.exe could not be located.' }
    }

    $iss = Join-Path $PSScriptRoot 'MCDeploy.iss'
    Require-File $iss
    Invoke-Checked $iscc @("/DMyAppVersion=$Version", $iss) $PSScriptRoot

    $setupExe = Join-Path $PSScriptRoot "output\MCDeploy-$Version-x64-Setup.exe"
    Require-File $setupExe
    $hash = (Get-FileHash $setupExe -Algorithm SHA256).Hash
    $sizeMb = [math]::Round((Get-Item $setupExe).Length / 1MB, 2)
    Write-Host ''
    Write-Host 'Installer created successfully:' -ForegroundColor Green
    Write-Host "  $setupExe"
    Write-Host "  Size: $sizeMb MB"
    Write-Host "  SHA256: $hash"
    Write-Host '  Public WEBSITE content: excluded'

    if (-not $KeepStage) { Remove-Item $stage -Recurse -Force }
} catch {
    $exitCode = 1
    Write-Host ''
    Write-Host "Installer build failed: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if (-not $NoPause) { [void](Read-Host 'Press Enter to close') }
}

if ($exitCode -ne 0) { exit $exitCode }
