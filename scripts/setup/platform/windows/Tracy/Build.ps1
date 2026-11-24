# Windows Tracy build orchestration.

# ------------------------------------------------------------
# Build and install verified Tracy tools
# ------------------------------------------------------------

function Invoke-GameWipTracyToolBuild
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot
    )

    $version = Get-GameWipTracyVersion

    if (Test-GameWipTracyToolSet)
    {
        Write-Host "  Ready: complete Tracy tool set for locked client $version"
        return
    }

    $ucrtBin = Join-Path $MsysRoot 'ucrt64\bin'
    $cmake = Join-Path $ucrtBin 'cmake.exe'

    if (-not (Test-Path -LiteralPath $cmake))
    {
        throw "UCRT64 CMake is required to build Tracy tools: $cmake"
    }

    $compilers = @(
        (Join-Path $ucrtBin 'gcc.exe'),
        (Join-Path $ucrtBin 'g++.exe'),
        (Join-Path $ucrtBin 'windres.exe'),
        (Join-Path $ucrtBin 'ninja.exe'),
        (Join-Path $ucrtBin 'objdump.exe')
    )

    foreach ($compiler in $compilers)
    {
        if (-not (Test-Path -LiteralPath $compiler))
        {
            throw "The UCRT64 Tracy build toolchain is incomplete; missing $compiler"
        }
    }
    $tracyRoot = Get-GameWipTracySourcePath
    $setupRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'tracy'
    $buildRoot = Join-Path $setupRoot 'ucrt64'
    $stageRoot = Join-Path $Script:OperationContext.Temp 'tracy-stage'
    $cacheRoot = Join-Path $setupRoot 'cpm-cache'
    $destination = Get-GameWipTracyToolRoot
    Write-Host "  Source: $tracyRoot"
    Write-Host "  Build trees: $buildRoot"
    Write-Host "  Staging: $stageRoot"
    Write-Host "  Verified destination: $destination"

    # Rebuild into operation-owned staging; the persistent installation changes
    # only after every project output and runtime dependency has been verified.
    if (Test-Path -LiteralPath $stageRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $stageRoot -OwnedRoot $Script:OperationContext.Temp -SuppressMutationTracking
    }
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    $compilerCompatibilityHeader = Write-GameWipTracyCompilerCompatibilityHeader -SetupRoot $setupRoot
    Invoke-GameWipTracyProjectBuild `
        -Version $version `
        -UcrtBin $ucrtBin `
        -CMake $cmake `
        -TracyRoot $tracyRoot `
        -BuildRoot $buildRoot `
        -StageRoot $stageRoot `
        -CacheRoot $cacheRoot `
        -CompilerCompatibilityHeader $compilerCompatibilityHeader

    Install-GameWipTracyToolSet `
        -StageRoot $stageRoot `
        -Destination $destination `
        -Version $version
}
