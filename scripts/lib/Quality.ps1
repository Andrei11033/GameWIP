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


function Get-GameWipMaintainedTrackedFile
{
    param([string[]]$Extensions = @(), [string[]]$ExactNames = @())
    $result = Invoke-GameWipProcess -FilePath git -Arguments @('-C', $RepositoryRoot, 'ls-files') -OutputMode LogOnly -TimeoutSeconds 30
    if ($result.ExitCode -ne 0)
    {
        throw 'Could not enumerate tracked files for repository quality.'
    }
    return @($result.Stdout |
            ForEach-Object { ([string]$_).Replace('\', '/') } |
            Where-Object { $_ -and $_ -notmatch '^(?:external|build|install|docs/releases)/' } |
            Where-Object {
                $leaf = [IO.Path]::GetFileName($_)
                $extension = [IO.Path]::GetExtension($_).ToLowerInvariant()
                $ExactNames -contains $leaf -or $Extensions -contains $extension
            })
}

function Get-GameWipPowerShellFile
{
    param([string[]]$Files)
    $extensions = @('.ps1', '.psd1', '.psm1')
    if ($null -ne $Files -and $Files.Count -ne 0)
    {
        return @($Files |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and $extensions -contains [IO.Path]::GetExtension($_).ToLowerInvariant() } |
                ForEach-Object { Get-Item -LiteralPath $_ })
    }
    return @(Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File |
            Where-Object { $_.FullName -notmatch '[\\/](?:build|external|install)[\\/]' -and $extensions -contains $_.Extension.ToLowerInvariant() })
}

function Get-GameWipCMakeFile
{
    param([string[]]$Files)
    if ($null -ne $Files -and $Files.Count -ne 0)
    {
        return @($Files |
                ForEach-Object { Resolve-GameWipRepositoryPath -Path $_ } |
                Where-Object { (Test-Path -LiteralPath $_ -PathType Leaf) -and ((Split-Path -Leaf $_) -eq 'CMakeLists.txt' -or $_ -match '\.cmake(?:\.in)?$') })
    }
    return @(Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File |
            Where-Object { $_.FullName -notmatch '[\\/](?:build|external|install)[\\/]' -and ($_.Name -eq 'CMakeLists.txt' -or $_.Name -match '\.cmake(?:\.in)?$') } |
            ForEach-Object { $_.FullName })
}

function Invoke-GameWipQualityNative
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [hashtable]$Environment = @{}
    )
    Invoke-GameWipNative -Name $Name -FilePath $FilePath -Arguments $Arguments -Environment $Environment -OutputMode Stream | Out-Null
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
                [IO.File]::WriteAllText($file.FullName, $expected, [Text.UTF8Encoding]::new($false))
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
    Invoke-GameWipNative -Name quality-ownership-status -FilePath $python -Arguments @('.github/scripts/check_quality_ownership.py', '--status') -OutputMode Stream
}

