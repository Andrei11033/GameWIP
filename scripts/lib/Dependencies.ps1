# Shared dependency-cache operations used by the project helper and setup utility.

Set-StrictMode -Version Latest

function Get-GameWipDependencyCacheRoot
{
    $cacheRoot = Resolve-GameWipRepositoryPath -Path ([string]$ProjectConfig.storage.cache)
    return [IO.Path]::GetFullPath((Join-Path $cacheRoot 'dependencies'))
}

function Get-GameWipDependencyLock
{
    $lockPath = Resolve-GameWipRepositoryPath -Path 'scripts/config/dependencies.json'
    return Read-GameWipJsonConfig `
        -Path $lockPath `
        -Name 'dependency lock'
}

function Get-GameWipDependencyCommit
{
    param([Parameter(Mandatory = $true)][string]$SourcePath)

    $gitCommand = Get-Command git -CommandType Application -ErrorAction Stop |
        Select-Object -First 1

    $commit = & $gitCommand.Source `
        -C $SourcePath `
        rev-parse HEAD `
        2>$null

    if ($LASTEXITCODE -ne 0)
    {
        return ''
    }

    return ([string]($commit | Select-Object -First 1)).Trim()
}

function Get-GameWipDependencyCacheStatus
{
    $lock = Get-GameWipDependencyLock
    $cacheRoot = Get-GameWipDependencyCacheRoot
    $manifestPath = Join-Path $cacheRoot 'cache-manifest.json'
    $manifest = $null

    if (Test-Path -LiteralPath $manifestPath -PathType Leaf)
    {
        $manifest = Read-GameWipJsonConfig `
            -Path $manifestPath `
            -Name 'dependency cache manifest'
    }

    $manifestDependencies = if ($null -ne $manifest -and $manifest.Contains('dependencies'))
    {
        $manifest.dependencies
    }
    else
    {
        @{}
    }

    $results = foreach ($entry in $lock.dependencies.GetEnumerator())
    {
        $dependencyId = [string]$entry.Key
        $dependency = $entry.Value
        $cacheDirectory = [string]$dependency.cacheDirectory

        $sourcePath = Join-Path `
            $cacheRoot `
            "fetchcontent\$cacheDirectory-src"

        $sourcePresent = Test-Path `
            -LiteralPath $sourcePath `
            -PathType Container

        $actualCommit = if ($sourcePresent)
        {
            Get-GameWipDependencyCommit -SourcePath $sourcePath
        }
        else
        {
            ''
        }

        $commitMatches = [string]::Equals(
            $actualCommit,
            [string]$dependency.commit,
            [StringComparison]::OrdinalIgnoreCase
        )

        $manifestEntry = if (
            $manifestDependencies -is [System.Collections.IDictionary] `
                -and $manifestDependencies.Contains($dependencyId)
        )
        {
            $manifestDependencies[$dependencyId]
        }
        else
        {
            $null
        }

        $manifestMatches = (
            $null -ne $manifestEntry `
                -and [string]$manifestEntry.repository -eq [string]$dependency.repository `
                -and [string]$manifestEntry.commit -eq [string]$dependency.commit
        )

        [PSCustomObject]@{
            Id = $dependencyId
            Repository = [string]$dependency.repository
            ExpectedCommit = [string]$dependency.commit
            ActualCommit = $actualCommit
            SourcePath = $sourcePath
            SourcePresent = $sourcePresent
            ManifestMatches = $manifestMatches
            CommitMatches = $commitMatches
            Ready = (
                $sourcePresent `
                    -and $manifestMatches `
                    -and $commitMatches
            )
        }
    }

    return @($results)
}

