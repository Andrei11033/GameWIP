# Source formatting and formatter validation.

# ------------------------------------------------------------
# Quality tool execution
# ------------------------------------------------------------

function Invoke-GameWipQualityNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [hashtable]$Environment = @{}
    )
    $pathPrefix = @(Get-GameWipManagedToolPath) -join [IO.Path]::PathSeparator
    Invoke-GameWipNative -Name $Name -FilePath $FilePath -Arguments $Arguments -PathPrefix $pathPrefix -Environment $Environment | Out-Null
}

function Invoke-GameWipGersemi
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('check', 'in-place')][string]$Mode,
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$Files = @()
    )

    $batchSize = 40
    $batchCount = [int][Math]::Ceiling($Files.Count / [double]$batchSize)
    $configuration = Join-Path $RepositoryRoot 'config\quality\gersemi.yml'

    # Keep each invocation below Windows command-line limits.
    for ($offset = 0; $offset -lt $Files.Count; $offset += $batchSize)
    {
        $last = [Math]::Min($offset + $batchSize - 1, $Files.Count - 1)
        $batch = @($Files[$offset..$last])
        $arguments = [System.Collections.Generic.List[string]]::new()
        $arguments.Add('--config')
        $arguments.Add($configuration)
        $arguments.Add("--$Mode")

        foreach ($file in $batch)
        {
            $arguments.Add($file)
        }

        $batchNumber = [int]($offset / $batchSize) + 1
        Invoke-GameWipQualityNative `
            -Name "gersemi-$Mode-$batchNumber-of-$batchCount" `
            -FilePath $Executable `
            -Arguments $arguments.ToArray()
    }
}

function Get-GameWipPrettierPluginArguments
{
    $toolInfo = Get-GameWipProjectTool -Id prettier
    $pluginDependency = @($toolInfo.provider.dependencies | Where-Object { $_.package -eq 'prettier-plugin-powershell' }) | Select-Object -First 1

    if ($null -eq $pluginDependency)
    {
        return @()
    }

    # Resolve the managed plugin explicitly because Prettier's ESM loader does
    # not reliably find globally installed package-name plugins from the repo.
    $moduleRoot = Get-GameWipNpmManagedModuleRoot
    $pluginEntry = Get-GameWipNpmInstalledPackageEntryPath `
        -ModuleRoot $moduleRoot `
        -Package ([string]$pluginDependency.package)
    return @('--plugin', $pluginEntry)
}

function Invoke-GameWipPowerShellQuality
{
    param([string[]]$Files)
    $moduleRoot = Get-GameWipQualityTool -Id psscriptanalyzer
    Import-Module (Join-Path $moduleRoot 'PSScriptAnalyzer.psd1') -Force
    $settings = Join-Path $RepositoryRoot 'config\quality\psscriptanalyzer.psd1'
    $targets = @(Get-GameWipPowerShellFile -Files $Files)
    $analysisFailures = [System.Collections.Generic.List[object]]::new()

    foreach ($file in $targets)
    {
        foreach ($finding in @(Invoke-ScriptAnalyzer -Path $file.FullName -Settings $settings -Severity Warning, Error))
        {
            $analysisFailures.Add($finding) | Out-Null
        }
    }

    if ($analysisFailures.Count -ne 0)
    {
        $analysisFailures | Format-Table -AutoSize | Out-Host
        throw "PSScriptAnalyzer reported $($analysisFailures.Count) warning/error finding(s)."
    }
}

function Test-GameWipPowerShellSyntax
{
    param(
        [Parameter(Mandatory = $true)][string]$ScriptDefinition,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseInput($ScriptDefinition, [ref]$tokens, [ref]$errors) | Out-Null
    $details = @($errors | ForEach-Object {
            '{0}:{1}:{2} {3}' -f $Path, $_.Extent.StartLineNumber, $_.Extent.StartColumnNumber, $_.Message
        })
    return [pscustomobject]@{
        Valid = $errors.Count -eq 0
        Details = $details
    }
}

function Invoke-GameWipPowerShellFormat
{
    param(
        [Parameter(Mandatory = $true)][string]$Prettier,
        [string[]]$Files
    )

    $moduleRoot = Get-GameWipQualityTool -Id psscriptanalyzer
    Import-Module (Join-Path $moduleRoot 'PSScriptAnalyzer.psd1') -Force
    $settings = Join-Path $RepositoryRoot 'config\quality\psscriptanalyzer.psd1'
    $targets = @(Get-GameWipPowerShellFile -Files $Files)

    if ($targets.Count -eq 0)
    {
        return
    }

    if ($null -eq $Script:OperationContext -or [string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        throw 'PowerShell formatting requires an initialized GameWIP operation-temp directory.'
    }

    $candidateRoot = Join-Path $Script:OperationContext.Temp 'powershell-format-candidates'
    New-Item -ItemType Directory -Path $candidateRoot | Out-Null
    $candidates = [System.Collections.Generic.List[object]]::new()

    for ($index = 0; $index -lt $targets.Count; ++$index)
    {
        $target = $targets[$index]
        $candidatePath = Join-Path $candidateRoot ('{0:D4}-{1}' -f $index, $target.Name)
        Copy-Item -LiteralPath $target.FullName -Destination $candidatePath
        $candidates.Add([pscustomobject]@{ Target = $target; Candidate = $candidatePath }) | Out-Null
    }

    $pluginArguments = Get-GameWipPrettierPluginArguments
    $candidatePaths = @($candidates | ForEach-Object { $_.Candidate })
    $prettierSucceeded = $true

    try
    {
        Invoke-GameWipQualityNative `
            -Name prettier-powershell-candidate `
            -FilePath $Prettier `
            -Arguments (@('--config', (Join-Path $RepositoryRoot 'config\quality\prettier.json'), '--write') + $pluginArguments + $candidatePaths)
    }

    catch
    {
        $prettierSucceeded = $false
        Write-GameWipOperationEvent `
            -Phase execute `
            -Severity warning `
            -Message "Prettier could not produce PowerShell candidates; using Invoke-Formatter fallback. $($_.Exception.Message)"
    }

    $acceptedPrettierCount = 0
    $fallbackCount = 0
    $changedCount = 0

    foreach ($candidate in $candidates)
    {
        $target = $candidate.Target
        $source = Read-GameWipUtf8Text -Path $target.FullName
        $formatted = $null

        if ($prettierSucceeded -and (Test-Path -LiteralPath $candidate.Candidate -PathType Leaf))
        {
            $prettierText = Read-GameWipUtf8Text -Path $candidate.Candidate
            $prettierSyntax = Test-GameWipPowerShellSyntax -ScriptDefinition $prettierText -Path $target.FullName

            if ($prettierSyntax.Valid)
            {
                try
                {
                    $formatted = [string](Invoke-Formatter -ScriptDefinition $prettierText -Settings $settings)
                    $finalSyntax = Test-GameWipPowerShellSyntax -ScriptDefinition $formatted -Path $target.FullName

                    if (-not $finalSyntax.Valid)
                    {
                        $formatted = $null
                    }
                    else
                    {
                        $acceptedPrettierCount++
                    }
                }

                catch
                {
                    $formatted = $null
                }
            }
        }

        if ($null -eq $formatted)
        {
            $fallbackCount++
            $formatted = [string](Invoke-Formatter -ScriptDefinition $source -Settings $settings)
            $fallbackSyntax = Test-GameWipPowerShellSyntax -ScriptDefinition $formatted -Path $target.FullName

            if (-not $fallbackSyntax.Valid)
            {
                throw (New-GameWipDiagnosticException `
                        -Code 'powershell-format-invalid' `
                        -Summary "PowerShell formatter produced invalid output for '$($target.FullName)'." `
                        -Details ($fallbackSyntax.Details -join "`n") `
                        -SuggestedActions @('Inspect the retained quality log.', 'Run PSScriptAnalyzer directly on the affected file.'))
            }
        }

        if ($formatted -cne $source)
        {
            Write-GameWipTextAtomic -Path $target.FullName -Content $formatted
            Add-GameWipOperationChange -Message "Formatted PowerShell source in $(Get-GameWipRepositoryRelativePath -Path $target.FullName)"
            $changedCount++
        }
    }

    Write-GameWipStatusLine `
        -Status pass `
        -Text "PowerShell formatting: checked $($targets.Count) file(s), accepted Prettier output for $acceptedPrettierCount, used Invoke-Formatter fallback for $fallbackCount, changed $changedCount." `
        -Semantic Success `
        -Indent 2
}

function Get-GameWipChangedRepositoryFile
{
    $paths = [System.Collections.Generic.HashSet[string]]::new((Get-GameWipPathComparer))

    foreach ($arguments in @(
            @('-C', $RepositoryRoot, 'diff', '--name-only', '--diff-filter=ACMR', 'HEAD'),
            @('-C', $RepositoryRoot, 'ls-files', '--others', '--exclude-standard')
        ))
    {
        $result = Invoke-GameWipProcess -FilePath git -Arguments $arguments -OutputMode LogOnly -TimeoutSeconds 30

        if ($result.ExitCode -ne 0)
        {
            throw 'Could not determine the changed-file quality scope.'
        }

        foreach ($line in $result.Stdout)
        {
            $path = ([string]$line).Trim().Replace('\', '/')

            if (-not [string]::IsNullOrWhiteSpace($path))
            {
                $paths.Add($path) | Out-Null
            }
        }
    }

    return @($paths | Sort-Object)
}

function Show-GameWipQualityCoverageStatus
{
    $python = (Resolve-GameWipPython).Path
    Invoke-GameWipNative -Name quality-ownership-status -FilePath $python -Arguments @('.github/scripts/check_quality_ownership.py', '--status')
}
