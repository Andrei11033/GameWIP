Set-StrictMode -Version Latest

function Invoke-GameWipMsys2
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string]$Command)
    $bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash)) { throw "MSYS2 bash was not found at $bash." }
    $env:CHERE_INVOKING = '1'
    Invoke-GameWipSetupNative -FilePath $bash -ArgumentList @('-lc', $Command) | Out-Null
}

function Invoke-GameWipMsys2PacmanWithRetry
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string]$Command,
        [ValidateRange(1, 10)][int]$MaxAttempts = 3,
        [ValidateRange(0, 60)][int]$RetryDelaySeconds = 2
    )
    for ($attempt = 1; $attempt -le $MaxAttempts; ++$attempt)
    {
        Write-Host "  pacman attempt $attempt of $MaxAttempts..." -ForegroundColor DarkGray
        try { Invoke-GameWipMsys2 -MsysRoot $MsysRoot -Command $Command; return }
        catch
        {
            if ($attempt -ge $MaxAttempts) { Write-Warning "pacman failed after $MaxAttempts attempts."; throw }
            Write-Warning "pacman attempt $attempt of $MaxAttempts failed: $($_.Exception.Message)"
            if ($RetryDelaySeconds -gt 0) { Start-Sleep -Seconds $RetryDelaySeconds }
        }
    }
}

function Get-GameWipMissingMsys2Package
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string[]]$Packages)
    $pacman = Join-Path $MsysRoot 'usr\bin\pacman.exe'
    if (-not (Test-Path -LiteralPath $pacman)) { return $Packages }
    $installedPackages = & $pacman -Qq
    if ($LASTEXITCODE -ne 0) { return $Packages }
    $installed = [System.Collections.Generic.HashSet[string]]::new([string[]]$installedPackages)
    return @($Packages | Where-Object { -not $installed.Contains($_) })
}

function Test-GameWipMsys2PackageSet
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string[]]$Packages)
    return @(Get-GameWipMissingMsys2Package -MsysRoot $MsysRoot -Packages $Packages).Count -eq 0
}

function Get-GameWipMsys2PackageConfig
{
    param([Parameter(Mandatory = $true)][hashtable]$ProjectTools)
    $result = @{ Common = @(); Ucrt64 = @(); Clang64 = @() }
    foreach ($toolInfo in @($ProjectTools.tools | Where-Object { $_.provider.kind -eq 'msys2' }))
    {
        $key = switch ([string]$toolInfo.provider.environment)
        {
            'common' { 'Common' }
            'ucrt64' { 'Ucrt64' }
            'clang64' { 'Clang64' }
            default { throw "Unknown MSYS2 environment '$($toolInfo.provider.environment)' for '$($toolInfo.id)'." }
        }
        $result[$key] += [string]$toolInfo.provider.package
        $providerDependencies = if ($toolInfo.provider.Contains('dependencies')) { @($toolInfo.provider.dependencies) } else { @() }
        foreach ($dependency in $providerDependencies)
        {
            $dependencyKey = switch ([string]$dependency.environment)
            {
                'common' { 'Common' }
                'ucrt64' { 'Ucrt64' }
                'clang64' { 'Clang64' }
                default { throw "Unknown MSYS2 dependency environment '$($dependency.environment)' for '$($toolInfo.id)'." }
            }
            $result[$dependencyKey] += [string]$dependency.package
        }
    }
    foreach ($key in @('Common', 'Ucrt64', 'Clang64')) { $result[$key] = @($result[$key] | Sort-Object -Unique) }
    return $result
}

function Install-GameWipMsys2PackageSet
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][hashtable]$PackageConfig, [switch]$Update)
    if ($Update)
    {
        Write-Host 'Updating the complete MSYS2 package database and system...'
        Invoke-GameWipMsys2PacmanWithRetry -MsysRoot $MsysRoot -Command 'pacman -Syu --noconfirm'
        Invoke-GameWipMsys2PacmanWithRetry -MsysRoot $MsysRoot -Command 'pacman -Syu --noconfirm'
    }
    $packages = @(@($PackageConfig.Common) + @($PackageConfig.Ucrt64) + @($PackageConfig.Clang64) | Sort-Object -Unique)
    Invoke-GameWipMsys2PacmanWithRetry -MsysRoot $MsysRoot -Command "pacman --needed --noconfirm -S $($packages -join ' ')"
    if (-not (Test-GameWipMsys2PackageSet -MsysRoot $MsysRoot -Packages $packages)) { throw 'One or more required MSYS2 packages failed verification.' }
}

function Test-GameWipMsys2Toolchain
{
    param([Parameter(Mandatory = $true)][hashtable]$ProjectTools)
    foreach ($toolInfo in @($ProjectTools.tools | Where-Object { $_.provider.kind -eq 'msys2' -and $_.capabilities.detectInstalled }))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $compatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected
        if ($compatibility -ne 'compatible') { throw "MSYS2 requirement '$($toolInfo.id)' is '$compatibility'." }
    }
    $cmake = $ProjectTools.tools | Where-Object { $_.id -eq 'cmake' } | Select-Object -First 1
    $detectedCMake = Get-GameWipDetectedTool -Tool $cmake
    Write-Host "  Ready: registry-declared UCRT64/CLANG64 requirements (CMake $($detectedCMake.Version))"
}
