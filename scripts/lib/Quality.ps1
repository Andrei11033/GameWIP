# Repository quality orchestration. Checks return evidence; presentation is aggregated here.

# ------------------------------------------------------------
# Quality scope and policy selection
# ------------------------------------------------------------

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Quality scope discovery
# ------------------------------------------------------------

function Get-GameWipQualityTool
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $toolInfo = Get-GameWipProjectTool -Id $Id
    $status = Get-GameWipToolStatus -Tool $toolInfo
    if (-not $status.Ready)
    {
        $details = @("Tool state: $($status.State)") + @($status.DependencyFailures | ForEach-Object { "$($_.Package): $($_.State)" })
        throw (New-GameWipDiagnosticException `
                -Code 'quality-tool-unavailable' `
                -Summary "Quality tool '$Id' is missing or incompatible." `
                -Details ($details -join '; ') `
                -SuggestedActions @('.\gamewip.bat tools ensure quality -Yes', '.\gamewip.bat tools status'))
    }
    return [string]$status.Detected.Location
}

function Select-GameWipQualityFile
{
    param([string[]]$Files, [string[]]$Extensions = @(), [string[]]$ExactNames = @())
    return @($Files | Where-Object {
            $leaf = [IO.Path]::GetFileName($_)
            $extension = [IO.Path]::GetExtension($_).ToLowerInvariant()
            $ExactNames -contains $leaf -or $Extensions -contains $extension
        })
}


function Get-GameWipMaintainedWorktreeFile
{
    param([string[]]$Extensions = @(), [string[]]$ExactNames = @())
    $result = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'ls-files', '--cached', '--others', '--exclude-standard') -OutputMode LogOnly -TimeoutSeconds 30
    if ($result.ExitCode -ne 0)
    {
        throw 'Could not enumerate maintained worktree files for repository quality.'
    }
    return @($result.Stdout |
            ForEach-Object { ([string]$_).Replace('\', '/') } |
            Where-Object { $_ -and $_ -notmatch '^(?:external|build|install|docs/releases)/' } |
            Where-Object { Test-Path -LiteralPath (Join-Path $RepositoryRoot $_) -PathType Leaf } |
            Where-Object {
                $leaf = [IO.Path]::GetFileName($_)
                $extension = [IO.Path]::GetExtension($_).ToLowerInvariant()
                $ExactNames -contains $leaf -or $Extensions -contains $extension
            } |
            Sort-Object -Unique)
}

function Test-GameWipQualityPolicyChange
{
    param([string[]]$Files)
    foreach ($file in @($Files))
    {
        $path = ([string]$file).Replace('\', '/')
        if ($path -in @('.clang-format', '.clang-tidy', '.editorconfig', 'scripts/config/commands.json', 'scripts/config/project-tools.json') -or
            $path.StartsWith('config/quality/', [StringComparison]::OrdinalIgnoreCase))
        {
            return $true
        }
    }
    return $false
}

function Get-GameWipQualityScope
{
    param([switch]$Changed)
    if (-not $Changed)
    {
        return [pscustomobject]@{ Requested = $false; UseChanged = $false; Expanded = $false; Files = @() }
    }
    $files = @(Get-GameWipChangedRepositoryFile)
    $expanded = Test-GameWipQualityPolicyChange -Files $files
    return [pscustomobject]@{
        Requested = $true
        UseChanged = -not $expanded
        Expanded = $expanded
        Files = @($files)
    }
}

function Get-GameWipPowerShellFile
{
    param([string[]]$Files)
    $extensions = @('.ps1', '.psd1', '.psm1')
    $selected = if ($null -ne $Files -and $Files.Count -ne 0)
    {
        @($Files)
    }
    else
    {
        @(Get-GameWipMaintainedWorktreeFile -Extensions $extensions)
    }
    return @($selected |
            ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
            Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and $extensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() } |
            ForEach-Object { Get-Item -LiteralPath $_ })
}

function Get-GameWipCMakeFile
{
    param([string[]]$Files)
    $selected = if ($null -ne $Files -and $Files.Count -ne 0)
    {
        @($Files)
    }
    else
    {
        @(Get-GameWipMaintainedWorktreeFile -ExactNames @('CMakeLists.txt') -Extensions @('.cmake', '.in'))
    }
    return @($selected |
            ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
            Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and ((Split-Path -Leaf $_) -eq 'CMakeLists.txt' -or $_ -match '\.cmake(?:\.in)?$') } |
            Sort-Object -Unique)
}