function Test-GameWipDependencyCache
{
    param([switch]$ThrowOnFailure)

    $cacheRoot = Get-GameWipDependencyCacheRoot
    $statuses = @(Get-GameWipDependencyCacheStatus)
    $invalid = @($statuses | Where-Object { -not $_.Ready })

    if ($invalid.Count -ne 0)
    {
        $details = @(
            $invalid | ForEach-Object {
                $reasons = @()

                if (-not $_.SourcePresent)
                {
                    $reasons += "source missing: $($_.SourcePath)"
                }

                if (-not $_.ManifestMatches)
                {
                    $reasons += 'cache manifest does not match dependencies.json'
                }

                if (-not $_.CommitMatches)
                {
                    $reasons += "source commit mismatch; expected $($_.ExpectedCommit), found $($_.ActualCommit)"
                }

                "  $($_.Id): $($reasons -join '; ')"
            }
        )

        $message = "GameWIP dependency cache is not ready:`n$($details -join "`n")"

        if ($ThrowOnFailure)
        {
            throw $message
        }

        Write-Host $message -ForegroundColor Yellow
        return $false
    }

    Write-GameWipSemanticText `
        -Object 'GameWIP dependency cache is ready:' `
        -Semantic Success `
        -NoNewline

    Write-Host ' ' -NoNewline

    Write-GameWipSemanticText `
        -Object $cacheRoot `
        -Semantic Muted
    return $true
}

function Invoke-GameWipDependencyPreparation
{
    $cacheRoot = Get-GameWipDependencyCacheRoot
    $prepareScript = Resolve-GameWipRepositoryPath `
        -Path 'cmake/PrepareGameWIPDependencies.cmake'

    New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
    # The lock is held by an exclusive file handle; a leftover file after a
    # crashed process is harmless because the next process can reopen it.
    $lockPath = Join-Path $cacheRoot '.prepare.lock'
    $lockStream = $null
    $lockDeadline = [DateTime]::UtcNow.AddMinutes(10)
    try
    {
        while ($null -eq $lockStream -and [DateTime]::UtcNow -lt $lockDeadline)
        {
            try
            {
                $lockStream = [IO.File]::Open(
                    $lockPath,
                    [IO.FileMode]::OpenOrCreate,
                    [IO.FileAccess]::ReadWrite,
                    [IO.FileShare]::None
                )
            }
            catch [IO.IOException]
            {
                Start-Sleep -Milliseconds 250
            }
        }

        if ($null -eq $lockStream)
        {
            throw (New-GameWipDiagnosticException `
                    -Code 'dependency-cache-lock-timeout' `
                    -Summary 'Timed out waiting for the dependency cache preparation lock.' `
                    -Details "Another dependency preparation process may still be active: $lockPath" `
                    -SuggestedActions @('Wait for the other dependency preparation operation to finish, then retry.', 'Inspect the retained helper run log if the lock remains unavailable.'))
        }

        $arguments = @(
            "-DGAMEWIP_DEPENDENCY_CACHE_DIR=$cacheRoot"
            '-P'
            $prepareScript
        )

        Invoke-GameWipNative `
            -Name 'dependencies-prepare' `
            -FilePath 'cmake' `
            -Arguments $arguments `
            -PathPrefix (Get-GameWipToolchainPathPrefix -PresetName 'dev') |
            Out-Null

        Test-GameWipDependencyCache -ThrowOnFailure
    }
    finally
    {
        if ($null -ne $lockStream)
        {
            $lockStream.Dispose()
        }
    }
}

function Get-GameWipDependencyPreparationPlan
{
    return @(
        'Read the pinned dependency lock file.',
        'Fetch or reuse Tracy and Google Benchmark in the shared local cache.',
        'Verify the resulting cache manifest.'
    )
}

function Invoke-GameWipDependencyPreparationOperation
{
    Invoke-GameWipMutation `
        -Summary 'Prepare the shared GameWIP dependency cache.' `
        -Risk local `
        -Plan (Get-GameWipDependencyPreparationPlan) `
        -Body {
        Invoke-GameWipDependencyPreparation | Out-Null
    } |
        Out-Null
}
