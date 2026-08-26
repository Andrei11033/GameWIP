# GameWIP repository quality orchestration. Checks return evidence; presentation is aggregated here.

Set-StrictMode -Version Latest

function Get-GameWipQualityTool
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $toolInfo = Get-GameWipProjectTool -Id $Id
    $detected = Get-GameWipDetectedTool -Tool $toolInfo
    if ((Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected) -ne 'compatible')
    {
        throw (New-GameWipDiagnosticException `
                -Code 'quality-tool-unavailable' `
                -Summary "Quality tool '$Id' is missing or incompatible." `
                -Details "Run '.\gamewip.bat tools ensure quality -Yes' to repair the declared quality toolchain." `
                -SuggestedActions @('.\gamewip.bat tools ensure quality -Yes', '.\gamewip.bat tools status'))
    }
    return [string]$detected.Location
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

function Invoke-GameWipPowerShellQuality
{
    param([switch]$Fix, [string[]]$Files)
    $moduleRoot = Get-GameWipQualityTool -Id psscriptanalyzer
    Import-Module (Join-Path $moduleRoot 'PSScriptAnalyzer.psd1') -Force
    $settings = Join-Path $RepositoryRoot 'config\quality\psscriptanalyzer.psd1'
    $targets = @(Get-GameWipPowerShellFile -Files $Files)
    if ($null -ne $Files -and @($Files).Count -ne 0 -and $targets.Count -eq 0)
    {
        return
    }
    $analysisFailures = [System.Collections.Generic.List[object]]::new()
    $formatFailures = [System.Collections.Generic.List[string]]::new()
    foreach ($file in $targets)
    {
        $current = Get-Content -Raw -LiteralPath $file.FullName
        $formatted = Invoke-Formatter -ScriptDefinition $current -Settings $settings
        $formatted = [regex]::Replace($formatted, '(?m)[ \t]+$', '')
        $expected = $formatted.TrimEnd() + "`n"
        if ($Fix)
        {
            if ($current.Replace("`r`n", "`n") -cne $expected)
            {
                Write-GameWipTextAtomic -Path $file.FullName -Content $expected
                Add-GameWipOperationChange -Message "Formatted $(Get-GameWipRepositoryRelativePath -Path $file.FullName)"
            }
        }
        elseif ($current.Replace("`r`n", "`n") -cne $expected)
        {
            $formatFailures.Add($file.FullName) | Out-Null
        }
        foreach ($finding in @(Invoke-ScriptAnalyzer -Path $file.FullName -Settings $settings -Severity Warning, Error))
        {
            $analysisFailures.Add($finding) | Out-Null
        }
    }
    if ($formatFailures.Count -ne 0)
    {
        throw "$($formatFailures.Count) PowerShell file(s) differ from repository formatting. Run '.\gamewip.bat quality fix'."
    }
    if ($analysisFailures.Count -ne 0)
    {
        $analysisFailures | Format-Table -AutoSize | Out-Host
        throw "PSScriptAnalyzer reported $($analysisFailures.Count) warning/error finding(s)."
    }
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

function Invoke-GameWipQualityCheck
{
    param([switch]$FailFast, [switch]$Changed, [AllowNull()]$ScopeInfo = $null)

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
    $python = (Resolve-GameWipPython).Path
    $managedPython = Get-GameWipPythonEnvironmentInterpreterPath -Root (Join-Path (Get-GameWipManagedToolRoot) 'python')
    if (Test-Path -LiteralPath $managedPython)
    {
        $python = $managedPython
    }

    # Resolve every required quality tool before running checks so one missing
    # executable cannot hide a second missing/incompatible tool.
    $tools = @{}
    $toolFailures = [System.Collections.Generic.List[string]]::new()
    $requiredToolIds = @('ruff', 'eslint', 'prettier', 'gersemi', 'yamllint', 'markdownlint-cli2', 'actionlint', 'jsonschema')
    Write-Host "Checking $($requiredToolIds.Count) required quality tools..."
    for ($toolIndex = 0; $toolIndex -lt $requiredToolIds.Count; ++$toolIndex)
    {
        $id = $requiredToolIds[$toolIndex]

        try
        {
            $toolInfo = Get-GameWipProjectTool -Id $id
            $detected = Get-GameWipDetectedTool -Tool $toolInfo
            $compatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected

            $semantic = switch ($compatibility)
            {
                'compatible'
                {
                    'Success'
                }
                'missing'
                {
                    'Failure'
                }
                default
                {
                    'Warning'
                }
            }

            Write-GameWipStatusLine `
                -Status "$($toolIndex + 1)/$($requiredToolIds.Count)" `
                -Text $id `
                -Suffix "($compatibility)" `
                -Semantic $semantic `
                -SuffixSemantic Muted `
                -Indent 2

            if ($compatibility -eq 'compatible')
            {
                $tools[$id] = [string]$detected.Location
                continue
            }

            $location = if ($detected.Location)
            {
                [string]$detected.Location
            }
            else
            {
                'not found'
            }

            $toolFailures.Add("${id}: $compatibility ($location)") | Out-Null
        }
        catch
        {
            Write-GameWipStatusLine `
                -Status "$($toolIndex + 1)/$($requiredToolIds.Count)" `
                -Text $id `
                -Suffix '(error)' `
                -Semantic Failure `
                -SuffixSemantic Muted `
                -Indent 2

            $toolFailures.Add("${id}: $($_.Exception.Message)") | Out-Null
        }
    }

    if ($toolFailures.Count -ne 0)
    {
        throw (New-GameWipDiagnosticException -Code quality-toolchain-incomplete -Summary "$($toolFailures.Count) quality tool(s) are unavailable." -Details ($toolFailures -join "`n") -SuggestedActions @('.\gamewip.bat tools ensure quality -Yes', '.\gamewip.bat tools status'))
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
                    Invoke-GameWipQualityNative -Name prettier-check -FilePath $tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--ignore-path', (Join-Path $qualityConfig 'prettier.ignore'), '--check') + $prettierFiles)
                } }
        },
        @{ Name = 'Prettier special JSON'; Body = { if (-not $useChangedScope -or $specialJsonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name prettier-special-json-check -FilePath $tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--parser', 'json', '--check') + $specialJsonFiles)
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
    param([Parameter(Mandatory = $true)]$ScopeInfo)

    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    $scope = @($ScopeInfo.Files)
    $useChangedScope = [bool]$ScopeInfo.UseChanged
    if ($ScopeInfo.Requested -and $scope.Count -eq 0)
    {
        return
    }

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

    $ruff = Get-GameWipQualityTool -Id ruff
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
    if (-not $useChangedScope -or $powerShellFiles.Count -ne 0)
    {
        Invoke-GameWipPowerShellQuality -Fix -Files $powerShellFiles
    }
    if ($prettierFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name prettier-write -FilePath (Get-GameWipQualityTool -Id prettier) -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--ignore-path', (Join-Path $qualityConfig 'prettier.ignore'), '--write') + $prettierFiles)
    }
    if ($specialJsonFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name prettier-special-json-write -FilePath (Get-GameWipQualityTool -Id prettier) -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--parser', 'json', '--write') + $specialJsonFiles)
    }
    if ($cmakeFiles.Count -ne 0)
    {
        Invoke-GameWipQualityNative -Name gersemi-format -FilePath (Get-GameWipQualityTool -Id gersemi) -Arguments (@('--config', (Join-Path $qualityConfig 'gersemi.yml'), '--in-place') + $cmakeFiles)
    }
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
    if ($Mode -eq 'fix')
    {
        Invoke-GameWipQualityFix -ScopeInfo $scopeInfo
    }
    Invoke-GameWipQualityCheck -FailFast:$FailFast -Changed:$Changed -ScopeInfo $scopeInfo
}