function Invoke-GameWipLineEndingNormalization
{
    # Formatters may choose their own platform newline or preserve trailing
    # whitespace. Apply the repository's text policy after every formatter has finished.
    param([string[]]$Files = @())

    $candidates = @(
        if ($null -ne $Files -and @($Files).Count -ne 0)
        {
            $Files |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Sort-Object -Unique
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Sort-Object -Unique
        }
    )

    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $normalizedCount = 0
    $skippedCount = 0
    foreach ($file in $candidates)
    {
        $bytes = [IO.File]::ReadAllBytes($file)
        if ($bytes -contains [byte]0)
        {
            $skippedCount++
            continue
        }

        try
        {
            $text = $utf8.GetString($bytes)
        }
        catch [Text.DecoderFallbackException]
        {
            $skippedCount++
            continue
        }

        $normalized = $text.Replace("`r`n", "`n").Replace("`r", "`n")
        $normalized = [regex]::Replace($normalized, '(?m)[ \t]+$', '')
        if ($normalized -cne $text)
        {
            Write-GameWipTextAtomic -Path $file -Content $normalized
            Add-GameWipOperationChange -Message "Normalized text whitespace in $(Get-GameWipRepositoryRelativePath -Path $file)"
            $normalizedCount++
        }
    }

    Write-GameWipStatusLine `
        -Status pass `
        -Text "Text normalization: checked $($candidates.Count) maintained file(s), normalized LF/trailing whitespace in $normalizedCount, skipped $skippedCount non-text/binary file(s)." `
        -Semantic Success `
        -Indent 2
}

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
    Invoke-GameWipNative -Name $Name -FilePath $FilePath -Arguments $Arguments -Environment $Environment | Out-Null
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
    $moduleRoot = Get-GameWipNpmGlobalModuleRoot
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
    $paths = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($arguments in @(
            @('-C', $RepositoryRoot, 'diff', '--name-only', '--diff-filter=ACMR', 'HEAD'),
            @('-C', $RepositoryRoot, 'diff', '--cached', '--name-only', '--diff-filter=ACMR'),
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

# ------------------------------------------------------------
# Check and fix workflows
# ------------------------------------------------------------

function Get-GameWipQualityToolchain
{
    $requiredToolIds = @(
        'python',
        'clang-format',
        'ruff',
        'psscriptanalyzer',
        'eslint',
        'prettier',
        'gersemi',
        'yamllint',
        'markdownlint-cli2',
        'actionlint',
        'jsonschema'
    )
    # The common checker also validates provider dependencies, such as npm
    # plugins, before quality mutates any maintained file.
    $check = Test-GameWipToolchain `
        -ToolIds $requiredToolIds `
        -DisplayStatus `
        -ThrowOnFailure `
        -FailureCode quality-toolchain-incomplete `
        -FailureSummary 'The declared quality toolchain is incomplete.' `
        -SuggestedActions @('.\gamewip.bat tools ensure quality -Yes', '.\gamewip.bat tools status')
    return $check.Tools
}

function Invoke-GameWipQualityCheck
{
    param(
        [switch]$FailFast,
        [switch]$Changed,
        [AllowNull()]$ScopeInfo = $null,
        [AllowNull()][hashtable]$Tools = $null
    )

    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    if ($null -eq $ScopeInfo)
    {
        $ScopeInfo = Get-GameWipQualityScope -Changed:$Changed
    }
    $scope = @($ScopeInfo.Files)
    $useChangedScope = [bool]$ScopeInfo.UseChanged
    if ($ScopeInfo.Requested -and $scope.Count -eq 0)
    {
        Write-GameWipSection 'Quality summary'
        Write-GameWipStatusLine `
            -Status PASS `
            -Text 'No changed maintained files.' `
            -Semantic Success `
            -Indent 2 `
            -MarkerWidth 6
        return
    }
    if ($ScopeInfo.Expanded)
    {
        Write-GameWipOperationEvent -Phase plan -Severity info -Message 'A quality policy/configuration file changed; expanding quality scope to the complete maintained worktree.'
    }

    if ($null -eq $Tools)
    {
        $Tools = Get-GameWipQualityToolchain
    }
    $tools = $Tools

    $cppFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl')
        })
    $pythonFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.py')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.py')
        })
    $powerShellFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.ps1', '.psd1', '.psm1')
        })
    $jsFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.js', '.mjs', '.cjs')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.js', '.mjs', '.cjs')
        })
    $eslintConfigFile = 'config/quality/eslint.config.js'
    if ($jsFiles.Count -ne 0 -and $jsFiles -notcontains $eslintConfigFile)
    {
        $jsFiles = @($eslintConfigFile) + $jsFiles
    }
    # PowerShell uses the guarded candidate/fallback path in the fix workflow;
    # the regular Prettier command must never write directly to repository PS files.
    $prettierFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
        })
    $specialJsonFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
        })
    $cmakeFiles = @(if ($useChangedScope)
        {
            Get-GameWipCMakeFile -Files $scope
        }
        else
        {
            Get-GameWipCMakeFile
        })
    $yamlFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.yml', '.yaml')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.yml', '.yaml')
        })
    $markdownFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.md')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.md')
        })

    $nodePath = Get-GameWipNpmGlobalModuleRoot
    $prettierPluginArguments = Get-GameWipPrettierPluginArguments
    $python = (Resolve-GameWipPython).Path
    $managedPython = Get-GameWipPythonEnvironmentInterpreterPath -Root (Join-Path (Get-GameWipManagedToolRoot) 'python')
    if (Test-Path -LiteralPath $managedPython)
    {
        $python = $managedPython
    }

    $checks = @(
        @{ Name = 'clang-format'; Body = { if (-not $useChangedScope -or $cppFiles.Count -ne 0)
                {
                    Invoke-GameWipFormat -Mode check -Files $cppFiles
                } }
        },
        @{ Name = 'ruff lint'; Body = { if (-not $useChangedScope -or $pythonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name ruff-check -FilePath $tools.ruff -Arguments (@('check', '--config', (Join-Path $qualityConfig 'ruff.toml')) + $pythonFiles)
                } }
        },
        @{ Name = 'ruff format'; Body = { if (-not $useChangedScope -or $pythonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name ruff-format-check -FilePath $tools.ruff -Arguments (@('format', '--check', '--config', (Join-Path $qualityConfig 'ruff.toml')) + $pythonFiles)
                } }
        },
        @{ Name = 'PowerShell'; Body = { if (-not $useChangedScope -or $powerShellFiles.Count -ne 0)
                {
                    Invoke-GameWipPowerShellQuality -Files $powerShellFiles
                } }
        },
        @{ Name = 'ESLint'; Body = { if (-not $useChangedScope -or $jsFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name eslint -FilePath $tools.eslint -Arguments (@('--config', (Join-Path $qualityConfig 'eslint.config.js')) + $jsFiles) -Environment @{ NODE_PATH = $nodePath }
                } }
        },
        @{ Name = 'Prettier'; Body = { if (-not $useChangedScope -or $prettierFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name prettier-check -FilePath $tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--ignore-path', (Join-Path $qualityConfig 'prettier.ignore'), '--check') + $prettierPluginArguments + $prettierFiles)
                } }
        },
        @{ Name = 'Prettier special JSON'; Body = { if (-not $useChangedScope -or $specialJsonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name prettier-special-json-check -FilePath $tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json')) + $prettierPluginArguments + @('--parser', 'json', '--check') + $specialJsonFiles)
                } }
        },
        @{ Name = 'Gersemi'; Body = { if (-not $useChangedScope -or $cmakeFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name gersemi-check -FilePath $tools.gersemi -Arguments (@('--config', (Join-Path $qualityConfig 'gersemi.yml'), '--check') + $cmakeFiles)
                } }
        },
        @{ Name = 'yamllint'; Body = { if (-not $useChangedScope -or $yamlFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name yamllint -FilePath $tools.yamllint -Arguments (@('-c', (Join-Path $qualityConfig 'yamllint.yml')) + $yamlFiles)
                } }
        },
        @{ Name = 'actionlint'; Body = { Invoke-GameWipQualityNative -Name actionlint -FilePath $tools.actionlint -Arguments @('-color') } },
        @{ Name = 'markdownlint'; Body = { if (-not $useChangedScope -or $markdownFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name markdownlint -FilePath $tools.'markdownlint-cli2' -Arguments (@('--config', (Join-Path $qualityConfig 'markdownlint-cli2.jsonc')) + $markdownFiles)
                } }
        },
        @{ Name = 'JSON Schema'; Body = { Invoke-GameWipQualityNative -Name schema-validation -FilePath $python -Arguments @('.github/scripts/validate_config_schemas.py') } },
        @{ Name = 'quality ownership'; Body = { Invoke-GameWipQualityNative -Name quality-ownership -FilePath $python -Arguments @('.github/scripts/check_quality_ownership.py') } },
        @{ Name = 'helper standardization'; Body = { Invoke-GameWipQualityNative -Name helper-standardization -FilePath $python -Arguments @('.github/scripts/check_helper_standardization.py') } },
        @{ Name = 'repository standards'; Body = { Invoke-GameWipQualityNative -Name repository-standards -FilePath $python -Arguments @('.github/scripts/check_repository_standards.py') } },
        @{ Name = 'repository standards tests'; Body = { Invoke-GameWipQualityNative -Name repository-standards-tests -FilePath $python -Arguments @('-m', 'unittest', 'discover', '-s', '.github/scripts', '-p', 'test_*.py') } },
        @{ Name = 'documentation standards'; Body = { Invoke-GameWipQualityNative -Name documentation-standards -FilePath $python -Arguments @('.github/scripts/check_documentation_standards.py') } },
        @{ Name = 'Markdown links'; Body = { Invoke-GameWipMarkdownLink } }
    )

    $results = [System.Collections.Generic.List[object]]::new()
    $total = $checks.Count
    for ($index = 0; $index -lt $total; ++$index)
    {
        $check = $checks[$index]
        Assert-GameWipNotCancelled
        Write-GameWipOperationEvent -Phase execute -Step "quality-$($index + 1)-of-$total" -Severity progress -Message "[$($index + 1)/$total] $($check.Name)"
        $clock = [Diagnostics.Stopwatch]::StartNew()
        try
        {
            & $check.Body
            $clock.Stop()
            $results.Add([pscustomobject]@{ Name = $check.Name; Status = 'PASS'; Duration = $clock.Elapsed.TotalSeconds; Error = '' }) | Out-Null
        }
        catch
        {
            $clock.Stop()
            $results.Add([pscustomobject]@{ Name = $check.Name; Status = 'FAIL'; Duration = $clock.Elapsed.TotalSeconds; Error = $_.Exception.Message }) | Out-Null
            if ($FailFast)
            {
                break
            }
        }
    }

    Write-GameWipSection 'Quality summary'
    foreach ($result in $results)
    {
        $semantic = if ($result.Status -eq 'PASS')
        {
            'Success'
        }
        else
        {
            'Failure'
        }

        $details = '{0,-28} {1,7:N2}s {2}' -f $result.Name, $result.Duration, $result.Error
        Write-GameWipStatusLine `
            -Status $result.Status `
            -Text $details `
            -Semantic $semantic `
            -Indent 2 `
            -MarkerWidth 6
    }
    $failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
    if ($failed.Count -ne 0)
    {
        throw (New-GameWipDiagnosticException -Code quality-failed -Summary "$($failed.Count) quality check(s) failed." -Details (($failed | ForEach-Object { "$($_.Name): $($_.Error)" }) -join "`n") -SuggestedActions @('Fix every failure listed in the quality summary.', "Use '-FailFast' only when debugging one check at a time."))
    }
}

function Invoke-GameWipQualityFix
{
    param(
        [Parameter(Mandatory = $true)]$ScopeInfo,
        [Parameter(Mandatory = $true)][hashtable]$Tools
    )

    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    $python = (Resolve-GameWipPython).Path
    $scope = @($ScopeInfo.Files)
    $useChangedScope = [bool]$ScopeInfo.UseChanged
    if ($ScopeInfo.Requested -and $scope.Count -eq 0)
    {
        return
    }

    $sourceDocumentationArguments = [System.Collections.Generic.List[string]]::new()
    $sourceDocumentationArguments.Add('.github/scripts/check_repository_standards.py') | Out-Null
    $sourceDocumentationArguments.Add('--fix-source-documentation') | Out-Null
    if ($useChangedScope)
    {
        foreach ($file in $scope)
        {
            $sourceDocumentationArguments.Add([string]$file.FullName) | Out-Null
        }
    }
    Invoke-GameWipQualityNative `
        -Name source-documentation-format `
        -FilePath $python `
        -Arguments $sourceDocumentationArguments.ToArray()

    $cppFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl')
        })
    $pythonFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.py')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.py')
        })
    $powerShellFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.ps1', '.psd1', '.psm1')
        })
    $prettierFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
        })
    $specialJsonFiles = @(if ($useChangedScope)
        {
            Select-GameWipQualityFile -Files $scope -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
        }
        else
        {
            Get-GameWipMaintainedWorktreeFile -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
        })
    $cmakeFiles = @(if ($useChangedScope)
        {
            Get-GameWipCMakeFile -Files $scope
        }
        else
        {
            Get-GameWipCMakeFile
        })
    $prettierPluginArguments = Get-GameWipPrettierPluginArguments

    $ruff = $Tools.ruff
    if (-not $useChangedScope)
    {
        Invoke-GameWipFormat -Mode apply
    }
    elseif ($cppFiles.Count -ne 0)
    {
        Invoke-GameWipFormat -Mode apply -Files $cppFiles
    }
    if ($pythonFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name ruff-fix -FilePath $ruff -Arguments (@('check', '--fix', '--config', (Join-Path $qualityConfig 'ruff.toml')) + $pythonFiles)
        Invoke-GameWipQualityNative -Name ruff-format -FilePath $ruff -Arguments (@('format', '--config', (Join-Path $qualityConfig 'ruff.toml')) + $pythonFiles)
    }
    if ($prettierFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name prettier-write -FilePath $Tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--ignore-path', (Join-Path $qualityConfig 'prettier.ignore'), '--write') + $prettierPluginArguments + $prettierFiles)
    }
    if ($specialJsonFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name prettier-special-json-write -FilePath $Tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json')) + $prettierPluginArguments + @('--parser', 'json', '--write') + $specialJsonFiles)
    }
    if ($cmakeFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name gersemi-format -FilePath $Tools.gersemi -Arguments (@('--config', (Join-Path $qualityConfig 'gersemi.yml'), '--in-place') + $cmakeFiles)
    }
    if (-not $useChangedScope -or $powerShellFiles.Count -ne 0)
    {
        Invoke-GameWipPowerShellFormat -Prettier $Tools.prettier -Files $powerShellFiles
        Invoke-GameWipPowerShellQuality -Files $powerShellFiles
    }

    $lineEndingFiles = if ($useChangedScope)
    {
        $scope
    }
    else
    {
        @()
    }
    Invoke-GameWipLineEndingNormalization -Files $lineEndingFiles
}

function Invoke-GameWipQuality
{
    param(
        [ValidateSet('check', 'fix')][string]$Mode = 'check',
        [switch]$FailFast,
        [switch]$Changed
    )
    Initialize-GameWipStorage
    $scopeInfo = Get-GameWipQualityScope -Changed:$Changed
    $qualityTools = $null
    if ($Mode -eq 'fix')
    {
        $qualityTools = Get-GameWipQualityToolchain
        Invoke-GameWipQualityFix -ScopeInfo $scopeInfo -Tools $qualityTools
    }
    Invoke-GameWipQualityCheck -FailFast:$FailFast -Changed:$Changed -ScopeInfo $scopeInfo -Tools $qualityTools
}
