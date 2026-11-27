# Repository quality gates and fixes.

# ------------------------------------------------------------
# Check and fix workflows
# ------------------------------------------------------------

function Get-GameWipQualityToolchain
{
    param([AllowNull()]$ScopeInfo = $null)

    $requiredToolIds = if ($null -ne $ScopeInfo)
    {
        (Get-GameWipQualityCheckPlan -ScopeInfo $ScopeInfo).Tools
    }
    else
    {
        (Get-GameWipQualityCheckPlan -ScopeInfo ([pscustomobject]@{ UseChanged = $false })).Tools
    }

    $helperCommand = Get-GameWipProjectHelperCommand
    # The common checker also validates provider dependencies, such as npm
    # plugins, before quality mutates any maintained file.
    $check = Test-GameWipToolchain `
        -ToolIds $requiredToolIds `
        -DisplayStatus `
        -ThrowOnFailure `
        -FailureCode quality-toolchain-incomplete `
        -FailureSummary 'The declared quality toolchain is incomplete.' `
        -SuggestedActions @("$helperCommand tool ensure quality -Yes", "$helperCommand tool status")

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
    # Resolve one consistent file scope and return early when it is empty.
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
        Write-GameWipOperationEvent -Phase plan -Severity info -Message 'A quality policy file or maintained path deletion changed; expanding quality checks to the complete maintained worktree.'
    }

    if ($null -eq $Tools)
    {
        $Tools = Get-GameWipQualityToolchain -ScopeInfo $ScopeInfo
    }
    $tools = $Tools

    $fileSelection = Get-GameWipQualityFileSelections -ScopeInfo $ScopeInfo
    $cppFiles = $fileSelection.Cpp
    $pythonFiles = $fileSelection.Python
    $powerShellFiles = $fileSelection.PowerShell
    $jsFiles = $fileSelection.JavaScript
    $eslintConfigFile = 'config/quality/eslint.config.js'

    if ($jsFiles.Count -ne 0 -and $jsFiles -notcontains $eslintConfigFile)
    {
        $jsFiles = @($eslintConfigFile) + $jsFiles
    }
    # PowerShell uses the guarded candidate/fallback path in the fix workflow;
    # the regular Prettier command must never write directly to repository PS files.
    $prettierFiles = $fileSelection.Prettier
    $specialJsonFiles = $fileSelection.SpecialJson
    $cmakeFiles = $fileSelection.CMake
    $yamlFiles = $fileSelection.Yaml
    $markdownFiles = $fileSelection.Markdown

    # Resolve shared tool paths once for all check actions.
    $nodePath = Get-GameWipNpmManagedModuleRoot
    $prettierPluginArguments = @()

    if ($fileSelection.Prettier.Count -ne 0 -or $specialJsonFiles.Count -ne 0)
    {
        $prettierPluginArguments = Get-GameWipPrettierPluginArguments
    }
    $python = (Resolve-GameWipPython).Path
    $managedPython = Get-GameWipPythonEnvironmentInterpreterPath -Root (Join-Path (Get-GameWipManagedToolRoot) 'python')

    if (Test-Path -LiteralPath $managedPython)
    {
        $python = $managedPython
    }
    $ruffCache = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'ruff'

    $checks = @(
        # C/C++ and Python sources
        @{
            Name = 'clang-format'
            Body = {
                if (-not $useChangedScope -or $cppFiles.Count -ne 0)
                {
                    Invoke-GameWipFormat -Mode check -Files $cppFiles
                }
            }
        },
        @{
            Name = 'ruff lint'
            Body = {
                if (-not $useChangedScope -or $pythonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name ruff-check `
                        -FilePath $tools.ruff `
                        -Arguments (@(
                            'check',
                            '--config',
                            (Join-Path $qualityConfig 'ruff.toml'),
                            '--cache-dir',
                            $ruffCache
                        ) + $pythonFiles)
                }
            }
        },
        @{
            Name = 'ruff format'
            Body = {
                if (-not $useChangedScope -or $pythonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name ruff-format-check `
                        -FilePath $tools.ruff `
                        -Arguments (@(
                            'format',
                            '--check',
                            '--config',
                            (Join-Path $qualityConfig 'ruff.toml'),
                            '--cache-dir',
                            $ruffCache
                        ) + $pythonFiles)
                }
            }
        },
        @{
            Name = 'PowerShell'
            Body = {
                if (-not $useChangedScope -or $powerShellFiles.Count -ne 0)
                {
                    Invoke-GameWipPowerShellQuality -Files $powerShellFiles
                }
            }
        },
        @{
            Name = 'ESLint'
            Body = {
                if (-not $useChangedScope -or $jsFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name eslint `
                        -FilePath $tools.eslint `
                        -Arguments (@('--config', (Join-Path $qualityConfig 'eslint.config.js')) + $jsFiles) `
                        -Environment @{ NODE_PATH = $nodePath }
                }
            }
        },

        # Configuration and documentation formats
        @{
            Name = 'Prettier'
            Body = {
                if (-not $useChangedScope -or $prettierFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name prettier-check `
                        -FilePath $tools.prettier `
                        -Arguments (@(
                            '--config',
                            (Join-Path $qualityConfig 'prettier.json'),
                            '--ignore-path',
                            (Join-Path $qualityConfig 'prettier.ignore'),
                            '--check'
                        ) + $prettierPluginArguments + $prettierFiles)
                }
            }
        },
        @{
            Name = 'Prettier special JSON'
            Body = {
                if (-not $useChangedScope -or $specialJsonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name prettier-special-json-check `
                        -FilePath $tools.prettier `
                        -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json')) + $prettierPluginArguments + @('--parser', 'json', '--check') + $specialJsonFiles)
                }
            }
        },
        @{
            Name = 'Gersemi'
            Body = {
                if (-not $useChangedScope -or $cmakeFiles.Count -ne 0)
                {
                    Invoke-GameWipGersemi -Mode check -Executable $tools.gersemi -Files $cmakeFiles
                }
            }
        },
        @{
            Name = 'yamllint'
            Body = {
                if (-not $useChangedScope -or $yamlFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name yamllint `
                        -FilePath $tools.yamllint `
                        -Arguments (@('-c', (Join-Path $qualityConfig 'yamllint.yml')) + $yamlFiles)
                }
            }
        },
        @{
            Name = 'actionlint'
            Body = {
                Invoke-GameWipQualityNative -Name actionlint -FilePath $tools.actionlint -Arguments @('-color')
            }
        },
        @{
            Name = 'markdownlint'
            Body = {
                if (-not $useChangedScope -or $markdownFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative `
                        -Name markdownlint `
                        -FilePath $tools.'markdownlint-cli2' `
                        -Arguments (@('--config', (Join-Path $qualityConfig 'markdownlint-cli2.jsonc')) + $markdownFiles)
                }
            }
        },

        # Project and repository contracts
        @{
            Name = 'JSON Schema'
            Body = {
                Invoke-GameWipQualityNative `
                    -Name schema-validation `
                    -FilePath $python `
                    -Arguments @('.github/scripts/validate_config_schemas.py')
            }
        },
        @{
            Name = 'quality ownership'
            Body = {
                Invoke-GameWipQualityNative `
                    -Name quality-ownership `
                    -FilePath $python `
                    -Arguments @('.github/scripts/check_quality_ownership.py')
            }
        },
        @{
            Name = 'helper standardization'
            Body = {
                Invoke-GameWipQualityNative `
                    -Name helper-standardization `
                    -FilePath $python `
                    -Arguments @('.github/scripts/check_helper_standardization.py')
            }
        },
        @{
            Name = 'repository standards'
            Body = {
                Invoke-GameWipQualityNative `
                    -Name repository-standards `
                    -FilePath $python `
                    -Arguments @('.github/scripts/check_repository_standards.py')
            }
        },
        @{
            Name = 'repository standards tests'
            Body = {
                Invoke-GameWipQualityNative `
                    -Name repository-standards-tests `
                    -FilePath $python `
                    -Arguments @('-m', 'unittest', 'discover', '-s', '.github/scripts', '-p', 'test_*.py')
            }
        },
        @{
            Name = 'documentation standards'
            Body = {
                Invoke-GameWipQualityNative `
                    -Name documentation-standards `
                    -FilePath $python `
                    -Arguments @('.github/scripts/check_documentation_standards.py')
            }
        },
        @{
            Name = 'Markdown links'
            Body = { Invoke-GameWipMarkdownLink }
        }
    )

    $checkPlan = Get-GameWipQualityCheckPlan -ScopeInfo $ScopeInfo
    $checks = @($checks | Where-Object { $checkPlan.Checks -contains $_.Name })

    # Execute the catalog in order and retain each result for one summary.
    $results = [System.Collections.Generic.List[object]]::new()
    $total = $checks.Count

    for ($index = 0; $index -lt $total; ++$index)
    {
        $check = $checks[$index]
        Assert-GameWipNotCancelled
        Write-GameWipOperationEvent `
            -Phase execute `
            -Step "quality-$($index + 1)-of-$total" `
            -Severity progress `
            -Message "[$($index + 1)/$total] $($check.Name)"
        $clock = [Diagnostics.Stopwatch]::StartNew()

        try
        {
            & $check.Body
            $clock.Stop()
            $results.Add([pscustomobject]@{
                    Name = $check.Name
                    Status = 'PASS'
                    Duration = $clock.Elapsed.TotalSeconds
                    Error = ''
                }) | Out-Null
        }

        catch
        {
            $clock.Stop()
            $results.Add([pscustomobject]@{
                    Name = $check.Name
                    Status = 'FAIL'
                    Duration = $clock.Elapsed.TotalSeconds
                    Error = $_.Exception.Message
                }) | Out-Null

            if ($FailFast)
            {
                break
            }
        }
    }

    # Present results together and report failures once.
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
        throw (New-GameWipDiagnosticException `
                -Code quality-failed `
                -Summary "$($failed.Count) quality check(s) failed." `
                -Details (($failed | ForEach-Object { "$($_.Name): $($_.Error)" }) -join "`n") `
                -SuggestedActions @(
                'Fix every failure listed in the quality summary.',
                "Use '-FailFast' only when debugging one check at a time."
            ))
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

    # Normalize source documentation before invoking language formatters.
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

    $fileSelection = Get-GameWipQualityFileSelections -ScopeInfo $ScopeInfo
    $useChangedScope = $fileSelection.UseChangedScope
    $cppFiles = $fileSelection.Cpp
    $pythonFiles = $fileSelection.Python
    $powerShellFiles = $fileSelection.PowerShell
    $prettierFiles = $fileSelection.Prettier
    $specialJsonFiles = $fileSelection.SpecialJson
    $cmakeFiles = $fileSelection.CMake
    $prettierPluginArguments = Get-GameWipPrettierPluginArguments

    $ruff = $Tools.ruff

    # Apply language and configuration formatters.
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
        Invoke-GameWipGersemi -Mode in-place -Executable $Tools.gersemi -Files $cmakeFiles
    }

    if (-not $useChangedScope -or $powerShellFiles.Count -ne 0)
    {
        Invoke-GameWipPowerShellFormat -Prettier $Tools.prettier -Files $powerShellFiles
        Invoke-GameWipPowerShellQuality -Files $powerShellFiles
    }

    # Reapply the repository's text policy after formatting.
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
