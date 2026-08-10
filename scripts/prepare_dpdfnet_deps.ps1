[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$OutputDirectory,
    [string]$DependencySourceDirectory = '',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SherpaVersion = '1.13.1'
$SherpaArchiveName = "sherpa-onnx-v$SherpaVersion-win-x64-shared-MD-Release-no-tts.tar.bz2"
$SherpaUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v$SherpaVersion/$SherpaArchiveName"
$SherpaSha256 = '6760b0e25eaad0dadffba9029b1270778e0dbfd43f314cf070b2f9c1dcb4af25'

$ModelName = 'dpdfnet2_48khz_hr.onnx'
$ModelUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/speech-enhancement-models/$ModelName"
$ModelSha256 = '0b399f8a58dc4d70d8cd97541f5c39869406145193b957d00a03b66070944928'

$RuntimeSha256 = [ordered]@{
    'sherpa-onnx-c-api.dll' = '94f23cb935c57fb2581c66d65f6602ab5b509d6fc269404296af0336871daba5'
    'onnxruntime.dll' = '8b695444d1a35ed0c8338b8c14438b3be5e0a3b222b88b1e7b4ce8753f135b50'
    'onnxruntime_providers_shared.dll' = 'ebc55b0f28e8a79cbf78e810a7f510ba70e75a2dfbcfcc6aca31ab2b8710a59a'
}
$HeaderSha256 = '41d830b46ff80abab93b00bf728128881db2b332e0e669c3f580677d09e60ce5'

function Get-AbsolutePath([string]$Path) {
    return [IO.Path]::GetFullPath($Path)
}

function Get-Sha256([string]$Path) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString(
            $algorithm.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

function Assert-VerifiedFile([string]$Path, [string]$ExpectedSha256, [string]$Description) {
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing vendored DPDFNet file: $Description ($Path). Run 'git lfs pull'."
    }
    $actual = Get-Sha256 $Path
    if ($actual -ne $ExpectedSha256) {
        throw "SHA-256 mismatch for vendored DPDFNet file: $Description. expected=$ExpectedSha256 actual=$actual. Run 'git lfs pull'."
    }
}

function Download-Verified([string]$Url, [string]$Path, [string]$ExpectedSha256) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        if ((Get-Sha256 $Path) -eq $ExpectedSha256) {
            Write-Output "Using verified cache: $Path"
            return
        }
        Remove-Item -LiteralPath $Path -Force
    }

    $temporary = "$Path.download-$([Guid]::NewGuid().ToString('N'))"
    try {
        Write-Output "Downloading: $Url"
        Invoke-WebRequest -Uri $Url -OutFile $temporary -UseBasicParsing -Headers @{ 'User-Agent' = 'VoxMic build' }
        $actual = Get-Sha256 $temporary
        if ($actual -ne $ExpectedSha256) {
            throw "SHA-256 mismatch for $Url. expected=$ExpectedSha256 actual=$actual"
        }
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

$outputRoot = Get-AbsolutePath $OutputDirectory
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($DependencySourceDirectory)) {
    $DependencySourceDirectory = Join-Path $repoRoot 'third_party\dpdfnet'
}
$vendorRoot = Get-AbsolutePath $DependencySourceDirectory
$vendorRuntimeRoot = Join-Path $vendorRoot 'runtime'
$vendorModelPath = Join-Path $vendorRoot "model/$ModelName"
$vendorHeaderPath = Join-Path $vendorRoot 'include/sherpa-onnx/c-api/c-api.h'
$useVendoredPayload = Test-Path -LiteralPath $vendorRoot -PathType Container
$cacheRoot = Join-Path $outputRoot 'downloads'
$preparedRoot = Join-Path $outputRoot 'dpdfnet'
$stageRoot = Join-Path $outputRoot (".stage-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $cacheRoot, $stageRoot -Force | Out-Null

try {
    $runtimeStage = Join-Path $stageRoot 'dpdfnet/runtime'
    $includeStage = Join-Path $stageRoot 'dpdfnet/include/sherpa-onnx/c-api'
    $modelStage = Join-Path $stageRoot 'dpdfnet/model'
    New-Item -ItemType Directory -Path $runtimeStage, $includeStage, $modelStage -Force | Out-Null

    if ($useVendoredPayload) {
        foreach ($name in $RuntimeSha256.Keys) {
            Assert-VerifiedFile (Join-Path $vendorRuntimeRoot $name) $RuntimeSha256[$name] "runtime/$name"
            Copy-Item -LiteralPath (Join-Path $vendorRuntimeRoot $name) -Destination (Join-Path $runtimeStage $name)
        }
        Assert-VerifiedFile $vendorHeaderPath $HeaderSha256 'include/sherpa-onnx/c-api/c-api.h'
        Assert-VerifiedFile $vendorModelPath $ModelSha256 "model/$ModelName"
        Copy-Item -LiteralPath $vendorHeaderPath -Destination (Join-Path $includeStage 'c-api.h')
        Copy-Item -LiteralPath $vendorModelPath -Destination (Join-Path $modelStage $ModelName)
        $sourceMode = 'vendored-third-party'
        Write-Output "Using vendored DPDFNet payload: $vendorRoot"
    }
    else {
        $archivePath = Join-Path $cacheRoot $SherpaArchiveName
        $modelPath = Join-Path $cacheRoot $ModelName
        Download-Verified $SherpaUrl $archivePath $SherpaSha256
        Download-Verified $ModelUrl $modelPath $ModelSha256

        $archiveExtractRoot = Join-Path $stageRoot 'sherpa'
        New-Item -ItemType Directory -Path $archiveExtractRoot -Force | Out-Null
        & tar.exe -xjf $archivePath -C $archiveExtractRoot
        if ($LASTEXITCODE -ne 0) { throw 'tar.exe failed to extract sherpa-onnx archive' }

        $packageRoot = Join-Path $archiveExtractRoot "sherpa-onnx-v$SherpaVersion-win-x64-shared-MD-Release-no-tts"
        if (!(Test-Path -LiteralPath $packageRoot -PathType Container)) {
            throw "Unexpected sherpa-onnx archive layout: $packageRoot"
        }

        foreach ($name in @('sherpa-onnx-c-api.dll')) {
            $source = Join-Path $packageRoot "lib/$name"
            if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing runtime file in archive: $name" }
            Copy-Item -LiteralPath $source -Destination (Join-Path $runtimeStage $name)
        }
        foreach ($name in @('onnxruntime.dll', 'onnxruntime_providers_shared.dll')) {
            $source = Join-Path $packageRoot "bin/$name"
            if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing runtime file in archive: $name" }
            Copy-Item -LiteralPath $source -Destination (Join-Path $runtimeStage $name)
        }

        $headerSource = Join-Path $packageRoot 'include/sherpa-onnx/c-api/c-api.h'
        if (!(Test-Path -LiteralPath $headerSource -PathType Leaf)) { throw 'Missing sherpa-onnx C API header' }
        Copy-Item -LiteralPath $headerSource -Destination (Join-Path $includeStage 'c-api.h')
        Copy-Item -LiteralPath $modelPath -Destination (Join-Path $modelStage $ModelName)
        $sourceMode = 'verified-download'
    }

    $metadata = [ordered]@{
        schema_version = 1
        source_mode = $sourceMode
        sherpa_onnx_version = $SherpaVersion
        sherpa_onnx_archive = $SherpaArchiveName
        sherpa_onnx_archive_sha256 = $SherpaSha256
        model = $ModelName
        model_sha256 = $ModelSha256
        runtime_files = @('sherpa-onnx-c-api.dll', 'onnxruntime.dll', 'onnxruntime_providers_shared.dll')
    }
    $metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stageRoot 'dpdfnet/metadata.json') -Encoding UTF8

    if ($Force -and (Test-Path -LiteralPath $preparedRoot)) {
        Remove-Item -LiteralPath $preparedRoot -Recurse -Force
    }
    elseif (Test-Path -LiteralPath $preparedRoot) {
        $existingMetadata = Join-Path $preparedRoot 'metadata.json'
        if (!(Test-Path -LiteralPath $existingMetadata -PathType Leaf)) {
            throw "Refusing to replace an unowned DPDFNet dependency directory: $preparedRoot"
        }
        Remove-Item -LiteralPath $preparedRoot -Recurse -Force
    }
    Move-Item -LiteralPath (Join-Path $stageRoot 'dpdfnet') -Destination $preparedRoot
    Write-Output "Prepared DPDFNet dependencies: $preparedRoot"
    Write-Output "  runtime: $(Join-Path $preparedRoot 'runtime')"
    Write-Output "  model:   $(Join-Path $preparedRoot "model/$ModelName")"
}
finally {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}