function Invoke-GameWipQualityCheck
{
    param([switch]$FailFast, [switch]$Changed)

    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    $scope = @(if ($Changed)
        {
            Get-GameWipChangedRepositoryFile
        })
    if ($Changed -and $scope.Count -eq 0)
    {
        Write-GameWipSection 'Quality summary'
        Write-Host '  [PASS] No changed maintained files.'
        return
    }

    $cppFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx', '.inl')
        })
    $pythonFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.py')
        }
        else
        {
            Get-GameWipMaintainedTrackedFile -Extensions @('.py')
        })
    $powerShellFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.ps1', '.psd1', '.psm1')
        })
    $jsFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.js', '.mjs', '.cjs')
        }
        else
        {
            Get-GameWipMaintainedTrackedFile -Extensions @('.js', '.mjs', '.cjs')
        })
    $eslintConfigFile = 'config/quality/eslint.config.js'
    if ($jsFiles.Count -ne 0 -and $jsFiles -notcontains $eslintConfigFile)
    {
        $jsFiles = @($eslintConfigFile) + $jsFiles
    }
    $prettierFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
        }
        else
        {
            Get-GameWipMaintainedTrackedFile -Extensions @('.js', '.mjs', '.cjs', '.json', '.jsonc', '.yml', '.yaml', '.css')
        })
    $specialJsonFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
        }
        else
        {
            Get-GameWipMaintainedTrackedFile -ExactNames @('.vsconfig', 'GameWIP.code-workspace')
        })
    $cmakeFiles = @(if ($Changed)
        {
            Get-GameWipCMakeFile -Files $scope
        }
        else
        {
            Get-GameWipCMakeFile
        })
    $yamlFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.yml', '.yaml')
        }
        else
        {
            Get-GameWipMaintainedTrackedFile -Extensions @('.yml', '.yaml')
        })
    $markdownFiles = @(if ($Changed)
        {
            Select-GameWipQualityFile -Files $scope -Extensions @('.md')
        }
        else
        {
            Get-GameWipMaintainedTrackedFile -Extensions @('.md')
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
        Write-Host "  [$($toolIndex + 1)/$($requiredToolIds.Count)] $id"
        try
        {
            $tools[$id] = Get-GameWipQualityTool -Id $id
        }
        catch
        {
            $toolFailures.Add("${id}: $($_.Exception.Message)") | Out-Null
        }
    }
    if ($toolFailures.Count -ne 0)
    {
        throw (New-GameWipDiagnosticException -Code quality-toolchain-incomplete -Summary "$($toolFailures.Count) quality tool(s) are unavailable." -Details ($toolFailures -join "`n") -SuggestedActions @('.\gamewip.bat tools ensure quality -Yes', '.\gamewip.bat tools status'))
    }

    $checks = @(
        @{ Name = 'clang-format'; Body = { if (-not $Changed -or $cppFiles.Count -ne 0)
                {
                    Invoke-GameWipFormat -Mode check -Files $cppFiles
                } }
        },
        @{ Name = 'ruff lint'; Body = { if (-not $Changed -or $pythonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name ruff-check -FilePath $tools.ruff -Arguments (@('check', '--config', (Join-Path $qualityConfig 'ruff.toml')) + $pythonFiles)
                } }
        },
        @{ Name = 'ruff format'; Body = { if (-not $Changed -or $pythonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name ruff-format-check -FilePath $tools.ruff -Arguments (@('format', '--check', '--config', (Join-Path $qualityConfig 'ruff.toml')) + $pythonFiles)
                } }
        },
        @{ Name = 'PowerShell'; Body = { if (-not $Changed -or $powerShellFiles.Count -ne 0)
                {
                    Invoke-GameWipPowerShellQuality -Files $powerShellFiles
                } }
        },
        @{ Name = 'ESLint'; Body = { if (-not $Changed -or $jsFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name eslint -FilePath $tools.eslint -Arguments (@('--config', (Join-Path $qualityConfig 'eslint.config.js')) + $jsFiles) -Environment @{ NODE_PATH = $nodePath }
                } }
        },
        @{ Name = 'Prettier'; Body = { if (-not $Changed -or $prettierFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name prettier-check -FilePath $tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--ignore-path', (Join-Path $qualityConfig 'prettier.ignore'), '--check') + $prettierFiles)
                } }
        },
        @{ Name = 'Prettier special JSON'; Body = { if (-not $Changed -or $specialJsonFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name prettier-special-json-check -FilePath $tools.prettier -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--parser', 'json', '--check') + $specialJsonFiles)
                } }
        },
        @{ Name = 'Gersemi'; Body = { if (-not $Changed -or $cmakeFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name gersemi-check -FilePath $tools.gersemi -Arguments (@('--config', (Join-Path $qualityConfig 'gersemi.yml'), '--check') + $cmakeFiles)
                } }
        },
        @{ Name = 'yamllint'; Body = { if (-not $Changed -or $yamlFiles.Count -ne 0)
                {
                    Invoke-GameWipQualityNative -Name yamllint -FilePath $tools.yamllint -Arguments (@('-c', (Join-Path $qualityConfig 'yamllint.yml')) + $yamlFiles)
                } }
        },
        @{ Name = 'actionlint'; Body = { Invoke-GameWipQualityNative -Name actionlint -FilePath $tools.actionlint -Arguments @('-color') } },
        @{ Name = 'markdownlint'; Body = { if (-not $Changed -or $markdownFiles.Count -ne 0)
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
        $marker = if ($result.Status -eq 'PASS')
        {
            '[PASS]'
        }
        else
        {
            '[FAIL]'
        }
        Write-Host ('  {0,-6} {1,-28} {2,7:N2}s {3}' -f $marker, $result.Name, $result.Duration, $result.Error)
    }
    $failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
    if ($failed.Count -ne 0)
    {
        throw (New-GameWipDiagnosticException -Code quality-failed -Summary "$($failed.Count) quality check(s) failed." -Details (($failed | ForEach-Object { "$($_.Name): $($_.Error)" }) -join "`n") -SuggestedActions @('Fix every failure listed in the quality summary.', "Use '-FailFast' only when debugging one check at a time."))
    }
}

function Invoke-GameWipQualityFix
{
    $qualityConfig = Join-Path $RepositoryRoot 'config\quality'
    Invoke-GameWipFormat -Mode apply
    Invoke-GameWipQualityNative -Name ruff-format -FilePath (Get-GameWipQualityTool -Id ruff) -Arguments (@('format', '--config', (Join-Path $qualityConfig 'ruff.toml')) + @(Get-GameWipMaintainedTrackedFile -Extensions @('.py')))
    Invoke-GameWipPowerShellQuality -Fix
    Invoke-GameWipQualityNative -Name prettier-write -FilePath (Get-GameWipQualityTool -Id prettier) -Arguments @('--config', (Join-Path $qualityConfig 'prettier.json'), '--ignore-path', (Join-Path $qualityConfig 'prettier.ignore'), '--write', '**/*.{js,json,jsonc,yml,yaml,css}')
    Invoke-GameWipQualityNative -Name prettier-special-json-write -FilePath (Get-GameWipQualityTool -Id prettier) -Arguments (@('--config', (Join-Path $qualityConfig 'prettier.json'), '--parser', 'json', '--write') + @(Get-GameWipMaintainedTrackedFile -ExactNames @('.vsconfig', 'GameWIP.code-workspace')))
    Invoke-GameWipQualityNative -Name gersemi-format -FilePath (Get-GameWipQualityTool -Id gersemi) -Arguments (@('--config', (Join-Path $qualityConfig 'gersemi.yml'), '--in-place') + @(Get-GameWipCMakeFile))
}

function Invoke-GameWipQuality
{
    param(
        [ValidateSet('check', 'fix')][string]$Mode = 'check',
        [switch]$FailFast,
        [switch]$Changed
    )
    Initialize-GameWipStorage
    if ($Mode -eq 'fix')
    {
        Invoke-GameWipQualityFix
    }
    Invoke-GameWipQualityCheck -FailFast:$FailFast -Changed:$Changed
}
